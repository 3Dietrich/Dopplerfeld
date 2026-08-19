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
#include "Util/CloneSpray.h"
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
        std::atomic<float>* globalMaxSpeed = nullptr;

        std::atomic<float>* flyKind     = nullptr;
        std::atomic<float>* flyStart    = nullptr;
        std::atomic<float>* flyDistance = nullptr;
        std::atomic<float>* flyApproach = nullptr;
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
        std::atomic<float>* wallGain[DopplerEngine::maxWalls]  {};

        std::atomic<float>* nWaveOn   = nullptr;
        std::atomic<float>* nWaveSize  = nullptr;

        std::atomic<float>* cloneTotal  = nullptr;
        std::atomic<float>* cloneReal   = nullptr;
        std::atomic<float>* cloneAuto   = nullptr;
        std::atomic<float>* cloneSpread = nullptr;
        std::atomic<float>* cloneLevel  = nullptr;

        std::atomic<float>* reflect2ndOn = nullptr;
        std::atomic<float>* bounceGain   = nullptr;
        std::atomic<float>* bounceGainDb = nullptr;

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
    AudioInSource     audioInSource;
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

    // Additive Mikrobewegung der Quelle M, vor sourceSmoothers eingehakt
    // (siehe advanceMotion()) - "echter Chorus" bei Stillstand.
    PositionJitter  sourceJitter;

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
    std::atomic<int>  activeCheapClones { 0 };

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
