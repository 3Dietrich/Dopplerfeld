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

    // Ziel der Quellbewegung. TODO H13: hier gehört der MotionSmoother aus
    // Plan 3.8 dazwischen; bis dahin geht das Ziel direkt in die Trajektorie.
    void setSourceTarget (Vec3 posMetres) { sourceTarget = posMetres; }
    Vec3 getSourceTarget() const { return sourceTarget; }

    void setListener (const ListenerState& l) { listener = l; }
    const ListenerState& getListener() const { return listener; }

    // Unstetiger Sprung. Der zweite Geometriesatz wird an der neuen Stelle
    // mit konstanter Vorgeschichte befüllt (klingt also ohne Anlaufzeit) und
    // gegen den alten überblendet; die Dauer kommt aus computeFadeSamples mit
    // FadeReason::SourcePosition und der Sprungweite.
    void jumpSourceTo (Vec3 posMetres);

    void setBoomLimitDb (double dB);
    void setAirAbsorptionAmount (double amount01);

    // Globaler Umschalter aus Plan 3.11 (fadeAuto/fadeManualMs), wirkt auf
    // den nächsten Geometriewechsel.
    // TODO H13: aus der APVTS speisen, zusammen mit dem gleichnamigen Setter
    // im SoundSourceHolder - beide hängen am selben Parameterpaar.
    void setManualFade (bool shouldUseManual, double seconds);

    // Der Mono-Strom der Quelle kommt von außen herein. TODO H13: hier steht
    // dann der SoundSourceHolder des Processors; die Engine selbst rendert
    // bewusst keine Quelle, damit der geteilte Signalpuffer genau einen
    // Schreiber hat.
    // sourceMono darf nullptr sein, dann wird Stille eingespeist.
    void process (juce::AudioBuffer<float>& stereoOut,
                  const float*              sourceMono,
                  const MediumState&        medium);

    // TODO H11: hier dockt fillSnapshot (FieldSnapshot&) an - dezimierte
    // Trajektorienpunkte, Wellenfront-Emissionszeiten, Ohrgeometrie und pro
    // Pfad Verzögerung/Amplitude/M_r (Plan 3.12). Die Pfad-Getter unten
    // liefern den Pfadanteil davon bereits lock-frei, und zwar vom hörbaren
    // (aktiven) Satz - die Anzeige zeigt während eines Fades also die
    // Geometrie, die überwiegend zu hören ist.
    int numPaths() const { return (int) geometry.active().paths.size(); }
    const PropagationPath& getPath (int index) const { return geometry.active().paths[(size_t) index]; }

    bool isCrossfading() const { return geometry.isFading(); }

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
    struct PathSet
    {
        void prepare (double sampleRate, int maxBlockSize, int numPaths,
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
        const SourceSignalBuffer* signal   = nullptr;
        const MediumState*        medium   = nullptr;
        const std::vector<int>*   pathEar  = nullptr;
        double                    blockStartTime = 0.0;
        double                    sr             = 0.0;
    };

    void pushTrajectory (double blockStart, double blockEnd);

    // Gemeinsamer Kern von Positionssprung und Feldgrößenwechsel.
    void startGeometrySwitch (Vec3 newPos, int fadeSamples);
    void configurePendingSet (Vec3 newPos);
    void applyArmedFieldChange();

    int fadeSamplesFor (FadeReason reason, double positionDeltaMetres) const;

    DualPathCrossfader<PathSet> geometry;

    SourceSignalBuffer signal;          // geteilt, genau ein Schreiber
    std::vector<int>   pathEar;         // welcher Pfad auf welchen Kanal

    SoundSource* source = nullptr;

    ListenerState listener;

    Vec3 sourceTarget { 0.0, 0.0, 0.0 };
    Vec3 prevTarget   { 0.0, 0.0, 0.0 };

    // Warteschlange der Länge eins auf Aufruferseite: der Crossfader merkt
    // sich nur die Dauer, die Zielgeometrie steht hier.
    Vec3 queuedJumpPos { 0.0, 0.0, 0.0 };

    double fieldMetres       = 100.0;
    bool   fieldChangeArmed  = false;
    double maxHistorySeconds = 1.0;

    // Pfadeinstellungen, damit ein frisch konfigurierter Satz sie nicht
    // verliert (PropagationPath::reset() lässt sie zwar stehen, aber beide
    // Sätze müssen von Anfang an gleich eingestellt sein).
    double boomLimitDb    = 30.0;
    double airAbsorbAmount = 1.0;

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
};
