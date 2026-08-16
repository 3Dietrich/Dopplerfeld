#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Motion/CriticallyDampedSpring.h"
#include "Motion/MotionPlayer.h"
#include "Motion/MotionRecorder.h"
#include "Motion/MotionSmoother.h"
#include "Motion/OneEuroSmoother.h"
#include "Motion/OnePoleSmoother.h"
#include "Motion/SlewLimiter.h"
#include "Physics/DopplerEngine.h"
#include "Physics/Listener.h"
#include "Physics/Medium.h"
#include "Physics/Vec3.h"
#include "Sources/EngineGenerator.h"
#include "Sources/SampleSource.h"
#include "Sources/SoundSourceHolder.h"
#include "Util/FieldSnapshot.h"

#include <atomic>

// Zusammenbau aller Bausteine (Plan 3.6 bis 3.13): Quellstufe -> Glättung ->
// DopplerEngine -> Ausgangsstufe. Der Processor selbst rechnet keine Physik,
// er verdrahtet nur - jede Formel steht in der Klasse, zu der sie gehört.
class DopplerfeldProcessor : public juce::AudioProcessor
{
public:
    // Feldhöhe = Breite * 400/700 (Plan 2.1). Positionen werden normiert
    // gespeichert und erst hier in Meter umgerechnet; dasselbe Verhältnis
    // benutzt FieldComponent für die Rückrichtung.
    static constexpr double fieldAspect = 400.0 / 700.0;

    // Obergrenze des fieldMetres-Parameters. Danach sind Signal- und
    // Trajektorienpuffer bemessen, damit eine Feldgrößenänderung im Betrieb
    // nichts mehr allokieren muss (Plan 2.12).
    static constexpr double maxFieldMetres = 10000.0;

    // Regelrate der Bewegungsaufzeichnung (Plan 3.9).
    static constexpr double motionRecordRateHz = 200.0;

    // Teilblocklänge, in der processBlock die Engine füttert. Zwischen zwei
    // Aufrufen zieht die Engine die Quellposition linear durch, kurze
    // Teilblöcke halten die Bewegung also nah am geglätteten Verlauf. 128 ist
    // ein Vielfaches der Solver-Schrittweite (Plan 2.11: 64), damit dabei
    // keine zusätzlichen Löserpunkte anfallen.
    static constexpr int motionChunkSamples = 128;

    DopplerfeldProcessor();
    ~DopplerfeldProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dopplerfeld"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==================================================================
    // Schnittstelle zum Editor (alles Message-Thread).

    void fillFieldSnapshot (FieldSnapshot& dest) const { dopplerEngine.fillSnapshot (dest); }

    // Aufnahme umschalten und Wiedergabe starten. Beide setzen nur ein
    // atomares Flag, das der Audiothread am Blockanfang abholt: eine volle
    // Kommandoqueue (Plan 3.12) trägt hier nichts bei, weil beide Kommandos
    // ohne Nutzlast auskommen, idempotent sind und ein Tastendruck ohnehin
    // nicht mehrfach pro Block ankommt. Ein direkter Aufruf wäre dagegen ein
    // echtes Datenrennen - MotionRecorder und MotionPlayer gehören
    // ausschließlich dem Audiothread.
    void toggleRecording() { recordToggleRequest.store (true); }
    void triggerPlayback() { playTriggerRequest.store (true); }
    void stopPlayback()    { stopTriggerRequest.store (true); }

    bool isRecording() const { return recordingActive.load(); }
    bool isPlayingMotion() const { return playbackActive.load(); }
    int  recordedFrameCount() const { return recordedFrames.load(); }

    // Lädt die Datei im Message-Thread (Pflicht, siehe SampleSource) und
    // schaltet bei Erfolg weich auf die Sample-Quelle um.
    bool loadSampleFile (const juce::File& file);

    // Quellwahl Motor <-> Sample. Bewusst kein APVTS-Parameter: Plan 3.11
    // führt keinen auf, und der geladene Sample-Pfad gehört ebenso wenig zum
    // gespeicherten Zustand (Plan Abschnitt 7).
    void selectSampleSource (bool shouldUseSample);
    bool isUsingSampleSource() const { return useSampleSource.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    //==================================================================
    // Die vier Glättungsverfahren aus Plan 3.8, alle vier fertig gebaut, plus
    // die Auswahl. Ein umgeschalteter std::unique_ptr müsste beim Typwechsel
    // im Audiothread allokieren - die vier Objekte zusammen sind kleiner als
    // ein Audioblock, also liegen sie dauerhaft bereit und es wechselt nur
    // der Zeiger.
    struct SmootherSet
    {
        OnePoleSmoother        onePole;
        CriticallyDampedSpring spring;
        SlewLimiter            slew;
        OneEuroSmoother        oneEuro;

        MotionSmoother* current = &spring;   // Plan 3.8: kritisch gedämpft ist der Default
        int             typeIndex = 1;       // Reihenfolge wie Params::smootherType

        void prepare (double tickRateHz);
        void reset (Vec3 pos);

        // Wechselt das Verfahren und setzt das neue auf die aktuelle Position,
        // damit die Bewegung nicht von der veralteten Position des zuletzt
        // benutzten Glätters aus wieder anfährt.
        void setType (int index, Vec3 currentPos);

        void applyParameters (double tauSeconds, double vMax, double aMax);

        void setTarget (Vec3 pos) { current->setTarget (pos); }
        void tick (Vec3& outPos, Vec3& outVel) { current->tick (outPos, outVel); }
    };

    void applyParameters();
    void handlePendingRequests();
    void advanceMotion (int numSamples);
    void applyOutputStage (juce::AudioBuffer<float>& buffer);

    Vec3 metresFromNormalised (double normX, double normY) const;

    // Zeigt auf den Rohwert eines Parameters (Message-Thread schreibt, hier
    // wird pro Block gelesen). Gegenüber einem APVTS-Listener spart das die
    // Frage, welcher Thread wann in welche Struktur schreiben darf: alle
    // Setter unten laufen damit ausschließlich im Audiothread.
    std::atomic<float>* raw (const char* paramID);

    // Einmal im Konstruktor aufgelöst - die Suche über die Parameter-ID
    // gehört nicht in den Block, der Zeiger dahinter bleibt konstant.
    struct ParamPointers
    {
        std::atomic<float>* fieldMetres = nullptr;

        std::atomic<float>* srcX = nullptr;
        std::atomic<float>* srcY = nullptr;

        std::atomic<float>* lisX       = nullptr;
        std::atomic<float>* lisY       = nullptr;
        std::atomic<float>* lisYaw     = nullptr;
        std::atomic<float>* earSpacing = nullptr;

        std::atomic<float>* rpm = nullptr;
        std::atomic<float>* harmRatio[4]  {};
        std::atomic<float>* harmDetune[4] {};
        std::atomic<float>* harmTrack[4]  {};
        std::atomic<float>* harmLevel[4]  {};

        std::atomic<float>* noiseFcLo    = nullptr;
        std::atomic<float>* noiseFcHi    = nullptr;
        std::atomic<float>* noiseGainLo  = nullptr;
        std::atomic<float>* noiseGainHi  = nullptr;
        std::atomic<float>* noiseQ       = nullptr;
        std::atomic<float>* jitterAmount = nullptr;
        std::atomic<float>* jitterRateHz = nullptr;
        std::atomic<float>* imbalance    = nullptr;

        std::atomic<float>* sampleGain  = nullptr;
        std::atomic<float>* samplePitch = nullptr;
        std::atomic<float>* loopStart   = nullptr;
        std::atomic<float>* loopEnd     = nullptr;
        std::atomic<float>* loopXfadeMs = nullptr;
        std::atomic<float>* eqLowGain   = nullptr;
        std::atomic<float>* eqMidGain   = nullptr;
        std::atomic<float>* eqMidFreq   = nullptr;
        std::atomic<float>* eqHighGain  = nullptr;

        std::atomic<float>* smootherType = nullptr;
        std::atomic<float>* smootherTau  = nullptr;
        std::atomic<float>* slewVmax     = nullptr;
        std::atomic<float>* slewAmax     = nullptr;
        std::atomic<float>* playSpeed    = nullptr;
        std::atomic<float>* playInterp   = nullptr;
        std::atomic<float>* playLoop     = nullptr;

        std::atomic<float>* boomLimitDb     = nullptr;
        std::atomic<float>* airAbsorbAmount = nullptr;

        std::atomic<float>* fadeAuto     = nullptr;
        std::atomic<float>* fadeManualMs = nullptr;

        std::atomic<float>* outputGain = nullptr;
        std::atomic<float>* limiterOn  = nullptr;
    };

    ParamPointers pp;

    //==================================================================
    // Signalkette

    EngineGenerator   engineGenerator;
    SampleSource      sampleSource;
    SoundSourceHolder sourceHolder;
    DopplerEngine     dopplerEngine;

    SmootherSet sourceSmoothers;
    SmootherSet listenerSmoothers;

    MotionRecorder motionRecorder;
    MotionPlayer   motionPlayer;

    // Mono-Zwischenpuffer für die Quellstufe. Die Engine schreibt ihn nicht,
    // sie liest ihn nur - deshalb genau einer, unabhängig von der Pfadanzahl.
    juce::AudioBuffer<float> monoScratch;

    //==================================================================
    // Zustand des Audiothreads

    ListenerState listenerState;

    Vec3 sourceTargetMetres;     // rohes Ziel aus srcX/srcY
    Vec3 listenerTargetMetres;   // rohes Ziel aus lisX/lisY
    Vec3 smoothedSourcePos;      // was tatsächlich in die Engine geht

    double targetYawRadians   = 0.0;
    double smoothedYawRadians = 0.0;

    // Eigener Mitschrieb statt getSampleRate(): den setzt der Host, nicht
    // prepareToPlay - ein Offline-Treiber, der den Processor direkt fährt,
    // bekäme dort sonst 0 und die ganze Kette bliebe stumm.
    double currentSampleRate = 0.0;

    double fieldMetresValue = 100.0;
    double lastFieldMetres  = 100.0;

    double lastBoomLimitDb    = 30.0;
    double lastAirAbsorbAmount = 1.0;

    double motionTickAccum   = 0.0;   // Rest-Samples bis zum nächsten Glätter-Tick
    double recorderTickAccum = 0.0;   // dito für die 200-Hz-Aufzeichnung

    // One-Pole-Koeffizient der Yaw-Glättung, aus smootherTau abgeleitet.
    double yawSmoothCoeff = 0.02;

    juce::SmoothedValue<float> outputGainLinear;
    bool limiterEnabled = true;

    //==================================================================
    // Message-Thread -> Audiothread

    std::atomic<bool> recordToggleRequest { false };
    std::atomic<bool> playTriggerRequest  { false };
    std::atomic<bool> stopTriggerRequest  { false };
    std::atomic<bool> sourceSwitchRequest { false };

    // Audiothread -> Message-Thread, nur zur Anzeige.
    std::atomic<bool> recordingActive { false };
    std::atomic<bool> playbackActive  { false };
    std::atomic<int>  recordedFrames  { 0 };

    std::atomic<bool> useSampleSource { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopplerfeldProcessor)
};
