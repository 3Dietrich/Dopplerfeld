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
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
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

    pp.flyKind     = raw (Params::flyKind);
    pp.flyStart    = raw (Params::flyStart);
    pp.flyDistance = raw (Params::flyDistance);
    pp.flySpeed    = raw (Params::flySpeed);

    pp.boomLimitDb     = raw (Params::boomLimitDb);
    pp.airAbsorbAmount = raw (Params::airAbsorbAmount);

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

    {
        const char* const onIds[]    { Params::wall1On,    Params::wall2On };
        const char* const xIds[]     { Params::wall1X,     Params::wall2X };
        const char* const yIds[]     { Params::wall1Y,     Params::wall2Y };
        const char* const angleIds[] { Params::wall1Angle, Params::wall2Angle };
        const char* const tiltIds[]  { Params::wall1Tilt,  Params::wall2Tilt };
        const char* const dampIds[]  { Params::wall1Damp,  Params::wall2Damp };

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
        }
    }

    pp.fadeAuto     = raw (Params::fadeAuto);
    pp.fadeManualMs = raw (Params::fadeManualMs);

    pp.outputGain = raw (Params::outputGain);
    pp.limiterOn  = raw (Params::limiterOn);

    // Der Motor ist die Default-Quelle: er klingt sofort, ohne dass erst eine
    // Datei geladen werden muss.
    sourceHolder.setSource (&engineGenerator);
    dopplerEngine.setSource (&sourceHolder);
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

    cloneSpray.prepare (sampleRate, 2);

    engineGenerator.prepare (sampleRate, maxBlock);
    sampleSource.prepare (sampleRate, maxBlock);

    sourceHolder.setSource (useSampleSource.load() ? static_cast<SoundSource*> (&sampleSource)
                                                   : static_cast<SoundSource*> (&engineGenerator));
    sourceHolder.prepare (sampleRate, maxBlock);
    sourceHolder.reset();

    dopplerEngine.setSource (&sourceHolder);
    dopplerEngine.prepare (sampleRate, maxBlock, maxFieldMetres);

    sourceSmoothers.prepare (DopplerEngine::trajectoryRateHz);
    listenerSmoothers.prepare (DopplerEngine::trajectoryRateHz);

    motionTickAccum   = 0.0;
    recorderTickAccum = 0.0;

    motionRecorder.prepare (motionRecordRateHz, 120.0);

    // Kapazität des Players einmal vorwärmen: der Clip wird später im
    // Audiothread übernommen (siehe handlePendingRequests), und eine
    // Zuweisung an einen Vector mit ausreichender Kapazität kommt ohne neue
    // Allokation aus. Nur beim allerersten prepareToPlay() - siehe
    // motionPlayerCapacityWarmed in PluginProcessor.h.
    if (! motionPlayerCapacityWarmed)
    {
        const size_t maxFrames = (size_t) std::ceil (motionRecordRateHz * 120.0);

        std::vector<Vec3> warmup (maxFrames);
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
    outputGainLinear.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pp.outputGain->load()));
}

bool DopplerfeldProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
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

void DopplerfeldProcessor::applyParameters()
{
    fieldMetresValue = (double) pp.fieldMetres->load();

    sourceTargetMetres   = metresFromNormalised ((double) pp.srcX->load(), (double) pp.srcY->load(),
                                                 (double) pp.srcZ->load());
    listenerTargetMetres = metresFromNormalised ((double) pp.lisX->load(), (double) pp.lisY->load(),
                                                 (double) pp.lisZ->load());

    targetYawRadians         = juce::degreesToRadians ((double) pp.lisYaw->load());
    listenerState.earSpacing = (double) pp.earSpacing->load();

    const bool fieldJustChanged = std::abs (fieldMetresValue - lastFieldMetres) > 1.0e-9;

    if (fieldJustChanged)
    {
        lastFieldMetres = fieldMetresValue;

        dopplerEngine.setFieldMetres (fieldMetresValue);

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
    flyBy.setSpeed ((double) pp.flySpeed->load());

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

    dopplerEngine.setSecondOrderEnabled (pp.reflect2ndOn->load() > 0.5f);
    dopplerEngine.setBounceGain ((double) pp.bounceGain->load());

    applyCloneParameters();

    // Wände: nur die Ziele einsammeln, gefolgt wird ihnen in advanceMotion().
    for (int w = 0; w < DopplerEngine::maxWalls; ++w)
    {
        wallTarget[w].on         = pp.wallOn[w]->load() > 0.5f;
        wallTarget[w].damping    = (double) pp.wallDamp[w]->load();
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
    outputGainLinear.setTargetValue (juce::Decibels::decibelsToGain (pp.outputGain->load()));
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

    if (engineResetRequest.exchange (false))
    {
        // "Audiomotor neu anlassen": setzt Trajektorie, Pfade und deren
        // Filter-/Envelope-Zustand zurueck (siehe dopplerEngine.reset()),
        // ohne Puffer neu zu allokieren. Quell-/Hoererziel bleiben die
        // zuletzt gesetzten Parameterwerte, kein Sprung auf einen anderen Ort.
        dopplerEngine.reset();
    }

    if (sourceSwitchRequest.exchange (false))
        sourceHolder.switchTo (useSampleSource.load() ? static_cast<SoundSource*> (&sampleSource)
                                                       : static_cast<SoundSource*> (&engineGenerator));

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

    if (flyStopRequest.exchange (false))
        flyBy.stop();

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

    flyBy.configure (kind, start, (double) pp.flyDistance->load(),
                     listenerState.head, (double) pp.srcZ->load());
    flyBy.setSpeed ((double) pp.flySpeed->load());
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

    sourceSmoothers.reset (runUp);

    Vec3 primedVel;

    for (int i = 1; i <= primeTicks; ++i)
    {
        sourceSmoothers.setTarget (runUp + direction * (speed * tickDt * (double) i));
        sourceSmoothers.tick (smoothedSourcePos, primedVel);
    }

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

void DopplerfeldProcessor::advanceMotion (int numSamples)
{
    const double sr = currentSampleRate;

    if (sr <= 0.0)
        return;

    // Der Glätter tickt auf der Trajektorienrate, nicht auf der Blockrate
    // (Plan 3.8) - seine Dynamik hängt damit nicht daran, welche Blockgröße
    // der Host gerade liefert.
    const double samplesPerTick  = sr / DopplerEngine::trajectoryRateHz;
    const double tickDt          = 1.0 / DopplerEngine::trajectoryRateHz;
    const double recordInterval  = 1.0 / motionRecordRateHz;

    motionTickAccum += (double) numSamples;

    while (motionTickAccum >= samplesPerTick)
    {
        motionTickAccum -= samplesPerTick;

        // Die Wiedergabe treibt dieselbe Kette wie Maus und Hostautomation
        // (Plan 3.9): sie liefert nur das Ziel, geglättet wird danach. Damit
        // ist auch die Auflage erfüllt, dass ein linear interpolierter Clip
        // zwingend durch den Glätter muss.
        // Rangfolge der Zielquellen: ein laufender Vorbeiflug hat Vorrang vor
        // der Bewegungswiedergabe, diese vor dem rohen Reglerziel. Alle drei
        // liefern nur ein ZIEL - geglättet, in die Trajektorie geschrieben und
        // gelöst wird danach für alle gleich (Plan 3.8/3.9).
        Vec3 target = sourceTargetMetres;
        bool bypassSmoothing = false;

        if (flyBy.isRunning())
        {
            target = flyBy.tick (tickDt);
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

        if (bypassSmoothing)
        {
            // Alle vier internen Verfahren synchron mitführen (nicht nur das
            // aktive) - sonst setzt ein Wechsel zurück zu Maus/Automation nach
            // dem Stop mit einem veralteten, "eingefrorenen" Zustand wieder
            // ein und springt.
            smoothedSourcePos = target;
            sourceSmoothers.reset (target);
        }
        else
        {
            sourceSmoothers.setTarget (target);

            Vec3 sourceVel;
            sourceSmoothers.tick (smoothedSourcePos, sourceVel);
        }

        listenerSmoothers.setTarget (listenerTargetMetres);

        Vec3 headVel;
        listenerSmoothers.tick (listenerState.head, headVel);

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

            s.on      = t.on;
            s.damping = t.damping;

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
    }
}

void DopplerfeldProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int    numSamples = buffer.getNumSamples();
    const double sr         = currentSampleRate;

    // Instrument ohne Eingang: was der Host im Puffer hinterlässt, gehört
    // nicht zum Signal.
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

        advanceMotion (n);

        const auto t0 = juce::Time::getHighResolutionTicks();
        sourceHolder.renderMono (monoScratch.getWritePointer (0), n);
        const auto t1 = juce::Time::getHighResolutionTicks();

        dopplerEngine.setSourceTarget (smoothedSourcePos);
        dopplerEngine.setListener (listenerState);

        for (int w = 0; w < DopplerEngine::maxWalls; ++w)
        {
            const auto& wall = wallSmoothed[w];
            dopplerEngine.setWall (w, wall.on, wall.anchor, wall.azimuthRad, wall.tiltRad,
                                   wall.damping);
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

    selectSampleSource (true);
    return true;
}

void DopplerfeldProcessor::selectSampleSource (bool shouldUseSample)
{
    useSampleSource.store (shouldUseSample);
    sourceSwitchRequest.store (true);
}

juce::AudioProcessorEditor* DopplerfeldProcessor::createEditor()
{
    return new DopplerfeldEditor (*this);
}

void DopplerfeldProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Nur die APVTS. Bewegungsaufzeichnungen leben ausdrücklich nur zur
    // Laufzeit (Plan Abschnitt 7), ebenso die Quellwahl und der Sample-Pfad.
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void DopplerfeldProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// Diese Fabrikfunktion verlangt JUCE von jedem Plugin-Projekt.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DopplerfeldProcessor();
}
