#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

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

// Fester Anker für relative Sample-Pfade (@dpa 20260818: "relativ zum
// Presets-Ordner!"). JUCE teilt getStateInformation()/setStateInformation()
// nicht mit, in welche Datei der Host gerade schreibt bzw. aus welcher er
// liest (weder die VST3/AU-Preset-Verwaltung noch "Save/Load State" der
// Standalone-App geben den Pfad weiter) - ohne einen selbst definierten,
// festen Ordner gäbe es also gar keinen Bezugspunkt für "relativ". Legt sich
// beim ersten Zugriff selbst an; Presets+Samples, die hier drunterliegen,
// bleiben beim Verschieben/Kopieren des ganzen Ordners portabel. Alles
// außerhalb (auch der bisherige projekteigene presets/-Ordner) landet
// weiterhin als absoluter Pfad im Preset - unverändert funktionsfähig, nur
// eben ortsgebunden.
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
    pp.srcJitterRateHz = raw (Params::srcJitterRateHz);

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
    pp.slewAmax     = raw (Params::slewAmax);
    pp.playSpeed    = raw (Params::playSpeed);
    pp.playInterp   = raw (Params::playInterp);
    pp.playLoop     = raw (Params::playLoop);
    pp.globalMaxSpeed = raw (Params::globalMaxSpeed);

    pp.flyKind     = raw (Params::flyKind);
    pp.flyStart    = raw (Params::flyStart);
    pp.flyDistance = raw (Params::flyDistance);
    pp.flyApproach = raw (Params::flyApproach);
    pp.flySpeed    = raw (Params::flySpeed);

    pp.boomLimitDb     = raw (Params::boomLimitDb);
    pp.airAbsorbAmount = raw (Params::airAbsorbAmount);
    pp.distanceCurve   = raw (Params::distanceCurve);

    pp.groundReflectionOn = raw (Params::groundReflectionOn);
    pp.groundDampAmount   = raw (Params::groundDampAmount);

    pp.nWaveOn   = raw (Params::nWaveOn);
    pp.nWaveSize = raw (Params::nWaveSize);

    pp.cloneTotal  = raw (Params::cloneTotal);
    pp.cloneReal   = raw (Params::cloneReal);
    pp.cloneAuto   = raw (Params::cloneAuto);
    pp.cloneSpread = raw (Params::cloneSpread);
    pp.cloneLevel  = raw (Params::cloneLevel);

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

    pp.fadeAuto     = raw (Params::fadeAuto);
    pp.fadeManualMs = raw (Params::fadeManualMs);

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

    monoScratch.setSize (1, maxBlock, false, true, true);

    // Scope (@dpa-Feedback: Zoom bis ~3s Zeitbasis): der Ringpuffer muss das
    // Doppelte der max. Zeitbasis fassen (ScopeComponent liest ein
    // Rohfenster, das doppelt so lang ist wie die Anzeige - Sicherheitsabstand
    // fuer die Trigger-Suche bei Sync, siehe dortigen Klassenkommentar).
    scopeRing.prepare (sampleRate, 2.0 * scopeMaxDisplaySeconds);

    cloneSpray.prepare (sampleRate, 2);

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
}

void DopplerfeldProcessor::restartEngine()
{
    // @dpa-Feedback: ein blosses dopplerEngine.reset() half nicht
    // zuverlaessig, verlaesslich half bisher nur ein Wechsel der Audio-
    // Puffergroesse - der loest naemlich einen echten prepareToPlay()-Lauf
    // aus. suspendProcessing() haelt processBlock() waehrenddessen an (kein
    // Datenrennen mit dem Audiothread), genau wie es ein Host beim Aendern
    // der Puffergroesse ohnehin tut - vom Nachrichten-Thread aus, weil
    // prepareToPlay() allokieren darf (sein Vertrag), im Audiothread waere
    // das verboten.
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
    return { normX * fieldMetresValue,
             normY * fieldMetresValue * fieldAspect,
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
    // war das ein Benutzereingriff und der Halt ist vorbei (applyParameters).
    heldSrcX = pp.srcX->load();
    heldSrcY = pp.srcY->load();
    heldSrcZ = pp.srcZ->load();
}

void DopplerfeldProcessor::applyParameters()
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

        const bool knobsUntouched = sourceTargetHeld
                                 && srcX == heldSrcX && srcY == heldSrcY && srcZ == heldSrcZ;

        if (! knobsUntouched)
        {
            sourceTargetHeld   = false;
            sourceTargetMetres = metresFromNormalised ((double) srcX, (double) srcY, (double) srcZ);
        }
    }

    listenerTargetMetres = metresFromNormalised ((double) pp.lisX->load(), (double) pp.lisY->load(),
                                                 (double) pp.lisZ->load());

    targetYawRadians         = juce::degreesToRadians ((double) pp.lisYaw->load());

    dopplerEngine.setPanoramaAmount (0.01 * (double) pp.panAmount->load());
    listenerState.earSpacing = (double) pp.earSpacing->load();

    sourceJitter.setAmount ((double) pp.srcJitterAmount->load());
    sourceJitter.setRate   ((double) pp.srcJitterRateHz->load());

    // Dieselben Regler wie fuer die Quelle: @dpa hat den Jitter dort
    // ausprobiert und will genau diesen auf den Klonen, nicht einen zweiten
    // Satz Regler.
    for (auto& j : cloneJitter)
    {
        j.setAmount ((double) pp.srcJitterAmount->load());
        j.setRate   ((double) pp.srcJitterRateHz->load());
    }

    const bool fieldJustChanged = std::abs (fieldMetresValue - lastFieldMetres) > 1.0e-9;

    if (fieldJustChanged)
    {
        lastFieldMetres = fieldMetresValue;

        dopplerEngine.setFieldMetres (fieldMetresValue);

        // Neuer Massstab: die nach einem Flug gehaltene Position gehoert zum
        // alten und wuerde jetzt an einer anderen Stelle im Feld liegen.
        sourceTargetHeld   = false;
        sourceTargetMetres = metresFromNormalised ((double) pp.srcX->load(), (double) pp.srcY->load(),
                                                   (double) pp.srcZ->load());

        // Positionen sind normiert gespeichert (Plan 2.1), ein neuer Maßstab
        // ist deshalb ein reiner Geometriesprung. Die Glätter dürfen ihn nicht
        // nachfahren, sonst hörte man ihn als rasende Bewegung statt als
        // überblendeten Sprung - die Engine blendet ihn selbst ab.
        sourceSmoothers.reset (sourceTargetMetres);
        listenerSmoothers.reset (listenerTargetMetres);

        smoothedSourcePos  = sourceTargetMetres;
        listenerState.head = listenerTargetMetres;
    }

    // --- Bewegungsglättung ---
    const double tau  = (double) pp.smootherTau->load();
    const double vMax = (double) pp.slewVmax->load();
    const double aMax = (double) pp.slewAmax->load();

    sourceSmoothers.setType ((int) pp.smootherType->load(), smoothedSourcePos);
    listenerSmoothers.setType ((int) pp.smootherType->load(), listenerState.head);

    sourceSmoothers.applyParameters (tau, vMax, aMax);
    listenerSmoothers.applyParameters (tau, vMax, aMax);

    yawSmoothCoeff = 1.0 - std::exp (-1.0 / (DopplerEngine::trajectoryRateHz * std::max (1.0e-3, tau)));

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

    // --- Motor ---
    engineGenerator.setRpm (pp.rpm->load());

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

    // Wie oben: der Setter läuft über alle Pfade beider Geometriesätze,
    // deshalb nur bei tatsächlicher Änderung.
    const bool   nWaveEnabled = pp.nWaveOn->load() > 0.5f;
    const double nWaveSize    = (double) pp.nWaveSize->load();

    if (nWaveEnabled != lastNWaveOn || std::abs (nWaveSize - lastNWaveSize) > 1.0e-9)
    {
        lastNWaveOn   = nWaveEnabled;
        lastNWaveSize = nWaveSize;
        dopplerEngine.setNWave (nWaveEnabled, nWaveSize);
    }

    // Beides ist inzwischen billig: die Engine legt Schalter und Dämpfung an
    // ihrer Fläche ab und reicht sie vor jedem Block an die Pfade durch, statt
    // dass hier über alle Pfade beider Geometriesätze gelaufen werden müsste.
    dopplerEngine.setGroundReflectionEnabled (pp.groundReflectionOn->load() > 0.5f);
    dopplerEngine.setGroundDampingAmount ((double) pp.groundDampAmount->load());
    dopplerEngine.setGroundGain (juce::Decibels::decibelsToGain ((double) pp.groundGain->load()));

    dopplerEngine.setSecondOrderEnabled (pp.reflect2ndOn->load() > 0.5f);
    dopplerEngine.setBounceGain ((double) pp.bounceGain->load());
    dopplerEngine.setBounceGainBoost (juce::Decibels::decibelsToGain ((double) pp.bounceGainDb->load()));

    applyCloneParameters();

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

    // Beim allerersten Durchgang (und nach einem Feldgrößenwechsel, der die
    // Fußpunkte in Metern verschiebt) ohne Anlauf auf das Ziel setzen - sonst
    // führe die Wand beim Start sichtbar und hörbar aus dem Ursprung heran.
    if (! wallStateInitialised || fieldJustChanged)
    {
        for (int w = 0; w < DopplerEngine::maxWalls; ++w)
            wallSmoothed[w] = wallTarget[w];

        wallStateInitialised = true;
    }

    // --- Crossfade ---
    // fadeAuto = an bedeutet: Dauer aus dem Anlass berechnen. Der Zeitregler
    // gilt nur, wenn der Schalter aus ist (Plan 3.7).
    const bool   manualFade    = pp.fadeAuto->load() < 0.5f;
    const double manualSeconds = (double) pp.fadeManualMs->load() * 0.001;

    sourceHolder.setManualFade (manualFade, manualSeconds);
    dopplerEngine.setManualFade (manualFade, manualSeconds);

    // --- Ausgang ---
    // Ausgangspegel, -36 bis +36 dB (siehe Params::outputGain).
    outputGainLinear.setTargetValue (
        juce::Decibels::decibelsToGain (pp.outputGain->load()));
    limiterEnabled = pp.limiterOn->load() > 0.5f;
}

void DopplerfeldProcessor::applyCloneParameters()
{
    const int  total      = (int) pp.cloneTotal->load();
    const int  wantedReal = std::min (total, (int) pp.cloneReal->load());
    const bool automatic  = pp.cloneAuto->load() > 0.5f;

    if (! automatic)
    {
        effectiveRealClones = wantedReal;
        cloneAutoHoldBlocks = 0;
    }
    else
    {
        // Automatik. Sie darf nur langsam und in beide Richtungen getrennt
        // reagieren, sonst pendelt sie im Takt ihrer eigenen Wirkung: ein Klon
        // weniger senkt die Last, die gesenkte Last holt ihn zurück, und das
        // Ganze schwingt im Blockraster.
        //
        // Deshalb zwei verschiedene Schwellen (Hysterese) und eine Haltezeit,
        // und deshalb kein PI-Regler: hier wird eine ganzzahlige Pfadanzahl
        // gestellt, kein stetiger Wert.
        const float load = cpuLoad.load (std::memory_order_relaxed);

        effectiveRealClones = std::min (effectiveRealClones, wantedReal);

        if (cloneAutoHoldBlocks > 0)
        {
            --cloneAutoHoldBlocks;
        }
        else if (load > 70.0f && effectiveRealClones > 0)
        {
            --effectiveRealClones;
            cloneAutoHoldBlocks = autoDownHoldBlocks;
        }
        else if (load < 40.0f && effectiveRealClones < wantedReal)
        {
            ++effectiveRealClones;
            cloneAutoHoldBlocks = autoUpHoldBlocks;
        }
    }

    effectiveRealClones = juce::jlimit (0, std::min (total, DopplerEngine::maxRealClones),
                                        effectiveRealClones);

    const int cheap = std::max (0, total - effectiveRealClones);

    dopplerEngine.setRealClones (effectiveRealClones, (double) pp.cloneSpread->load());

    cloneSpray.setCount (cheap);
    cloneSpray.setLevel ((double) pp.cloneLevel->load());

    // Die billigen Klone stellen die Streuung in ZEIT dar, nicht im Raum: sie
    // haben keine Geometrie. Umgerechnet wird deshalb über die
    // Schallgeschwindigkeit - eine Streuung von drei Metern heißt knapp neun
    // Millisekunden Laufzeitunterschied, also genau das, was ein echter Klon in
    // dieser Entfernung auch hätte.
    const double spreadMs = (double) pp.cloneSpread->load() / 343.2 * 1000.0;

    cloneSpray.setSpreadMs (spreadMs);
    cloneSpray.setJitterMs (std::min (4.0, 0.2 * spreadMs + 0.3));

    activeRealClones.store (effectiveRealClones);
    activeCheapClones.store (cheap);
}

void DopplerfeldProcessor::handlePendingRequests()
{
    if (panicRequest.exchange (false))
    {
        // Sofort, ohne auf die Parameter zu warten: das ist der Knopf für den
        // Fall, dass die CPU-Anzeige oben steht und der Ton wegbleibt. Der
        // Editor setzt die Parameter zusätzlich zurück, damit die Schalter
        // zeigen, was passiert ist - aber die Wirkung darf nicht davon
        // abhängen, dass der Message-Thread noch durchkommt.
        dopplerEngine.disableAllReflections();

        effectiveRealClones = 0;
        cloneAutoHoldBlocks = 0;

        cloneSpray.setCount (0);
        cloneSpray.reset();

        activeRealClones.store (0);
        activeCheapClones.store (0);
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
        // setStateInformation() hat die Framezahl vorher auf dasselbe Maximum
        // gedeckelt.
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
        // (siehe advanceMotion()) - sonst springt die Quelle beim manuellen
        // Stopp-Knopf genauso auf die alte, vorherige Zielposition.
        // Beim Stopp-Knopf bleibt die Quelle dort stehen, wo sie gerade ist -
        // nicht dort, wo ihr Ziel steht. Das Ziel laeuft um den Glaetter-
        // Nachlauf voraus, und ihm noch hinterherzulaufen waere ein Stueck
        // Flug, das niemand ausgeloest hat.
        if (flyBy.isRunning())
            holdSourceTargetAt (smoothedSourcePos);

        flyBy.stop();
    }

    if (flyTriggerRequest.exchange (false))
        startFlyBy();

    if (playTriggerRequest.exchange (false))
        motionPlayer.trigger (dopplerEngine.currentTime());

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
    if (start == FlyByGenerator::Start::Continuous)
        dopplerEngine.startLinearMotion (smoothedSourcePos, direction * speed);
    else
        dopplerEngine.jumpSourceTo (smoothedSourcePos);
}

void DopplerfeldProcessor::advanceMotion (double untilTime)
{
    const double sr = currentSampleRate;

    if (sr <= 0.0)
        return;

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

        if (flyBy.isRunning())
        {
            target = flyBy.tick (tickDt);


            // Endpunkt merken: sobald der Flug endet (isRunning() faellt in
            // diesem tick() auf false), faellt der naechste Durchlauf auf
            // sourceTargetMetres zurueck - ohne diese Synchronisation waere
            // das der Punkt von VOR dem Flug, und die Quelle spraenge dorthin
            // (@dpa-Repro: Vorbeiflug endet, M springt an die alte Stelle).
            // So bleibt sie stattdessen einfach dort stehen, wo der Flug
            // endete - kein Sprung, kein neuer Sonderzustand.
            // Am Ende der Strecke steht die Quelle auf dem geplanten Endpunkt,
            // das Ziel um einen Nachlauf davor (setTargetLead). Gehalten wird
            // deshalb der ENDPUNKT, nicht die letzte Zielposition: sonst zoege
            // der Glaetter die Quelle noch einen Nachlauf weiter darueber
            // hinaus.
            if (! flyBy.isRunning())
                holdSourceTargetAt (flyBy.plannedEnd());
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
        }

        // M-Jitter (@dpa 20260818): additiv VOR jeglicher Glaettung auf das
        // Ziel aufgeschlagen, egal ob es von Maus/Automation, Vorbeiflug oder
        // Wiedergabe kommt - immer aktiv, kein Sonderfall fuer Stillstand
        // (bei Bewegung geht der kleine Jitter im normalen Doppler unter, im
        // Stillstand ist er die einzige Bewegung und dominiert von selbst).
        // Der Jitter selbst ist als Summe stetig driftender Sinusse gebaut
        // (siehe PositionJitter), braucht also KEINEN nachgeschalteten
        // Glaetter, um klickfrei zu bleiben - deshalb ist es unbedenklich,
        // ihn auch im bypassSmoothing-Zweig direkt in target zu addieren.
        target += sourceJitter.tick (tickDt);

        // Die Klone wackeln auf derselben Rate wie die Quelle, jeder fuer sich.
        // Ihr Versatz sitzt in der Geometrie der Engine, nicht in der Bahn -
        // alle Klone lesen dieselbe Trajektorie und unterscheiden sich nur
        // darin, von wo aus sie gehoert werden.
        for (int i = 0; i < (int) cloneJitter.size(); ++i)
            dopplerEngine.setCloneJitterOffset (i, cloneJitter[(size_t) i].tick (tickDt));

        if (bypassSmoothing)
        {
            // Ueberschwinger-Waechter (siehe Klassenkommentar zu
            // wasMotionSlewGuardActive im Header): beim Einstieg in den
            // Bypass-Zweig auf der aktuellen Position aufsetzen, damit kein
            // Sprung entsteht (der erste Tick liefert dann exakt target,
            // wie zuvor) - danach begrenzt der Waechter jeden weiteren Tick
            // auf slewVmax/slewAmax.
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
        dopplerEngine.pushSourceTick (smoothedSourcePos);
    }
}

void DopplerfeldProcessor::applyOutputStage (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();

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
                x = softClip (x);

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

    if (numSamples <= 0 || sr <= 0.0 || monoScratch.getNumSamples() <= 0)
        return;

    // CPU-Anzeige (@dpa-Feedback): Wanduhrzeit für den kompletten DSP-Teil
    // dieses Blocks gegen die Audiozeit, die er liefert - >100% heißt hörbar
    // zu langsam, nicht nur ein abstrakter Prozentwert.
    const auto blockStartTicks = juce::Time::getHighResolutionTicks();

    applyParameters();
    handlePendingRequests();

    // T steht in Phase 1 fest auf 20 °C (Plan 2.2 und Abschnitt 7). airTempC
    // existiert im Layout, wird hier aber bewusst nicht gelesen: eine Änderung
    // von c verschiebt sämtliche Laufzeiten und wäre ein unstetiges Ereignis,
    // dessen Crossfade-Anlass (MediumChange) erst in Phase 2 verdrahtet wird.
    const MediumState medium;

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

        // Fenster auf den Ausgabepuffer, keine Kopie und keine Allokation.
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(),
                                        buffer.getNumChannels(), start, n);

        dopplerEngine.process (chunk, monoScratch.getReadPointer (0), medium);
        const auto t2 = juce::Time::getHighResolutionTicks();

        sourceTicks  += (double) (t1 - t0);
        physicsTicks += (double) (t2 - t1);

        start += n;
    }

    // Billige Klone: sie arbeiten auf dem fertigen Stereosignal und kosten
    // keinen Loeseraufruf. Vor der Ausgangsstufe, damit Gain und Limiter auch
    // fuer sie gelten.
    cloneSpray.process (buffer);

    applyOutputStage (buffer);

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
    // Das Dateiformat bleibt damit dasselbe wie bisher (copyXmlToBinary), und
    // ältere Presets ohne diese Property laden unverändert weiter. Quellwahl
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

    if (samplePath.isNotEmpty())
    {
        const juce::File sampleFile (samplePath);
        const juce::File presetsRoot = presetsRootDirectory();

        // Liegt die Datei unter dem Presets-Anker, kommt der relative Pfad
        // ins Preset (portabel), sonst wie bisher der absolute (siehe
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

    apvts.replaceState (tree);
}

// Diese Fabrikfunktion verlangt JUCE von jedem Plugin-Projekt.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DopplerfeldProcessor();
}
