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

    // Wie jumpTo(), aber mit gleichförmiger Bewegung statt Ruhe als
    // Vorgeschichte: M(t) = pos + vel * (t - time) für den gesamten Puffer.
    //
    // Damit lässt sich ein Vorbeiflug so anfangen, als sei die Quelle schon
    // immer geflogen. Der Unterschied zu jumpTo() ist nicht kosmetisch: nach
    // jumpTo() steht in der Vorgeschichte eine ruhende Quelle, der Löser findet
    // dort M_r = 0 und die Bewegung setzt mit einem Geschwindigkeitssprung ein
    // - hörbar als Knacken beziehungsweise, bei Überschall, als ein Knall, den
    // die Bahn gar nicht hergibt. Nach fillLinear() ist die Vorgeschichte
    // dieselbe Gerade, auf der es weitergeht, und im Löser passiert an der Naht
    // schlicht nichts Besonderes.
    // spanSeconds begrenzt, wie weit die gleichförmige Bewegung zurückreicht;
    // davor ruht die Quelle an dem Punkt, an dem die Gerade beginnt. Diese
    // Begrenzung ist keine Bequemlichkeit, sondern nötig: der Puffer deckt eine
    // endliche Laufzeit ab (bei n_max rund 42 s, also gut 14 km). Eine
    // unbegrenzte Rückrechnung würde die Quelle bei Überschall so weit
    // entfernen, dass ihr Schall die volle Pufferlänge nicht mehr schafft - der
    // Löser fände dann gar keine Wurzel und das Ergebnis wäre Stille (siehe
    // "Puffer kürzer als die Laufzeit" in RetardedTimeSolver::solve).
    void fillLinear (Vec3 pos, Vec3 vel, double time, double spanSeconds);

    // Catmull-Rom-Interpolation zwischen den Stützstellen um t. false,
    // wenn t außerhalb von [oldestTime(), newestTime()] liegt.
    bool sampleAt (double t, Vec3& outPos, Vec3& outVel) const;

    // Wie sampleAt(), liefert aber nur die Position. Das Residuum des Lösers
    // (F = c*(t_h - t) - |L - M(t)|) braucht die Geschwindigkeit nicht, und
    // diese Auswertung ist die mit Abstand häufigste Operation im Audiothread
    // - hier fällt eine ganze Catmull-Rom-Interpolation pro Aufruf weg.
    bool samplePositionAt (double t, Vec3& outPos) const;

    // Größte Geschwindigkeit |v_M(t)| im Fenster [t0, jetzt], O(log n) über
    // eine intern gepflegte monotone Deque (Standardalgorithmus "sliding
    // window maximum"), gepflegt beim Schreiben in push()/reset().
    //
    // Die Abfrage ist ausdrücklich NICHT destruktiv: sie sucht den ersten
    // Eintrag ab t0 binär, statt den Vorderrand bis dorthin wegzuwerfen.
    // Nur so darf jeder Empfangspunkt sein EIGENES Fenster fragen - und genau
    // das braucht der Löser, dessen Suchfenster von |L| abhängt und damit für
    // Ohr, Spiegelohr und Feldgröße verschieden ausfällt. Mit destruktiver
    // Eviktion hätte der erste Frager dem zweiten die Einträge weggeworfen,
    // die dieser für seine (größere) Schranke noch braucht.
    double maxSpeedSince (double t0) const;

    // Größter Abstand |M(t)| zum Koordinatenursprung im Fenster [t0, jetzt],
    // nach demselben Muster über eine zweite monotone Deque. Damit lässt sich
    // die maximal mögliche Laufzeit abschätzen: R(t) = |L - M(t)| <=
    // |L| + max |M(t)|. Der Löser begrenzt darüber sein Suchfenster (siehe
    // RetardedTimeSolver::solve).
    double maxDistanceSince (double t0) const;

    double oldestTime() const;
    double newestTime() const;

private:
    void fillConstant (Vec3 pos, double newestSampleTime);
    void pushSpeedSample (double t, double speed);
    void pushDistanceSample (double t, double distance);
    int  ringSlot (std::int64_t idx) const;
    const TrajectorySample& sampleAtClampedIndex (std::int64_t idx) const;

    // Größter Index k mit t(k) <= t. Rechnet den Index aus dem gleichförmigen
    // Raster direkt aus, statt ihn zu suchen (siehe .cpp).
    std::int64_t indexBefore (double t) const;

    std::vector<TrajectorySample> ring;
    int    capacity = 0;
    double gridDt   = 0.001;

    // Fortlaufend, nie gewrappt - wie viele Punkte insgesamt geschrieben
    // wurden. Der gültige Bereich ist [writeIndex - capacity, writeIndex - 1].
    std::int64_t writeIndex = 0;

    // Max über [t0, jetzt] aus einer monotonen Deque, ohne sie zu verändern.
    static double maxSince (const std::deque<std::pair<double, double>>& d, double t0);

    // (Zeit, Geschwindigkeit), streng monoton fallend von vorn nach hinten.
    // Eviktiert wird ausschließlich beim Schreiben, wenn ein Eintrag aus dem
    // Ringpuffer fällt - nie beim Abfragen (siehe maxSpeedSince).
    std::deque<std::pair<double, double>> speedDeque;

    // (Zeit, |p|) nach demselben Muster wie speedDeque.
    std::deque<std::pair<double, double>> distDeque;
};
