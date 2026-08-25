// Messprogramm zum zeitverkehrt gehoerten Zweig im Ueberschall.
//
// Nicht Teil der CMake-Tests - eigenstaendig zu bauen, weil es nur den Loeser
// und die Trajektorie braucht (keine JUCE-Abhaengigkeit):
//
//   clang++ -std=c++20 -O2 -I Source Tests/reverse_probe.cpp \
//     Source/Physics/RetardedTimeSolver.cpp Source/Physics/SourceTrajectory.cpp \
//     -o /tmp/reverse_probe && /tmp/reverse_probe
//
// Geometrie aus dem Preset "presets/test/Rueckwaerts-unecht" (Mach 1,124).
// Die vier Laeufe trennen, woran der Rueckwaertszweig stirbt: am Rand der
// Bewegungshistorie (A), am Ringpuffer (D) oder gar nicht erst geboren (C).

#include "Physics/RetardedTimeSolver.h"
#include "Physics/SourceTrajectory.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static constexpr double trajRateHz    = 1000.0;
static constexpr double gridDt        = 1.0 / trajRateHz;
static constexpr double bufferSeconds = 30.0;

// Preset-Werte
static constexpr double vFly     = 385.746;   // m/s  -> Mach ~1,12
static constexpr double sideDist = 329.224;   // m    seitlicher Abstand
static constexpr double dz       = 100.0 - 1.750;
static constexpr double xStart   = -1421.73;  // m    Anflugstrecke

static Vec3 posAt (double t) { return Vec3 { xStart + vFly * t, sideDist, dz }; }

// cutAt < 0 = kein Rundenschnitt. Sonst wird dort die Historie neu aufgesetzt,
// genau wie DopplerEngine::configureSet es beim Loop-Schnitt tut: mit jumpTo()
// (ruhende Vorgeschichte) oder, wenn cutLinear, mit fillLinear() und der
// Anfangsgeschwindigkeit der neuen Runde.
static void run (const char* label, double cutAt, double tEnd, bool historyFlying = false,
                 bool cutLinear = false)
{
    const MediumState medium;
    const double c = medium.speedOfSound();

    SourceTrajectory traj;
    traj.prepare (trajRateHz, bufferSeconds);

    if (historyFlying)
    {
        // Die Quelle fliegt schon lange vor t=0 gleichfoermig - keine kuenstliche
        // Grenze zwischen Ruhe und Bewegung in der Vorgeschichte.
        traj.fillLinear (posAt (0.0), Vec3 { vFly, 0.0, 0.0 }, 0.0, bufferSeconds - 1.0);
    }
    else
    {
        traj.reset (posAt (0.0), 0.0);
    }

    RetardedTimeSolver solver;
    const Vec3 receiver { 0.0, 0.0, 0.0 };

    std::printf ("\n=== %s   (c = %.1f m/s, Mach %.3f)\n", label, c, vFly / c);
    if (cutAt >= 0.0)
        std::printf ("    Rundenschnitt bei t_h = %.2f s (jumpTo auf die aktuelle Position)\n", cutAt);

    bool     hadReverse   = false;
    double   reverseBirth = -1.0;
    double   reverseDeath = -1.0;
    double   lastRevT_e   = 0.0;
    bool     cutDone      = false;
    long long pushed      = 0;

    for (double t_h = 0.0; t_h <= tEnd; t_h += gridDt)
    {
        while ((double) (pushed + 1) * gridDt <= t_h)
        {
            ++pushed;
            traj.push (posAt ((double) pushed * gridDt), (double) pushed * gridDt);
        }

        if (cutAt >= 0.0 && ! cutDone && t_h >= cutAt)
        {
            if (cutLinear)
            {
                // Der Weg mit Vorgeschwindigkeit, Zahlen wie in
                // DopplerEngine::configureSet: die Gerade reicht nur so weit
                // zurueck, wie der Schall von dort den Hoerer noch erreicht.
                const double reach   = 0.9 * 331.3 * bufferSeconds;
                const double startR  = (receiver - posAt (t_h)).length();
                const double allowed = std::max (0.0, (reach - startR) / vFly);

                traj.fillLinear (posAt (t_h), Vec3 { vFly, 0.0, 0.0 }, t_h,
                                 std::min (bufferSeconds, allowed));
            }
            else
            {
                // Position bleibt stehen, die Historie wird mit ihr
                // ueberschrieben - eine ruhende Quelle vor dem Schnitt.
                traj.jumpTo (posAt (t_h), t_h);
            }

            solver.reset();
            cutDone = true;
            std::printf ("    [%.3f s] SCHNITT (%s): Historie neu aufgesetzt, Zweige vergessen\n",
                         t_h, cutLinear ? "fillLinear, bewegte Vorgeschichte"
                                        : "jumpTo, ruhende Vorgeschichte");
        }

        Root roots[8];
        const int n = solver.solve (traj, medium, receiver, t_h, roots, 8);

        // Rueckwaertszweig: M_r > 1, dann laeuft dt_e/dt_h negativ.
        int rev = -1;
        for (int i = 0; i < n; ++i)
            if (roots[i].machRadial > 1.0)
                rev = i;

        if (rev >= 0)
        {
            if (! hadReverse)
            {
                hadReverse   = true;
                reverseBirth = t_h;
                std::printf ("    [%.3f s] Rueckwaertszweig GEBOREN  t_e=%.3f  M_r=%.3f  Wurzeln=%d\n",
                             t_h, roots[rev].t_e, roots[rev].machRadial, n);
            }
            lastRevT_e = roots[rev].t_e;
        }
        else if (hadReverse && reverseDeath < 0.0)
        {
            reverseDeath = t_h;
            std::printf ("    [%.3f s] Rueckwaertszweig TOT         zuletzt t_e=%.3f  Wurzeln=%d\n",
                         t_h, lastRevT_e, n);
            std::printf ("             aelteste Trajektorienzeit = %.3f s\n", traj.oldestTime());
        }
    }

    if (hadReverse && reverseDeath < 0.0)
        std::printf ("    Rueckwaertszweig lebt bis zum Ende (t_h = %.2f s), zuletzt t_e=%.3f\n",
                     tEnd, lastRevT_e);

    if (hadReverse)
        std::printf ("    Lebensdauer: %.3f s\n",
                     (reverseDeath < 0.0 ? tEnd : reverseDeath) - reverseBirth);

    std::printf ("    Solver-Zaehler: trackLost=%llu  newId=%llu (davon nah=%llu)  Wurzelzahl-Wechsel=%llu\n",
                 (unsigned long long) solver.trackLostCount(),
                 (unsigned long long) solver.newIdCount(),
                 (unsigned long long) solver.newIdNearCount(),
                 (unsigned long long) solver.rootCountFlips());
}

int main()
{
    // A: durchgehende Historie, kein Rundenschnitt.
    run ("A  ohne Rundenschnitt", -1.0, 8.0);

    // B: Rundenschnitt mitten im Flug, wie beim Loop-Play.
    run ("B  mit Rundenschnitt bei 5,0 s", 5.0, 8.0);

    // C: Schnitt VOR der Geburt des Rueckwaertszweigs - so liegt der Loop-Schnitt
    //    bei @dpa, der ihn ja mehrfach pro Aufnahme durchlaeuft.
    run ("C  mit Rundenschnitt bei 3,0 s (VOR der Geburt)", 3.0, 8.0);

    // E: derselbe Schnitt wie C, aber mit bewegter Vorgeschichte - der Fix.
    run ("E  Rundenschnitt bei 3,0 s MIT Vorgeschwindigkeit", 3.0, 8.0, false, true);

    // F: derselbe Schnitt wie B, aber mit bewegter Vorgeschichte.
    run ("F  Rundenschnitt bei 5,0 s MIT Vorgeschwindigkeit", 5.0, 8.0, false, true);

    // D: Vorgeschichte durchgehend fliegend - das waere der Naturfall, in dem
    //    die Quelle keinen Anfang hat.
    run ("D  Vorgeschichte durchgehend fliegend, kein Schnitt", -1.0, 8.0, true);

    return 0;
}
