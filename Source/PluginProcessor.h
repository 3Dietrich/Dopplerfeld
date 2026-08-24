#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Motion/CriticallyDampedSpring.h"
#include "Motion/MotionPlayer.h"
#include "Motion/FlyByGenerator.h"
#include "Motion/MotionRecorder.h"
#include "Motion/MotionSmoother.h"
#include "Motion/OneEuroSmoother.h"
#include "Motion/OnePoleSmoother.h"
#include "Motion/PositionJitter.h"
#include "Motion/SlewLimiter.h"
#include "Physics/DopplerEngine.h"
#include "Physics/Listener.h"
#include "Physics/Medium.h"
#include "Physics/Vec3.h"
#include "Sources/EngineGenerator.h"
#include "Sources/SampleSource.h"
#include "Sources/AudioInSource.h"
#include "Sources/SoundSourceHolder.h"
#include "Util/FieldSnapshot.h"
#include "Util/ScopeRingBuffer.h"

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

    // Vorbeiflug-Wegvorschau kommt vom Generator, nicht von der Engine (der
    // Generator ist Sache des Processors, siehe advanceMotion()) - deshalb
    // hier nachgetragen statt in DopplerEngine::fillSnapshot() selbst.
    void fillFieldSnapshot (FieldSnapshot& dest) const
    {
        dopplerEngine.fillSnapshot (dest);

        dest.flyByActive = flyBy.isRunning();

        if (dest.flyByActive)
        {
            dest.flyByPlannedEnd     = flyBy.plannedEnd();
            dest.flyByNearestPoint   = flyBy.nearestPoint();
            dest.flyByNearestDistance = flyBy.nearestDistanceMetres();
        }
    }

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

    // Wie oft der Begrenzer im letzten Block eingegriffen hat. Null heisst: der
    // Ausgang laeuft frei.
    int limiterHits() const { return limiterHitCount.load (std::memory_order_relaxed); }

    // Was gerade tatsaechlich gerechnet wird (@dpa: kein stiller Deckel).
    int realCloneCount()  const { return activeRealClones.load(); }
    // Billige Klone gibt es seit ihrer Entfernung nicht mehr (@dpa: "nur
    // echte Klones, alles andere weg") - bleibt als 0-Konstante stehen, weil
    // der einzige Aufrufer in PluginEditor.cpp sitzt und dort nicht
    // angefasst werden darf.
    int cheapCloneCount() const { return 0; }

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

    // Engine-Restart nach einem State-Load (@dpa: "bei/nach jedem State-load
    // Engine Restart triggern"). setStateInformation() darf restartEngine()
    // nicht selbst rufen: je nach Host laeuft der Recall mitten in der
    // Audio-Verkabelung, und prepareToPlay() darf nur vom Nachrichten-Thread
    // aus allokieren. Deshalb nur ein Flag - der Editor-Timer (Nachrichten-
    // Thread, wie die Peak-Consumer unten) holt es ab und ruft restartEngine().
    void requestEngineRestart() { engineRestartRequest.store (true); }
    bool consumeEngineRestartRequest() { return engineRestartRequest.exchange (false); }

    int  recordedFrameCount() const { return recordedFrames.load(); }

    // Linearer Spitzenwert seit dem letzten Abruf (Levelmeter, @dpa-Feedback).
    // exchange() statt load(): der Editor pollt mit ~30Hz, dazwischen laufen
    // viele Audioblöcke - ohne Zurücksetzen gingen Spitzen zwischen zwei
    // Abrufen verloren, mit exchange() bekommt jeder Abruf das echte Maximum
    // seit dem vorigen.
    float consumeOutputPeakL() { return outPeakL.exchange (0.0f, std::memory_order_relaxed); }
    float consumeOutputPeakR() { return outPeakR.exchange (0.0f, std::memory_order_relaxed); }

    // Groesste vom Scope anzeigbare Zeitbasis (@dpa-Feedback: erst 3s, dann
    // auf 10s erweitert). Bestimmt, wie gross scopeRing in prepareToPlay()
    // angelegt wird, UND die obere Zoom-Grenze, die der Editor der
    // ScopeComponent mitgibt (siehe DopplerfeldEditor::refreshDisplay()) -
    // ein Wert statt zweier, die auseinanderlaufen koennten.
    static constexpr double scopeMaxDisplaySeconds = 10.0;

    // Scope (@dpa-Feedback): liest die juengsten `numSamples` Ausgangs-
    // Samples (nach Gain/Limiter, dieselbe Stelle wie das Levelmeter) aus
    // dem Ringpuffer. numSamples liegt in der Hand des Aufrufers (Editor/
    // ScopeComponent) - der Processor kennt die aktuelle Zoomstufe bewusst
    // nicht, das ist reine UI-Sache.
    void fillScopeWindow (float* destL, float* destR, int numSamples) const
    {
        scopeRing.readLatest (destL, destR, numSamples);
    }

    // Groesse des kompletten Ringpuffers (@dpa-Feedback: "im freezed Scope
    // frei herumsuchen") - der Editor liest beim Einfrieren die GESAMTE
    // bisher aufgezeichnete Historie (nicht nur ein Anzeigefenster) und
    // reicht sie an ScopeComponent::enterHistoryMode() weiter, damit sich
    // darin frei pannen laesst, ohne dass neue Live-Daten das Bild noch
    // veraendern.
    int scopeRingCapacity() const { return scopeRing.capacity(); }

    // Siehe ScopeRingBuffer::writePosition() - der Scope ordnet damit ein
    // gefundenes Ereignis der Zeitachse zu.
    std::uint32_t scopeWritePosition() const { return scopeRing.writePosition(); }

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

    // Pfad der zuletzt erfolgreich geladenen Sample-Datei, leer wenn noch nie
    // geladen. Nur zur Anzeige/zum Speichern gedacht (Message-Thread) - der
    // Audiothread kennt nur den fertig dekodierten Puffer in sampleSource.
    juce::String loadedSamplePath() const { return samplePath; }

    // Quellwahl Motor <-> Sample <-> Audio In (@dpa-Feedback: dritte Quelle).
    // Bewusst kein APVTS-Parameter: Plan 3.11 führt keinen auf. Quellwahl und
    // Sample-Pfad hängen trotzdem am gespeicherten Zustand, siehe
    // getStateInformation()/setStateInformation() - dort als eigene
    // ValueTree-Property statt als APVTS-Parameter, aus demselben Grund wie
    // die Bewegungsaufzeichnung (nicht automatisierbar).
    enum class SourceKind { Motor, Sample, AudioIn };
    void selectSourceKind (SourceKind kind);
    SourceKind currentSourceKind() const { return static_cast<SourceKind> (sourceKindSelected.load()); }

    // Motor-Gating (@dpa-Feedback): bei aktiviertem Schalter klingt der Motor
    // nur, waehrend/nachdem M gegriffen ist - Start beim Greifen, nach dem
    // Loslassen erst positionstechnisch zur Ruhe kommen (Nachlauf zu Ende
    // laufen lassen), DANN in Ruhe ausfaden. Wirkt nur auf die Motor-Quelle
    // (Sample/Audio In liefern eigenen, oft schon fertigen Klang - "starten"
    // ergibt dort keinen Sinn) und nur auf M, nicht auf L (der Hoerer klingt
    // selbst nicht). Bewusst kein Parameter, wie Quellwahl/Nachlauf.
    void setMotorGateEnabled (bool shouldGate);
    bool isMotorGateEnabled() const { return motorGateEnabled.load(); }

    // Von FieldComponent::onSourceGrabbed/onSourceReleased aufgerufen
    // (Message-Thread) - setzt nur Anfrage-Flags, die eigentliche Umschaltung
    // laeuft wie ueberall sonst im Audiothread (handlePendingRequests()).
    void notifySourceGrabbed()  { sourceGrabRequest.store (true); }
    void notifySourceReleased() { sourceReleaseRequest.store (true); }

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

        // Wie reset(), aber ohne den Slew-Limiter: der läuft während der
        // Catmull-Rom-Clip-Wiedergabe aktiv als Überschwinger-Wächter mit
        // (siehe wasMotionSlewGuardActive in DopplerfeldProcessor) und
        // braucht sein eigenes vel über die Ticks hinweg, um zu bremsen/zu
        // beschleunigen - ein Reset hier würde ihn jeden Tick auf Stillstand
        // zurückwerfen und faktisch lahmlegen.
        void resetExceptSlew (Vec3 pos);

        void applyParameters (double tauSeconds, double vMax, double aMax);

        void setTarget (Vec3 pos) { current->setTarget (pos); }
        void tick (Vec3& outPos, Vec3& outVel) { current->tick (outPos, outVel); }
    };

    void applyParameters();
    void handlePendingRequests();

    // Klone: Reglerstand einlesen, Zahl an die Engine weiterreichen. Nur aus
    // applyParameters() (Audiothread).
    void applyCloneParameters();

    // Setzt den Vorbeiflug auf: Generator, Glätter-Vorwärmung und die zur
    // Startvariante passende Trajektorien-Vorgeschichte. Nur aus dem
    // Audiothread (handlePendingRequests).
    void startFlyBy();
    // untilTime in Sekunden auf der Zeitachse der Engine: getickt wird,
    // solange der nächste Bahnpunkt davor liegt.
    void advanceMotion (double untilTime);

    // Quelle bleibt stehen, wo ein Vorbeiflug endete, statt zum Reglerwert
    // zurueckzulaufen. Gilt, bis jemand den Regler bewegt.
    void holdSourceTargetAt (Vec3 posMetres);

    // Fluggeschwindigkeit, die der gemeinsame Tempo-Deckel wirklich durchlaesst.
    double effectiveFlySpeed() const;
    void applyOutputStage (juce::AudioBuffer<float>& buffer);

    // Rohzeiger auf die zur SourceKind gehoerende SoundSource - ein Ort
    // statt derselben Fallunterscheidung in prepareToPlay() UND
    // handlePendingRequests() (sourceSwitchRequest).
    SoundSource* sourceForKind (SourceKind kind);

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

        std::atomic<float>* srcJitterAmount = nullptr;
        std::atomic<float>* srcJitterRateHz = nullptr;
        std::atomic<float>* srcJitterOn     = nullptr;

        // Eigener Tempo-Deckel des Wacklers, entkoppelt vom Bahn-Deckel
        // globalMaxSpeed - siehe Params::srcJitterMaxSpeed.
        std::atomic<float>* srcJitterMaxSpeed = nullptr;
        std::atomic<float>* masterOn        = nullptr;

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

        // Betriebsart (siehe Params::engineKind) und die beiden Hubschrauber-
        // eigenen Regler, nur in dieser Betriebsart hoerbar.
        std::atomic<float>* engineKind     = nullptr;
        std::atomic<float>* propSpan      = nullptr;
        std::atomic<float>* propLevelDb   = nullptr;
        std::atomic<float>* heliRotorHz    = nullptr;
        std::atomic<float>* heliBladeCount = nullptr;
        std::atomic<float>* heliDoppler     = nullptr;
        std::atomic<float>* heliRotorRadius = nullptr;

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
        std::atomic<float>* globalMaxSpeed = nullptr;

        std::atomic<float>* flyKind     = nullptr;
        std::atomic<float>* flyStart    = nullptr;
        std::atomic<float>* flyDistance = nullptr;
        std::atomic<float>* flyApproach = nullptr;
        std::atomic<float>* flySpeed    = nullptr;
        std::atomic<float>* flyLoop     = nullptr;

        std::atomic<float>* boomLimitDb     = nullptr;
        std::atomic<float>* nWaveGainDb     = nullptr;
        std::atomic<float>* harmSine[4] { nullptr, nullptr, nullptr, nullptr };
        std::atomic<float>* engineLevelDb = nullptr;
        std::atomic<float>* rocketShock   = nullptr;
        std::atomic<float>* rotorSlap     = nullptr;

        // Klangformung und Stossform der beiden Rausch-Betriebsarten,
        // siehe Params::jetVoice und die vier daneben.
        std::atomic<float>* jetVoice        = nullptr;
        std::atomic<float>* jetTone         = nullptr;
        std::atomic<float>* rocketVoice     = nullptr;
        std::atomic<float>* rocketTone      = nullptr;
        std::atomic<float>* rocketShockSize = nullptr;
        std::atomic<float>* rocketShockRate = nullptr;
        std::atomic<float>* reverseGainDb   = nullptr;
        std::atomic<float>* shockDuckAmount = nullptr;
        std::atomic<float>* shockDuckRange  = nullptr;
        std::atomic<float>* jumpBoom        = nullptr;
        std::atomic<float>* shadowTailMs    = nullptr;
        std::atomic<float>* airAbsorbAmount = nullptr;
        std::atomic<float>* distanceCurve   = nullptr;

        // Ausbreitungsmedium (Source/Physics/Medium.h), siehe Params::airTempC/
        // airAltitude.
        std::atomic<float>* airTempC    = nullptr;
        std::atomic<float>* airAltitude = nullptr;

        std::atomic<float>* groundReflectionOn = nullptr;
        std::atomic<float>* groundDampAmount   = nullptr;

        // Je Wand: an/aus, Fußpunkt (normiert), Richtung, Neigung, Dämpfung.
        std::atomic<float>* wallOn[DopplerEngine::maxWalls]    {};
        std::atomic<float>* wallX[DopplerEngine::maxWalls]     {};
        std::atomic<float>* wallY[DopplerEngine::maxWalls]     {};
        std::atomic<float>* wallAngle[DopplerEngine::maxWalls] {};
        std::atomic<float>* wallTilt[DopplerEngine::maxWalls]  {};
        std::atomic<float>* wallDamp[DopplerEngine::maxWalls]  {};
        std::atomic<float>* wallGain[DopplerEngine::maxWalls]  {};

        std::atomic<float>* nWaveOn   = nullptr;
        std::atomic<float>* nWaveSize  = nullptr;

        std::atomic<float>* cloneTotal  = nullptr;
        std::atomic<float>* cloneRealLevel = nullptr;
        std::atomic<float>* cloneSpread = nullptr;

        std::atomic<float>* reflect2ndOn = nullptr;
        std::atomic<float>* bounceGain   = nullptr;
        std::atomic<float>* bounceGainDb = nullptr;

        std::atomic<float>* fadeAuto     = nullptr;
        std::atomic<float>* fadeManualMs = nullptr;

        std::atomic<float>* imbalanceOctave = nullptr;
        std::atomic<float>* groundGain = nullptr;
        std::atomic<float>* panAmount  = nullptr;
        std::atomic<float>* outputGain = nullptr;
        std::atomic<float>* limiterOn  = nullptr;
    };

    ParamPointers pp;

    //==================================================================
    // Signalkette

    EngineGenerator   engineGenerator;
    SampleSource      sampleSource;
    AudioInSource     audioInSource;
    SoundSourceHolder sourceHolder;
    DopplerEngine     dopplerEngine;

    SmootherSet sourceSmoothers;
    SmootherSet listenerSmoothers;

    MotionRecorder  motionRecorder;
    MotionPlayer    motionPlayer;
    FlyByGenerator  flyBy;

    // Ein beendeter Flug soll wiederholt werden (Params::flyLoop). Gesetzt in
    // der Tick-Schleife, ausgefuehrt am Anfang des naechsten advanceMotion() -
    // siehe Kommentar dort.
    bool flyLoopRestartPending = false;

    // Geglaettete Flugrichtung der Quelle, fuer die Ausrichtung des
    // Propellerpaars (siehe DopplerEngine::setPropellerOffset). Geglaettet,
    // weil der Versatz der beiden Propeller eine ECHTE Position ist: zappelte
    // die Richtung, zappelten die beiden Schallquellen mit, und ein
    // Positionssprung ist formal Ueberschall. Bei Stillstand bleibt die
    // zuletzt bekannte Richtung stehen, statt auf null zu fallen.
    Vec3 propellerHeading { 1.0, 0.0, 0.0 };

    // Zeitkonstante der Richtungsglaettung, in Sekunden. Kurz genug, dass die
    // Fluegel einer Kurve folgen, lang genug, dass ein Ruck in der Bahn sie
    // nicht herumreisst.
    static constexpr double propellerHeadingTau = 0.15;

    // Hauptschalter, siehe Params::masterOn. Der Pegel faehrt weich auf 0 und
    // erst DANACH steigt der Block ganz aus - haette er das sofort getan, waere
    // das Abschalten ein Knacks. Umgekehrt beim Einschalten: die Engine setzt
    // neu auf, bevor wieder eingeblendet wird, denn waehrend der Stille wurde
    // keine Bewegung mitgeschrieben.
    // Blendendauer des Hauptschalters. Lang genug, dass nichts knackt, kurz
    // genug, dass "aus" sich wie aus anfuehlt.
    static constexpr double masterFadeSeconds = 0.12;

    double masterGain      = 1.0;
    bool   masterWasOn     = true;

    //------------------------------------------------------------------
    // Schnitt (@dpa 20260824: "ich wollte einen Uebergang im Sinne von
    // 'cutten': Ende erreicht, leise, umbau, laut, start").
    //
    // Eine Positionsaenderung, die KEINE Bewegung ist - ein geladener
    // Zustand, der Rundenwechsel einer Wiedergabe - darf nicht durch die
    // Glaetter laufen. Sie machte daraus sonst eine echte Bewegung ueber die
    // volle Strecke in Glaettungszeit: bei 1400 m und tau 0,145 s einige
    // tausend m/s. Der Loeser sieht dort Ueberschall, spaltet die Wurzel auf
    // und rechnet ein Vielfaches - gemessen im load_check-Abschnitt
    // "Sprungnaht" das 43-fache an Loeser-Auswertungen, und zwar nicht als
    // kurze Spitze, sondern so lange, wie die schnelle Stelle in der
    // Vorgeschichte der Bahn steht (Pufferlaenge, mehrere Sekunden).
    //
    // Der Schnitt macht daraus drei Schritte:
    //   1. Ausblenden (cutFadeSeconds), Quelle steht dabei still.
    //   2. Bei Pegel null: Glaetter und Geometrie an der neuen Stelle neu
    //      aufsetzen (DopplerEngine::cutTo) - ein Umbau, keine Bewegung.
    //   3. Einblenden.
    //
    // Ausgefuehrt wird Schritt 2 am Anfang des Blocks NACH dem Ende der
    // Ausblende (handlePendingRequests). Dazwischen liegt der Ausgang auf
    // null, der Umbau ist also in jedem Fall unhoerbar.
    enum class CutState { Idle, FadingOut, FadingIn };

    // Kurz genug, dass es als Schnitt und nicht als Blende wahrgenommen wird,
    // lang genug, dass keine Flanke knackt.
    static constexpr double cutFadeSeconds = 0.012;

    CutState cutState = CutState::Idle;
    double   cutGain  = 1.0;

    // Wohin geschnitten wird. Wird beim Anmelden gesetzt, nicht beim
    // Ausfuehren: das Ziel gehoert zu dem Ereignis, das den Schnitt
    // ausgeloest hat.
    Vec3 cutTargetMetres { 0.0, 0.0, 0.0 };

    // Waehrend der Ausblende steht die Quelle - sonst faenge sie an zu
    // fliegen, bevor der Schnitt sie versetzt.
    Vec3 cutHoldMetres { 0.0, 0.0, 0.0 };

    // Die Ausblende ist durch, der Umbau steht am Anfang des naechsten
    // Blocks an. Nur vom Audiothread beschrieben und gelesen.
    bool cutExecutePending = false;

    // Nach dem Umbau die Wiedergabe an den Rundenanfang setzen. Trennt den
    // Rundenwechsel vom Zustandsladen, die sonst denselben Weg gehen.
    bool cutRewindsPlayer = false;

    // Nach dem Umbau den Vorbeiflug starten (@dpa 20260824: die Sprungkante
    // "bitte immer ohne (Sprung-)Bewegung im Audio, demnach immer ohne
    // N-Wave und ohne Doppler (durch den Sprung), gefadet, default fuer
    // Vorbeiflug! ohne On/Off toggle").
    //
    // Ein Vorbeiflug beginnt damit, dass die Quelle an den Startpunkt der
    // Strecke versetzt wird - und beim Rundenwechsel vom Ende zurueck an den
    // Anfang. Beides ist Umbau, keine Bewegung: es wird geschnitten, nicht
    // ueberblendet. Der Schnitt ruft dafuer startFlyBy() im stillen Fenster
    // auf, wo Glaetter und Geometrie ohne Zuhoerer gesetzt werden koennen.
    bool cutStartsFlyBy = false;

    // Meldet einen Schnitt an. Nur aus dem Audiothread aufzurufen.
    //
    // Das Ziel darf beim Vorbeiflug offenbleiben (startFlyBy setzt es
    // selbst); dann zaehlt nur die Stille drumherum.
    void beginCut (Vec3 targetMetres, bool rewindPlayer, bool startsFlyBy = false);

    // Additive Mikrobewegung der Quelle M, vor sourceSmoothers eingehakt
    // (siehe advanceMotion()) - "echter Chorus" bei Stillstand.
    PositionJitter  sourceJitter;

    // Eigener Wackler je echtem Klon, siehe advanceMotion().
    std::array<PositionJitter, (size_t) DopplerEngine::maxRealClones> cloneJitter;

    // Die Wackler laufen bewusst an Glaettung und Tempo-Deckel vorbei, siehe
    // advanceMotion(): eingestellte Meter sollen als Ausschlag auch ankommen.

    // Die Kapazitäts-Vorwärmung in prepareToPlay() darf nur beim allerersten
    // Mal laufen - prepareToPlay() wird vom Host bei jeder Blockgrößen-/
    // Samplerate-Änderung erneut gerufen, ein zweites Mal würde eine bereits
    // geladene/aufgenommene Bewegung mit einem leeren Clip überschreiben.
    bool motionPlayerCapacityWarmed = false;

    // Wächter gegen Catmull-Rom-Überschwinger (@dpa 20260818, "Thor swings
    // Hammer"-Repro): bei Clip-Wiedergabe mit Catmull-Rom-Interpolation
    // überspringt advanceMotion() den kompletten sourceSmoothers-Schritt
    // (siehe Kommentar dort) - eine scharfe Schwungumkehr im aufgezeichneten
    // Pfad kann die Spline dabei kurz über Schallgeschwindigkeit hinausschiessen
    // lassen, ungebremst von slewVmax/slewAmax. Der Wächter ist die eigene
    // SlewLimiter-Instanz aus sourceSmoothers (Params::slewVmax/slewAmax
    // werden ihr ohnehin immer nachgeführt, siehe SmootherSet::
    // applyParameters - unabhängig davon, ob "Slew Limiter" als Verfahren
    // ausgewählt ist), NICHT der von @dpa gewählte Smoother-Typ: eine
    // Tau-basierte Glättung würde auch legitime grosse Bewegungen abrunden/
    // verlangsamen (das war der eigentliche Einwand), ein Slew-Limiter mit
    // ausreichend hohem Vmax/Amax lässt normales Tempo praktisch unverändert
    // durch und kappt nur den Überschwinger-Spitzenwert.
    bool wasMotionSlewGuardActive = false;

    // Mono-Zwischenpuffer für die Quellstufe. Die Engine schreibt ihn nicht,
    // sie liest ihn nur - deshalb genau einer, unabhängig von der Pfadanzahl.
    juce::AudioBuffer<float> monoScratch;

    //==================================================================
    // Zustand des Audiothreads

    ListenerState listenerState;

    Vec3 sourceTargetMetres;     // rohes Ziel aus srcX/srcY
    
    // Haltezustand nach einem Vorbeiflug samt des Reglerstands, den er meint
    // (siehe holdSourceTargetAt).
    bool  sourceTargetHeld = false;
    float heldSrcX = 0.0f, heldSrcY = 0.0f, heldSrcZ = 0.0f;
    Vec3 listenerTargetMetres;   // rohes Ziel aus lisX/lisY
    Vec3 smoothedSourcePos;      // was tatsächlich in die Engine geht

    double targetYawRadians   = 0.0;
    double smoothedYawRadians = 0.0;

    // Motor-Gating (@dpa-Feedback), reiner Audiothread-Zustand - siehe
    // setMotorGateEnabled()/applyMotorGate(). Sustaining = normaler Betrieb
    // (Motor klingt), Idle = wartet auf den naechsten Griff (stumm),
    // Attacking/Releasing = laufende Ein-/Ausblendung, AwaitingRest = M
    // losgelassen, aber die Position ist noch in Bewegung (Nachlauf) - erst
    // wenn sourceVel unter die Ruheschwelle faellt, beginnt das Ausfaden.
    enum class MotorGateState { Sustaining, Attacking, AwaitingRest, Releasing, Idle };
    MotorGateState motorGateState = MotorGateState::Sustaining;
    float          motorGateGain  = 1.0f;

    // Deckelt die Wartezeit in AwaitingRest - falls die Geschwindigkeit aus
    // welchem Grund auch immer nie ganz unter die Schwelle faellt, faedet
    // trotzdem irgendwann aus, statt den Motor fuer immer laut zu halten.
    double motorGateAwaitSeconds = 0.0;
    static constexpr double motorGateAwaitTimeoutSeconds = 6.0;

    // Anfahrt schnell (Klick vermeiden), Ausfaden ausdruecklich langsam/ruhig
    // (@dpa: "in aller Ruhe... extra 2..3s ausfaden").
    static constexpr double motorGateAttackSeconds  = 0.03;
    static constexpr double motorGateReleaseSeconds = 2.5;

    // Ab wann "steht" - dieselbe Schwelle wie FieldComponent::
    // coastMinSpeedSquared (0,05 m/s), damit "Ruhe" ueberall dasselbe meint.
    static constexpr double motorGateRestSpeedThreshold = 0.05;

    // Reine Audiothread-Verwaltung: ob M gerade tatsaechlich gegriffen ist
    // (unabhaengig vom Gate-Zustand) und der zuletzt gesehene Wert von
    // motorGateEnabled, um dessen Flankenwechsel zu erkennen (kein eigenes
    // Anfrage-Flag noetig, ein einfacher Wertevergleich pro Block reicht).
    bool motorGateHeld          = false;
    bool motorGateEnabledShadow = false;

    void applyMotorGate (float* mono, int numSamples);

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
    double lastNWaveGainDb     = 0.0;

    // Dieselbe Wiedervorlage wie bei der N-Welle: die drei Setter laufen ueber
    // alle Pfade beider Geometriesaetze und werden darum nur bei einer echten
    // Aenderung angestossen.
    double lastReverseGainDb   = 0.0;
    double lastShockDuckRange  = -1.0;
    double lastShockDuckAmount = 1.0;
    double lastJumpBoom        = 0.0;
    double lastShadowTailMs    = 1.0;

    // Wie viele Klone gerade WIRKLICH mit Loeserphysik laufen - seit der
    // entfernten Automatik immer genau min(Regler, maxRealClones), aber der
    // eigene Wert bleibt (Notaus setzt ihn auf 0, ohne den Regler anzufassen).
    int    effectiveRealClones = 0;

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

        // Reiner Amplitudenfaktor - springt wie damping sofort (kein
        // Klick-Risiko wie bei anchor/azimuth/tilt: die Geometrie und damit
        // die Laufzeit des Pfades bleibt unberuehrt, nur die Lautstaerke
        // aendert sich blockweise, genau wie bei bounceGain/wallDamp).
        double gainLinear = 1.0;
    };

    WallState wallTarget[DopplerEngine::maxWalls];
    WallState wallSmoothed[DopplerEngine::maxWalls];
    bool      wallStateInitialised = false;

    double recorderTickAccum = 0.0;   // dito für die 200-Hz-Aufzeichnung

    // One-Pole-Koeffizient der Yaw-Glättung, aus smootherTau abgeleitet.
    double yawSmoothCoeff = 0.02;

    juce::SmoothedValue<float> outputGainLinear;
    bool limiterEnabled = true;

    // Wie oft der Begrenzer eingegriffen hat, fuer die Statuszeile.
    std::atomic<int> limiterHitCount { 0 };

    // Scope-Ringpuffer (@dpa-Feedback), gefuellt in applyOutputStage() an
    // derselben Stelle wie das Levelmeter. Immer aktiv, unabhaengig davon,
    // ob der Scope im Editor gerade eingeblendet ist - das Schreiben ist ein
    // Array-Zugriff pro Sample, kein messbarer Zusatzaufwand, und so bleibt
    // sofort Signal da, sobald der Scope eingeschaltet wird (kein stiller
    // Deckel, der erst "warmlaufen" muesste).
    ScopeRingBuffer scopeRing;

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
    std::atomic<bool> engineRestartRequest{ false };

    // Ein Zustand wurde geladen (setStateInformation). Anders als der
    // Engine-Restart braucht dieser Weg keinen Editor: der Audiothread holt
    // das Flag am Blockanfang ab und schneidet selbst. Ein Host ohne offenes
    // Fenster flog sonst die ganze Strecke zur geladenen Position ab.
    std::atomic<bool> stateLoadRequest    { false };

    std::atomic<bool> motorGateEnabled    { false };
    std::atomic<bool> sourceGrabRequest   { false };
    std::atomic<bool> sourceReleaseRequest{ false };

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

    // Werte aus SourceKind, atomar statt des Enums selbst (std::atomic<enum
    // class> ginge zwar auch, int ist hier aber schon ueberall sonst der
    // Zustandstyp fuer sowas in dieser Datei).
    std::atomic<int> sourceKindSelected { 0 };   // SourceKind::Motor

    // Nur Message-Thread (gesetzt in loadSampleFile(), gelesen in
    // getStateInformation() und für die Anzeige im Editor). Kein Atomic
    // nötig, juce::String ist hier nie aus dem Audiothread erreichbar.
    juce::String samplePath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopplerfeldProcessor)
};
