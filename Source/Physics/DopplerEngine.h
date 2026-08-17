#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Listener.h"
#include "Medium.h"
#include "PropagationPath.h"
#include "SourceSignalBuffer.h"
#include "SourceTrajectory.h"
#include "Vec3.h"

#include "Sources/SoundSource.h"
#include "Util/Crossfader.h"
#include "Util/FieldSnapshot.h"

#include <atomic>
#include <cstdint>
#include <vector>

// Klammert Quelle, geteilte Puffer und Pfadliste (Plan 3.6).
//
// Das ist die Grenzschicht zur JUCE-Welt: juce::AudioBuffer taucht hier auf,
// darunter (PropagationPath, Listener, PathTransform, Solver, Puffer) bleibt
// alles JUCE-frei und damit offline prüfbar.
//
// paths ist bewusst ein Vector und keine zwei Member. Phase 1 hat genau zwei
// Einträge (linkes/rechtes Ohr, beide mit Identity-Transform); Bodenreflexion
// und Wände aus Phase 2 sind zusätzliche Einträge mit anderem PathTransform,
// sonst ändert sich nichts.
//
// Geometriesprünge (Positionssprung, Feldgrößenwechsel) laufen über einen
// DualPathCrossfader<PathSet>: zwei komplette Sätze aus Trajektorie und
// Pfaden, die BEIDE denselben SourceSignalBuffer lesen. Genau dafür sind
// Trajektorie und Signal getrennte Klassen (Plan 3.2/3.7) - der Fade kostet
// kurzzeitig doppelte Löserlast, aber keinen zweiten Signalpuffer.
class DopplerEngine
{
public:
    // Rasterrate der Trajektorie (Plan 2.12). Auch die Untergrenze der
    // Scan-Schrittweite im Löser hängt daran.
    static constexpr double trajectoryRateHz = 1000.0;

    void prepare (double sampleRate, int maxBlock, double maxFieldMetres);
    void reset();

    // Zeigertausch, kein Besitz - die Lebensdauer verwaltet der Aufrufer.
    // Der Quellwechsel-Crossfade sitzt nicht hier, sondern im
    // SoundSourceHolder (Plan 3.7), der selbst eine SoundSource ist.
    void setSource (SoundSource* src) { source = src; }
    SoundSource* getSource() const { return source; }

    // Löst bei tatsächlicher Änderung einen Geometrie-Crossfade aus (60 ms,
    // FadeReason::FieldSize). Begründung für die Umsetzung ohne zweite
    // DopplerEngine steht in der .cpp bei applyArmedFieldChange().
    void setFieldMetres (double metres);
    double getFieldMetres() const { return fieldMetres; }

    // Bereits GEGLÄTTETE Quellposition (Plan 3.8): der MotionSmoother sitzt
    // im Processor davor und tickt auf trajectoryRateHz, hier kommt nur noch
    // sein Ergebnis an. Zwischen zwei Aufrufen zieht die Engine linear durch
    // (siehe pushTrajectory), der Aufrufer ruft deshalb in kurzen Teilblöcken.
    void setSourceTarget (Vec3 posMetres) { sourceTarget = posMetres; }
    Vec3 getSourceTarget() const { return sourceTarget; }

    void setListener (const ListenerState& l) { listener = l; }
    const ListenerState& getListener() const { return listener; }

    // Unstetiger Sprung. Der zweite Geometriesatz wird an der neuen Stelle
    // mit konstanter Vorgeschichte befüllt (klingt also ohne Anlaufzeit) und
    // gegen den alten überblendet; die Dauer kommt aus computeFadeSamples mit
    // FadeReason::SourcePosition und der Sprungweite.
    void jumpSourceTo (Vec3 posMetres);

    // Wie jumpSourceTo(), aber der neue Geometriesatz bekommt eine
    // gleichförmig BEWEGTE Vorgeschichte statt einer ruhenden. Damit fängt ein
    // Vorbeiflug an, als sei die Quelle schon immer geflogen: kein Positions-
    // und kein Geschwindigkeitssprung, den der Löser als Ereignis auffassen
    // müsste.
    //
    // Wie weit die Gerade zurückreicht, rechnet die Engine selbst aus - sie
    // kennt als Einzige die Pufferlänge und damit die längste Laufzeit, die
    // noch abgedeckt ist (siehe SourceTrajectory::fillLinear).
    void startLinearMotion (Vec3 posMetres, Vec3 velocity);

    void setBoomLimitDb (double dB);
    void setAirAbsorptionAmount (double amount01);

    // Reflektierende Flächen. Index 0 ist der Direktschall (immer an, keine
    // Spiegelung), Index 1 der Boden, danach die Wände.
    //
    // Die Spiegelpfade existieren immer (sie werden in prepare() mit angelegt),
    // gerechnet werden sie nur im eingeschalteten Zustand - ein Umschalten
    // allokiert also nichts und kostet ausgeschaltet auch keine Löserzeit. Beim
    // Wiedereinschalten liegt ihr letzter Löserzeitpunkt in der Vergangenheit;
    // PropagationPath sät sich daraufhin von selbst neu (Lückenerkennung in
    // process()), und die Zweige rampen über den Anti-Klick-Envelope ein statt
    // zu knacken.
    static constexpr int maxWalls    = 2;
    static constexpr int surfaceCount = 2 + maxWalls;   // direkt, Boden, Wände

    void setGroundReflectionEnabled (bool shouldBeEnabled);
    bool isGroundReflectionEnabled() const { return surfaces[1].enabled; }

    // Höhendämpfung der Reflexion, 0..1. Wirkt nur auf die Spiegelpfade.
    void setGroundDampingAmount (double amount01);

    // Eine Wand als unendliche Ebene: Fußpunkt, Richtung der Wandlinie in der
    // Draufsicht und Neigung um genau diese Linie (siehe
    // wallMirrorTransform()). Anders als der Boden darf sie sich bewegen -
    // deshalb wird ihre Abbildung nicht einmalig gesetzt, sondern vor jedem
    // Block aus diesen Werten gebildet. Der Aufrufer glättet sie, damit ein
    // gezogener Regler den Spiegelempfänger nicht springen lässt.
    void setWall (int index, bool enabled, Vec3 anchorMetres,
                  double azimuthRad, double tiltRad, double damping01);

    // Mehrfachreflexionen: genau EINE zusätzliche Generation, also Wege der
    // Form Quelle -> Fläche X -> Fläche Y -> Ohr mit X != Y. Mehr nicht, und
    // ausdrücklich keine Rekursion.
    //
    // Warum das nicht aufschwingen kann (das war der Grund, aus dem der Punkt
    // ursprünglich zurückgestellt wurde): das hier ist die
    // Spiegelquellen-Methode, kein Rückkopplungsnetz. Jeder Weg ist ein
    // eigener PropagationPath, der den geteilten Quellsignalpuffer LIEST und
    // additiv auf den Ausgang schreibt. Kein Pfad schreibt je in den Puffer
    // zurück, kein Ausgang ist irgendwo Eingang - es gibt schlicht keine
    // Schleife, die eine Verstärkung aufsammeln könnte. Der Ausgang ist eine
    // endliche Summe endlich vieler beschränkter Terme (jeder einzelne durch
    // 1/(R_min·eps) begrenzt, wie jeder Pfad im Projekt).
    //
    // bounceGain ist trotzdem da und liegt unter 1: die Dämpfung an der Fläche
    // ist ein Tiefpass mit Gleichstromverstärkung 1, nimmt also nur Höhen und
    // keinen Pegel. Ohne einen ausdrücklichen Faktor je Generation wäre eine
    // zweifach reflektierte Welle nur durch den längeren Weg leiser, was für
    // eine reale Fläche zu wenig ist.
    void setSecondOrderEnabled (bool shouldBeEnabled);
    void setBounceGain (double gain01);

    // Alles außer dem Direktschall aus - die minimale sichere Konfiguration.
    void disableAllReflections();

    // Globaler Umschalter aus Plan 3.11 (fadeAuto/fadeManualMs), wirkt auf
    // den nächsten Geometriewechsel. Denselben Setter hat der
    // SoundSourceHolder - beide hängen am selben Parameterpaar.
    void setManualFade (bool shouldUseManual, double seconds);

    // Der Mono-Strom der Quelle kommt von außen herein (im Plugin aus dem
    // SoundSourceHolder des Processors); die Engine rendert bewusst keine
    // Quelle selbst, damit der geteilte Signalpuffer genau einen Schreiber
    // hat. sourceMono darf nullptr sein, dann wird Stille eingespeist.
    void process (juce::AudioBuffer<float>& stereoOut,
                  const float*              sourceMono,
                  const MediumState&        medium);

    // Anzeigedaten für den Message-Thread (Plan 3.12). Der Audiothread legt
    // den Snapshot am Blockende in einen von zwei Puffern und tauscht einen
    // atomaren Index; hier wird aus dem veröffentlichten Puffer kopiert.
    // Kein Lock, keine Allokation, keine Rückwirkung auf den Audiothread.
    void fillSnapshot (FieldSnapshot& dest) const;

    // Pfadanteil derselben Daten, lock-frei und einzeln abfragbar, und zwar
    // vom hörbaren (aktiven) Satz - während eines Fades zeigt die Anzeige
    // also die Geometrie, die überwiegend zu hören ist.
    int numPaths() const { return (int) geometry.active().paths.size(); }
    const PropagationPath& getPath (int index) const { return geometry.active().paths[(size_t) index]; }

    bool isCrossfading() const { return geometry.isFading(); }

    // Summe der Löser-Auswertungen über ALLE Pfade beider Geometriesätze -
    // maschinenunabhängiges Lastmaß für Regressionstests (load_check). Die
    // Wanduhr allein taugt dafür nicht: sie schwankt auf einem beschäftigten
    // Rechner um Faktor zwei.
    std::uint64_t solverEvaluations() const;

    // Sekunden seit prepare()/reset(), gemeinsame Zeitbasis von Signalpuffer,
    // Trajektorie und Löser.
    double currentTime() const { return (double) sampleClock / sr; }

private:
    // Ein kompletter Geometriesatz: eigene Trajektorie, eigene Pfade und
    // damit eigener Löser-, Filter- und Envelope-Zustand. Nicht kopiert wird
    // der Signalpuffer - der liegt in der Engine und wird nur gelesen.
    //
    // Die Ohrgeometrie gehört mit in den Satz: der ausblendende Satz friert
    // seine ein (prevListener == listener, also Ohrgeschwindigkeit 0). Sonst
    // würde ein Feldgrößenwechsel, der ja auch den Hörer verschiebt, den
    // Sprung über die Ohrgeschwindigkeit in den alten Satz zurückholen.
    // Eine reflektierende Fläche. Index 0 (Direktschall) trägt keine
    // Spiegelung und ist immer aktiv; alle anderen sind es nur, wenn der
    // Benutzer sie einschaltet.
    struct Surface
    {
        PathTransform transform;      // Identität beim Direktschall
        bool          enabled = true;
        double        damping = 0.0;  // Höhenverlust an der Fläche, 0..1

        // Eckfrequenz bei voller Dämpfung. Modellkonstante je Flächenart, kein
        // Regler: der Regler ist die Stärke.
        double        dampFcHz = 800.0;
    };

    // Ein Ausbreitungsweg als Rezept: über welche Flächen er läuft, in der
    // Reihenfolge, in der der Schall sie trifft.
    //
    //   first < 0            - Direktschall
    //   first >= 0, second<0 - eine Reflexion
    //   beide >= 0           - zwei Reflexionen (first zuerst getroffen)
    //
    // Die Abbildung des EMPFÄNGERS läuft dabei genau andersherum:
    // |L - σ_second(σ_first(M))| = |σ_first(σ_second(L)) - M|, weil jede
    // Spiegelung eine Isometrie ist. Deshalb ist die äußere Abbildung σ_first.
    struct PathRecipe
    {
        int ear    = 0;
        int first  = -1;
        int second = -1;

        int order() const { return (first < 0 ? 0 : (second < 0 ? 1 : 2)); }
    };

    struct PathSet
    {
        // Die Länge des Rezeptvektors bestimmt die Pfadanzahl.
        void prepare (double sampleRate, int maxBlockSize, size_t pathCount,
                      double trajRateHz, double maxSeconds);
        void reset (Vec3 pos, double time, const ListenerState& l);

        // Ein Trajektorien-Stützpunkt; merkt sich die Position, damit ein
        // eingefrorener Satz sie ohne Zutun der Engine weiterschreiben kann.
        void push (Vec3 pos, double t);

        // Renderer-Vertrag des DualPathCrossfader. Liest über die Zeiger, die
        // die Engine vor jedem Block setzt.
        void renderInto (juce::AudioBuffer<float>& dest, int numSamples);

        SourceTrajectory             trajectory;
        std::vector<PropagationPath> paths;

        Vec3 lastPos { 0.0, 0.0, 0.0 };

        ListenerState listener;
        ListenerState prevListener;

        // Blockkontext, von der Engine vor jedem process() gesetzt.
        const SourceSignalBuffer*      signal  = nullptr;
        const MediumState*             medium  = nullptr;
        const std::vector<PathRecipe>* recipes = nullptr;
        const DopplerEngine*           engine  = nullptr;
        double                         blockStartTime = 0.0;
        double                         sr             = 0.0;
    };

    // Ist dieser Weg gerade zu rechnen, und wie sieht er aus? Beides hängt am
    // Zustand der beteiligten Flächen und wird deshalb hier und nicht im
    // PathSet beantwortet (die Flächen gehören der Engine, nicht dem Satz).
    bool          recipeEnabled (const PathRecipe& r) const;
    PathTransform recipeTransform (const PathRecipe& r) const;
    double        recipeDamping (const PathRecipe& r) const;
    double        recipeDampFcHz (const PathRecipe& r) const;

    void pushTrajectory (double blockStart, double blockEnd);

    // Schreibt den Anzeige-Snapshot in den gerade nicht veröffentlichten
    // Puffer und tauscht ihn ein (Audiothread, am Blockende).
    void publishSnapshot (const MediumState& medium);

    // Gemeinsamer Kern von Positionssprung und Feldgrößenwechsel.
    void startGeometrySwitch (Vec3 newPos, Vec3 preVelocity, int fadeSamples);
    void configurePendingSet (Vec3 newPos, Vec3 preVelocity);
    void applyArmedFieldChange();

    int fadeSamplesFor (FadeReason reason, double positionDeltaMetres) const;

    DualPathCrossfader<PathSet> geometry;

    SourceSignalBuffer      signal;     // geteilt, genau ein Schreiber
    std::vector<PathRecipe> recipes;    // parallel zu PathSet::paths

    Surface surfaces[surfaceCount];

    bool   secondOrderOn = false;
    double bounceGain    = 0.6;

    SoundSource* source = nullptr;

    ListenerState listener;

    Vec3 sourceTarget { 0.0, 0.0, 0.0 };
    Vec3 prevTarget   { 0.0, 0.0, 0.0 };

    // Warteschlange der Länge eins auf Aufruferseite: der Crossfader merkt
    // sich nur die Dauer, die Zielgeometrie steht hier.
    Vec3 queuedJumpPos { 0.0, 0.0, 0.0 };
    Vec3 queuedJumpVel { 0.0, 0.0, 0.0 };

    double fieldMetres       = 100.0;
    bool   fieldChangeArmed  = false;
    double maxHistorySeconds = 1.0;

    // Pfadeinstellungen, damit ein frisch konfigurierter Satz sie nicht
    // verliert (PropagationPath::reset() lässt sie zwar stehen, aber beide
    // Sätze müssen von Anfang an gleich eingestellt sein).
    double boomLimitDb    = 30.0;
    double airAbsorbAmount = 1.0;

    // Eckfrequenz der Bodendämpfung bei voller Stärke. Rund ein Kilohertz ist
    // die Gegend, in der eine streifende Reflexion an Gras/Erde ihre Höhen
    // verliert. Eine Wand ist typischerweise härter und behält mehr davon.
    static constexpr double groundDampFcHz = 800.0;
    static constexpr double wallDampFcHz   = 2500.0;

    // Lage der Wände, für die Anzeige mitgeführt (die Abbildung selbst steht
    // in surfaces[] und ist daraus nicht mehr ablesbar).
    struct WallGeometry
    {
        Vec3   anchor;
        double azimuthRad = 0.0;
        double tiltRad    = 0.0;
    };

    WallGeometry wallGeometry[maxWalls];

    bool   useManualFade = false;
    double manualFadeSeconds = 0.05;

    double sr       = 0.0;
    int    maxBlock = 0;

    // Fortlaufender Sample-Zähler, nie gewrappt - deckungsgleich mit
    // SourceSignalBuffer::writePosition(), damit timeToIndex() und die
    // Zeitachse des Lösers dieselbe Null haben.
    std::int64_t sampleClock = 0;

    // Nächster zu schreibender Trajektorien-Stützpunkt als ganzzahliger
    // Rasterindex, damit die Rasterzeiten nicht wegdriften. Gemeinsam für
    // beide Sätze: sie müssen auf demselben Raster liegen, sonst laufen die
    // beiden Zeitachsen während eines Fades auseinander.
    std::int64_t nextTrajIndex = 0;

    std::vector<float> silence;   // Ersatz, wenn kein Quellsignal anliegt

    // --- Anzeige-Snapshot (Plan 3.12) ---
    //
    // Zwei Puffer plus atomarer Index: der Schreiber füllt immer den, den der
    // Leser gerade nicht liest. Die Generationszählung darüber deckt den
    // einzigen verbleibenden Fall ab, den der Index allein nicht abdeckt -
    // dass der Audiothread während einer laufenden Lesekopie zweimal
    // veröffentlicht und dabei den gelesenen Puffer wieder einholt. Ungerade
    // Generation heißt "Schreiben läuft"; fillSnapshot verwirft die Kopie
    // dann und probiert es erneut, statt den Audiothread warten zu lassen.
    static constexpr double snapshotIntervalSeconds = 1.0 / 60.0;

    FieldSnapshot              snapshotBuffers[2];
    std::atomic<int>           snapshotIndex { 0 };
    std::atomic<unsigned int>  snapshotGeneration { 0 };
    int                        snapshotWriteSlot = 1;
    double                     lastSnapshotTime  = -1.0;
};
