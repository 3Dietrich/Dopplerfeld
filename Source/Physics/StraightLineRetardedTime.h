#pragma once

#include "Vec3.h"

#include <cmath>

// Geschlossene Lösung der Retarded-Time-Gleichung für den Sonderfall
// "geradlinig gleichförmige Quelle, ruhender Empfänger".
//
// Genau dieser Fall ist der Vorbeiflug (FlyByGenerator): die Bahn ist eine
// Gerade, das Tempo konstant, der Hörer steht. Und er ist zugleich der teuerste
// Fall für den numerischen Löser - an der Schallwand (M_r nahe 1) wird das
// Residuum flach, die Lipschitz-Schritte werden winzig, und ein einzelner Block
// kostet das Vier- bis Fünffache des Schnitts. Gemessen liegen die teuersten
// Blöcke durchweg bei |M_r| zwischen 1,01 und 1,08 (siehe load_check).
//
// Hier gibt es dafür gar keine Suche. Mit M(t) = P + V·t und ruhendem L wird aus
//
//     c·(t_h − t_e) = |L − M(t_e)|
//
// nach dem Quadrieren eine gewöhnliche quadratische Gleichung in t_e:
//
//     (c² − V²)·t_e² − 2·(c²·t_h − D·V)·t_e + (c²·t_h² − D·D) = 0
//
// mit D = L − P. Unterschall (V < c) hat einen positiven Leitkoeffizienten und
// genau eine kausale Wurzel; Überschall (V > c) einen negativen und innerhalb
// des Mach-Kegels zwei. Das ist dieselbe Aussage wie in Plan 2.4/2.6, nur
// direkt ablesbar statt erarbeitet.
//
// WOZU das hier dient: NICHT als Ersatz für den numerischen Löser. Der rechnet
// gegen die tatsächlich geschriebene Trajektorie, und die weicht von der idealen
// Geraden ab, sobald ein Glätter, ein Jitter oder eine Handbewegung im Spiel
// ist. Diese Lösung liefert stattdessen einen sehr guten STARTWERT. Der Löser
// muss von dort aus nur noch lokal nachziehen, statt sein ganzes Fenster
// abzutasten - aus Hunderten Auswertungen werden eine Handvoll, und die
// Zuordnung der Zweige bleibt stabil, weil der Startwert auch an der Kaustik
// nicht mehr danebenliegt.
//
// JUCE-frei und ohne Zustand, damit sie sich einzeln prüfen lässt - dieselbe
// Auflage wie für RetardedTimeSolver.
namespace straightline
{

struct Roots
{
    int    count = 0;
    double t_e[2] {};   // aufsteigend sortiert
};

// linePoint/lineVelocity beschreiben die Quelle als M(t) = linePoint +
// lineVelocity·t, in derselben Zeitbasis wie t_h. receiver ist der ruhende
// Empfangspunkt (bei einer Spiegelung der GESPIEGELTE Punkt, die Geometrie
// bleibt dieselbe). c ist die Schallgeschwindigkeit.
//
// Geliefert werden nur kausale Wurzeln, also t_e <= t_h. Bei Unterschall ist
// das genau eine, im Mach-Kegel zwei, ausserhalb keine.
inline Roots solve (Vec3 linePoint, Vec3 lineVelocity, Vec3 receiver,
                    double c, double t_h)
{
    Roots out;

    const Vec3   D  = receiver - linePoint;
    const double vv = lineVelocity.lengthSquared();
    const double dv = D.dot (lineVelocity);
    const double dd = D.lengthSquared();

    const double a = c * c - vv;
    const double b = -2.0 * (c * c * t_h - dv);
    const double e = c * c * t_h * t_h - dd;

    // Entartet: Quelle genau mit Schallgeschwindigkeit. Dann fällt der
    // quadratische Term weg und es bleibt eine lineare Gleichung - derselbe
    // Grenzfall, an dem der numerische Löser sein flaches Residuum sieht.
    if (std::abs (a) < 1.0e-12)
    {
        if (std::abs (b) < 1.0e-12)
            return out;

        const double t = -e / b;

        if (t <= t_h)
            out.t_e[out.count++] = t;

        return out;
    }

    const double disc = b * b - 4.0 * a * e;

    if (disc < 0.0)
        return out;   // kein Schall von dieser Geraden erreicht den Punkt gerade

    const double sq = std::sqrt (disc);

    double r1 = (-b - sq) / (2.0 * a);
    double r2 = (-b + sq) / (2.0 * a);

    if (r1 > r2)
    {
        const double tmp = r1;
        r1 = r2;
        r2 = tmp;
    }

    // Kausalität: der Schall kann nicht aus der Zukunft kommen. Die Wurzel bei
    // t_e == t_h ist der Sonderfall "Quelle sitzt auf dem Empfänger" und wird
    // mitgenommen, R ist dort null.
    if (r1 <= t_h)
        out.t_e[out.count++] = r1;

    if (r2 <= t_h)
        out.t_e[out.count++] = r2;

    return out;
}

} // namespace straightline
