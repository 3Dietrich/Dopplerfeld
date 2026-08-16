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

    pp.lisX       = raw (Params::lisX);
    pp.lisY       = raw (Params::lisY);
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

    pp.boomLimitDb     = raw (Params::boomLimitDb);
    pp.airAbsorbAmount = raw (Params::airAbsorbAmount);

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
    // Allokation aus.
    {
        const size_t maxFrames = (size_t) std::ceil (motionRecordRateHz * 120.0);

        std::vector<Vec3> warmup (maxFrames);
        motionPlayer.setClip (warmup, motionRecordRateHz);
        motionPlayer.setClip ({}, motionRecordRateHz);
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

Vec3 DopplerfeldProcessor::metresFromNormalised (double normX, double normY) const
{
    // z bleibt in Phase 1 null, wird aber mitgeführt (Plan 2.1).
    return { normX * fieldMetresValue,
             normY * fieldMetresValue * fieldAspect,
             0.0 };
}

void DopplerfeldProcessor::applyParameters()
{
    fieldMetresValue = (double) pp.fieldMetres->load();

    sourceTargetMetres   = metresFromNormalised ((double) pp.srcX->load(), (double) pp.srcY->load());
    listenerTargetMetres = metresFromNormalised ((double) pp.lisX->load(), (double) pp.lisY->load());

    targetYawRadians         = juce::degreesToRadians ((double) pp.lisYaw->load());
    listenerState.earSpacing = (double) pp.earSpacing->load();

    if (std::abs (fieldMetresValue - lastFieldMetres) > 1.0e-9)
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

void DopplerfeldProcessor::handlePendingRequests()
{
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

    if (playTriggerRequest.exchange (false))
        motionPlayer.trigger (dopplerEngine.currentTime());

    recordingActive.store (motionRecorder.isRecording());
    playbackActive.store (motionPlayer.isPlaying());
    recordedFrames.store (motionRecorder.numFrames());
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
        const Vec3 target = motionPlayer.isPlaying() ? motionPlayer.tick (tickDt)
                                                     : sourceTargetMetres;

        sourceSmoothers.setTarget (target);

        Vec3 sourceVel;
        sourceSmoothers.tick (smoothedSourcePos, sourceVel);

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

    applyParameters();
    handlePendingRequests();

    // T steht in Phase 1 fest auf 20 °C (Plan 2.2 und Abschnitt 7). airTempC
    // existiert im Layout, wird hier aber bewusst nicht gelesen: eine Änderung
    // von c verschiebt sämtliche Laufzeiten und wäre ein unstetiges Ereignis,
    // dessen Crossfade-Anlass (MediumChange) erst in Phase 2 verdrahtet wird.
    const MediumState medium;

    const int chunkSize = std::min (motionChunkSamples, monoScratch.getNumSamples());

    for (int start = 0; start < numSamples; )
    {
        const int n = std::min (chunkSize, numSamples - start);

        advanceMotion (n);

        sourceHolder.renderMono (monoScratch.getWritePointer (0), n);

        dopplerEngine.setSourceTarget (smoothedSourcePos);
        dopplerEngine.setListener (listenerState);

        // Fenster auf den Ausgabepuffer, keine Kopie und keine Allokation.
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(),
                                        buffer.getNumChannels(), start, n);

        dopplerEngine.process (chunk, monoScratch.getReadPointer (0), medium);

        start += n;
    }

    applyOutputStage (buffer);
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
