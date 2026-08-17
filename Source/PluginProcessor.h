#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Motion/CriticallyDampedSpring.h"
#include "Motion/MotionPlayer.h"
#include "Motion/FlyByGenerator.h"
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
#include "Util/CloneSpray.h"
#include "Util/FieldSnapshot.h"

#include <atomic>
#include <vector>

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

    // Regelrate und Höchstlänge der Bewegungsaufzeichnung (Plan 3.9). Beide
    // Zahlen bemessen die Kapazität von Recorder und Player und damit die
    // Obergrenze, bis zu der eine geladene Aufzeichnung ohne Allokation im
    // Audiothread übernommen werden kann.
    static constexpr double motionRecordRateHz    = 200.0;
    static constexpr double motionRecordMaxSeconds = 120.0;

    // Wie viele Frames dabei höchstens zusammenkommen. Eine geladene
    // Aufzeichnung wird darauf abgeschnitten.
    static constexpr int motionRecordMaxFrames =
        (int) (motionRecordRateHz * motionRecordMaxSeconds);

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

    // Vorbeiflug-Generator. Wie Record/Play über atomare Anfragen, damit die
    // Zustandsänderung im Audiothread passiert - der Generator konfiguriert
    // beim Start Glätter UND Trajektorien-Vorgeschichte, und beides gehört
    // ausschließlich dorthin.
    void triggerFlyBy() { flyTriggerRequest.store (true); }
    void stopFlyBy()    { flyStopRequest.store (true); }
    bool isFlyingBy() const { return flyByActive.load(); }

    // Was gerade tatsaechlich gerechnet wird - bei eingeschalteter Automatik
    // weicht die Zahl der echten Klone vom Regler ab, und genau das soll man
    // sehen koennen (@dpa: kein stiller Deckel).
    int realCloneCount()  const { return activeRealClones.load(); }
    int cheapCloneCount() const { return activeCheapClones.load(); }

    // Notaus: zurueck auf die minimale sichere Konfiguration - nur der
    // Direktpfad pro Ohr, keine Reflexionen, keine Klone. Greift im
    // Audiothread beim naechsten Block; der Editor setzt zusaetzlich die
    // Parameter zurueck, damit die Schalter zeigen, was passiert ist.
    void panicToMinimal() { panicRequest.store (true); }

    // "Audiomotor neu anlassen" (@dpa-Feedback): ein blosses
    // dopplerEngine.reset() reichte nachweislich nicht - verlaesslich half
    // bisher nur ein Wechsel der Audio-Puffergroesse, weil der einen echten
    // prepareToPlay()-Durchlauf ausloest (setzt zusaetzlich Klangquelle
    // (engineGenerator/sampleSource/sourceHolder) und beide Positions-
    // glaetter zurueck - Stellen, die dopplerEngine.reset() gar nicht
    // beruehrt). restartEngine() macht deshalb genau das: haelt processBlock()
    // an (suspendProcessing), ruft prepareToPlay() mit den aktuellen Werten
    // erneut auf, gibt wieder frei - vom Nachrichten-Thread aus, nicht aus
    // handlePendingRequests() heraus, weil prepareToPlay() selbst allokieren
    // darf (das ist sein Vertrag), im Audiothread waere das verboten.
    void restartEngine();
    int  recordedFrameCount() const { return recordedFrames.load(); }

    // Linearer Spitzenwert seit dem letzten Abruf (Levelmeter, @dpa-Feedback).
    // exchange() statt load(): der Editor pollt mit ~30Hz, dazwischen laufen
    // viele Audioblöcke - ohne Zurücksetzen gingen Spitzen zwischen zwei
    // Abrufen verloren, mit exchange() bekommt jeder Abruf das echte Maximum
    // seit dem vorigen.
    float consumeOutputPeakL() { return outPeakL.exchange (0.0f, std::memory_order_relaxed); }
    float consumeOutputPeakR() { return outPeakR.exchange (0.0f, std::memory_order_relaxed); }

    // Geglättete CPU-Auslastung des Audiothreads in Prozent des Echtzeit-
    // Budgets (@dpa-Feedback: "CPU-Echtzeit-Anzeige"). >100% heißt: der Block
    // hat länger gebraucht, als er Audiozeit lieferte - hörbar als Aussetzer.
    float cpuLoadPercent() const { return cpuLoad.load (std::memory_order_relaxed); }

    // Aufschlüsselung (@dpa: "was zieht so stark?"): Quellrendern (Motor/
    // Sample) getrennt von der Physik (DopplerEngine: Löser+Ausbreitung).
    // Beide Anteile zusammen ergeben ungefähr cpuLoadPercent() (Rest ist
    // Glättung/Ausgangsstufe, normalerweise vernachlässigbar).
    float cpuLoadSourcePercent()  const { return cpuLoadSource.load (std::memory_order_relaxed); }
    float cpuLoadPhysicsPercent() const { return cpuLoadPhysics.load (std::memory_order_relaxed); }

    // Maschinenunabhängiges Lastmaß des Lösers für Messläufe (load_check),
    // siehe DopplerEngine::solverEvaluations().
    std::uint64_t solverEvaluations() const { return dopplerEngine.solverEvaluations(); }

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

    // Klone: Reglerstand einlesen, Automatik nachfuehren, echte und billige
    // Anzahl setzen. Nur aus applyParameters() (Audiothread).
    void applyCloneParameters();

    // Haltezeiten der Klon-Automatik in Bloecken (bei 48 kHz / 512 Samples rund
    // 10,7 ms je Block). Runter darf sie schnell reagieren - da geht es darum,
    // Aussetzer zu vermeiden - hoch nur zoegerlich, damit sie nicht sofort
    // wieder zurueckholt, was sie gerade abgeworfen hat.
    static constexpr int autoDownHoldBlocks = 20;    // ~0,2 s
    static constexpr int autoUpHoldBlocks   = 180;   // ~2 s

    // Setzt den Vorbeiflug auf: Generator, Glätter-Vorwärmung und die zur
    // Startvariante passende Trajektorien-Vorgeschichte. Nur aus dem
    // Audiothread (handlePendingRequests).
    void startFlyBy();
    void advanceMotion (int numSamples);
    void applyOutputStage (juce::AudioBuffer<float>& buffer);

    // x/y kommen normiert herein und werden mit dem Feldmaßstab multipliziert,
    // z kommt bereits in Metern (siehe Params.cpp) und geht unverändert durch.
    Vec3 metresFromNormalised (double normX, double normY, double zMetres) const;

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
        std::atomic<float>* srcZ = nullptr;

        std::atomic<float>* lisX       = nullptr;
        std::atomic<float>* lisY       = nullptr;
        std::atomic<float>* lisZ       = nullptr;
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

        std::atomic<float>* flyKind     = nullptr;
        std::atomic<float>* flyStart    = nullptr;
        std::atomic<float>* flyDistance = nullptr;
        std::atomic<float>* flySpeed    = nullptr;

        std::atomic<float>* boomLimitDb     = nullptr;
        std::atomic<float>* airAbsorbAmount = nullptr;
        std::atomic<float>* distanceCurve   = nullptr;

        std::atomic<float>* groundReflectionOn = nullptr;
        std::atomic<float>* groundDampAmount   = nullptr;

        // Je Wand: an/aus, Fußpunkt (normiert), Richtung, Neigung, Dämpfung.
        std::atomic<float>* wallOn[DopplerEngine::maxWalls]    {};
        std::atomic<float>* wallX[DopplerEngine::maxWalls]     {};
        std::atomic<float>* wallY[DopplerEngine::maxWalls]     {};
        std::atomic<float>* wallAngle[DopplerEngine::maxWalls] {};
        std::atomic<float>* wallTilt[DopplerEngine::maxWalls]  {};
        std::atomic<float>* wallDamp[DopplerEngine::maxWalls]  {};

        std::atomic<float>* nWaveOn   = nullptr;
        std::atomic<float>* nWaveSize  = nullptr;

        std::atomic<float>* cloneTotal  = nullptr;
        std::atomic<float>* cloneReal   = nullptr;
        std::atomic<float>* cloneAuto   = nullptr;
        std::atomic<float>* cloneSpread = nullptr;
        std::atomic<float>* cloneLevel  = nullptr;

        std::atomic<float>* reflect2ndOn = nullptr;
        std::atomic<float>* bounceGain   = nullptr;

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

    // Billige Nachbildung der Klone, die keine eigene Loeserphysik bekommen.
    // Sitzt hinter der Engine und vor der Ausgangsstufe.
    CloneSpray cloneSpray;

    SmootherSet sourceSmoothers;
    SmootherSet listenerSmoothers;

    MotionRecorder  motionRecorder;
    MotionPlayer    motionPlayer;
    FlyByGenerator  flyBy;

    // Die Kapazitäts-Vorwärmung in prepareToPlay() darf nur beim allerersten
    // Mal laufen - prepareToPlay() wird vom Host bei jeder Blockgrößen-/
    // Samplerate-Änderung erneut gerufen, ein zweites Mal würde eine bereits
    // geladene/aufgenommene Bewegung mit einem leeren Clip überschreiben.
    bool motionPlayerCapacityWarmed = false;

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
    double lastDistanceCurve   = 0.0;
    bool   lastNWaveOn         = false;
    double lastNWaveSize       = 15.0;

    // Wie viele Klone gerade WIRKLICH mit Loeserphysik laufen. Bei
    // eingeschalteter Automatik weicht das vom Regler ab, deshalb ein eigener
    // Wert - und deshalb wird er auch angezeigt, statt still zu wirken.
    int    effectiveRealClones = 0;

    // Zaehler fuer die Automatik, in Bloecken. Sie darf nur langsam und
    // getrennt in beide Richtungen reagieren, sonst pendelt sie im Takt der
    // eigenen Wirkung.
    int    cloneAutoHoldBlocks = 0;

    // Geglättete Wandlage. Eine Wand ist eine Spiegelebene; springt sie, dann
    // springt der gespiegelte Empfänger und damit die Laufzeit des ganzen
    // Reflexionspfades - man hörte einen Klick. Der Regler schreibt deshalb
    // wie überall sonst nur ein Ziel, gefolgt wird ihm über denselben
    // One-Pole wie beim Yaw (yawSmoothCoeff, aus smootherTau abgeleitet).
    struct WallState
    {
        Vec3   anchor;
        double azimuthRad = 0.0;
        double tiltRad    = 0.0;
        bool   on         = false;
        double damping    = 0.3;
    };

    WallState wallTarget[DopplerEngine::maxWalls];
    WallState wallSmoothed[DopplerEngine::maxWalls];
    bool      wallStateInitialised = false;

    double motionTickAccum   = 0.0;   // Rest-Samples bis zum nächsten Glätter-Tick
    double recorderTickAccum = 0.0;   // dito für die 200-Hz-Aufzeichnung

    // One-Pole-Koeffizient der Yaw-Glättung, aus smootherTau abgeleitet.
    double yawSmoothCoeff = 0.02;

    juce::SmoothedValue<float> outputGainLinear;
    bool limiterEnabled = true;

    //==================================================================
    // Message-Thread -> Audiothread

    // Geladene Bewegungsaufzeichnung. setStateInformation() legt sie hier ab
    // und setzt danach das Flag; abgeholt wird sie am Blockanfang im
    // Audiothread (handlePendingRequests). Direkt in motionRecorder/
    // motionPlayer zu schreiben wäre ein echtes Datenrennen - die beiden
    // gehören ausschließlich dem Audiothread, siehe toggleRecording().
    //
    // Die Kapazität wird im Konstruktor einmal auf die Höchstlänge gestellt
    // und danach nie überschritten. Damit gibt der Vektor seinen Speicher nie
    // wieder her, und ein zweites Laden kann dem Audiothread nicht den Boden
    // unter den Füßen wegziehen, während er noch kopiert - schlimmstenfalls
    // liest er dann Werte aus zwei Presets, was zwei Preset-Wechsel innerhalb
    // eines Audioblocks (~10 ms) voraussetzt. Eine echte Kommandoqueue wäre
    // der lupenreine Weg, siehe die Begründung bei toggleRecording().
    std::vector<Vec3> stagedMotionFrames;
    double            stagedMotionRateHz = motionRecordRateHz;

    std::atomic<bool> motionLoadRequest    { false };
    std::atomic<bool> motionLoadWasPlaying { false };

    std::atomic<bool> recordToggleRequest { false };
    std::atomic<bool> playTriggerRequest  { false };
    std::atomic<bool> stopTriggerRequest  { false };
    std::atomic<bool> sourceSwitchRequest { false };
    std::atomic<bool> flyTriggerRequest   { false };
    std::atomic<bool> flyStopRequest      { false };
    std::atomic<bool> panicRequest        { false };

    // Audiothread -> Message-Thread, nur zur Anzeige.
    std::atomic<bool>  recordingActive { false };
    std::atomic<bool>  playbackActive  { false };
    std::atomic<bool>  flyByActive     { false };
    std::atomic<float> outPeakL { 0.0f };
    std::atomic<float> outPeakR { 0.0f };
    std::atomic<float> cpuLoad        { 0.0f };   // siehe cpuLoadPercent()
    std::atomic<float> cpuLoadSource  { 0.0f };   // siehe cpuLoadSourcePercent()
    std::atomic<float> cpuLoadPhysics { 0.0f };   // siehe cpuLoadPhysicsPercent()
    std::atomic<int>  recordedFrames  { 0 };
    std::atomic<int>  activeRealClones  { 0 };
    std::atomic<int>  activeCheapClones { 0 };

    std::atomic<bool> useSampleSource { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopplerfeldProcessor)
};
