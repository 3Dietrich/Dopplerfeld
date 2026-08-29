#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

// Die eingebettete Kopie des Startzustands (siehe loadStartPresetOnFirstRun()
// und den Abschnitt zum Startzustand in CMakeLists.txt).
#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr double pi    = 3.14159265358979323846;
constexpr double twoPi = 6.283185307179586476925;

// Einträge im gespeicherten Zustand, die KEINE Parameter sind: sie hängen als
// Property am Wurzelknoten des APVTS-Baums statt in der Parameterliste. Eine
// Bewegungsaufzeichnung ist kein automatisierbarer Regler, und ein
// Wiedergabezustand auch nicht.
//
// Namen an einer Stelle, aus demselben Grund wie in Params.h: ein Tippfehler
// an der Verwendungsstelle würde stumm eine leere Property lesen.
constexpr const char* motionFramesId  = "motionFrames";
constexpr const char* motionRateId    = "motionRateHz";
constexpr const char* motionPlayingId = "motionWasPlaying";

// Quellwahl und Sample-Pfad, aus demselben Grund wie oben eigene Properties
// statt APVTS-Parameter (siehe SourceKind-Kommentar im Header). Der Pfad
// bleibt bewusst ein Dateipfad statt eingebetteter Audiodaten (@dpa-
// Entscheidung 20260818) - das Sample muss beim Weitergeben eines Presets
// mitkopiert werden, dafür bleiben die Preset-Dateien klein.
constexpr const char* sourceKindId  = "sourceKind";
constexpr const char* samplePathId  = "samplePath";
constexpr const char* motorGateId   = "motorGateEnabled";
constexpr const char* panelOpenId   = "panelsOpen";

// Merkposten in der Einstellungsdatei (nicht im Zustand): steht er, ist der
// Startzustand auf diesem Rechner schon einmal eingespielt worden.
constexpr const char* startPresetKey = "startPresetLoaded";

// Fester Anker für relative Sample-Pfade (@dpa 20260818: "relativ zum
// Presets-Ordner!"). JUCE teilt getStateInformation()/setStateInformation()
// nicht mit, in welche Datei der Host gerade schreibt bzw. aus welcher er
// liest (weder die VST3/AU-Preset-Verwaltung noch "Save/Load State" der
// Standalone-App geben den Pfad weiter) - ohne einen selbst definierten,
// festen Ordner gäbe es also gar keinen Bezugspunkt für "relativ". Legt sich
// beim ersten Zugriff selbst an; Presets+Samples, die hier drunterliegen,
// bleiben beim Verschieben/Kopieren des ganzen Ordners portabel. Alles
// außerhalb dieses Ankers landet als absoluter Pfad im Preset - voll
// funktionsfähig, nur eben ortsgebunden.
juce::File presetsRootDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Dopplerfeld")
                   .getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

// Ein Frame sind drei double (x, y, z). Ausgeschrieben statt die Vec3-Struktur
// roh zu kopieren: das Dateiformat soll nicht am Speicherlayout eines
// C++-Typs hängen.
constexpr size_t doublesPerFrame = 3;

// Winkeldifferenz auf (-π, π]: ohne das würde die Yaw-Glättung beim Sprung
// über den Nulldurchgang den langen Weg einmal um den Kopf herum nehmen.
double wrapToPi (double a)
{
    a = std::fmod (a + pi, twoPi);

    if (a < 0.0)
        a += twoPi;

    return a - pi;
}

// Sicherheitsbegrenzer (Plan 6, H13). Bewusst ohne Lookahead und ohne
// Attackzeit: der Überschallknall ist ein Ereignis von wenigen Samples, ein
// zeitkonstantengesteuerter Limiter ließe genau dessen Spitze durch und der
// Host bekäme sie hart geclippt. Unterhalb der Kniestelle bleibt das Signal
// unverändert, darüber läuft es über tanh gegen 1 - die Kennlinie ist an der
// Kniestelle stetig und hat dort auch dieselbe Steigung, knickt also nicht.
// Lock-freies Maximum: mehrere Aufrufe pro Abrufintervall dürfen ihr jeweils
// größtes Sample eintragen, ohne dass sich Audiothread und Message-Thread
// gegenseitig blockieren (siehe consumeOutputPeakL/R in PluginProcessor.h).
void updatePeak (std::atomic<float>& peak, float v)
{
    float cur = peak.load (std::memory_order_relaxed);
    while (v > cur && ! peak.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
}

double softClip (double x)
{
    constexpr double knee = 0.7;

    const double magnitude = std::abs (x);

    if (magnitude <= knee)
        return x;

    const double over    = (magnitude - knee) / (1.0 - knee);
    const double limited = knee + (1.0 - knee) * std::tanh (over);

    return x < 0.0 ? -limited : limited;
}
}

//======================================================================
// SmootherSet

void DopplerfeldProcessor::SmootherSet::prepare (double tickRateHz)
{
    onePole.prepare (tickRateHz);
    spring.prepare  (tickRateHz);
    slew.prepare    (tickRateHz);
    oneEuro.prepare (tickRateHz);
}

void DopplerfeldProcessor::SmootherSet::reset (Vec3 pos)
{
    // Alle vier, nicht nur das aktive Verfahren: ein späterer Typwechsel darf
    // nicht auf einem Zustand von vor dem Reset aufsetzen.
    onePole.reset (pos);
    spring.reset  (pos);
    slew.reset    (pos);
    oneEuro.reset (pos);

    onePole.setTarget (pos);
    spring.setTarget  (pos);
    slew.setTarget    (pos);
    oneEuro.setTarget (pos);
}

void DopplerfeldProcessor::SmootherSet::resetExceptSlew (Vec3 pos)
{
    onePole.reset (pos);
    spring.reset  (pos);
    oneEuro.reset (pos);

    onePole.setTarget (pos);
    spring.setTarget  (pos);
    oneEuro.setTarget (pos);
}

void DopplerfeldProcessor::SmootherSet::setType (int index, Vec3 currentPos)
{
    index = juce::jlimit (0, 3, index);

    if (index == typeIndex)
        return;

    typeIndex = index;

    MotionSmoother* next = &spring;

    switch (index)
    {
        case 0:  next = &onePole; break;
        case 1:  next = &spring;  break;
        case 2:  next = &slew;    break;
        default: next = &oneEuro; break;
    }

    // Das neue Verfahren übernimmt die Position des alten. Ohne das würde die
    // Quelle beim Umschalten von dort weiterfahren, wo dieses Verfahren
    // zuletzt stand - hörbar als Sprung mitten in der Bewegung.
    next->reset (currentPos);
    next->setTarget (currentPos);

    current = next;
}

void DopplerfeldProcessor::SmootherSet::applyParameters (double tauSeconds, double vMax, double aMax)
{
    const double tau = std::max (1.0e-3, tauSeconds);

    onePole.setTau (tau);
    spring.setTau  (tau);

    slew.setVMax (vMax);
    slew.setAMax (aMax);

    // Der Regler heißt Tau und meint "so lange braucht eine Sprungantwort".
    // Beim 1€-Filter ist das Gegenstück die Mindest-Grenzfrequenz, also 1/τ -
    // damit wirkt derselbe Regler bei allen vier Verfahren in dieselbe
    // Richtung, statt bei einem davon wirkungslos zu sein.
    oneEuro.setMinCutoffHz (1.0 / tau);
}

//======================================================================
// Aufbau

DopplerfeldProcessor::DopplerfeldProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", Params::createParameterLayout())
{
    pp.fieldMetres = raw (Params::fieldMetres);

    pp.srcX = raw (Params::srcX);
    pp.srcY = raw (Params::srcY);
    pp.srcZ = raw (Params::srcZ);

    pp.lisX       = raw (Params::lisX);
    pp.lisY       = raw (Params::lisY);
    pp.lisZ       = raw (Params::lisZ);
    pp.lisYaw     = raw (Params::lisYaw);
    pp.earSpacing = raw (Params::earSpacing);

    pp.srcJitterAmount = raw (Params::srcJitterAmount);
    pp.srcJitterSpeed  = raw (Params::srcJitterSpeed);
    pp.srcJitterZAmount = raw (Params::srcJitterZAmount);
    pp.srcJitterOn     = raw (Params::srcJitterOn);
    pp.masterOn        = raw (Params::masterOn);

    pp.rpm = raw (Params::rpm);

    const char* const ratioIds[4]  { Params::harmRatio1,  Params::harmRatio2,  Params::harmRatio3,  Params::harmRatio4 };
    const char* const detuneIds[4] { Params::harmDetune1, Params::harmDetune2, Params::harmDetune3, Params::harmDetune4 };
    const char* const trackIds[4]  { Params::harmTrack1,  Params::harmTrack2,  Params::harmTrack3,  Params::harmTrack4 };
    const char* const levelIds[4]  { Params::harmLevel1,  Params::harmLevel2,  Params::harmLevel3,  Params::harmLevel4 };

    for (int i = 0; i < 4; ++i)
    {
        pp.harmRatio[i]  = raw (ratioIds[i]);
        pp.harmDetune[i] = raw (detuneIds[i]);
        pp.harmTrack[i]  = raw (trackIds[i]);
        pp.harmLevel[i]  = raw (levelIds[i]);
    }

    pp.noiseFcLo    = raw (Params::noiseFcLo);
    pp.noiseFcHi    = raw (Params::noiseFcHi);
    pp.noiseGainLo  = raw (Params::noiseGainLo);
    pp.noiseGainHi  = raw (Params::noiseGainHi);
    pp.noiseQ       = raw (Params::noiseQ);
    pp.jitterAmount = raw (Params::jitterAmount);
    pp.jitterRateHz = raw (Params::jitterRateHz);
    pp.imbalance    = raw (Params::imbalance);

    pp.engineKind     = raw (Params::engineKind);
    pp.propSpan       = raw (Params::propSpan);
    pp.propLevelDb    = raw (Params::propLevelDb);
    pp.heliRotorHz    = raw (Params::heliRotorHz);
    pp.heliBladeCount = raw (Params::heliBladeCount);
    pp.heliDoppler     = raw (Params::heliDoppler);
    pp.heliQuantise    = raw (Params::heliQuantise);
    pp.heliRotorRadius = raw (Params::heliRotorRadius);

    pp.sampleGain  = raw (Params::sampleGain);
    pp.samplePitch = raw (Params::samplePitch);
    pp.loopStart   = raw (Params::loopStart);
    pp.loopEnd     = raw (Params::loopEnd);
    pp.loopXfadeMs = raw (Params::loopXfadeMs);
    pp.eqLowGain   = raw (Params::eqLowGain);
    pp.eqMidGain   = raw (Params::eqMidGain);
    pp.eqMidFreq   = raw (Params::eqMidFreq);
    pp.eqHighGain  = raw (Params::eqHighGain);

    pp.smootherType = raw (Params::smootherType);
    pp.smootherTau  = raw (Params::smootherTau);
    pp.slewVmax     = raw (Params::slewVmax);
    pp.playSpeed    = raw (Params::playSpeed);
    pp.playInterp   = raw (Params::playInterp);
    pp.playLoop     = raw (Params::playLoop);
    pp.globalMaxSpeed = raw (Params::globalMaxSpeed);

    pp.flyKind     = raw (Params::flyKind);
    pp.flyStart    = raw (Params::flyStart);
    pp.flyDistance = raw (Params::flyDistance);
    pp.flyApproach = raw (Params::flyApproach);
    pp.flySpeed    = raw (Params::flySpeed);
    pp.flyLoop     = raw (Params::flyLoop);

    pp.boomLimitDb     = raw (Params::boomLimitDb);
    pp.nWaveGainDb     = raw (Params::nWaveGainDb);
    for (int i = 0; i < 4; ++i)
        pp.harmSine[i] = raw (Params::harmSine[i]);

    pp.oscOn = raw (Params::oscOn);

    pp.engineLevelDb  = raw (Params::engineLevelDb);
    pp.rocketShock    = raw (Params::rocketShock);
    pp.rotorSlap      = raw (Params::rotorSlap);

    pp.jetVoice        = raw (Params::jetVoice);
    pp.jetTone         = raw (Params::jetTone);
    pp.rocketVoice     = raw (Params::rocketVoice);
    pp.rocketTone      = raw (Params::rocketTone);
    pp.rocketShockSize = raw (Params::rocketShockSize);
    pp.rocketFarColour = raw (Params::rocketFarColour);
    pp.rocketShockRate = raw (Params::rocketShockRate);
    pp.extraPathGainDb = raw (Params::extraPathGainDb);
    pp.shockDuckRange  = raw (Params::shockDuckRange);
    pp.jumpBoom        = raw (Params::jumpBoom);
    pp.jumpBoomSize    = raw (Params::jumpBoomSize);
    pp.airAbsorbAmount = raw (Params::airAbsorbAmount);
    pp.distanceCurve   = raw (Params::distanceCurve);
    pp.airTempC        = raw (Params::airTempC);
    pp.airAltitude     = raw (Params::airAltitude);

    pp.groundReflectionOn = raw (Params::groundReflectionOn);
    pp.groundDampAmount   = raw (Params::groundDampAmount);

    pp.nWaveOn   = raw (Params::nWaveOn);
    pp.nWaveSize = raw (Params::nWaveSize);
    pp.nWaveEdge = raw (Params::nWaveEdge);
    pp.nWavePressure = raw (Params::nWavePressure);

    pp.cloneTotal  = raw (Params::cloneTotal);
    pp.cloneRealLevel = raw (Params::cloneRealLevel);
    pp.cloneSpread = raw (Params::cloneSpread);

    pp.reflect2ndOn = raw (Params::reflect2ndOn);
    pp.bounceGain   = raw (Params::bounceGain);
    pp.bounceGainDb = raw (Params::bounceGainDb);

    {
        const char* const onIds[]    { Params::wall1On,    Params::wall2On };
        const char* const xIds[]     { Params::wall1X,     Params::wall2X };
        const char* const yIds[]     { Params::wall1Y,     Params::wall2Y };
        const char* const angleIds[] { Params::wall1Angle, Params::wall2Angle };
        const char* const tiltIds[]  { Params::wall1Tilt,  Params::wall2Tilt };
        const char* const dampIds[]  { Params::wall1Damp,  Params::wall2Damp };
        const char* const gainIds[]  { Params::wall1Gain,  Params::wall2Gain };

        static_assert (DopplerEngine::maxWalls == 2,
                       "Die ID-Listen oben sind je Wand von Hand geschrieben - eine dritte "
                       "Wand braucht dort einen dritten Eintrag, sonst liest sie ins Leere.");

        for (int w = 0; w < DopplerEngine::maxWalls; ++w)
        {
            pp.wallOn[w]    = raw (onIds[w]);
            pp.wallX[w]     = raw (xIds[w]);
            pp.wallY[w]     = raw (yIds[w]);
            pp.wallAngle[w] = raw (angleIds[w]);
            pp.wallTilt[w]  = raw (tiltIds[w]);
            pp.wallDamp[w]  = raw (dampIds[w]);
            pp.wallGain[w]  = raw (gainIds[w]);
        }
    }


    {
        static_assert (Params::tapCount == DopplerEngine::maxTaps,
                       "Die Parameterliste und die Pfadliste der Engine muessen gleich viele "
                       "Abgriffpunkte kennen, sonst zeigen Regler ins Leere oder Pfade bleiben "
                       "unerreichbar.");

        for (int t = 0; t < Params::tapCount; ++t)
        {
            using namespace Params::TapPart;

            pp.tapOn[t]       = raw (Params::tapId (t, on).toRawUTF8());
            pp.tapX[t]        = raw (Params::tapId (t, x).toRawUTF8());
            pp.tapY[t]        = raw (Params::tapId (t, y).toRawUTF8());
            pp.tapZ[t]        = raw (Params::tapId (t, z).toRawUTF8());
            pp.tapType[t]     = raw (Params::tapId (t, type).toRawUTF8());
            pp.tapRoom[t]     = raw (Params::tapId (t, room).toRawUTF8());
            pp.tapDecay[t]    = raw (Params::tapId (t, decay).toRawUTF8());
            pp.tapDamp[t]     = raw (Params::tapId (t, damp).toRawUTF8());
            pp.tapGain[t]     = raw (Params::tapId (t, gain).toRawUTF8());
            pp.tapWidth[t]    = raw (Params::tapId (t, width).toRawUTF8());
            pp.tapPredelay[t] = raw (Params::tapId (t, predelay).toRawUTF8());
        }
    }

    pp.imbalanceOctave = raw (Params::imbalanceOctave);
    pp.groundGain = raw (Params::groundGain);
    pp.panAmount  = raw (Params::panAmount);
    pp.outputGain = raw (Params::outputGain);
    pp.limiterOn  = raw (Params::limiterOn);

    // Übergabepuffer der geladenen Aufzeichnung einmal auf Höchstlänge
    // bringen. Danach wächst er nie mehr, gibt seinen Speicher nie her und
    // kann dem Audiothread deshalb nicht unter den Händen wegwandern (siehe
    // stagedMotionFrames in PluginProcessor.h).
    stagedMotionFrames.reserve ((size_t) motionRecordMaxFrames);

    // Der Motor ist die Default-Quelle: er klingt sofort, ohne dass erst eine
    // Datei geladen werden muss.
    sourceHolder.setSource (&engineGenerator);
    dopplerEngine.setSource (&sourceHolder);

    // Zuletzt, wenn alles verdrahtet ist: beim allerersten Oeffnen steht hier
    // nicht der Grundzustand, sondern das Start-Preset.
    loadStartPresetOnFirstRun();
}

SoundSource* DopplerfeldProcessor::sourceForKind (SourceKind kind)
{
    switch (kind)
    {
        case SourceKind::Sample:  return &sampleSource;
        case SourceKind::AudioIn: return &audioInSource;
        case SourceKind::Motor:
        default:                  return &engineGenerator;
    }
}

void DopplerfeldProcessor::applyMotorGate (float* mono, int numSamples)
{
    if (mono == nullptr || numSamples <= 0)
        return;

    // Wirkt nur auf die Motor-Quelle - Sample/Audio In liefern eigenen,
    // fremdbestimmten Klang, "starten" ergibt dort keinen Sinn (Header-
    // Kommentar setMotorGateEnabled()).
    if (currentSourceKind() != SourceKind::Motor)
        return;

    // Sustaining (normale Lautstaerke) und AwaitingRest (noch voll hoerbar,
    // wartet nur auf die Ruheposition) brauchen keine Rampe.
    if (motorGateState == MotorGateState::Sustaining
        || motorGateState == MotorGateState::AwaitingRest)
        return;

    if (motorGateState == MotorGateState::Idle)
    {
        juce::FloatVectorOperations::clear (mono, numSamples);
        return;
    }

    const double sr = currentSampleRate;
    if (sr <= 0.0)
        return;

    const bool  attacking = (motorGateState == MotorGateState::Attacking);
    const float target    = attacking ? 1.0f : 0.0f;
    const float step      = (float) (1.0 / std::max (1.0e-3, (attacking ? motorGateAttackSeconds
                                                                        : motorGateReleaseSeconds) * sr));

    for (int i = 0; i < numSamples; ++i)
    {
        motorGateGain = attacking ? std::min (target, motorGateGain + step)
                                  : std::max (target, motorGateGain - step);
        mono[i] *= motorGateGain;
    }

    if (attacking && motorGateGain >= 1.0f)
        motorGateState = MotorGateState::Sustaining;
    else if (! attacking && motorGateGain <= 0.0f)
        motorGateState = MotorGateState::Idle;
}

std::atomic<float>* DopplerfeldProcessor::raw (const char* paramID)
{
    auto* value = apvts.getRawParameterValue (paramID);

    // Eine ID, die es im Layout nicht gibt, liefert nullptr und würde erst im
    // Block auffallen - hier fällt sie im Debug-Build sofort auf.
    jassert (value != nullptr);

    return value;
}

void DopplerfeldProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Die Engine wird mindestens auf die Teilblocklänge vorbereitet, auch wenn
    // der Host kleinere Blöcke schickt - sonst wäre der Teilblock größer als
    // das, was ihre Zwischenpuffer tragen.
    const int maxBlock = std::max (samplesPerBlock, motionChunkSamples);

    currentSampleRate = sampleRate;

    // Der Massstab, in dem die Geometrie aufgesetzt wird. Muss vor
    // applyParameters() stehen: alle Meterkoordinaten haengen daran.
    appliedFieldMetres = (double) pp.fieldMetres->load();

    monoScratch.setSize (1, maxBlock, false, true, true);

    // Scope (@dpa-Feedback: Zoom bis ~3s Zeitbasis): der Ringpuffer muss das
    // Doppelte der max. Zeitbasis fassen (ScopeComponent liest ein
    // Rohfenster, das doppelt so lang ist wie die Anzeige - Sicherheitsabstand
    // fuer die Trigger-Suche bei Sync, siehe dortigen Klassenkommentar).
    scopeRing.prepare (sampleRate, 2.0 * scopeMaxDisplaySeconds);

    // Wiedergabepuffer fuer den Scope-Play-Toggle: hoechstens ein sichtbares
    // Fenster lang (scopeMaxDisplaySeconds, s. dortigen Kommentar), einmal
    // reserviert - renderScopePlayback() darf danach nie mehr allokieren.
    {
        const int scopePlaybackCapacity = (int) std::ceil (scopeMaxDisplaySeconds * sampleRate);

        scopePlaybackL.assign ((size_t) scopePlaybackCapacity, 0.0f);
        scopePlaybackR.assign ((size_t) scopePlaybackCapacity, 0.0f);
        stagedScopePlaybackL.assign ((size_t) scopePlaybackCapacity, 0.0f);
        stagedScopePlaybackR.assign ((size_t) scopePlaybackCapacity, 0.0f);
        stagedScopePlaybackLength = 0;

        scopePlaybackShotPhase             = ScopeShotPhase::Idle;
        scopePlaybackShotGain              = 0.0;
        scopePlaybackShotStep              = 0.0;
        scopePlaybackShotFadeSamplesCurrent = 1;
        scopePlaybackReadPos               = 0;
        scopePlaybackLength                = 0;
        scopePlaybackSwapPending           = false;
        scopeReplaceGain                   = 0.0;

        scopePlaybackModeEnabled.store (false);
        scopePlaybackStartRequest.store (false);
        scopePlaybackProgressOut.store (0.0f);
        scopePlaybackActiveOut.store (false);
    }

    engineGenerator.prepare (sampleRate, maxBlock);
    sampleSource.prepare (sampleRate, maxBlock);
    audioInSource.prepare (sampleRate, maxBlock);

    sourceHolder.setSource (sourceForKind (currentSourceKind()));
    sourceHolder.prepare (sampleRate, maxBlock);
    sourceHolder.reset();

    dopplerEngine.setSource (&sourceHolder);
    dopplerEngine.prepare (sampleRate, maxBlock, maxFieldMetres);

    sourceSmoothers.prepare (DopplerEngine::trajectoryRateHz);
    listenerSmoothers.prepare (DopplerEngine::trajectoryRateHz);
    sourceJitter.prepare (DopplerEngine::trajectoryRateHz);

    // Jeder echte Klon bekommt seinen eigenen Wackler, mit eigenem Startwert -
    // sonst durchlaufen alle dieselbe Zufallsfolge und wackeln im Gleichtakt,
    // was den Schwarm wieder zu einer einzigen Quelle zusammenzieht.
    for (size_t i = 0; i < cloneJitter.size(); ++i)
    {
        cloneJitter[i].prepare (DopplerEngine::trajectoryRateHz);
        cloneJitter[i].setSeed (0x9e3779b9u * (std::uint32_t) (i + 1) + 0x5eed4a11u);
    }

    recorderTickAccum = 0.0;

    motionRecorder.prepare (motionRecordRateHz, motionRecordMaxSeconds);

    // Kapazität des Players einmal vorwärmen: der Clip wird später im
    // Audiothread übernommen (siehe handlePendingRequests), und eine
    // Zuweisung an einen Vector mit ausreichender Kapazität kommt ohne neue
    // Allokation aus. Nur beim allerersten prepareToPlay() - siehe
    // motionPlayerCapacityWarmed in PluginProcessor.h.
    if (! motionPlayerCapacityWarmed)
    {
        std::vector<Vec3> warmup ((size_t) motionRecordMaxFrames);
        motionPlayer.setClip (warmup, motionRecordRateHz);
        motionPlayer.setClip ({}, motionRecordRateHz);

        motionPlayerCapacityWarmed = true;
    }

    // Parameterstand einlesen, BEVOR die Engine ihre Anfangsgeometrie
    // festlegt: sonst startete die Trajektorie im Ursprung und der erste Block
    // müsste den Weg zur eingestellten Position als Bewegung nachholen - man
    // hörte das als Anfahrgeräusch bei jedem Start.
    applyParameters();

    sourceSmoothers.reset (sourceTargetMetres);
    listenerSmoothers.reset (listenerTargetMetres);

    smoothedSourcePos  = sourceTargetMetres;
    listenerState.head = listenerTargetMetres;
    smoothedYawRadians = targetYawRadians;
    listenerState.yaw  = targetYawRadians;

    dopplerEngine.setFieldMetres (fieldMetresValue);
    dopplerEngine.setSourceTarget (smoothedSourcePos);
    dopplerEngine.setListener (listenerState);

    // Nimmt den eben angemeldeten Feldgrößen-Fade wieder zurück und setzt
    // beide Geometriesätze auf die Startposition.
    dopplerEngine.reset();

    lastFieldMetres = fieldMetresValue;

    outputGainLinear.reset (sampleRate, 0.02);
    outputGainLinear.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (pp.outputGain->load()));

    // Ein Neustart setzt ohnehin alles an die Zielposition - ein noch
    // laufender Schnitt haette danach nichts mehr umzubauen und liesse den
    // Ausgang nur unnoetig leise stehen.
    cutState          = CutState::Idle;
    cutGain           = 1.0;
    cutExecutePending = false;
    cutRewindsPlayer  = false;
    cutStartsFlyBy    = false;
    cutTargetMetres   = smoothedSourcePos;
    cutFadeActive     = cutFadeSeconds;

    // Kein Feldwechsel offen: der Neustart steht bereits im aktuellen
    // Massstab.
    fieldCutPending      = false;
    cutWaitsForField     = false;
    fieldSettleRemaining = 0.0;
    cutPreVelocity    = Vec3{};
    cutHoldMetres     = smoothedSourcePos;
}

void DopplerfeldProcessor::restartEngine()
{
    // @dpa-Feedback: ein blosses dopplerEngine.reset() greift nicht
    // zuverlaessig - nur ein Wechsel der Audio-Puffergroesse hilft, der loest
    // naemlich einen echten prepareToPlay()-Lauf aus. suspendProcessing()
    // haelt processBlock() waehrenddessen an (kein Datenrennen mit dem
    // Audiothread), genau wie es ein Host beim Aendern der Puffergroesse
    // ohnehin tut - vom Nachrichten-Thread aus, weil prepareToPlay()
    // allokieren darf (sein Vertrag), im Audiothread waere das verboten.
    const double sr = getSampleRate();
    const int    bs = getBlockSize();

    if (sr <= 0.0 || bs <= 0)
        return;   // noch nie prepareToPlay()-initialisiert, nichts zu tun

    suspendProcessing (true);
    prepareToPlay (sr, bs);
    suspendProcessing (false);
}

bool DopplerfeldProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Eingang fuer die "Audio In"-Quelle (@dpa-Feedback) - mono, weil jede
    // Quelle laut SoundSource-Vertrag mono ist. Manche Hosts/Formate lassen
    // den Eingangsbus trotzdem deaktiviert (IS_SYNTH-typisch); das bleibt
    // zulaessig, "Audio In" ist dann einfach still (siehe AudioInSource).
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono();
}

//======================================================================
// Parameter

Vec3 DopplerfeldProcessor::metresFromNormalised (double normX, double normY, double zMetres) const
{
    // z ist eine echte dritte Achse (Höhe über dem Boden) und geht in Metern
    // durch, während x/y am Feldmaßstab hängen. Ein Feldgrößenwechsel
    // verschiebt deshalb die Höhen nicht - genau das ist gewollt, sonst würde
    // ein größeres Feld den Hörer wachsen lassen.
    // appliedFieldMetres, nicht der Reglerwert: waehrend ein
    // Feldgroessenwechsel auf Ruhe wartet, steht die Geometrie noch im alten
    // Massstab, und die Positionen muessen dazu passen (siehe
    // appliedFieldMetres im Header).
    return { normX * appliedFieldMetres,
             normY * appliedFieldMetres * fieldAspect,
             zMetres };
}

double DopplerfeldProcessor::effectiveFlySpeed() const
{
    // Der gemeinsame Tempo-Deckel (Params::globalMaxSpeed) klemmt die
    // tatsaechlich zurueckgelegte Strecke je Tick. Liegt er unter der
    // eingestellten Fluggeschwindigkeit, kommt die Quelle nicht mit - der
    // Generator waere dann laengst am Ende der Strecke, waehrend sie noch
    // mittendrin ist, und der Flug bricht weit vor dem Endpunkt ab (gemessen
    // am Preset woandersVorbeiflug: 1825 m zu frueh, Flug nach 0,84 s statt
    // 1,98 s beendet).
    //
    // Deshalb fliegt der Generator von vornherein mit dem, was der Deckel
    // durchlaesst. Ein stiller Deckel ist das nicht: die Geschwindigkeit steht
    // in der Anzeige, und beide Regler gehoeren @dpa.
    return std::min ((double) pp.flySpeed->load(), (double) pp.globalMaxSpeed->load());
}

void DopplerfeldProcessor::holdSourceTargetAt (Vec3 posMetres)
{
    sourceTargetMetres = posMetres;
    sourceTargetHeld   = true;

    // Der Reglerstand, den dieser Haltezustand meint. Bewegt sich einer davon,
    // ist das ein Benutzereingriff und der Halt ist vorbei (applyParameters).
    heldSrcX = pp.srcX->load();
    heldSrcY = pp.srcY->load();
    heldSrcZ = pp.srcZ->load();
}

void DopplerfeldProcessor::applyParameters()
{
    applySourceAndListenerParameters();
    applyJitterParameters();
    applyFieldSizeParameters();
    applySmoothingParameters();
    applyFlyByAndPlaybackParameters();
    applyEngineParameters();
    applySampleParameters();
    applyPhysicsParameters();
    applyNWaveAndShockParameters();
    applyReflectionParameters();
    applyCloneParameters();
    applyWallParameters();
    applyTapParameters();
    applyMediumAndOutputParameters();
}

void DopplerfeldProcessor::applySourceAndListenerParameters()
{
    fieldMetresValue = (double) pp.fieldMetres->load();

    // Nach einem Vorbeiflug bleibt die Quelle dort stehen, wo der Flug endete
    // (siehe holdSourceTargetAt). Der Regler zeigt dann noch auf den
    // Startpunkt, und ihm blind zu folgen hiesse: die Quelle rast nach dem
    // Flug dorthin zurueck - gemessen mit bis zu 1651 m/s auf einer Bahn, die
    // mit 200 m/s geflogen ist. Erst wenn jemand den Regler wirklich bewegt,
    // gewinnt wieder er.
    {
        const float srcX = pp.srcX->load();
        const float srcY = pp.srcY->load();
        const float srcZ = pp.srcZ->load();

        // Identitaetsvergleich mit Absicht: gefragt ist nicht "fast gleich",
        // sondern "hat der Regler seit dem Festhalten ueberhaupt einen
        // Schreibzugriff gesehen". Eine Toleranz wuerde genau die kleinen
        // Bewegungen verschlucken, auf die es hier ankommt.
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
        const bool knobsUntouched = sourceTargetHeld
                                 && srcX == heldSrcX && srcY == heldSrcY && srcZ == heldSrcZ;
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE

        if (! knobsUntouched)
        {
            sourceTargetHeld   = false;
            sourceTargetMetres = metresFromNormalised ((double) srcX, (double) srcY, (double) srcZ);

            // Ein Griff an die Quelle beendet auch ein laufendes Auslaufen:
            // wer sie wieder anfasst, will sie dort haben und nicht noch ein
            // Stueck weiterfliegen sehen.
            coastActive = false;
        }
    }

    listenerTargetMetres = metresFromNormalised ((double) pp.lisX->load(), (double) pp.lisY->load(),
                                                 (double) pp.lisZ->load());

    targetYawRadians         = juce::degreesToRadians ((double) pp.lisYaw->load());

    dopplerEngine.setPanoramaAmount (0.01 * (double) pp.panAmount->load());
    listenerState.earSpacing = (double) pp.earSpacing->load();
}

void DopplerfeldProcessor::applyJitterParameters()
{
    // Ein Schalter fuer das Wackeln insgesamt, Quelle wie Klone. Abgeschaltet
    // wird ueber den Ausschlag und nicht durch Ueberspringen des tick():
    // die Oszillatoren laufen weiter, also setzt das Wackeln beim
    // Wiedereinschalten dort ein, wo es gerade waere, statt aus der
    // eingefrorenen Phase heraus anzuspringen.
    const bool   jitterOn     = pp.srcJitterOn->load() > 0.5f;
    const double jitterAmount = jitterOn ? (double) pp.srcJitterAmount->load() : 0.0;

    // Bahngeschwindigkeit des Wacklers, in m/s. Zusammen mit dem Ausschlag
    // beschreibt sie die Bewegung vollstaendig; die Frequenz rechnet der
    // Wackler sich daraus selbst aus (siehe PositionJitter::setSpeed).
    //
    // Der Wackler laeuft NICHT durch den harten Schrittdeckel in
    // advanceMotion - dort wuerde er zerhackt. Einen eigenen Deckel braucht
    // er nicht: schneller als hier eingestellt bewegt er sich nicht.
    const double jitterSpeed = (double) pp.srcJitterSpeed->load();

    // Anteil der Hoehe am Wackeln (@dpa 20260824). Bei 0 bleibt die Quelle
    // auf ihrer eingestellten Hoehe und wackelt nur in der Ebene.
    const double jitterZAmount  = (double) pp.srcJitterZAmount->load();

    sourceJitter.setAmount    (jitterAmount);
    sourceJitter.setSpeed     (jitterSpeed);
    sourceJitter.setZFactor   (jitterZAmount);

    // Dieselben Regler wie fuer die Quelle: @dpa hat den Jitter dort
    // ausprobiert und will genau diesen auf den Klonen, nicht einen zweiten
    // Satz Regler.
    for (auto& j : cloneJitter)
    {
        j.setAmount    (jitterAmount);
        j.setSpeed     (jitterSpeed);
        j.setZFactor   (jitterZAmount);
    }
}

void DopplerfeldProcessor::applyFieldSizeParameters()
{
    // Feldgroesse: nicht anwenden, sondern in die Stille legen (siehe
    // appliedFieldMetres im Header). Angewendet wird sie erst im stillen
    // Fenster des Schnitts, und erst, wenn der Regler zur Ruhe gekommen ist.
    const bool fieldJustChanged = std::abs (fieldMetresValue - lastFieldMetres) > 1.0e-9;

    if (fieldJustChanged)
    {
        lastFieldMetres = fieldMetresValue;

        // "Dann von vorne": jede weitere Aenderung setzt die Ruhezeit zurueck.
        fieldSettleRemaining = fieldSettleSeconds;

        if (! fieldCutPending)
        {
            fieldCutPending  = true;
            cutWaitsForField = true;
            cutFadeActive    = fieldFadeSeconds;

            // Ziel vorlaeufig die aktuelle Stelle - die endgueltige steht erst
            // fest, wenn der neue Massstab uebernommen ist
            // (handlePendingRequests).
            beginCut (smoothedSourcePos, false);
        }
    }

    if (fieldCutPending)
    {
        fieldSettleRemaining -= (double) currentBlockSamples / std::max (1.0, currentSampleRate);

        // Erst wenn der Regler steht UND die Blende unten ist. Beides muss
        // stimmen: umgebaut wird nur in der Stille, und nur einmal.
        if (fieldSettleRemaining <= 0.0 && cutGain <= 0.0)
        {
            cutWaitsForField  = false;
            cutExecutePending = true;
        }
    }
}

void DopplerfeldProcessor::applySmoothingParameters()
{
    // --- Bewegungsglättung ---
    const double tau  = (double) pp.smootherTau->load();
    const double vMax = (double) pp.slewVmax->load();
    // Die Beschleunigungsgrenze folgt aus dem Tempo (siehe
    // SlewLimiter::accelTimeSeconds) - ein Regler statt zweier.
    const double aMax = vMax / SlewLimiter::accelTimeSeconds;

    sourceSmoothers.setType ((int) pp.smootherType->load(), smoothedSourcePos);
    listenerSmoothers.setType ((int) pp.smootherType->load(), listenerState.head);

    sourceSmoothers.applyParameters (tau, vMax, aMax);
    listenerSmoothers.applyParameters (tau, vMax, aMax);

    yawSmoothCoeff = 1.0 - std::exp (-1.0 / (DopplerEngine::trajectoryRateHz * std::max (1.0e-3, tau)));
}

void DopplerfeldProcessor::applyFlyByAndPlaybackParameters()
{
    // --- Vorbeiflug ---
    // Nur das Tempo wird laufend nachgeführt; Bahnart, Startvariante und
    // Abstand legt der Start fest (siehe handlePendingRequests). Eine
    // Bahnart, die sich mitten im Flug ändert, wäre ein Positionssprung -
    // das Tempo dagegen ist genau der Wert, den @dpa live automatisieren will.
    flyBy.setSpeed (effectiveFlySpeed());

    // --- Wiedergabe ---
    motionPlayer.setSpeed ((double) pp.playSpeed->load());
    motionPlayer.setLooping (pp.playLoop->load() > 0.5f);
    motionPlayer.setInterp (pp.playInterp->load() < 0.5f ? MotionPlayer::Interp::Linear
                                                         : MotionPlayer::Interp::CatmullRom);
}

void DopplerfeldProcessor::applyEngineParameters()
{
    // --- Motor ---
    engineGenerator.setRpm (pp.rpm->load());
    engineGenerator.setKindLevelDb (pp.engineLevelDb->load());
    engineGenerator.setRocketShock (pp.rocketShock->load());
    engineGenerator.setRotorSlap (pp.rotorSlap->load());

    // Klangvorlage + Klangfarbe je Rausch-Betriebsart, dazu die Form der
    // Druckstoesse. Beides wirkt nur in der jeweiligen Betriebsart, wird aber
    // wie alle uebrigen Werte hier bedingungslos durchgereicht - der
    // Generator entscheidet, was er davon braucht.
    engineGenerator.setJetVoice ((int) pp.jetVoice->load(), pp.jetTone->load());
    engineGenerator.setRocketVoice ((int) pp.rocketVoice->load(), pp.rocketTone->load());
    engineGenerator.setRocketShockShape (pp.rocketShockSize->load(), pp.rocketShockRate->load());
    engineGenerator.setRocketFarColour (pp.rocketFarColour->load());

    // Dasselbe Reglerpaar, das die Stossfronten der Ausbreitung steuert, geht
    // auch an die Rakete: ihre Druckstoesse entstehen im Generator und nicht
    // als Kegelankunft, und ohne diese Weitergabe wirkt "Duck-Reichw." dort
    // gar nicht (@dpa 20260825: "ich habe bei Rakete noch keinen Unterschied
    // zwischen den Druckreichweiten gehoert").
    engineGenerator.setRocketShockDuck ((float) shockDuckFixedAmount,
                                        pp.shockDuckRange->load());

    for (int i = 0; i < 4; ++i)
        engineGenerator.setSineMode (i, pp.harmSine[i]->load() > 0.5f);

    engineGenerator.setHarmonicsOn (pp.oscOn->load() > 0.5f);

    for (int i = 0; i < 4; ++i)
        engineGenerator.setHarmonic (i,
                                     pp.harmRatio[i]->load(),
                                     pp.harmDetune[i]->load(),
                                     pp.harmTrack[i]->load(),
                                     pp.harmLevel[i]->load());

    engineGenerator.setNoiseParams (pp.noiseFcLo->load(), pp.noiseFcHi->load(),
                                    pp.noiseGainLo->load(), pp.noiseGainHi->load(),
                                    pp.noiseQ->load());
    engineGenerator.setJitter (pp.jitterAmount->load(), pp.jitterRateHz->load());
    engineGenerator.setImbalance (pp.imbalance->load());
    engineGenerator.setImbalanceOctave (pp.imbalanceOctave->load());

    engineGenerator.setEngineKind ((int) pp.engineKind->load());

    // Propellerpaar: eingeschaltet ueber dieselbe Betriebsart-Auswahl wie die
    // Klangarten (Index 4, siehe Params::engineKind), aber umgesetzt in der
    // Geometrie statt im Generator - es sind zwei zusaetzliche Schallwege,
    // kein zweiter Klang.
    dopplerEngine.setPropellers ((int) pp.engineKind->load() == 4,
                                 juce::Decibels::decibelsToGain ((double) pp.propLevelDb->load()));
    engineGenerator.setHeliRotor (pp.heliRotorHz->load(), pp.heliBladeCount->load());
    engineGenerator.setRotorDoppler (pp.heliDoppler->load() > 0.5f);
    engineGenerator.setRotorQuantise (pp.heliQuantise->load() > 0.5f);
    engineGenerator.setRotorRadius (pp.heliRotorRadius->load());

    // Wie stark die Sichtlinie zum Hoerer in der Rotorebene liegt (siehe
    // EngineGenerator::setRotorInPlane). Der Rotor liegt waagerecht, seine
    // Ebene ist also xy - der Anteil ist damit der waagerechte Abstand,
    // geteilt durch den vollen. Von der Seite gesehen 1, direkt darueber 0.
    //
    // Gerechnet aus den ZIELpositionen, nicht aus den geglaetteten: dieser
    // Wert steuert eine Klangfarbe, keine Laufzeit, und er wandert ohnehin nur
    // so schnell wie der Ueberflug selbst.
    {
        const Vec3   toListener = listenerTargetMetres - sourceTargetMetres;
        const double flat       = std::sqrt (toListener.x * toListener.x
                                             + toListener.y * toListener.y);
        const double full       = toListener.length();

        engineGenerator.setRotorInPlane (full > 1.0e-6 ? (float) (flat / full) : 1.0f);

        // Derselbe Abstand fuettert die Rakete (siehe
        // EngineGenerator::setRocketDistance): Klangfarbe aus der Ferne und
        // Tiefe der Absenkung durch die Druckstoesse haengen daran.
        engineGenerator.setRocketDistance ((float) full);
    }

    // Wie schnell die Quelle auf den Hoerer zufaehrt. Auf der vorlaufenden
    // Rotorseite addiert sich das zur Umfangsgeschwindigkeit der Blattspitze -
    // erst zusammen reissen sie die Grenze, ab der die Verdichtungsstoesse
    // sich von der Spitze loesen (siehe EngineGenerator::setRotorFlightSpeed).
    engineGenerator.setRotorFlightSpeed ((float) sourceClosingSpeed);
}

void DopplerfeldProcessor::applySampleParameters()
{
    // --- Sample ---
    sampleSource.setGainDb (pp.sampleGain->load());
    sampleSource.setPitchSemitones (pp.samplePitch->load());
    sampleSource.setLoopStart (pp.loopStart->load());
    sampleSource.setLoopEnd (pp.loopEnd->load());
    sampleSource.setLoopXfadeMs (pp.loopXfadeMs->load());
    sampleSource.setEqLowGainDb (pp.eqLowGain->load());
    sampleSource.setEqMidGainDb (pp.eqMidGain->load());
    sampleSource.setEqMidFreqHz (pp.eqMidFreq->load());
    sampleSource.setEqHighGainDb (pp.eqHighGain->load());
}

void DopplerfeldProcessor::applyPhysicsParameters()
{
    // --- Physik ---
    // Beide Setter laufen über alle Pfade beider Geometriesätze, deshalb nur
    // bei tatsächlicher Änderung.
    const double boom = (double) pp.boomLimitDb->load();

    if (std::abs (boom - lastBoomLimitDb) > 1.0e-9)
    {
        lastBoomLimitDb = boom;
        dopplerEngine.setBoomLimitDb (boom);
    }

    const double airAbsorb = (double) pp.airAbsorbAmount->load();

    if (std::abs (airAbsorb - lastAirAbsorbAmount) > 1.0e-9)
    {
        lastAirAbsorbAmount = airAbsorb;
        dopplerEngine.setAirAbsorptionAmount (airAbsorb);
    }

    // Wie oben nur bei tatsaechlicher Aenderung: der Setter laeuft ueber alle
    // Pfade beider Geometriesaetze.
    const double distCurve = (double) pp.distanceCurve->load();

    if (std::abs (distCurve - lastDistanceCurve) > 1.0e-9)
    {
        lastDistanceCurve = distCurve;
        dopplerEngine.setDistanceCurve (distCurve);
    }
}

void DopplerfeldProcessor::applyNWaveAndShockParameters()
{
    // Wie oben: der Setter läuft über alle Pfade beider Geometriesätze,
    // deshalb nur bei tatsächlicher Änderung.
    const bool   nWaveEnabled = pp.nWaveOn->load() > 0.5f;
    const double nWaveSize    = (double) pp.nWaveSize->load();
    const double nWaveGainDb  = (double) pp.nWaveGainDb->load();
    const double nWaveEdge    = (double) pp.nWaveEdge->load();
    const double nWavePressure = (double) pp.nWavePressure->load();

    if (nWaveEnabled != lastNWaveOn || std::abs (nWaveSize - lastNWaveSize) > 1.0e-9
        || std::abs (nWaveGainDb - lastNWaveGainDb) > 1.0e-9
        || std::abs (nWaveEdge - lastNWaveEdge) > 1.0e-9
        || std::abs (nWavePressure - lastNWavePressure) > 1.0e-9)
    {
        lastNWaveOn     = nWaveEnabled;
        lastNWaveSize   = nWaveSize;
        lastNWaveGainDb = nWaveGainDb;
        lastNWaveEdge   = nWaveEdge;
        lastNWavePressure = nWavePressure;
        dopplerEngine.setNWave (nWaveEnabled, nWaveSize,
                                juce::Decibels::decibelsToGain (nWaveGainDb),
                                nWaveEdge, nWavePressure);
    }

    // Rueckwaerts-Pegel, Stossfront-Absenkung und Schattenausklang: dieselbe
    // Wiedervorlage wie bei der N-Welle, denn auch diese Setter laufen ueber
    // alle Pfade beider Geometriesaetze.
    const double extraPathGainDb = (double) pp.extraPathGainDb->load();
    // Fest auf voller Tiefe: waehrend eine Stossfront ueber den Hoerweg
    // laeuft, kommt nichts anderes durch (@dpa 20260827: "Front-Duck (=1)
    // kann weg" - er stand ohnehin immer dort).
    const double shockDuckAmount = shockDuckFixedAmount;
    const double shockDuckRange  = (double) pp.shockDuckRange->load();
    const double jumpBoom        = (double) pp.jumpBoom->load();


    if (std::abs (extraPathGainDb - lastExtraPathGainDb) > 1.0e-9)
    {
        lastExtraPathGainDb = extraPathGainDb;
        dopplerEngine.setExtraPathGain (juce::Decibels::decibelsToGain (extraPathGainDb));
    }

    if (std::abs (shockDuckAmount - lastShockDuckAmount) > 1.0e-9
        || std::abs (shockDuckRange - lastShockDuckRange) > 1.0e-9)
    {
        lastShockDuckRange = shockDuckRange;
        lastShockDuckAmount = shockDuckAmount;
        dopplerEngine.setShockDuck (shockDuckAmount, shockDuckRange);
    }


    if (std::abs (jumpBoom - lastJumpBoom) > 1.0e-9)
    {
        lastJumpBoom = jumpBoom;
        dopplerEngine.setJumpBoom (jumpBoom);
    }

    const double jumpBoomSize = (double) pp.jumpBoomSize->load();

    if (std::abs (jumpBoomSize - lastJumpBoomSize) > 1.0e-9)
    {
        lastJumpBoomSize = jumpBoomSize;
        dopplerEngine.setJumpSize (jumpBoomSize);
    }

}

void DopplerfeldProcessor::applyReflectionParameters()
{
    // Beides ist inzwischen billig: die Engine legt Schalter und Dämpfung an
    // ihrer Fläche ab und reicht sie vor jedem Block an die Pfade durch, statt
    // dass hier über alle Pfade beider Geometriesätze gelaufen werden müsste.
    dopplerEngine.setGroundReflectionEnabled (pp.groundReflectionOn->load() > 0.5f);
    dopplerEngine.setGroundDampingAmount ((double) pp.groundDampAmount->load());
    dopplerEngine.setGroundGain (juce::Decibels::decibelsToGain ((double) pp.groundGain->load()));

    dopplerEngine.setSecondOrderEnabled (pp.reflect2ndOn->load() > 0.5f);
    dopplerEngine.setBounceGain ((double) pp.bounceGain->load());
    dopplerEngine.setBounceGainBoost (juce::Decibels::decibelsToGain ((double) pp.bounceGainDb->load()));
}

void DopplerfeldProcessor::applyWallParameters()
{
    // Wände: nur die Ziele einsammeln, gefolgt wird ihnen in advanceMotion().
    for (int w = 0; w < DopplerEngine::maxWalls; ++w)
    {
        wallTarget[w].on         = pp.wallOn[w]->load() > 0.5f;
        wallTarget[w].damping    = (double) pp.wallDamp[w]->load();
        wallTarget[w].gainLinear = juce::Decibels::decibelsToGain ((double) pp.wallGain[w]->load());
        wallTarget[w].anchor     = metresFromNormalised ((double) pp.wallX[w]->load(),
                                                         (double) pp.wallY[w]->load(), 0.0);
        wallTarget[w].azimuthRad = juce::degreesToRadians ((double) pp.wallAngle[w]->load());
        wallTarget[w].tiltRad    = juce::degreesToRadians ((double) pp.wallTilt[w]->load());
    }

    // Beim allerersten Durchgang und nach jedem Schnitt ohne Anlauf auf das
    // Ziel setzen - sonst fuehre die Wand beim Start sichtbar und hoerbar aus
    // dem Ursprung heran, und beim Zustandswechsel wanderte sie von der alten
    // zur neuen Stelle (siehe snapWallsPending im Header).
    //
    // Der Feldmassstab braucht hier keinen eigenen Anlass mehr: eine
    // Feldgroessenaenderung laeuft seit dem 27.08. selbst ueber einen Schnitt,
    // und der setzt dasselbe Merkzeichen.
    if (! wallStateInitialised || snapWallsPending)
    {
        for (int w = 0; w < DopplerEngine::maxWalls; ++w)
            wallSmoothed[w] = wallTarget[w];

        wallStateInitialised = true;
        snapWallsPending     = false;
    }
}

void DopplerfeldProcessor::applyTapParameters()
{
    // Wie bei den Waenden nur die Ziele einsammeln; gefolgt wird ihnen in
    // advanceMotion().
    for (int t = 0; t < Params::tapCount; ++t)
    {
        TapState& target = tapTarget[t];

        target.on         = pp.tapOn[t]->load() > 0.5f;
        target.pos        = metresFromNormalised ((double) pp.tapX[t]->load(),
                                                  (double) pp.tapY[t]->load(),
                                                  (double) pp.tapZ[t]->load());
        target.type       = (int) std::lround (pp.tapType[t]->load());
        target.room       = (double) pp.tapRoom[t]->load();
        target.decay      = (double) pp.tapDecay[t]->load();
        target.damping    = (double) pp.tapDamp[t]->load();
        target.gainLinear = juce::Decibels::decibelsToGain ((double) pp.tapGain[t]->load());
        target.width      = (double) pp.tapWidth[t]->load();
        target.predelay   = pp.tapPredelay[t]->load() > 0.5f;
    }

    // Beim ersten Durchgang und nach jedem Schnitt sofort auf das Ziel, sonst
    // wanderte der Punkt beim Presetwechsel sichtbar von der alten zur neuen
    // Stelle - dasselbe Argument wie bei den Waenden, und deshalb auch
    // dasselbe Merkzeichen.
    if (! tapStateInitialised || snapTapsPending)
    {
        for (int t = 0; t < Params::tapCount; ++t)
            tapSmoothed[t] = tapTarget[t];

        tapStateInitialised = true;
        snapTapsPending     = false;
    }
}

void DopplerfeldProcessor::applyMediumAndOutputParameters()
{
    // --- Ausgang ---
    // Ausgangspegel, -36 bis +36 dB (siehe Params::outputGain), multipliziert
    // mit dem Dichte-Pegelfaktor der Hoehe (Params::airAltitude): der
    // Schalldruck einer gegebenen Quelle skaliert naeherungsweise mit der
    // Luftdichte, in duenner Hoehenluft ist alles leiser. Das ist ein reiner
    // Amplitudenfaktor am Ausgang, keine Physik der Ausbreitung selbst -
    // deshalb hier und nicht in den Physics-Pfaden (die rechnen weiterhin nur
    // mit c(T) ueber MediumState).
    //
    // Normiert auf die Regler-Defaults (20°C, 0 m): dort ist der reale
    // Dichtefaktor rho/rho0 physikalisch NICHT exakt 1.0 (rho0 = 1,225 kg/m^3
    // gilt bei 15°C), sondern rund 0,983 - durch Teilen mit dem Faktor bei
    // Default-Werten (statt einer fest eingetragenen Zahl) klingt jedes
    // Preset ohne diese beiden Parameter unveraendert, auch falls sich die
    // Defaults je aendern sollten.
    MediumState currentMedium;
    currentMedium.tempCelsius    = (double) pp.airTempC->load();
    currentMedium.altitudeMetres = (double) pp.airAltitude->load();

    const MediumState defaultMedium;   // tempCelsius=20, altitudeMetres=0
    const double densityGain = currentMedium.densityGain() / defaultMedium.densityGain();

    // Der Ausgangspegel des GELADENEN Zustands darf erst gelten, wenn auch
    // sein Klang gilt. Waehrend der Ausblende steht noch das Signal des
    // vorigen Presets im Ausgang - bekaeme es schon den neuen Pegel, waere
    // genau das der Ausbruch, den @dpa hoert: 600kmh-Drone@600m2 liegt bei
    // +12 dB, drone@1km2 bei +26 dB, das alte Signal liefe also 14 dB zu laut
    // aus. Uebernommen wird der Wert hart im stillen Fenster
    // (handlePendingRequests), wie Geometrie, Hoerer und Waende auch.
    const double outTarget = juce::Decibels::decibelsToGain (pp.outputGain->load()) * densityGain;

    if (stateLoadRequest.load() || (cutClearsSignal && cutState == CutState::FadingOut))
        pendingOutputGainLinear = outTarget;
    else
        outputGainLinear.setTargetValue ((float) outTarget);

    limiterEnabled = pp.limiterOn->load() > 0.5f;
}

void DopplerfeldProcessor::applyCloneParameters()
{
    // Keine Automatik, keine billige Nachbildung mehr (@dpa: "nur echte
    // Klones, alles andere weg, keine 'billigen', die bringen nichts") - die
    // Zahl der echten Klone ist ab jetzt fest gleich der Gesamtzahl, nur nach
    // oben durch maxRealClones gedeckelt.
    const int total = (int) pp.cloneTotal->load();

    effectiveRealClones = juce::jlimit (0, DopplerEngine::maxRealClones, total);

    // cloneRealLevel ist seit @dpas Wunsch ein dB-Gain (-36..+36dB, 0dB =
    // unveraendert) statt eines 0..1-Pegels - die Umrechnung in den linearen
    // Faktor gehoert hierher, DopplerEngine bekommt nur noch den fertigen
    // Faktor.
    const double gainLinear = juce::Decibels::decibelsToGain ((double) pp.cloneRealLevel->load());

    // Der Z-Anteil ist ausdruecklich DERSELBE Regler wie beim Wackler der
    // Quelle (@dpa 20260826: "zwei mal den Control scheint zuviel, aber in
    // beiden ist er wichtig.. vielleicht gegenseitig ferngesteuert/gleich
    // geschaltet?") - ein Parameter, zwei Regler im UI (Bewegung und
    // Schwarm), die dadurch von selbst gleich stehen.
    dopplerEngine.setRealClones (effectiveRealClones,
                                 (double) pp.cloneSpread->load(),
                                 (double) pp.srcJitterZAmount->load(),
                                 gainLinear);

    activeRealClones.store (effectiveRealClones);
}

void DopplerfeldProcessor::beginCut (Vec3 targetMetres, bool rewindPlayer, bool startsFlyBy,
                                     Vec3 preVelocity, bool clearsSignal)
{
    // Ein bereits laufender Schnitt wird nicht neu angestossen, sondern nur
    // umgelenkt: sein Ziel ist immer das zuletzt angemeldete. Sonst kaskadieren
    // zwei kurz aufeinanderfolgende Ereignisse (Zustand laden waehrend eines
    // Rundenwechsels) zu einer Kette halber Blenden.
    cutTargetMetres  = targetMetres;
    cutPreVelocity   = preVelocity;
    cutRewindsPlayer = cutRewindsPlayer || rewindPlayer;
    cutStartsFlyBy   = cutStartsFlyBy   || startsFlyBy;
    cutClearsSignal  = cutClearsSignal  || clearsSignal;

    if (cutState == CutState::FadingOut)
        return;

    // Wo die Quelle waehrend der Ausblende steht. Nicht das Ziel: sie soll
    // sich bis zum Schnitt gar nicht mehr bewegen.
    cutHoldMetres = smoothedSourcePos;
    cutState      = CutState::FadingOut;

    // Ein gewoehnlicher Schnitt blendet kurz. Der Feldwechsel setzt seine
    // laengere Dauer direkt danach selbst (siehe fieldCutPending).
    if (! cutWaitsForField)
        cutFadeActive = cutFadeSeconds;
}

void DopplerfeldProcessor::handlePendingRequests()
{
    // Der Umbau des Schnitts, angemeldet von der Ausblende im Ausgang. Er
    // steht ganz vorn: alles, was danach in diesem Block passiert, soll schon
    // die neue Lage sehen.
    if (cutExecutePending)
    {
        cutExecutePending = false;

        // Das gespeicherte Quellsignal des vorigen Zustands loeschen (siehe
        // cutClearsSignal im Header). Es steht ganz vorn, damit der Umbau
        // darunter schon auf einem leeren Puffer aufsetzt.
        if (cutClearsSignal)
        {
            cutClearsSignal = false;

            dopplerEngine.clearSignalHistory();

            // Der zurueckgehaltene Ausgangspegel des geladenen Zustands, jetzt
            // ohne Rampe: die Rampe waere wieder ein Uebergang zwischen zwei
            // Presets, und der Ausgang ist an dieser Stelle still.
            outputGainLinear.setCurrentAndTargetValue ((float) pendingOutputGainLinear);
        }

        // Der neue Massstab wird hier uebernommen, im stillen Fenster und vor
        // allem anderen: die Ziele darunter sollen ihn schon sehen. Danach ist
        // eine Feldgroessenaenderung nur noch ein gewoehnlicher Schnitt an
        // eine andere Stelle.
        if (fieldCutPending)
        {
            fieldCutPending = false;

            appliedFieldMetres = fieldMetresValue;

            // Die nach einem Flug gehaltene Position gehoert zum alten
            // Massstab und laege jetzt woanders im Feld.
            sourceTargetHeld     = false;
            sourceTargetMetres   = metresFromNormalised ((double) pp.srcX->load(),
                                                         (double) pp.srcY->load(),
                                                         (double) pp.srcZ->load());
            listenerTargetMetres = metresFromNormalised ((double) pp.lisX->load(),
                                                         (double) pp.lisY->load(),
                                                         (double) pp.lisZ->load());

            listenerSmoothers.reset (listenerTargetMetres);
            listenerState.head = listenerTargetMetres;

            cutTargetMetres = sourceTargetMetres;

            // Die Engine braucht den Wert fuer die Anzeige der Wellenfronten.
            // Die Ueberblendung, die er sonst anmeldet, nimmt cutTo() gleich
            // wieder zurueck - umgebaut wird geschnitten, nicht geblendet.
            dopplerEngine.setFieldMetres (fieldMetresValue);
        }

        if (cutRewindsPlayer)
            motionPlayer.restartRound();

        cutRewindsPlayer = false;

        if (cutStartsFlyBy)
        {
            cutStartsFlyBy = false;

            // startFlyBy() setzt Glaetter, Bahn-Vorgeschichte und Geometrie
            // selbst - hier im stillen Fenster, also ohne dass irgendetwas
            // davon zu hoeren waere.
            startFlyBy();
        }
        else
        {
            // Glaetter UND Geometrie an der neuen Stelle neu aufsetzen. Beides
            // gehoert zusammen: ein Glaetter, der noch am alten Ort steht, schriebe
            // im naechsten Tick den Weg dorthin zurueck.
            smoothedSourcePos = cutTargetMetres;
            sourceSmoothers.reset (cutTargetMetres);
            wasMotionSlewGuardActive = false;

            // Der HOERER springt mit (@dpa 20260828: "der L schaltet beim
            // Preset Umschalten nicht sofort um (wie alles andere, wie es
            // sollte), sondern wird von alt zu neu gesmoothed bewegt, was zu
            // keinem Preset gehoert, nur im Uebergang vorkommt und falsch
            // ist").
            //
            // Ein Schnitt ist ein Umbau, keine Bewegung - und das gilt fuer
            // Quelle UND Hoerer. Liefe der Hoerer weiter durch seinen
            // Glaetter, fuehre er die Strecke zwischen den beiden Presets ab:
            // eine Kopfbewegung mit Doppler und Pegelfahrt, die in keinem der
            // beiden Zustaende vorkommt.
            //
            // Der Sprung ist hier unhoerbar, weil er im stillen Fenster
            // zwischen Aus- und Einblende liegt - dieselbe Ueberlegung wie
            // bei der Quelle eine Zeile darueber.
            listenerState.head = listenerTargetMetres;
            listenerSmoothers.reset (listenerTargetMetres);

            // Und die Waende: sie haengen am selben Massstab und an denselben
            // Reglern, also gehoeren sie in denselben Umbau. Sie springen erst
            // im naechsten Block, denn ihre Ziele werden in applyParameters()
            // gebildet (siehe snapWallsPending im Header).
            snapWallsPending = true;
            snapTapsPending  = true;

            smoothedYawRadians = targetYawRadians;
            listenerState.yaw  = targetYawRadians;

            dopplerEngine.setListener (listenerState);

            dopplerEngine.setSourceTarget (cutTargetMetres);

            // Die Vorgeschwindigkeit gehoert zum Ereignis, das den Schnitt
            // ausgeloest hat: der Rundenwechsel einer Wiedergabe uebergibt die
            // Anfangsgeschwindigkeit der neuen Runde, ein geladener Zustand
            // nichts. Nur so bekommt die Bahn eine bewegte statt einer
            // ruhenden Vorgeschichte (siehe cutPreVelocity im Header).
            // Plus den Wackler-Versatz: genau diese Summe schreibt der
            // naechste Tick in die Bahn (siehe lastSourceJitter im Header).
            // Ohne ihn steht zwischen Vorgeschichte und erstem Tick ein
            // Sprung in Hoehe des Ausschlags.
            dopplerEngine.cutTo (cutTargetMetres + lastSourceJitter, cutPreVelocity);
        }

        cutPreVelocity = Vec3{};

        cutState = CutState::FadingIn;
    }

    if (panicRequest.exchange (false))
    {
        // Sofort, ohne auf die Parameter zu warten: das ist der Knopf für den
        // Fall, dass die CPU-Anzeige oben steht und der Ton wegbleibt. Der
        // Editor setzt die Parameter zusätzlich zurück, damit die Schalter
        // zeigen, was passiert ist - aber die Wirkung darf nicht davon
        // abhängen, dass der Message-Thread noch durchkommt.
        dopplerEngine.disableAllReflections();

        effectiveRealClones = 0;

        activeRealClones.store (0);
    }

    // Ein geladener Zustand ist ein Umbau, keine Bewegung: geschnitten statt
    // hingeflogen (siehe CutState im Header). applyParameters() lief in
    // diesem Block schon, sourceTargetMetres steht also bereits auf der
    // geladenen Position.
    if (stateLoadRequest.exchange (false))
    {
        // Die Ruhezeit des Feldgroessenreglers gilt dem DREHENDEN Regler:
        // solange @dpa ihn bewegt, soll nicht bei jedem Zwischenwert umgebaut
        // werden. Ein geladener Zustand bringt seinen Wert fertig mit, es gibt
        // nichts abzuwarten - und jede Zehntelsekunde Warten ist eine
        // Zehntelsekunde, in der das alte Signal noch klingt.
        fieldSettleRemaining = 0.0;

        beginCut (sourceTargetMetres, false, false, Vec3{}, true);
    }

    // Nachlauf: die Maus wurde losgelassen, ab hier traegt die Quelle sich
    // selbst weiter (siehe startSourceCoast im Header).
    if (coastRequest.exchange (false))
    {
        // Die Geschwindigkeit der QUELLE, nicht die der Maus: nur so geht das
        // Auslaufen ohne Knick aus der gezogenen Bewegung hervor (siehe
        // lastSourceVelocity im Header).
        const Vec3 v = lastSourceVelocity;

        coastPos = smoothedSourcePos;

        // Beide Stufen auf die Anfangsgeschwindigkeit: so beginnt die zweite
        // (die zaehlt) mit Steigung null statt mit einem Knick.
        coastVelocityStage1 = v;
        coastVelocityStage2 = v;

        const double speed = v.length();

        // Restweg bis zum Loslasspunkt, PROJIZIERT auf die Fahrtrichtung: nur
        // der Anteil, den der Nachlauf ueberhaupt zuruecklegen kann. Was quer
        // dazu fehlt, bleibt liegen - die Richtung zu drehen waere der Knick,
        // den die zwei Stufen gerade vermeiden sollen.
        //
        // Ein negativer Anteil heisst, die Quelle ist schon ueber den Punkt
        // hinaus; dann gilt die Untergrenze und sie kommt zuegig zur Ruhe.
        if (speed > coastRestSpeed)
        {
            const Vec3   toTarget = sourceTargetMetres - smoothedSourcePos;
            const double along    = toTarget.dot (v) / speed;

            coastTau = juce::jlimit (coastTauMin, coastTauMax,
                                     along / (2.0 * speed));
        }
        else
            coastTau = coastTauSeconds;

        coastActive = speed > coastRestSpeed;

        // Das Ziel bleibt der LOSLASSPUNKT, nicht die Stelle, an der der
        // Nachlauf gerade steht. Das ist der zweite Teil derselben Sache:
        // schafft der Nachlauf den Rueckstand nicht ganz, laeuft die Quelle
        // danach mit dem gewohnten Glaetter dorthin weiter, statt auf halbem
        // Weg stehenzubleiben. Gehalten wird es, damit ein Griff an den Regler
        // den Nachlauf weiterhin beendet (siehe knobsUntouched in
        // applyParameters) - ohne das Halten waere er nach einem Block vorbei.
        if (coastActive)
            holdSourceTargetAt (sourceTargetMetres);
    }

    if (sourceSwitchRequest.exchange (false))
        sourceHolder.switchTo (sourceForKind (currentSourceKind()));

    // Motor-Gating (@dpa-Feedback): Flankenwechsel von motorGateEnabled zuerst
    // (kein eigenes Anfrage-Flag, siehe Header), dann die diskreten Greif-/
    // Loslass-Ereignisse - eine Reihenfolge, damit ein Griff, der genau in
    // dem Block passiert, in dem das Gating gerade erst aktiviert wurde, den
    // richtigen Startzustand sieht statt vom alten ueberschrieben zu werden.
    {
        const bool gateEnabledNow = motorGateEnabled.load();

        if (gateEnabledNow != motorGateEnabledShadow)
        {
            motorGateEnabledShadow = gateEnabledNow;

            // Aktiviert: sofort stumm, ausser M ist genau jetzt schon
            // gegriffen (dann normal hoerbar weiterlaufen, bis losgelassen
            // wird). Deaktiviert: sofort wieder normal hoerbar.
            motorGateState = gateEnabledNow
                ? (motorGateHeld ? MotorGateState::Sustaining : MotorGateState::Idle)
                : MotorGateState::Sustaining;
        }
    }

    if (sourceGrabRequest.exchange (false))
    {
        motorGateHeld = true;

        if (motorGateEnabledShadow)
        {
            // Startet die Einblendung ab dem aktuellen Gain, nicht ab 0 -
            // ein erneuter Griff mitten im Ausfaden (AwaitingRest/Releasing)
            // faedet so von dort weiter ein, statt hart neu anzusetzen.
            motorGateState = MotorGateState::Attacking;
        }
    }

    if (sourceReleaseRequest.exchange (false))
    {
        motorGateHeld = false;

        if (motorGateEnabledShadow && motorGateState != MotorGateState::Idle)
        {
            motorGateState        = MotorGateState::AwaitingRest;
            motorGateAwaitSeconds = 0.0;
        }
    }

    if (recordToggleRequest.exchange (false))
    {
        if (motionRecorder.isRecording())
        {
            motionRecorder.stopRecording();

            // Der Clip wandert hier in den Player und nicht im Message-Thread:
            // beide Objekte gehören ausschließlich dem Audiothread. Die Kopie
            // allokiert nicht (Kapazität ist vorgewärmt) und kostet einmalig
            // beim Loslassen des Aufnahmeknopfs einen Memcpy von höchstens
            // 24000 Vec3.
            motionPlayer.setClip (motionRecorder.frames(), motionRecordRateHz);
        }
        else
        {
            motionRecorder.startRecording (dopplerEngine.currentTime());
        }
    }

    if (motionLoadRequest.exchange (false))
    {
        // Aus dem gespeicherten Zustand geladene Aufzeichnung übernehmen
        // (@dpa: "State laden muss Record laden"). Beide Ziele allokieren
        // dabei nicht: ihre Kapazitäten stehen seit prepareToPlay() fest, und
        // setStateInformation() deckelt die Framezahl auf dasselbe Maximum.
        motionRecorder.setFrames (stagedMotionFrames);
        motionPlayer.setClip (stagedMotionFrames, stagedMotionRateHz);

        // "Und wenn Play beim Save aktiv war, soll es beim Laden direkt
        // play'en" - derselbe Weg wie der Play-Knopf, nur ohne Umweg über
        // dessen Anfrage-Flag: hier läuft schon der Audiothread. Ein leerer
        // Clip lässt trigger() von selbst wirkungslos bleiben.
        if (motionLoadWasPlaying.load())
            motionPlayer.trigger (dopplerEngine.currentTime());
    }

    if (flyStopRequest.exchange (false))
    {
        // Gleiche Synchronisation wie beim natuerlichen Ende der Strecke
        // (siehe advanceMotion()), damit die Quelle auch beim Stopp-Knopf
        // nicht an ihre Zielposition springt.
        // Beim Stopp-Knopf bleibt die Quelle dort stehen, wo sie gerade ist -
        // nicht dort, wo ihr Ziel steht. Das Ziel laeuft um den Glaetter-
        // Nachlauf voraus, und ihm noch hinterherzulaufen waere ein Stueck
        // Flug, das niemand ausgeloest hat.
        if (flyBy.isRunning())
            holdSourceTargetAt (smoothedSourcePos);

        flyBy.stop();

        // Von Hand gestoppt heisst gestoppt, auch bei eingeschalteter
        // Dauerschleife.
        flyLoopRestartPending = false;
    }

    if (flyTriggerRequest.exchange (false))
    {
        // Der Sprung an den Startpunkt der Strecke ist Umbau, keine Bewegung
        // (siehe cutStartsFlyBy). Das Ziel bleibt offen - startFlyBy() setzt
        // es selbst, sobald der Schnitt es aufruft.
        beginCut (smoothedSourcePos, false, true);
    }

    if (playTriggerRequest.exchange (false))
    {
        motionPlayer.trigger (dopplerEngine.currentTime());

        // Der Start einer Wiedergabe ist dieselbe Naht wie ihr
        // Rundenwechsel: die Quelle steht irgendwo, der Clip faengt woanders
        // an. Ohne Schnitt floege sie die Strecke dazwischen ab - beim Start
        // genauso teuer wie am Rundenpunkt.
        if (motionPlayer.isPlaying())
            beginCut (motionPlayer.firstFrame(), false, false,
                      motionPlayer.startVelocity());
    }

    if (stopTriggerRequest.exchange (false))
        motionPlayer.stop();

    recordingActive.store (motionRecorder.isRecording());
    playbackActive.store (motionPlayer.isPlaying());
    flyByActive.store (flyBy.isRunning());
    recordedFrames.store (motionRecorder.numFrames());
}

void DopplerfeldProcessor::startFlyBy()
{
    const auto kind = pp.flyKind->load() < 0.5f ? FlyByGenerator::Kind::ThroughScreen
                                                : FlyByGenerator::Kind::Crossing;
    const auto start = pp.flyStart->load() < 0.5f ? FlyByGenerator::Start::Continuous
                                                  : FlyByGenerator::Start::Abrupt;

    flyBy.configure (kind, start, (double) pp.flyDistance->load(), (double) pp.flyApproach->load(),
                     listenerState.head, (double) pp.srcZ->load());
    flyBy.setSpeed (effectiveFlySpeed());
    flyBy.start();

    // Glätter vorwärmen. Beide Startvarianten verlangen, dass die Quelle im
    // ersten Moment BEREITS mit voller Geschwindigkeit fliegt - beim
    // Knall-Start ist genau das der Effekt, beim kontinuierlichen wäre eine
    // anlaufende Quelle der Widerspruch zur vorbelegten Vorgeschichte.
    //
    // Statt den Glättern eine Anfangsgeschwindigkeit von außen aufzudrücken
    // (die vier Verfahren haben dafür keine gemeinsame Schnittstelle, und der
    // Slew-Limiter müsste seine Beschleunigungsgrenze umgehen), werden sie
    // hier mit einem gleichförmig wandernden Ziel eingelaufen. Danach steht
    // jedes Verfahren in seinem eigenen eingeschwungenen Zustand - genau dem,
    // den es auch nach einer Weile Flug hätte.
    const Vec3   direction = flyBy.startVelocity().normalised();
    const double speed     = flyBy.startVelocity().length();
    const double tickDt    = 1.0 / DopplerEngine::trajectoryRateHz;

    // Fünf Zeitkonstanten reichen für jedes der vier Verfahren; die
    // Obergrenze deckelt die Arbeit im Audiothread auf zwei Sekunden
    // Simulationszeit, also ein paar tausend Additionen.
    const double tau        = std::max (1.0e-3, (double) pp.smootherTau->load());
    const int    primeTicks = juce::jlimit (1, 2000,
                                            (int) std::ceil (5.0 * tau * DopplerEngine::trajectoryRateHz));

    const Vec3 runUp = flyBy.startPosition() - direction * (speed * tickDt * (double) primeTicks);

    // ERSTER DURCHLAUF: nur messen, wie gross der Nachlauf ueberhaupt ist.
    sourceSmoothers.reset (runUp);

    Vec3 primedVel;

    for (int i = 1; i <= primeTicks; ++i)
    {
        sourceSmoothers.setTarget (runUp + direction * (speed * tickDt * (double) i));
        sourceSmoothers.tick (smoothedSourcePos, primedVel);
    }

    // Nachlauf des Glaetters (@dpa 20260819: "Start und Endpunkt des
    // Vorbeifluges oft falsch", Preset woandersVorbeiflug).
    //
    // Nach dem Vorwaermen steht das ZIEL auf flyBy.startPosition(), die
    // geglaettete Position aber um den eingeschwungenen Nachlauf dahinter - bei
    // smootherTau 0,64 s und 1107 m/s sind das 705 m, ein Drittel der 2191 m
    // langen Bahn. Gemessen statt aus tau*v geschaetzt, denn diese Formel gilt
    // nur fuer einen der vier Glaetter.
    const double lag = (flyBy.startPosition() - smoothedSourcePos).dot (direction);

    // ZWEITER DURCHLAUF, um genau diesen Nachlauf nach vorn verschoben. Danach
    // steht die POSITION auf dem Startpunkt und das Ziel genau einen Nachlauf
    // davor - also exakt im eingeschwungenen Zustand.
    //
    // Beide Seiten muessen verschoben werden. Nur das Ziel vorzuspulen (erster
    // Versuch, cb0d13a) hinterlaesst eine Luecke von zwei Nachlaeufen, die der
    // Glaetter im ersten Moment aufholen muss - eine Beschleunigung, die es im
    // Flug gar nicht geben darf. Sie schreibt eine zusaetzliche Mach-Front in
    // die Trajektorie, die spaeter als zweite Welle eintrifft (@dpa: "es
    // scheint mit Mach1 zu starten ... wieder mit rotem CPU peak, natuerlich
    // mit Aussetzern").
    const Vec3 runUpShifted = runUp + direction * lag;

    sourceSmoothers.reset (runUpShifted);

    for (int i = 1; i <= primeTicks; ++i)
    {
        sourceSmoothers.setTarget (runUpShifted + direction * (speed * tickDt * (double) i));
        sourceSmoothers.tick (smoothedSourcePos, primedVel);
    }

    // Und die Bahn um denselben Nachlauf vorspulen, damit das Ziel dort
    // weitermacht, wo das Vorwaermen es hinterlassen hat. Ohne das saehe der
    // Glaetter im ersten echten Tick einen Ruecksprung.
    flyBy.setTargetLead (lag);

    // Der Unterschied zwischen den beiden Startvarianten steckt allein in der
    // Vorgeschichte, die der neue Geometriesatz mitbekommt:
    //
    //   kontinuierlich - dieselbe Gerade, rückwärts fortgesetzt. Der Löser
    //                    sieht eine Quelle, die schon immer geflogen ist.
    //   Knall-Start    - eine ruhende Quelle am Startpunkt. Die Bewegung setzt
    //                    schlagartig ein; das ist bewusst unphysikalisch und
    //                    als reproduzierbarer Testfall für den Überschallknall
    //                    gedacht.
    //
    // Gesetzt wird die tatsächlich geglättete Position, nicht der ideale
    // Startpunkt: der Glätter hat im eingeschwungenen Zustand einen festen
    // Nachlauf, und die Vorgeschichte muss zu dem passen, was gleich
    // weitergeschrieben wird.
    // Geschnitten, nicht ueberblendet: startFlyBy() laeuft ausschliesslich im
    // stillen Fenster eines Schnitts (siehe cutStartsFlyBy). Ein
    // Geometrie-Crossfade waere hier zweierlei zu viel - er liesse den alten
    // Satz waehrenddessen weiterfliegen (also den Sprung als Bewegung hoeren)
    // und rechnete dafuer auch noch zwei komplette Loesersaetze.
    // Der Wackler-Versatz gehoert dazu: die Bahn bekommt in jedem Tick
    // smoothedSourcePos PLUS ihn, und eine Vorgeschichte ohne ihn stellt dem
    // ersten Tick einen Sprung in Hoehe des Ausschlags voran (siehe
    // lastSourceJitter im Header).
    if (start == FlyByGenerator::Start::Continuous)
    {
        // Die Vorgeschichte bekommt die Geschwindigkeit, mit der das
        // Vorwaermen tatsaechlich geendet hat - NICHT das eingestellte Tempo.
        //
        // Die beiden sind nicht dasselbe: die Glaetter laufen mit einem
        // gleichfoermig wandernden Ziel ein und stehen am Ende dicht am
        // Sollwert, aber eben nur dicht. Mit dem Sollwert gefuellt, steht an
        // der Naht zwischen erfundener Vorgeschichte und echter Bahn ein
        // Sprung in der Geschwindigkeit. Gemessen mit Tests/loop_peak.cpp am
        // Preset "Vorbeiflug mit Hickser": 214,8 gegen 206,2 m/s, also vier
        // Prozent. Zu hoeren ist das nicht am Start, sondern eine Laufzeit
        // spaeter - bei 444 m Abstand 1,3 s, mitten in der Anfahrt, als
        // Sprung der Tonhoehe um 120 Cent (@dpa: "immer bei der Anfahrt
        // gibt's einen kleinen hickser. IN DER FAHRT!!").
        dopplerEngine.cutTo (smoothedSourcePos + lastSourceJitter, primedVel);
    }
    else
    {
        dopplerEngine.cutTo (smoothedSourcePos + lastSourceJitter);

        // Der Knall-Start setzt eine ruhende Quelle schlagartig auf volle
        // Fahrt. Genau das ist die Kante, die spaeter beim Hoerer ankommt -
        // hier wird sie markiert, damit die Pfade sie erkennen, sobald ihre
        // Emissionszeit darueber laeuft (siehe DopplerEngine::markSourceJump).
        // Der kontinuierliche Start bekommt keine Marke: dort springt nichts.
        //
        // Ein Rundenwechsel der Dauerschleife bekommt sie sehr wohl. Er ist
        // ein Losfliegen wie jedes andere, und welche Sorte Losfliegen es
        // sein soll, sagt allein die Startvariante (@dpa 20260825: "Knall-
        // Start ... noch immer nicht zu hoeren!!" - er arbeitet in
        // Dauerschleife, und ohne Marke je Runde blieb es beim einen Knall
        // des allerersten Starts).
        //
        // Der SPRUNG selbst bleibt davon unberuehrt lautlos: die Quelle wird
        // ausgeblendet, umgesetzt und wieder aufgeblendet (CutState). Zu
        // hoeren ist die Druckwelle des Anfahrens, nicht die Ortsveraenderung.
        // Wer auch die nicht will, waehlt die Startvariante "Kontinuierlich".
        dopplerEngine.markSourceJump (speed);
    }
}

void DopplerfeldProcessor::advanceMotion (double untilTime)
{
    const double sr = currentSampleRate;

    if (sr <= 0.0)
        return;

    // Vorgemerkte Wiederholung des Vorbeifluges (siehe unten in der
    // Tick-Schleife). Es laeuft derselbe Weg wie bei einem frisch ausgeloesten
    // Flug: Glaetter vorwaermen und einen neuen Geometriesatz mit passender
    // Vorgeschichte setzen. Dadurch ist der Ruecksprung an den Anfang der
    // Strecke kein Positionssprung, sondern ein ueberblendeter
    // Geometriewechsel - genau das, was ein Sprung sonst waere (formal
    // Ueberschall, also ein Knacken).
    if (flyLoopRestartPending)
    {
        flyLoopRestartPending = false;

        // Vom Ende der Strecke zurueck an ihren Anfang ist derselbe Umbau wie
        // beim ersten Start - geschnitten, nicht geflogen (@dpa: "weil das
        // andere ist voellig sinnlos: von ende auf anfang springen??").
        //
        if (pp.flyLoop->load() > 0.5f)
            beginCut (smoothedSourcePos, false, true);
    }

    // Der Glätter tickt auf der Trajektorienrate, nicht auf der Blockrate
    // (Plan 3.8) - seine Dynamik hängt damit nicht daran, welche Blockgröße
    // der Host gerade liefert.
    const double tickDt          = 1.0 / DopplerEngine::trajectoryRateHz;
    const double recordInterval  = 1.0 / motionRecordRateHz;

    // Getaktet wird am Raster der Bahn selbst, nicht an einem eigenen
    // Sample-Zähler: jeder Tick gehört zu genau einem Bahnpunkt und wird am
    // Ende der Schleife auch dort abgelegt. Ein zweiter Zähler könnte gegen
    // die Bahn wegdriften, und genau diese Drift war die Zickzack-Bahn.
    while (dopplerEngine.nextTrajectoryTime() <= untilTime)
    {

        // Die Wiedergabe treibt dieselbe Kette wie Maus und Hostautomation
        // (Plan 3.9): sie liefert nur das Ziel, geglättet wird danach. Damit
        // ist auch die Auflage erfüllt, dass ein linear interpolierter Clip
        // zwingend durch den Glätter muss.
        // Rangfolge der Zielquellen: ein laufender Vorbeiflug hat Vorrang vor
        // der Bewegungswiedergabe, diese vor dem rohen Reglerziel. Alle drei
        // liefern nur ein ZIEL - geglättet, in die Trajektorie geschrieben und
        // gelöst wird danach für alle gleich (Plan 3.8/3.9).
        // Fuer den gemeinsamen Tempo-Deckel unten: Positionen vor diesem Tick,
        // um daraus die tatsaechlich zurueckgelegte Strecke zu messen -
        // unabhaengig davon, welcher Smoother/Generator sie erzeugt hat.
        const Vec3 prevSourcePos = smoothedSourcePos;
        const Vec3 prevHeadPos   = listenerState.head;

        Vec3 target = sourceTargetMetres;
        bool bypassSmoothing = false;

        // Die Position steht schon fertig fest und geht ohne jede Glaettung
        // durch - siehe den Nachlauf weiter unten, der einzige Fall.
        bool positionIsFinal = false;

        if (flyBy.isRunning())
        {
            target = flyBy.tick (tickDt);


            // Endpunkt merken: sobald der Flug endet (isRunning() faellt in
            // diesem tick() auf false), faellt der naechste Durchlauf auf
            // sourceTargetMetres zurueck - ohne diese Synchronisation waere
            // das der Punkt von VOR dem Flug, und die Quelle spraenge dorthin
            // (@dpa-Repro: Vorbeiflug endet, M springt an die alte Stelle).
            // Mit ihr bleibt die Quelle einfach dort stehen, wo der Flug
            // endete - kein Sprung, kein neuer Sonderzustand.
            // Am Ende der Strecke steht die Quelle auf dem geplanten Endpunkt,
            // das Ziel um einen Nachlauf davor (setTargetLead). Gehalten wird
            // deshalb der ENDPUNKT, nicht die letzte Zielposition: sonst zoege
            // der Glaetter die Quelle noch einen Nachlauf weiter darueber
            // hinaus.
            if (! flyBy.isRunning())
            {
                holdSourceTargetAt (flyBy.plannedEnd());

                // Dauerschleife (@dpa 20260821): nur vormerken, nicht hier
                // neu starten. Der Neustart setzt die Glaetter zurueck und
                // schreibt einen neuen Geometriesatz - mitten in der
                // Tick-Schleife, die an nextTrajectoryTime() haengt, waere das
                // ein Eingriff in die Laufbedingung dieser Schleife selbst.
                // Ausgefuehrt wird er darum am Anfang des naechsten
                // advanceMotion(), also hoechstens einen Block spaeter.
                flyLoopRestartPending = pp.flyLoop->load() > 0.5f;
            }
        }
        else if (coastActive)
        {
            // Auslaufen nach dem Loslassen: die Geschwindigkeit laeuft durch
            // zwei Ein-Pol-Stufen gegen null, die Position ist ihr Integral
            // (Herleitung und Kurve stehen bei coastVelocityStage1 im Header).
            const double coeff = 1.0 - std::exp (-tickDt / coastTau);

            coastVelocityStage1 -= coastVelocityStage1 * coeff;
            coastVelocityStage2 += (coastVelocityStage1 - coastVelocityStage2) * coeff;

            coastPos += coastVelocityStage2 * tickDt;

            // Weder Glaetter noch Slew-Waechter: die Kurve IST die fertige
            // Bewegung. Der Waechter begrenzt jeden Tick auf seine
            // Beschleunigungsrampe und baute das Auslaufen erst ueber
            // Zehntelsekunden auf, statt es mit der Geschwindigkeit beginnen
            // zu lassen, mit der losgelassen wurde (gemessen mit
            // Tests/coast_probe.cpp: aus 20 m/s wurden im ersten Zehntel
            // 1,1 m/s). Der gemeinsame Tempo-Deckel weiter unten gilt
            // weiterhin - der ist keine Glaettung, sondern eine Obergrenze.
            target          = coastPos;
            positionIsFinal = true;

            // Das Ziel wird hier NICHT mitgefuehrt: es steht auf dem
            // Loslasspunkt (gesetzt beim Start des Nachlaufs) und bleibt
            // dort. Endet der Nachlauf davor, uebernimmt der Glaetter den
            // Rest - genau das, was ohne Nachlauf von selbst passiert waere.
            // Wuerde das Ziel mitwandern, waere der Rueckstand des Glaetters
            // beim Loslassen endgueltig verloren.

            if (coastVelocityStage2.length() < coastRestSpeed)
                coastActive = false;
        }
        else if (motionPlayer.isPlaying())
        {
            target = motionPlayer.tick (tickDt);

            // CatmullRom-Wiedergabe ist bereits C1-glatt (siehe MotionPlayer.h),
            // ein zusätzlicher Glätter mit fester Zeitkonstante ist dafür nicht
            // nötig - und bei hoher Play-Speed sogar schädlich: das Ziel
            // wandert schneller durch den Clip, als die feste Zeitkonstante
            // hinterherkommt, der Glätter schneidet Ecken und die Bewegung
            // wird kleiner/runder statt schneller (@dpa-Repro: Play Speed
            // hoch + Loop). Nur bei Linear (bloss C0-stetig) bleibt der
            // Glätter Pflicht, siehe Klassenkommentar in MotionPlayer.h.
            bypassSmoothing = (motionPlayer.getInterp() == MotionPlayer::Interp::CatmullRom);

            // Rundenende erreicht: die Wiedergabe steht jetzt auf dem letzten
            // Frame und wartet. Der Schnitt blendet aus, setzt sie zurueck und
            // blendet wieder ein (@dpa: "Ende erreicht, leise, umbau, laut,
            // start"). Ohne den Schnitt liefe der Weg vom Ende zum Anfang als
            // echte Bewegung durch die Glaettung.
            if (motionPlayer.atLoopEdge())
                beginCut (motionPlayer.firstFrame(), true, false,
                          motionPlayer.startVelocity());
        }

        // Waehrend der Ausblende steht die Quelle still. Was sie jetzt noch
        // zuruecklegte, wuerde der Schnitt ohnehin ueberschreiben - und ein
        // weiterlaufendes Ziel liesse den Glaetter genau die Bewegung
        // schreiben, die hier vermieden werden soll.
        if (cutState == CutState::FadingOut)
            target = cutHoldMetres;

        // M-Jitter (@dpa 20260818): additiv VOR jeglicher Glaettung auf das
        // Ziel aufgeschlagen, egal ob es von Maus/Automation, Vorbeiflug oder
        // Wiedergabe kommt - immer aktiv, kein Sonderfall fuer Stillstand
        // (bei Bewegung geht der kleine Jitter im normalen Doppler unter, im
        // Stillstand ist er die einzige Bewegung und dominiert von selbst).
        // Der Jitter selbst ist als Summe stetig driftender Sinusse gebaut
        // (siehe PositionJitter), braucht also KEINEN nachgeschalteten
        // Glaetter, um klickfrei zu bleiben - deshalb ist es unbedenklich,
        // ihn auch im bypassSmoothing-Zweig direkt in target zu addieren.
        // Der Wackler wird NICHT auf das Ziel addiert und laeuft daher weder
        // durch die Bewegungsglaettung noch unter den Tempo-Deckel. Beide
        // fressen ihn sonst fast vollstaendig auf: die Glaettung daempft ihn
        // frequenzabhaengig weg (bei tau 0,145 s und 2 Hz bleiben 23 %), und der
        // Deckel klemmt die Schrittweite - ein Ausschlag von 5 m bei 2 Hz
        // braucht rund 63 m/s Spitze, bei einem Deckel von 1 m/s bleibt davon
        // ein Kriechen uebrig. Eingestellte Meter kaemen so nie an
        // (@dpa 20260820: "kaum Bewegung, obwohl es sich stark bewegen muesste").
        //
        // Der Deckel heisst "max Fly speed" und meint die Fluggeschwindigkeit,
        // nicht das Zittern auf der Stelle; die Glaettung wiederum braucht der
        // Wackler nicht, weil er als Summe stetig driftender Sinusse schon
        // C1-stetig gebaut ist (siehe PositionJitter) und deshalb von sich aus
        // klickfrei bleibt. Er kommt erst bei der Uebergabe an die Bahn obendrauf,
        // ganz am Ende dieses Ticks.
        //
        // Das ist zugleich der Grund, warum die Klone ohne eigenen Glaettersatz
        // auskommen: die Quellposition enthaelt den Wackler nun unveraendert,
        // der Abzug im Klon-Versatz hebt ihn also exakt auf statt nur naeherungsweise.
        const Vec3 sourceJitterNow = sourceJitter.tick (tickDt);

        // Fuer den naechsten Schnitt merken, siehe lastSourceJitter im Header.
        lastSourceJitter = sourceJitterNow;

        // Die Klone wackeln auf derselben Rate wie die Quelle, jeder fuer sich.
        // Ihr Versatz sitzt in der Geometrie der Engine, nicht in der Bahn -
        // alle Klone lesen dieselbe Trajektorie und unterscheiden sich nur
        // darin, von wo aus sie gehoert werden.
        for (int i = 0; i < (int) cloneJitter.size(); ++i)
        {
            // Der Wackler der Quelle steckt in der Quellposition, auf der die
            // Klone sitzen, bereits drin. Bliebe er stehen, truegen die Klone
            // zwei Wackler: den der Quelle, dem sie folgen, und ihren eigenen -
            // die Quelle waere der ruhige Mittelpunkt, um den die anderen
            // kreisen. Abgezogen bleibt von jeder Fliege genau ihr eigener
            // Wackler um den gemeinsamen, ruhenden Ankerpunkt
            // (@dpa 20260820: "jede wie alle anderen durcheinander").
            dopplerEngine.setCloneJitterOffset (i, cloneJitter[(size_t) i].tick (tickDt)
                                                       - sourceJitterNow);
        }

        if (positionIsFinal)
        {
            smoothedSourcePos = target;

            // Alle vier Verfahren auf der Position halten, damit der naechste
            // Griff dort weitermacht statt an der Stelle, an der zuletzt
            // geglaettet wurde.
            sourceSmoothers.reset (target);
        }
        else if (bypassSmoothing)
        {
            // Ueberschwinger-Waechter (siehe Klassenkommentar zu
            // wasMotionSlewGuardActive im Header): beim Einstieg in den
            // Bypass-Zweig auf der aktuellen Position aufsetzen, damit kein
            // Sprung entsteht (der erste Tick liefert dann exakt target) -
            // danach begrenzt der Waechter jeden weiteren Tick auf slewVmax.
            if (! wasMotionSlewGuardActive)
                sourceSmoothers.slew.reset (target);

            sourceSmoothers.slew.setTarget (target);

            Vec3 guardVel;
            sourceSmoothers.slew.tick (smoothedSourcePos, guardVel);
        }
        else
        {
            sourceSmoothers.setTarget (target);

            Vec3 sourceVel;
            sourceSmoothers.tick (smoothedSourcePos, sourceVel);
        }

        wasMotionSlewGuardActive = bypassSmoothing;

        // Nach einem Nachlauf faengt der Waechter wieder von vorn an: seine
        // Position ist gerade extern gesetzt worden.
        if (positionIsFinal)
            wasMotionSlewGuardActive = false;

        listenerSmoothers.setTarget (listenerTargetMetres);

        Vec3 headVel;
        listenerSmoothers.tick (listenerState.head, headVel);

        // Gemeinsamer Tempo-Deckel (@dpa: "ein 'max Fly speed' fuer alles"):
        // letzte Stufe nach JEDER Quelle (Maus/Automation-Glaettung, Flug,
        // Wiedergabe) - klemmt die in diesem Tick tatsaechlich zurueckgelegte
        // Strecke, nicht den Regler-Mechanismus selbst. Wirkt dadurch
        // unabhaengig davon, welcher der vier Smoother oder der Vorbeiflug-
        // Generator das Ziel geliefert hat, ohne dass jeder einzeln sein
        // eigenes Limit kennen muesste. Default ist so hoch, dass er nichts
        // begrenzt, bis @dpa ihn bewusst herunterstellt.
        {
            const double maxStep = (double) pp.globalMaxSpeed->load() * tickDt;

            auto clampStep = [maxStep] (Vec3 prev, Vec3& current)
            {
                const Vec3   delta = current - prev;
                const double dist  = delta.length();

                if (dist > maxStep && dist > 1.0e-9)
                    current = prev + delta * (maxStep / dist);
            };

            clampStep (prevSourcePos, smoothedSourcePos);
            clampStep (prevHeadPos,   listenerState.head);
        }

        // Der tatsaechlich gefahrene Schritt als Geschwindigkeit - dort setzt
        // der Nachlauf an (siehe lastSourceVelocity im Header).
        if (tickDt > 0.0)
            lastSourceVelocity = (smoothedSourcePos - prevSourcePos) * (1.0 / tickDt);

        // Naeherungsgeschwindigkeit fuer den Rotor (siehe
        // sourceClosingSpeed im Header): aus dem tatsaechlichen Schritt
        // dieses Ticks, projiziert auf die Sichtlinie zum Hoerer.
        {
            const Vec3   toListener = listenerState.head - smoothedSourcePos;
            const double distance   = toListener.length();

            if (distance > 1.0e-6 && tickDt > 0.0)
            {
                const Vec3 step = smoothedSourcePos - prevSourcePos;

                sourceClosingSpeed = step.dot (toListener) / (distance * tickDt);
            }
        }

        // Motor-Gating (@dpa-Feedback): nach dem Loslassen erst abwarten, bis
        // die Quelle wirklich steht (Nachlauf zu Ende), dann erst ausfaden -
        // "positionstechnisch zur Ruhe kommen, DANN in Ruhe ausfaden". Aus dem
        // tatsaechlichen Positionsschritt gerechnet (nicht aus einem eigenen
        // Geschwindigkeitswert), damit es unabhaengig davon funktioniert, ob
        // gerade geglaettet oder eine Wiedergabe/ein Vorbeiflug lief.
        if (motorGateState == MotorGateState::AwaitingRest)
        {
            motorGateAwaitSeconds += tickDt;

            const double speed = (smoothedSourcePos - prevSourcePos).length() / tickDt;

            if (speed < motorGateRestSpeedThreshold
                || motorGateAwaitSeconds >= motorGateAwaitTimeoutSeconds)
                motorGateState = MotorGateState::Releasing;
        }

        if (bypassSmoothing)
        {
            // One-Pole/Spring/One-Euro synchron mitführen (nicht den aktiven
            // Ueberschwinger-Waechter slew, siehe resetExceptSlew) - sonst
            // setzt ein Wechsel zurück zu Maus/Automation nach dem Stop mit
            // einem veralteten, "eingefrorenen" Zustand wieder ein und
            // springt. Mit der (eventuell durch den Tempo-Deckel gekappten)
            // Position, nicht dem rohen Ziel von oben.
            sourceSmoothers.resetExceptSlew (smoothedSourcePos);
        }

        // Yaw bekommt einen eigenen One-Pole statt durch den Positionsglätter
        // zu laufen: der rechnet in Metern (der Slew-Limiter klemmt m/s, der
        // 1€-Filter koppelt alle Achsen über |dx/dt|), für einen Winkel gäbe
        // es dort keine sinnvolle Grenze. Geglättet werden muss er trotzdem -
        // er geht als Ableitung über die Ohrgeschwindigkeit in den Doppler ein
        // (Plan 3.5), ein im Blockraster springender Yaw wäre dort ein
        // Geschwindigkeitsstoß.
        smoothedYawRadians = wrapToPi (smoothedYawRadians
                                       + wrapToPi (targetYawRadians - smoothedYawRadians) * yawSmoothCoeff);
        listenerState.yaw  = smoothedYawRadians;

        // Wände nach demselben Muster: an/aus und Dämpfung springen sofort
        // (der Schalter blendet über den Anti-Klick-Envelope des Pfades ein,
        // die Dämpfung ist ein Filterkoeffizient), Lage und Winkel werden
        // gezogen. Ohne das Ziehen wäre ein gedrehter Wandregler ein Sprung
        // der Spiegelebene und damit ein Sprung der Laufzeit - ein Klick.
        for (int w = 0; w < DopplerEngine::maxWalls; ++w)
        {
            WallState&       s = wallSmoothed[w];
            const WallState& t = wallTarget[w];

            s.on         = t.on;
            s.damping    = t.damping;
            s.gainLinear = t.gainLinear;

            s.anchor += (t.anchor - s.anchor) * yawSmoothCoeff;

            s.azimuthRad = wrapToPi (s.azimuthRad
                                     + wrapToPi (t.azimuthRad - s.azimuthRad) * yawSmoothCoeff);
            s.tiltRad   += (t.tiltRad - s.tiltRad) * yawSmoothCoeff;
        }

        // Abgriffpunkte nach demselben Muster: nur der Ort wird gezogen.
        for (int t = 0; t < Params::tapCount; ++t)
        {
            TapState&       s = tapSmoothed[t];
            const TapState& d = tapTarget[t];

            const Vec3 pos = s.pos;

            s = d;
            s.pos = pos + (d.pos - pos) * yawSmoothCoeff;
        }

        // Aufgezeichnet wird die GEGLÄTTETE Position (Plan 3.9), sonst klänge
        // die Wiedergabe anders als die Live-Bewegung.
        recorderTickAccum += tickDt;

        if (recorderTickAccum >= recordInterval)
        {
            recorderTickAccum -= recordInterval;
            motionRecorder.pushSmoothed (smoothedSourcePos, dopplerEngine.currentTime());
        }

        // Das Ergebnis dieses Ticks IST der nächste Bahnpunkt - unverändert
        // und mit der Zeit, zu der der Glätter ihn gerechnet hat. Erst hier,
        // ganz am Ende: Tempo-Deckel und Motor-Gate oben verändern
        // smoothedSourcePos noch.
        // smoothedSourcePos bleibt der jitterfreie Bahnpunkt - Glaetter,
        // Tempo-Deckel, Motor-Gate und die Aufzeichnung rechnen alle damit
        // weiter, und der naechste Tick setzt darauf auf. Wuerde der Wackler
        // dort hineinwandern, deckelte sich seine eigene Geschwindigkeit im
        // Folgetick selbst, und die Aufnahme truege ihn ein zweites Mal.
        // --- Propellerpaar ausrichten ---
        //
        // Die beiden Propeller sitzen an den Fluegeln, also quer zur
        // Flugrichtung und waagerecht (@dpa: "immer flach in der Richtung des
        // fluges"). Die Richtung kommt aus der tatsaechlich zurueckgelegten
        // Strecke dieses Ticks, nicht aus einem Reglerwert - so stimmt sie
        // fuer jede Bewegungsquelle gleichermassen (Maus, Vorbeiflug,
        // Wiedergabe).
        //
        // Geglaettet, und zwar aus demselben Grund wie ueberall sonst: der
        // Versatz IST eine Position. Zappelte die Richtung, zappelten die
        // beiden Schallquellen mit, und ein Positionssprung ist formal
        // Ueberschall. Steht die Quelle still, bleibt die zuletzt bekannte
        // Richtung stehen, statt auf null zu fallen - sonst klappten die
        // Fluegel im Stillstand zusammen.
        {
            const Vec3   step     = smoothedSourcePos - prevSourcePos;
            const double stepLen  = step.length();

            // Fahrtwind fuer den Motorgenerator: derselbe Schritt, nur als
            // Betrag. Je schneller die Quelle fliegt, desto lauter rauscht
            // sie von sich aus (@dpa 20260824: "Die Luftwiderstandsgeraeusche
            // haben alle, vielleicht unterschiedlich, aber je schneller um so
            // lauter").
            engineGenerator.setAirspeed ((float) (stepLen / tickDt));

            if (stepLen > 1.0e-9)
            {
                const Vec3   dir   = step * (1.0 / stepLen);
                const double coeff = 1.0 - std::exp (-tickDt / propellerHeadingTau);

                propellerHeading = propellerHeading + (dir - propellerHeading) * coeff;

                const double headingLen = propellerHeading.length();

                if (headingLen > 1.0e-9)
                    propellerHeading = propellerHeading * (1.0 / headingLen);
            }

            // Quer zur Flugrichtung, in der Waagerechten: z ist die Hoehe, das
            // Kreuzprodukt mit der Hochachse liegt also in der xy-Ebene. Fliegt
            // die Quelle exakt senkrecht, bleibt davon nichts uebrig - dann
            // steht der Fluegel eben quer zur x-Achse, statt zu verschwinden.
            Vec3 side { propellerHeading.y, -propellerHeading.x, 0.0 };

            const double sideLen = side.length();

            side = sideLen > 1.0e-9 ? side * (1.0 / sideLen) : Vec3 { 1.0, 0.0, 0.0 };

            const double halfSpan = 0.5 * (double) pp.propSpan->load();

            dopplerEngine.setPropellerOffset (0, side * ( halfSpan));
            dopplerEngine.setPropellerOffset (1, side * (-halfSpan));
        }

        dopplerEngine.pushSourceTick (smoothedSourcePos + sourceJitterNow);
    }
}

void DopplerfeldProcessor::requestScopePlayback (const float* left, const float* right, int length)
{
    length = juce::jlimit (0, (int) stagedScopePlaybackL.size(), length);

    std::copy (left, left + length, stagedScopePlaybackL.begin());
    std::copy (right, right + length, stagedScopePlaybackR.begin());

    // Puffer steht, BEVOR das Flag ihn ankuendigt - derselbe Weg wie
    // stagedMotionFrames/motionLoadRequest (s. Header dort): kein Lock, der
    // Audiothread liest beides erst, nachdem er das Flag gesehen hat.
    stagedScopePlaybackLength = length;
    scopePlaybackStartRequest.store (true);
}

void DopplerfeldProcessor::startStagedScopePlayback()
{
    const int n = juce::jlimit (0, (int) scopePlaybackL.size(), stagedScopePlaybackLength);

    if (n <= 0)
    {
        scopePlaybackShotPhase = ScopeShotPhase::Idle;
        scopePlaybackLength    = 0;
        return;
    }

    std::copy (stagedScopePlaybackL.begin(), stagedScopePlaybackL.begin() + n, scopePlaybackL.begin());
    std::copy (stagedScopePlaybackR.begin(), stagedScopePlaybackR.begin() + n, scopePlaybackR.begin());

    scopePlaybackLength  = n;
    scopePlaybackReadPos = 0;

    // Fade-Laenge adaptiv: hoechstens die halbe Pufferlaenge, sonst waere
    // beim kuerzesten moeglichen Fenster (ScopeComponent::minDisplaySamples
    // = 128) die Einblende-Rampe laenger als der ganze Puffer.
    const int desiredFade = (int) (scopePlaybackShotFadeSeconds * juce::jmax (1.0, currentSampleRate));
    scopePlaybackShotFadeSamplesCurrent = juce::jlimit (1, juce::jmax (1, n / 2), desiredFade);

    scopePlaybackShotStep  = 1.0 / (double) scopePlaybackShotFadeSamplesCurrent;
    scopePlaybackShotGain  = 0.0;
    scopePlaybackShotPhase = ScopeShotPhase::FadingIn;
}

void DopplerfeldProcessor::renderScopePlayback (juce::AudioBuffer<float>& buffer)
{
    // Reines Level-Flag wie motorGateEnabled - kein Flankenvergleich noetig,
    // die Rampe auf modeTarget im Sample-Loop unten ist ohnehin jeden Block
    // idempotent (sie tut nur etwas, wenn scopeReplaceGain noch nicht dort
    // steht, egal ob sich modeOn gerade eben geaendert hat oder schon
    // laenger so steht).
    const bool modeOn = scopePlaybackModeEnabled.load();

    // Den Ausgangspegel HERAUSRECHNEN (@dpa 20260828: "das Abspielen von
    // Scope ist viel zu laut ... das liegt am Output").
    //
    // Der Ringpuffer nimmt NACH Gain und Limiter auf - er soll ja zeigen, was
    // tatsaechlich hinausgeht (siehe applyOutputStage). Eingespeist wird die
    // Wiedergabe aber DAVOR, damit Limiter, Pegelmesser und Scope sie genauso
    // behandeln wie das Dopplersignal. Damit lief der Ausgangspegel ein
    // zweites Mal darueber: bei @dpas +15,5 dB war die Wiedergabe genau um
    // diese 15,5 dB zu laut.
    //
    // Geteilt statt umgehaengt: so bleibt die Wiedergabe im selben Signalweg
    // wie das Dopplersignal, kommt am Ausgang aber genau mit dem Pegel an,
    // mit dem sie aufgenommen wurde.
    const double outGain    = (double) outputGainLinear.getTargetValue();
    const double replayScale = outGain > 1.0e-6 ? 1.0 / outGain : 1.0;

    if (scopePlaybackStartRequest.exchange (false) && modeOn)
    {
        if (scopePlaybackShotPhase == ScopeShotPhase::Idle)
        {
            startStagedScopePlayback();
        }
        else
        {
            // Es spielt noch etwas: erst sauber ausblenden (kurze eigene
            // Rampe, hoechstens so lang wie noch Puffer uebrig ist), danach
            // uebernimmt der Sample-Loop unten den wartenden Puffer selbst
            // (scopePlaybackSwapPending).
            const int remaining   = juce::jmax (1, scopePlaybackLength - scopePlaybackReadPos);
            const int fadeSamples = juce::jmin (scopePlaybackShotFadeSamplesCurrent, remaining);

            scopePlaybackShotStep  = 1.0 / (double) juce::jmax (1, fadeSamples);
            scopePlaybackShotPhase = ScopeShotPhase::FadingOut;
            scopePlaybackSwapPending = true;
        }
    }

    const double modeStep   = 1.0 / std::max (1.0, scopePlaybackModeFadeSeconds * currentSampleRate);
    const double modeTarget = modeOn ? 1.0 : 0.0;

    const int    numSamples = buffer.getNumSamples();
    const int    numCh      = buffer.getNumChannels();
    float* const* data      = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        // Doppler <-> Wiedergabe blenden. Diese eine Rampe reicht fuer
        // klickfreies Ein- UND Ausschalten: der aktive Abspielvorgang
        // braucht dafuer keine eigene Reaktion, er wird durch die sinkende
        // Blende einfach unhoerbar (s. Klassenkommentar im Header).
        if (scopeReplaceGain < modeTarget)
            scopeReplaceGain = std::min (modeTarget, scopeReplaceGain + modeStep);
        else if (scopeReplaceGain > modeTarget)
            scopeReplaceGain = std::max (modeTarget, scopeReplaceGain - modeStep);

        float previewL = 0.0f, previewR = 0.0f;

        if (scopePlaybackShotPhase != ScopeShotPhase::Idle && scopePlaybackLength > 0)
        {
            // Ausklang antriggern, kurz vor dem Pufferende - Leseposition
            // und Rampe laufen dadurch synchron aus: die Rampe erreicht
            // exakt dann null, wenn die Leseposition das Ende erreicht.
            if (scopePlaybackShotPhase == ScopeShotPhase::Playing
                && scopePlaybackReadPos >= scopePlaybackLength - scopePlaybackShotFadeSamplesCurrent)
            {
                scopePlaybackShotStep  = 1.0 / (double) juce::jmax (1, scopePlaybackShotFadeSamplesCurrent);
                scopePlaybackShotPhase = ScopeShotPhase::FadingOut;
            }

            if (scopePlaybackReadPos < scopePlaybackLength)
            {
                previewL = scopePlaybackL[(size_t) scopePlaybackReadPos] * (float) scopePlaybackShotGain;
                previewR = scopePlaybackR[(size_t) scopePlaybackReadPos] * (float) scopePlaybackShotGain;
                ++scopePlaybackReadPos;
            }

            if (scopePlaybackShotPhase == ScopeShotPhase::FadingIn)
            {
                scopePlaybackShotGain += scopePlaybackShotStep;

                if (scopePlaybackShotGain >= 1.0)
                {
                    scopePlaybackShotGain  = 1.0;
                    scopePlaybackShotPhase = ScopeShotPhase::Playing;
                }
            }
            else if (scopePlaybackShotPhase == ScopeShotPhase::FadingOut)
            {
                scopePlaybackShotGain -= scopePlaybackShotStep;

                if (scopePlaybackShotGain <= 0.0)
                {
                    scopePlaybackShotGain = 0.0;

                    if (scopePlaybackSwapPending)
                    {
                        scopePlaybackSwapPending = false;
                        startStagedScopePlayback();
                    }
                    else
                    {
                        scopePlaybackShotPhase = ScopeShotPhase::Idle;
                        scopePlaybackLength    = 0;
                    }
                }
            }
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float src = ((ch == 0 || numCh <= 1) ? previewL : previewR) * (float) replayScale;
            data[ch][i] = data[ch][i] * (1.0f - (float) scopeReplaceGain) + src * (float) scopeReplaceGain;
        }
    }

    const bool audible = modeOn && scopePlaybackShotPhase != ScopeShotPhase::Idle;

    scopePlaybackActiveOut.store (audible, std::memory_order_relaxed);
    scopePlaybackProgressOut.store (scopePlaybackLength > 0
        ? (float) scopePlaybackReadPos / (float) scopePlaybackLength : 0.0f,
        std::memory_order_relaxed);
}

void DopplerfeldProcessor::applyOutputStage (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();

    int limiterHits = 0;

    float* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        // Ein Wert pro Sample, nicht pro Kanal - sonst liefe die Rampe pro
        // Block doppelt so schnell wie eingestellt.
        const double gain = (double) outputGainLinear.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            double x = (double) data[ch][i] * gain;

            // Letzte Stelle vor dem Host: ein NaN aus einer entgleisten
            // Rechnung bliebe sonst in dessen Signalkette hängen, bis das
            // Projekt neu geladen wird.
            if (! std::isfinite (x))
                x = 0.0;

            if (limiterEnabled)
            {
                // Mitzaehlen, wie oft der Begrenzer wirklich eingreift. Ohne
                // diese Zahl sieht man dem Ausgang nicht an, ob er gerade
                // zusammengefahren wird - und ein Schwarm, der in die
                // Begrenzung laeuft, klingt dann nach einer einzigen Stimme
                // statt nach vielen.
                if (std::abs (x) > 0.95)
                    ++limiterHits;

                x = softClip (x);
            }

            data[ch][i] = (float) x;

            // Levelmeter (@dpa-Feedback): NACH Gain+Limiter messen, damit die
            // Anzeige das zeigt, was tatsächlich rausgeht.
            updatePeak (ch == 0 ? outPeakL : outPeakR, (float) std::abs (x));
        }

        // Scope (@dpa-Feedback): dieselbe Stelle wie das Levelmeter, NACH
        // Gain+Limiter. Bei Mono-Bussen liegt nur Kanal 0 vor - dann geht
        // derselbe Wert auf beide Scope-Spuren, statt eine leere Spur zu
        // zeigen.
        const float scopeL = data[0][i];
        const float scopeR = numCh > 1 ? data[1][i] : scopeL;
        scopeRing.push (scopeL, scopeR);
    }

    limiterHitCount.store (limiterHits, std::memory_order_relaxed);
}

void DopplerfeldProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int    numSamples = buffer.getNumSamples();
    const double sr         = currentSampleRate;

    // "Audio In"-Quelle (@dpa-Feedback): den Host-Eingang VOR dem Loeschen
    // sichern - AudioInSource kopiert ihn in eigenen Speicher, danach gehoert
    // der Puffer wieder ganz dem Ausgang (Kommentar direkt darunter). Manche
    // Hosts/Formate lassen den Eingangsbus deaktiviert (IS_SYNTH-typisch) -
    // dann bleibt "Audio In" einfach still statt etwas zu erfinden.
    {
        auto inBus = getBusBuffer (buffer, true, 0);
        audioInSource.pushBlock (inBus.getNumChannels() > 0 ? inBus.getReadPointer (0) : nullptr,
                                 numSamples);
    }

    // Instrument ohne (genutzten) Eingang: was der Host im Puffer
    // hinterlässt, gehört nicht zum Ausgangssignal.
    buffer.clear();

    // Hauptschalter. Ausgeschaltet wird nicht abrupt: erst faehrt der Pegel
    // ueber masterFadeSeconds auf null, und ERST wenn er dort angekommen ist,
    // steigt der Block vorzeitig aus. Solange gerechnet wird, kostet es das
    // Uebliche; danach nichts mehr, denn Loeser, Engine und Ausgangsstufe
    // werden gar nicht erst betreten (@dpa: "huebsch ausgefadet und dann ist
    // stille (und 0 CPU)").
    {
        const bool wantOn = pp.masterOn->load() > 0.5f;

        // Wiedereinschalten: waehrend der Stille lief die Bewegung nicht mit,
        // die Bahn hat also ein Loch. Die Engine setzt deshalb neu auf, bevor
        // wieder Ton kommt - sonst waere der erste Block ein Sprung ueber die
        // gesamte Pause hinweg.
        if (wantOn && ! masterWasOn)
        {
            dopplerEngine.reset();
            sourceSmoothers.reset (smoothedSourcePos);
            listenerSmoothers.reset (listenerState.head);
        }

        masterWasOn = wantOn;

        if (! wantOn && masterGain <= 1.0e-4)
        {
            masterGain = 0.0;

            // Die Anzeigen werden weiter unten gefuellt, dorthin kommen wir
            // gleich nicht mehr. Ohne das blieben CPU-Last und Ausschlag auf
            // ihrem letzten Wert von vor dem Abschalten stehen, und die Anzeige
            // behauptete Last, die es nicht mehr gibt.
            cpuLoad.store        (0.0f, std::memory_order_relaxed);
            cpuLoadSource.store  (0.0f, std::memory_order_relaxed);
            cpuLoadPhysics.store (0.0f, std::memory_order_relaxed);
            outPeakL.store       (0.0f, std::memory_order_relaxed);
            outPeakR.store       (0.0f, std::memory_order_relaxed);

            return;                      // ab hier kostet der Block nichts mehr
        }
    }

    if (numSamples <= 0 || sr <= 0.0 || monoScratch.getNumSamples() <= 0)
        return;

    // CPU-Anzeige (@dpa-Feedback): Wanduhrzeit für den kompletten DSP-Teil
    // dieses Blocks gegen die Audiozeit, die er liefert - >100% heißt hörbar
    // zu langsam, nicht nur ein abstrakter Prozentwert.
    const auto blockStartTicks = juce::Time::getHighResolutionTicks();

    currentBlockSamples = numSamples;

    applyParameters();
    handlePendingRequests();

    // T und Hoehe kommen aus den Physik-Parametern (Params::airTempC/
    // airAltitude, siehe applyParameters()) - c(T) bestimmt darueber direkt
    // die Mach-Schwelle in allen Loeserpfaden. Der Dichte-Pegelfaktor der
    // Hoehe wirkt NICHT hier, sondern als reiner Ausgangs-Gain (siehe
    // applyParameters(), "--- Ausgang ---").
    MediumState medium;
    medium.tempCelsius    = (double) pp.airTempC->load();
    medium.altitudeMetres = (double) pp.airAltitude->load();

    const int chunkSize = std::min (motionChunkSamples, monoScratch.getNumSamples());

    // Aufschlüsselung fürs CPU-Feedback (@dpa: "was zieht so stark?"):
    // Quellrendern (Motor/Sample) getrennt von der Physik (DopplerEngine:
    // Löser + Ausbreitung) gemessen, damit die Statuszeile zeigt, welcher
    // der beiden Blöcke gerade den Löwenanteil braucht.
    double sourceTicks = 0.0, physicsTicks = 0.0;

    for (int start = 0; start < numSamples; )
    {
        const int n = std::min (chunkSize, numSamples - start);

        // Fälliger Geometriewechsel vor den Bahnpunkten dieses Teilblocks.
        dopplerEngine.beginChunk();

        // Bis einen Rasterpunkt über das Teilblockende hinaus ticken, damit
        // die Bahn den ganzen Block abdeckt (siehe fillTrajectoryUpTo).
        advanceMotion (dopplerEngine.blockEndTime (n) + 1.0 / DopplerEngine::trajectoryRateHz);

        const auto t0 = juce::Time::getHighResolutionTicks();
        sourceHolder.renderMono (monoScratch.getWritePointer (0), n);
        applyMotorGate (monoScratch.getWritePointer (0), n);
        const auto t1 = juce::Time::getHighResolutionTicks();

        dopplerEngine.setListener (listenerState);

        for (int w = 0; w < DopplerEngine::maxWalls; ++w)
        {
            const auto& wall = wallSmoothed[w];
            dopplerEngine.setWall (w, wall.on, wall.anchor, wall.azimuthRad, wall.tiltRad,
                                   wall.damping, wall.gainLinear);
        }

        for (int t = 0; t < Params::tapCount; ++t)
        {
            const auto& tap = tapSmoothed[t];

            dopplerEngine.setTap (t, tap.on, tap.pos);
            dopplerEngine.setTapReverb (t, tap.type, tap.room, tap.decay, tap.damping,
                                        tap.gainLinear, tap.width, tap.predelay);
        }

        // Fenster auf den Ausgabepuffer, keine Kopie und keine Allokation.
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(),
                                        buffer.getNumChannels(), start, n);

        dopplerEngine.process (chunk, monoScratch.getReadPointer (0), medium);
        const auto t2 = juce::Time::getHighResolutionTicks();

        sourceTicks  += (double) (t1 - t0);
        physicsTicks += (double) (t2 - t1);

        start += n;
    }

    // Scope-Play-Toggle (s. Header): VOR applyOutputStage(), damit Gain/
    // Limiter/Levelmeter/Scope-Ringpuffer die Wiedergabe genauso behandeln
    // wie das normale Dopplersignal.
    renderScopePlayback (buffer);

    applyOutputStage (buffer);

    // Schnittblende (siehe CutState im Header). Sie liegt vor der Blende des
    // Hauptschalters, weil sie ein anderes Ziel hat: nicht "aus", sondern eine
    // kurze Luecke, in der der Umbau unhoerbar passiert. Je Sample gerechnet -
    // bei 12 ms Blende waere eine Blockgrenze als Stufe hoerbar.
    if (cutState != CutState::Idle)
    {
        const double step  = 1.0 / std::max (1.0, cutFadeActive * sr);
        const int    numCh = buffer.getNumChannels();

        float* const* data = buffer.getArrayOfWritePointers();

        for (int n = 0; n < numSamples; ++n)
        {
            if (cutState == CutState::FadingOut)
            {
                cutGain = std::max (0.0, cutGain - step);

                // Unten angekommen: den Umbau fuer den Anfang des naechsten
                // Blocks anmelden (handlePendingRequests). Der Rest dieses
                // Blocks laeuft dabei auf null weiter - genau die Luecke, in
                // der nichts zu hoeren ist.
                // Wartet ein Feldgroessenwechsel auf Ruhe, bleibt die
                // Blende unten liegen - ausgeloest wird dort (siehe
                // fieldCutPending in applyParameters).
                if (cutGain <= 0.0 && ! cutWaitsForField)
                    cutExecutePending = true;
            }
            else
            {
                cutGain = std::min (1.0, cutGain + step);

                if (cutGain >= 1.0)
                    cutState = CutState::Idle;
            }

            for (int ch = 0; ch < numCh; ++ch)
                data[ch][n] *= (float) cutGain;
        }
    }

    // Blende des Hauptschalters, ganz am Ende: sie liegt hinter Begrenzer und
    // Ausgangspegel, damit das Ausblenden von keiner Regelung wieder
    // hochgezogen wird. Je Sample gerechnet, nicht je Block - eine
    // Blockgrenze waere bei 0,12 s Blende sonst als Stufe hoerbar.
    {
        const double target = (pp.masterOn->load() > 0.5f) ? 1.0 : 0.0;
        const double step   = 1.0 / std::max (1.0, masterFadeSeconds * sr);
        const int    numCh  = buffer.getNumChannels();

        // target ist genau 0.0 oder 1.0, und masterGain laeuft in Schritten
        // exakt auf einen der beiden zu - der Vergleich fragt "steht die
        // Rampe schon am Ziel", nicht "sind zwei gerechnete Werte aehnlich".
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
        const bool fadeRunning = (masterGain != target) || masterGain < 1.0;
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE

        if (fadeRunning)
        {
            float* const* data = buffer.getArrayOfWritePointers();

            for (int n = 0; n < numSamples; ++n)
            {
                if (masterGain < target)
                    masterGain = std::min (target, masterGain + step);
                else if (masterGain > target)
                    masterGain = std::max (target, masterGain - step);

                for (int ch = 0; ch < numCh; ++ch)
                    data[ch][n] *= (float) masterGain;
            }
        }
    }

    const double elapsedSeconds = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - blockStartTicks);
    const double budgetSeconds  = (double) numSamples / sr;
    const double loadPercent    = 100.0 * elapsedSeconds / std::max (1.0e-9, budgetSeconds);

    // Leicht geglättet (Ein-Pol, ~10 Blöcke Zeitkonstante) - der rohe Wert
    // springt block für block stark, das wäre als Zahl kaum ablesbar.
    const float prev = cpuLoad.load (std::memory_order_relaxed);
    cpuLoad.store (prev + 0.1f * ((float) loadPercent - prev), std::memory_order_relaxed);

    // Aufschlüsselung genauso geglättet, jeweils relativ zum selben Budget.
    const double sourceSeconds  = juce::Time::highResolutionTicksToSeconds ((juce::int64) sourceTicks);
    const double physicsSeconds = juce::Time::highResolutionTicksToSeconds ((juce::int64) physicsTicks);
    const float  sourcePercent  = (float) (100.0 * sourceSeconds  / std::max (1.0e-9, budgetSeconds));
    const float  physicsPercent = (float) (100.0 * physicsSeconds / std::max (1.0e-9, budgetSeconds));

    const float prevSrc = cpuLoadSource.load (std::memory_order_relaxed);
    cpuLoadSource.store (prevSrc + 0.1f * (sourcePercent - prevSrc), std::memory_order_relaxed);
    const float prevPhys = cpuLoadPhysics.load (std::memory_order_relaxed);
    cpuLoadPhysics.store (prevPhys + 0.1f * (physicsPercent - prevPhys), std::memory_order_relaxed);
}

//======================================================================
// Editor-Schnittstelle

bool DopplerfeldProcessor::loadSampleFile (const juce::File& file)
{
    if (! sampleSource.loadFile (file))
        return false;

    samplePath = file.getFullPathName();
    selectSourceKind (SourceKind::Sample);
    return true;
}

void DopplerfeldProcessor::selectSourceKind (SourceKind kind)
{
    sourceKindSelected.store (static_cast<int> (kind));
    sourceSwitchRequest.store (true);
}

void DopplerfeldProcessor::setMotorGateEnabled (bool shouldGate)
{
    motorGateEnabled.store (shouldGate);
}

juce::AudioProcessorEditor* DopplerfeldProcessor::createEditor()
{
    return new DopplerfeldEditor (*this);
}

void DopplerfeldProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // APVTS plus die Bewegungsaufzeichnung samt Wiedergabezustand (@dpa:
    // "Recorded muss in state!"). Die Aufzeichnung hängt als binäre Property
    // am Wurzelknoten der Kopie; JUCE schreibt MemoryBlock-Properties beim
    // Umwandeln in XML als base64-Attribut mit und liest sie ebenso zurück.
    // Das Dateiformat bleibt dabei das gewöhnliche (copyXmlToBinary), und
    // Presets ohne diese Property laden unverändert weiter. Quellwahl
    // und Sample-Pfad hängen als eigene Properties genauso mit dran (@dpa:
    // "state muss Quellen/Sample-Pfad merken!"), siehe sourceKindId/
    // samplePathId weiter unten.
    auto state = apvts.copyState();

    if (! state.isValid())
        return;

    std::vector<Vec3> frames;

    if (motionRecorder.copyFrames (frames) && ! frames.empty())
    {
        juce::MemoryBlock block (frames.size() * doublesPerFrame * sizeof (double));
        auto* values = static_cast<double*> (block.getData());

        for (size_t i = 0; i < frames.size(); ++i)
        {
            values[i * doublesPerFrame + 0] = frames[i].x;
            values[i * doublesPerFrame + 1] = frames[i].y;
            values[i * doublesPerFrame + 2] = frames[i].z;
        }

        state.setProperty (motionFramesId, juce::var (block), nullptr);
        state.setProperty (motionRateId, motionRecordRateHz, nullptr);
    }
    else
    {
        // Ohne Aufnahme darf keine alte Property stehenbleiben: apvts.state
        // trägt sie nach einem Laden weiter, und ein Speichern danach schriebe
        // sonst die Aufzeichnung des vorigen Presets erneut mit.
        state.removeProperty (motionFramesId, nullptr);
        state.removeProperty (motionRateId, nullptr);
    }

    // Immer geschrieben, auch ohne Aufzeichnung: das ist gleichzeitig die
    // Marke, an der setStateInformation() einen Zustand MIT Bewegungsteil von
    // einem älteren ohne unterscheidet.
    state.setProperty (motionPlayingId, isPlayingMotion(), nullptr);

    // Quellwahl immer schreiben (auch Motor - 0 ist sonst nicht von "Property
    // fehlt, also alter Preset" unterscheidbar). Sample-Pfad nur, wenn
    // tatsächlich je eines geladen wurde, sonst bliebe eine leere Property
    // stehen, die setStateInformation() beim Laden unnötig prüfen müsste.
    state.setProperty (sourceKindId, static_cast<int> (currentSourceKind()), nullptr);

    // "Motor bei Griff" ist kein APVTS-Parameter (reiner Schalter, siehe
    // setMotorGateEnabled()) und muss deshalb wie die Quellwahl als eigene
    // Property mit in den State, sonst verliert ihn jeder Preset-/Host-Recall.
    state.setProperty (motorGateId, isMotorGateEnabled(), nullptr);

    // Welche Panels offen standen, gehoert zum Preset: ein Preset fuer den
    // Vorbeiflug soll die Bewegung offen zeigen, eines fuer den Motor den
    // Motor. Bitmaske, siehe setPanelOpenMask().
    state.setProperty (panelOpenId, getPanelOpenMask(), nullptr);

    if (samplePath.isNotEmpty())
    {
        const juce::File sampleFile (samplePath);
        const juce::File presetsRoot = presetsRootDirectory();

        // Liegt die Datei unter dem Presets-Anker, kommt der relative Pfad
        // ins Preset (portabel), sonst der absolute (siehe
        // presetsRootDirectory()).
        state.setProperty (samplePathId,
                            sampleFile.isAChildOf (presetsRoot)
                                ? sampleFile.getRelativePathFrom (presetsRoot)
                                : samplePath,
                            nullptr);
    }
    else
        state.removeProperty (samplePathId, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DopplerfeldProcessor::loadStartPresetOnFirstRun()
{
    // Nur die Standalone-App. Sie ist die einzige, bei der "was zuletzt
    // aufgerufen war" ueberhaupt eine Bedeutung hat - sie legt ihren Zustand
    // selbst ab. Ein Host haelt den Zustand im Projekt, und ein Testprogramm
    // haelt gar keinen: liefe der Merker auch dort, waere das erste Oeffnen
    // schon von einem ctest-Lauf verbraucht.
    //
    // isStandaloneApp() haengt an START_JUCE_APPLICATION und steht damit
    // unabhaengig vom Konstruktionszeitpunkt fest - anders als
    // StandalonePluginHolder::getInstance(), das waehrend createPlugin() noch
    // leer sein kann, und genau dort stehen wir hier.
    if (! juce::JUCEApplicationBase::isStandaloneApp())
        return;

    // Eigene ApplicationProperties, dieselbe Datei wie fuer die uebrigen
    // Merkposten der Oberflaeche. Function-lokales static: die
    // Storage-Parameter stehen genau einmal fest, bevor irgendein Zugriff
    // stattfindet (ApplicationProperties liefert erst nach
    // setStorageParameters() eine echte PropertiesFile zurueck).
    static juce::ApplicationProperties properties;
    static bool initialised = false;

    if (! initialised)
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "Dopplerfeld";
        options.filenameSuffix      = ".settings";
        options.folderName          = "Dopplerfeld";
        options.osxLibrarySubFolder = "Application Support";

        properties.setStorageParameters (options);
        initialised = true;
    }

    auto* settings = properties.getUserSettings();

    if (settings == nullptr || settings->getBoolValue (startPresetKey, false))
        return;

    // Der Merker faellt VOR dem Laden, nicht danach: bricht das Einspielen
    // aus irgendeinem Grund ab, soll es beim naechsten Start trotzdem beim
    // gewohnten Verhalten bleiben statt jedes Mal erneut zu versuchen.
    settings->setValue (startPresetKey, true);
    settings->saveIfNeeded();

    setStateInformation (BinaryData::StartPreset_dopplerfeld,
                         BinaryData::StartPreset_dopplerfeldSize);
}

bool DopplerfeldProcessor::stateBlockIsOurs (const void* data, int sizeInBytes) const
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    return xml != nullptr && xml->hasTagName (apvts.state.getType());
}

void DopplerfeldProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
        return;

    // Zustand aus einer Fassung mit eigenem "Lauter"-Regler: sein Wert wird auf
    // den Ausgangspegel addiert, damit ein altes Preset genauso laut bleibt.
    // Ohne das faellt der Boost beim Laden ersatzlos weg, und alles, was @dpa
    // damit auf Pegel gebracht hat, waere danach bis zu 36 dB zu leise.
    {
        double         legacyBoostDb = 0.0;
        juce::ValueTree legacyNode;

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto child = tree.getChild (i);

            if (child.getProperty ("id").toString() == Params::loudBoostLegacy)
            {
                legacyBoostDb = (double) child.getProperty ("value");
                legacyNode    = child;
            }
        }

        if (legacyNode.isValid())
        {
            if (legacyBoostDb > 0.0)
            {
                for (int i = 0; i < tree.getNumChildren(); ++i)
                {
                    auto child = tree.getChild (i);

                    if (child.getProperty ("id").toString() == Params::outputGain)
                    {
                        const double merged = (double) child.getProperty ("value") + legacyBoostDb;
                        child.setProperty ("value", juce::jlimit (-36.0, 36.0, merged), nullptr);
                    }
                }
            }

            // Danach muss der alte Eintrag WEG. Der Zustandsbaum behaelt Kinder,
            // die zu keinem Parameter mehr gehoeren, und wird beim naechsten
            // Speichern samt ihnen wieder abgelegt. Bliebe er stehen, addierte
            // sich derselbe Boost bei JEDEM Laden erneut auf den Ausgangspegel,
            // bis der am oberen Anschlag steht - eingestellt bleibt dann gar
            // nichts mehr.
            tree.removeChild (legacyNode, nullptr);
        }
    }

    // Zustand aus einer Fassung mit "Hektik" und "Jit Max": beide sind zu
    // einem einzigen Tempo zusammengefasst worden (@dpa 20260825, siehe
    // PositionJitter::setSpeed). Ohne Umrechnung startete jedes bestehende
    // Preset auf dem Default-Tempo, und der Wackler saehe dort voellig anders
    // aus als eingestellt.
    //
    // Umgerechnet wird mit derselben Formel, mit der die alte Fassung ihre
    // Spitzengeschwindigkeit bestimmt hat:
    //     v_peak = A * 2pi * f * 2*sqrt(3),
    // gedeckelt auf den damaligen "Jit Max". Der Faktor 2*sqrt(3) ist der
    // unguenstigste Fall der drei gewuerfelten Achsen und stand in genau
    // dieser Form im alten Tempo-Deckel. Heraus kommt die Zahl, die der
    // Wackler damals tatsaechlich erreicht hat - jetzt steht sie im Regler,
    // statt sich aus dreien zu ergeben.
    {
        auto findChild = [&tree] (const char* id) -> juce::ValueTree
        {
            for (int i = 0; i < tree.getNumChildren(); ++i)
            {
                const auto child = tree.getChild (i);

                if (child.getProperty ("id").toString() == id)
                    return child;
            }

            return {};
        };

        auto legacyRate  = findChild (Params::srcJitterRateLegacy);
        auto legacyLimit = findChild (Params::srcJitterMaxSpeedLegacy);

        // Nur umrechnen, wenn der alte Wert da ist UND der neue fehlt: ein
        // Zustand, in dem jemand das Tempo schon von Hand gesetzt hat, darf
        // nicht von einem stehengebliebenen Altwert ueberschrieben werden.
        if (legacyRate.isValid() && ! findChild (Params::srcJitterSpeed).isValid())
        {
            const auto amountNode = findChild (Params::srcJitterAmount);

            const double amountM = amountNode.isValid()
                                 ? (double) amountNode.getProperty ("value")
                                 : 0.0;

            const double rateHz = (double) legacyRate.getProperty ("value");

            double speed = amountM * 6.283185307179586 * rateHz * 3.4641016151377544;

            if (legacyLimit.isValid())
            {
                const double limit = (double) legacyLimit.getProperty ("value");

                if (limit > 0.0)
                    speed = std::min (speed, limit);
            }

            if (auto* parameter = apvts.getParameter (Params::srcJitterSpeed))
            {
                juce::ValueTree node (tree.getChild (0).getType());
                node.setProperty ("id", Params::srcJitterSpeed, nullptr);
                node.setProperty ("value", parameter->getNormalisableRange()
                                                     .snapToLegalValue ((float) speed), nullptr);
                tree.appendChild (node, nullptr);
            }
        }

        // Die abgeloesten Eintraege danach WEG - sonst wanderten sie beim
        // naechsten Speichern wieder mit in die Datei und blieben dort auf
        // Dauer als Ballast stehen.
        if (legacyRate.isValid())
            tree.removeChild (legacyRate, nullptr);

        if (legacyLimit.isValid())
            tree.removeChild (legacyLimit, nullptr);
    }

    // Bewegungsteil herausholen, aber NICHT selbst anwenden: MotionRecorder
    // und MotionPlayer gehören ausschließlich dem Audiothread (siehe
    // toggleRecording()). Übergeben wird deshalb über denselben
    // Anfrage-Mechanismus wie Aufnahme und Wiedergabe.
    //
    // Ein Zustand ohne die Marke motionPlayingId kommt aus einer Fassung, die
    // den Bewegungsteil noch nicht gespeichert hat. Dann bleibt eine laufende
    // Aufzeichnung stehen, statt von einem alten Preset stillschweigend
    // gelöscht zu werden.
    if (tree.hasProperty (motionPlayingId))
    {
        stagedMotionFrames.clear();
        stagedMotionRateHz = motionRecordRateHz;

        if (const auto* block = tree.getProperty (motionFramesId).getBinaryData())
        {
            const size_t stored = block->getSize() / (doublesPerFrame * sizeof (double));

            // Deckelung schon hier, nicht erst im Audiothread: dort steht die
            // Kapazität fest und darf nicht wachsen. Ein Preset mit einer
            // anderen Höchstlänge verliert deshalb sein Ende, statt eine
            // Allokation im Audiothread zu erzwingen.
            const size_t count = std::min (stored, (size_t) motionRecordMaxFrames);

            stagedMotionFrames.resize (count);

            const auto* values = static_cast<const double*> (block->getData());

            for (size_t i = 0; i < count; ++i)
                stagedMotionFrames[i] = { values[i * doublesPerFrame + 0],
                                          values[i * doublesPerFrame + 1],
                                          values[i * doublesPerFrame + 2] };

            const double storedRate = (double) tree.getProperty (motionRateId, motionRecordRateHz);

            if (storedRate > 0.0)
                stagedMotionRateHz = storedRate;
        }

        motionLoadWasPlaying.store ((bool) tree.getProperty (motionPlayingId, false));
        motionLoadRequest.store (true);
    }

    // Sample zuerst laden (das schaltet intern immer auf SourceKind::Sample
    // um, siehe loadSampleFile()), danach erst die gespeicherte Quellwahl
    // anwenden - so landet man am Ende bei Motor/AudioIn, obwohl zusätzlich
    // ein Sample geladen war, statt es dabei stumm auf Sample zu belassen.
    // Fehlt die Datei am gespeicherten Pfad (verschoben/gelöscht/anderer
    // Rechner), bleibt es beim zuletzt geladenen bzw. leeren Sample - kein
    // Abbruch, das Preset lädt trotzdem.
    if (tree.hasProperty (samplePathId))
    {
        const juce::String stored = tree.getProperty (samplePathId).toString();

        // Absoluter Pfad (alte Presets, oder Sample lag außerhalb des
        // Presets-Ankers) wird direkt genommen, ein relativer gegen
        // presetsRootDirectory() aufgelöst.
        const juce::File file = juce::File::isAbsolutePath (stored)
                                     ? juce::File (stored)
                                     : presetsRootDirectory().getChildFile (stored);

        if (file.existsAsFile())
            loadSampleFile (file);
    }

    if (tree.hasProperty (sourceKindId))
    {
        const int kind = juce::jlimit (0, 2, (int) tree.getProperty (sourceKindId, 0));
        selectSourceKind (static_cast<SourceKind> (kind));
    }

    // Fehlt die Property (Preset aus einer Fassung ohne den Schalter), bleibt
    // die aktuelle Einstellung stehen - wie bei den anderen nachtraeglich
    // eingefuehrten Properties.
    if (tree.hasProperty (motorGateId))
        setMotorGateEnabled ((bool) tree.getProperty (motorGateId, false));

    // Aeltere Presets kennen die Maske nicht. Dort klappt die Spalte zu
    // (@dpa 20260828: "da haette ich gerne: alles zuklappen") - dasselbe
    // Ergebnis, als haette das Preset eine leere Maske mitgebracht.
    panelOpenMask.store (tree.hasProperty (panelOpenId)
                             ? (int) tree.getProperty (panelOpenId, 0)
                             : 0);
    panelOpenMaskVersion.fetch_add (1);

    // Was der Zustand NICHT enthaelt, geht auf seinen Grundwert zurueck.
    //
    // apvts.replaceState() setzt nur, was im Baum steht; jeder Parameter ohne
    // Eintrag behaelt seinen aktuellen Wert. Ein Preset aus einer aelteren
    // Fassung erbt sonst alles, was seither dazugekommen ist, vom zuletzt
    // geladenen Preset - und klingt beim zweiten Laden anders als beim
    // ersten (@dpa 20260827: "zu leise und ich kann einfach nirgends finden,
    // warum ... schalte 'MachChaos' ein und dann nochmal das erste ... nach
    // Minuten wurde ein anderes Beispiel ploetzlich wieder laut").
    //
    // Wie gross das ist: von 144 in den Presets vorkommenden Parametern
    // fehlen "woanders Vorbeiflug" 50, "spacerocket flyby" 45, "pick & move
    // it" und "record - fastest slaps" je 44. Neun der Presets tragen weder
    // die Fahne (extraPathGainDb, 0 bis -60 dB) noch die Druckwelle
    // (nWavePressure) - beides entscheidet ueber den Pegel.
    //
    // Ergaenzt wird VOR dem Uebernehmen, nicht danach: so steht der
    // vollstaendige Satz auch in apvts.state, und wer das Preset danach
    // sichert, schreibt es vollstaendig zurueck.
    {
        for (auto* parameter : getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

            if (ranged == nullptr)
                continue;

            const juce::String id = ranged->paramID;
            bool found = false;

            for (int i = 0; i < tree.getNumChildren(); ++i)
                if (tree.getChild (i).getProperty ("id").toString() == id)
                {
                    found = true;
                    break;
                }

            if (found)
                continue;

            juce::ValueTree node ("PARAM");
            node.setProperty ("id", id, nullptr);
            node.setProperty ("value",
                              ranged->convertFrom0to1 (ranged->getDefaultValue()),
                              nullptr);
            tree.appendChild (node, nullptr);
        }
    }

    apvts.replaceState (tree);

    // Zum Schluss: der Ladevorgang als Schnitt im Audiothread. Er greift im
    // naechsten Block und auch dann, wenn gar kein Fenster offen ist (siehe
    // CutState im Header).
    //
    // Ein requestEngineRestart() an dieser Stelle brachte die Quelle
    // ebenfalls ohne Anflug an ihre geladene Stelle (@dpa: "bei/nach jedem
    // State-load Engine Restart triggern"), kostet aber zweierlei:
    //
    //   - Der Restart laeuft ueber prepareToPlay() und haelt den Audiothread
    //     dabei an. Gemessen im load_check-Abschnitt "Sprungnaht": 7 ms bei
    //     einem Blockbudget von 10,7 ms, bei kleineren Puffern also mehrere
    //     Bloecke am Stueck.
    //   - Er setzt die Zeitachse zurueck und leert dabei den Signalpuffer.
    //     Danach ist es still, bis der Schall die neue Strecke einmal
    //     zurueckgelegt hat - bei 1000 m gut drei Sekunden.
    //
    // Beides faellt bei jedem Preset-Wechsel an. Der Schnitt braucht keines
    // von beidem: er setzt Glaetter und Geometrie an der neuen Stelle auf, laesst
    // den Signalpuffer aber stehen, und die Vorgeschichte der Bahn ist an der
    // neuen Stelle vollstaendig gefuellt - der neue Ort klingt sofort.
    //
    // Der Knopf "Audiomotor neu anlassen" bleibt unveraendert von Hand
    // erreichbar; er ist weiterhin das Mittel gegen den ungeklaerten
    // Ton-Ausfall (siehe restartEngine()).
    stateLoadRequest.store (true);
}

// Diese Fabrikfunktion verlangt JUCE von jedem Plugin-Projekt.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DopplerfeldProcessor();
}
