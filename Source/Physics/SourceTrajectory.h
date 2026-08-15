#pragma once

#include "Vec3.h"
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

// Ein Stützpunkt der Bewegungsbahn, siehe Plan 2.10. Wird sowohl als
// Ringpuffer-Eintrag hier als auch später vom Löser (H4) gelesen, deshalb
// öffentlich und JUCE-frei.
struct TrajectorySample
{
    double t = 0.0;    // Sekunden seit Epoch
    Vec3   p;          // Meter
    Vec3   v;          // m/s, beim Schreiben aus der Differenz zum Vorgänger berechnet
    float  speed = 0.0f;  // |v|, für den Überschall-Max-Test
};

// Der geteilte Bewegungsverlauf der Quelle (Plan 3.2). Physisch immer auf
// die volle Kapazität angelegt, fortlaufende int64-Schreibposition wie im
// granular-Vorbild (granular/Source/PluginProcessor.cpp) - Modulo passiert
// erst beim Zugriff (ringSlot), nie beim Fortschreiben von writeIndex.
//
// Reines C++, keine JUCE-Abhängigkeit, damit die Klasse einzeln und offline
// testbar bleibt (Plan 3.4).
class SourceTrajectory
{
public:
    void prepare (double trajRateHz, double maxSeconds);

    // Füllt den gesamten Puffer rückwärts konstant mit initialPos, endend
    // bei startTime (Plan 2.6: "vor dem ältesten Puffereintrag ruhte die
    // Quelle an ihrer ältesten bekannten Position").
    void reset (Vec3 initialPos, double startTime);

    // Berechnet v selbst aus der Differenz zu push()s letztem Punkt.
    void push (Vec3 pos, double time);

    // Überschreibt die komplette Vorgeschichte konstant mit der neuen
    // Position, wie reset() - der Baustein für Positionssprünge: eine
    // zweite Trajektorie wird so sofort an der neuen Stelle befüllt, damit
    // sie ohne Anlaufzeit klingt (Plan 3.2/3.7).
    void jumpTo (Vec3 pos, double time);

    // Catmull-Rom-Interpolation zwischen den Stützstellen um t. false,
    // wenn t außerhalb von [oldestTime(), newestTime()] liegt.
    bool sampleAt (double t, Vec3& outPos, Vec3& outVel) const;

    // O(1) über eine intern gepflegte monotone Deque (Standardalgorithmus
    // "sliding window maximum"), gepflegt beim Schreiben in push()/reset().
    // Geht von einem an "jetzt" verankerten Fenster aus, wie es der Löser
    // benutzt (t1 == newestTime()); mit t0 wird zusätzlich vorne eviktiert.
    double maxSpeedInWindow (double t0, double t1) const;

    double oldestTime() const;
    double newestTime() const;

private:
    void fillConstant (Vec3 pos, double newestSampleTime);
    void pushSpeedSample (double t, double speed);
    int  ringSlot (std::int64_t idx) const;
    const TrajectorySample& sampleAtClampedIndex (std::int64_t idx) const;

    std::vector<TrajectorySample> ring;
    int    capacity = 0;
    double gridDt   = 0.001;

    // Fortlaufend, nie gewrappt - wie viele Punkte insgesamt geschrieben
    // wurden. Der gültige Bereich ist [writeIndex - capacity, writeIndex - 1].
    std::int64_t writeIndex = 0;

    // (Zeit, Geschwindigkeit), streng monoton fallend von vorn nach hinten.
    // mutable, weil maxSpeedInWindow() bei älterem t0 vorne eviktieren darf,
    // ohne den logischen (Puffer-)Zustand der Klasse zu ändern.
    mutable std::deque<std::pair<double, double>> speedDeque;
};
