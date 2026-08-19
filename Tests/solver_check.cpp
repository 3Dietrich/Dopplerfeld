// Prüfprogramm für den Retarded-Time-Löser (Plan H4). Kein JUCE, kein Audio -
// nur RetardedTimeSolver, SourceTrajectory und die geschlossene Lösung aus
// Plan 2.5 als unabhängige Referenz.
//
// exit 0 = alle Abnahmekriterien erfüllt, exit 1 = mindestens eines nicht.

#include "Physics/PathTransform.h"
#include "Physics/RetardedTimeSolver.h"
#include "Physics/StraightLineRetardedTime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{

// ---------------------------------------------------------------- Rahmen

int failures = 0;

void ok (const char* name, const char* detail)
{
    std::printf ("  [ok]   %-46s %s\n", name, detail);
}

void fail (const char* name, const char* detail)
{
    std::printf ("  [FAIL] %-46s %s\n", name, detail);
    ++failures;
}

void check (bool condition, const char* name, const char* detail)
{
    if (condition)
        ok (name, detail);
    else
        fail (name, detail);
}

// ------------------------------------------------- gemeinsame Konstanten

constexpr double trajRateHz    = 1000.0;   // Plan 2.12
constexpr double bufferSeconds = 40.0;     // Plan 2.12
constexpr double gridDt        = 1.0 / trajRateHz;

// Toleranz "besser als ein Sample" laut H4-Abnahme, bezogen auf 48 kHz.
constexpr double sampleTol = 1.0 / 48000.0;

// --------------------------------------- geschlossene Lösung, Plan 2.5
//
// M(t) = M0 + v*t, L ruhend. Mit A = L - M(t_h) und der Laufzeit τ = t_h - t_e:
//   (c² - |v|²) τ² - 2 (A·v) τ - |A|² = 0
//   τ = [ (A·v) ± sqrt( (A·v)² + (c² - |v|²) |A|² ) ] / (c² - |v|²)
// Liefert die positiven Lösungen aufsteigend sortiert.
int closedFormTau (Vec3 A, Vec3 v, double c, double outTau[2])
{
    const double den  = c * c - v.lengthSquared();
    const double av   = A.dot (v);
    const double disc = av * av + den * A.lengthSquared();

    int n = 0;

    if (std::abs (den) < 1.0e-9)
    {
        // Grenzfall |v| = c: die quadratische Gleichung entartet zur linearen.
        if (std::abs (av) > 1.0e-12)
        {
            const double tau = -A.lengthSquared() / (2.0 * av);
            if (tau > 0.0)
                outTau[n++] = tau;
        }
        return n;
    }

    if (disc < 0.0)
        return 0;

    const double s   = std::sqrt (disc);
    const double t1  = (av + s) / den;
    const double t2  = (av - s) / den;

    if (t1 > 0.0) outTau[n++] = t1;
    if (t2 > 0.0) outTau[n++] = t2;

    if (n == 2 && outTau[0] > outTau[1])
        std::swap (outTau[0], outTau[1]);

    return n;
}

// Füllt eine Trajektorie mit gleichförmiger Bewegung M(t) = M0 + v*t von
// tStart (Ruhe davor, Plan 2.6) bis tEnd auf dem 1-kHz-Raster.
void fillUniform (SourceTrajectory& traj, Vec3 M0, Vec3 v, double tStart, double tEnd)
{
    traj.prepare (trajRateHz, bufferSeconds);
    traj.reset (M0 + v * tStart, tStart);

    const int steps = (int) std::llround ((tEnd - tStart) / gridDt);

    for (int k = 1; k <= steps; ++k)
    {
        const double t = tStart + (double) k * gridDt;
        traj.push (M0 + v * t, t);
    }
}

// -------------------------------------------------------------- Test 1
// Ruhende Quelle: die triviale Wurzel t_e = t_h - R/c.
void testRestingSource()
{
    std::printf ("\nRuhende Quelle\n");

    const MediumState medium;
    const double c = medium.speedOfSound();

    const Vec3 source { 0.0, 0.0, 0.0 };
    const Vec3 listener { 100.0, 0.0, 0.0 };

    SourceTrajectory traj;
    fillUniform (traj, source, Vec3 { 0.0, 0.0, 0.0 }, 0.0, 5.0);

    RetardedTimeSolver solver;
    solver.setMinScanStep (gridDt);

    const double t_h = traj.newestTime();

    Root roots[8];
    const int n = solver.solve (traj, medium, listener, t_h, roots, 8);

    char buf[256];
    std::snprintf (buf, sizeof (buf), "n = %d", n);
    check (n == 1, "genau eine Wurzel", buf);

    if (n != 1)
        return;

    const double expected = t_h - 100.0 / c;
    const double err      = std::abs (roots[0].t_e - expected);

    std::snprintf (buf, sizeof (buf), "|dt| = %.3e s (%.4f Samples @48k)",
                   err, err / sampleTol);
    check (err < sampleTol, "t_e trifft t_h - R/c", buf);

    std::snprintf (buf, sizeof (buf), "R = %.9f m, M_r = %.3e", roots[0].R, roots[0].machRadial);
    check (std::abs (roots[0].R - 100.0) < 1.0e-9 && std::abs (roots[0].machRadial) < 1.0e-12,
           "R und M_r der ruhenden Quelle", buf);
}

// -------------------------------------------------------------- Test 2
// Gleichförmige Unterschallbewegung gegen die geschlossene Lösung.
void testUniformSubsonic()
{
    std::printf ("\nGleichfoermige Unterschallbewegung (Mach 0,6)\n");

    const MediumState medium;
    const double c = medium.speedOfSound();

    // Schräg an einem seitlich versetzten Hörer vorbei, damit M_r im Verlauf
    // beide Vorzeichen annimmt (Annäherung und Entfernung).
    const Vec3 M0 { -400.0, -60.0, 0.0 };
    const Vec3 v  { 0.6 * c, 0.15 * c, 0.0 };
    const Vec3 listener { 120.0, 40.0, 0.0 };

    SourceTrajectory traj;
    fillUniform (traj, M0, v, 0.0, 20.0);

    RetardedTimeSolver solver;
    solver.setMinScanStep (gridDt);

    double worstErr = 0.0;
    int    badCount = 0;

    // Ab 6 s, damit beide Wurzeln sicher im gleichförmig bewegten Abschnitt
    // liegen und nicht im eingefrorenen Rand vor t = 0.
    for (double t_h = 6.0; t_h <= 20.0; t_h += 0.01)
    {
        Root roots[8];
        const int n = solver.solve (traj, medium, listener, t_h, roots, 8);

        if (n != 1)
        {
            ++badCount;
            continue;
        }

        const Vec3 A = listener - (M0 + v * t_h);

        double tau[2];
        const int nTau = closedFormTau (A, v, c, tau);

        if (nTau != 1)
        {
            ++badCount;
            continue;
        }

        worstErr = std::max (worstErr, std::abs (roots[0].t_e - (t_h - tau[0])));
    }

    char buf[256];
    std::snprintf (buf, sizeof (buf), "%d Abweichungen von 'genau eine Wurzel'", badCount);
    check (badCount == 0, "durchgehend genau eine Wurzel", buf);

    std::snprintf (buf, sizeof (buf), "max |dt| = %.3e s (%.5f Samples @48k)",
                   worstErr, worstErr / sampleTol);
    check (worstErr < sampleTol, "numerisch == geschlossene Loesung (2.5)", buf);
}

// -------------------------------------------------------------- Test 3
// Gleichförmige Überschallbewegung: zwei Wurzeln, geschlossene Lösung.
void testUniformSupersonic()
{
    std::printf ("\nGleichfoermige Ueberschallbewegung (Mach 2)\n");

    const MediumState medium;
    const double c = medium.speedOfSound();

    const Vec3 M0 { -1500.0, 0.0, 0.0 };
    const Vec3 v  { 2.0 * c, 0.0, 0.0 };
    const Vec3 listener { 0.0, 100.0, 0.0 };

    SourceTrajectory traj;
    fillUniform (traj, M0, v, 0.0, 5.0);

    RetardedTimeSolver solver;
    solver.setMinScanStep (gridDt);

    double worstErr  = 0.0;
    int    missing   = 0;
    int    checked   = 0;

    for (double t_h = 3.0; t_h <= 5.0; t_h += 0.01)
    {
        Root roots[8];
        const int n = solver.solve (traj, medium, listener, t_h, roots, 8);

        const Vec3 A = listener - (M0 + v * t_h);

        double tau[2];
        const int nTau = closedFormTau (A, v, c, tau);

        if (nTau != 2)
            continue;   // ausserhalb des Mach-Kegels, hier nicht der Testfall

        // Erwartet: beide analytischen Wurzeln plus die Randwurzel aus der
        // Ruhephase vor Pufferbeginn (Plan 2.6).
        for (int k = 0; k < 2; ++k)
        {
            const double want = t_h - tau[k];

            // Die geschlossene Lösung gilt nur für eine seit jeher
            // gleichförmig bewegte Quelle. Wurzeln, die in die Ruhephase vor
            // t = 0 fallen, gehören zur Randbedingung aus Plan 2.6 und sind
            // hier nicht die Referenz. 50 ms Abstand zum Bewegungsbeginn,
            // damit auch die Catmull-Rom-Glättung des Knicks (±2 Rasterschritte)
            // sicher draussen bleibt.
            if (want < 0.05)
                continue;

            double best = 1.0e9;
            for (int i = 0; i < n; ++i)
                best = std::min (best, std::abs (roots[i].t_e - want));

            if (best > sampleTol)
                ++missing;

            worstErr = std::max (worstErr, best);
            ++checked;
        }
    }

    char buf[256];
    std::snprintf (buf, sizeof (buf), "%d von %d analytischen Wurzeln nicht getroffen",
                   missing, checked);
    check (checked >= 200 && missing == 0, "beide Ueberschallwurzeln gefunden", buf);

    std::snprintf (buf, sizeof (buf), "max |dt| = %.3e s (%.5f Samples @48k)",
                   worstErr, worstErr / sampleTol);
    check (worstErr < sampleTol, "numerisch == geschlossene Loesung (2.5)", buf);

    // Der zeitverkehrte Zweig (Plan 2.8) muss auch als solcher gemeldet
    // werden: die frühere der beiden Wurzeln hat M_r > 1.
    {
        const double t_h = 4.0;

        Root roots[8];
        const int n = solver.solve (traj, medium, listener, t_h, roots, 8);

        double maxMr = -1.0e9;
        for (int i = 0; i < n; ++i)
            maxMr = std::max (maxMr, roots[i].machRadial);

        std::snprintf (buf, sizeof (buf), "n = %d, max M_r = %.4f", n, maxMr);
        check (n == 3 && maxMr > 1.0, "drei Zweige, einer mit M_r > 1", buf);
    }
}

// -------------------------------------------------------------- Test 4
// Kreisbewegung über 60 s: keine verlorene Wurzel, keine Identitätswechsel.
void testCircularNoDropouts()
{
    std::printf ("\nKreisbewegung, 60 s ohne Wurzelverlust\n");

    const MediumState medium;

    const double radius  = 50.0;
    const double omega   = 2.0;                 // rad/s -> |v| = 100 m/s, Mach 0,29
    const Vec3   centre  { 0.0, 0.0, 0.0 };
    const Vec3   listener { 30.0, 0.0, 0.0 };   // ausserhalb der Mitte: echter Doppler

    const double duration = 60.0;

    auto posAt = [&] (double t)
    {
        return centre + Vec3 { radius * std::cos (omega * t), radius * std::sin (omega * t), 0.0 };
    };

    auto velAt = [&] (double t)
    {
        return Vec3 { -radius * omega * std::sin (omega * t),
                       radius * omega * std::cos (omega * t), 0.0 };
    };

    SourceTrajectory traj;
    traj.prepare (trajRateHz, bufferSeconds);
    traj.reset (posAt (0.0), 0.0);

    RetardedTimeSolver solver;
    solver.setMinScanStep (gridDt);

    const double solverDt = 64.0 / 48000.0;   // solverStride 64 bei 48 kHz, Plan 2.11

    std::int64_t pushed = 0;
    int  calls          = 0;
    int  wrongCount     = 0;
    int  idChanges      = 0;
    int  firstId        = -1;
    double maxJump      = 0.0;
    double prevTe       = 0.0;
    bool   havePrev     = false;
    double maxRErr      = 0.0;
    double maxMrErr     = 0.0;

    for (double t_h = solverDt; t_h <= duration; t_h += solverDt)
    {
        while ((double) (pushed + 1) * gridDt <= t_h)
        {
            ++pushed;
            const double t = (double) pushed * gridDt;
            traj.push (posAt (t), t);
        }

        Root roots[8];
        const int n = solver.solve (traj, medium, listener, t_h, roots, 8);
        ++calls;

        if (n != 1)
        {
            ++wrongCount;
            havePrev = false;
            continue;
        }

        if (firstId < 0)
            firstId = roots[0].id;
        else if (roots[0].id != firstId)
            ++idChanges;

        if (havePrev)
            maxJump = std::max (maxJump, std::abs (roots[0].t_e - prevTe));

        prevTe   = roots[0].t_e;
        havePrev = true;

        // Invariante aus der Gleichung selbst: R muss exakt die Laufstrecke
        // c*(t_h - t_e) sein. Ein Griff auf den falschen Trajektorienpunkt
        // beim Ausfüllen von Root fiele hier sofort auf.
        maxRErr = std::max (maxRErr,
                            std::abs (roots[0].R - medium.speedOfSound() * (t_h - roots[0].t_e)));

        // M_r gegen die analytische Radialgeschwindigkeit - erst ab 50 ms
        // Emissionszeit, davor liegt t_e in der Ruhephase vor Pufferbeginn
        // (Plan 2.6), wo die Kreisformel nicht die Referenz ist.
        if (roots[0].t_e > 0.05)
        {
            const Vec3   p     = posAt (roots[0].t_e);
            const Vec3   uHat  = (listener - p).normalised();
            const double mrRef = uHat.dot (velAt (roots[0].t_e)) / medium.speedOfSound();

            maxMrErr = std::max (maxMrErr, std::abs (roots[0].machRadial - mrRef));
        }
    }

    char buf[256];
    std::snprintf (buf, sizeof (buf), "%d Aussetzer bei %d Solver-Schritten", wrongCount, calls);
    check (wrongCount == 0, "in jedem Schritt genau eine Wurzel", buf);

    std::snprintf (buf, sizeof (buf), "%d Wechsel, id = %d", idChanges, firstId);
    check (idChanges == 0, "Zweig-id bleibt ueber 60 s stabil", buf);

    // dt_e/dt_h = 1/(1 - M_r); bei Mach 0,29 liegt der Faktor zwischen 0,77
    // und 1,4, ein Sprung deutlich darüber wäre ein übersprungener Zweig.
    std::snprintf (buf, sizeof (buf), "max dt_e/dt_h = %.4f", maxJump / solverDt);
    check (maxJump < 2.0 * solverDt, "t_e laeuft stetig weiter", buf);

    std::snprintf (buf, sizeof (buf), "max |R - c*(t_h - t_e)| = %.3e m", maxRErr);
    check (maxRErr < 1.0e-6, "R passt zur Laufzeit", buf);

    // Toleranz 2e-3: die Trajektorie rechnet v als Rückwärtsdifferenz, das
    // ist die mittlere Geschwindigkeit über ein Rasterintervall und liegt
    // damit um dt/2 = 0,5 ms zurück. Bei omega = 2 rad/s sind das
    // |a|*dt/2 = 0,1 m/s von 100 m/s, also rund 3e-4 in M_r.
    std::snprintf (buf, sizeof (buf), "max |dM_r| = %.3e", maxMrErr);
    check (maxMrErr < 2.0e-3, "M_r trifft die analytische Radialgeschw.", buf);
}

// -------------------------------------------------------------- Test 5
// Mach-2-Vorbeiflug: das zusätzliche Wurzelpaar entsteht genau dann, wenn der
// Mach-Kegel mit Halbwinkel arcsin(1/2) = 30° den Empfänger überstreicht.
void testMachConeArrival()
{
    std::printf ("\nMach-2-Vorbeiflug, Kegelankunft\n");

    const MediumState medium;
    const double c = medium.speedOfSound();

    const double speed = 2.0 * c;
    const double d     = 100.0;    // seitlicher Abstand des Empfängers
    const double tPass = 1.0;      // Zeit der grössten Annäherung

    const Vec3 M0 { -speed * tPass, 0.0, 0.0 };
    const Vec3 v  { speed, 0.0, 0.0 };
    const Vec3 listener { 0.0, d, 0.0 };

    SourceTrajectory traj;
    fillUniform (traj, M0, v, 0.0, 1.6);

    RetardedTimeSolver solver;
    solver.setMinScanStep (gridDt);

    // Analytisch: der Empfänger liegt auf dem Kegelmantel, wenn
    // sin α = c/|v| = 1/2, also α = 30°. Mit der Quelle bei x = speed*(t_h - tPass)
    // und dem Empfänger bei (0, d) heisst das d/sqrt(x² + d²) = 1/2, also
    // x = d*sqrt(3) und damit t_boom = tPass + d*sqrt(3)/|v|.
    const double tBoom = tPass + d * std::sqrt (3.0) / speed;

    // Die zugehörige Doppelwurzel liegt bei M_r = 1, das ist x = -d/sqrt(3):
    const double teFold = tPass - d / (std::sqrt (3.0) * speed);

    const double solverDt = 8.0 / 48000.0;   // Plan 2.11: bei Überschall Stride 8

    double tDetect  = -1.0;
    double midTe    = 0.0;
    int    nAtDetect = 0;

    Root prevRoots[8];
    int  prevN = 0;

    for (double t_h = tPass; t_h <= 1.6; t_h += solverDt)
    {
        Root roots[8];
        const int n = solver.solve (traj, medium, listener, t_h, roots, 8);

        if (tDetect < 0.0 && n >= 3)
        {
            tDetect   = t_h;
            nAtDetect = n;

            // Die beiden neuen Wurzeln sind die, die im vorigen Schritt noch
            // nicht da waren; ihr Mittelwert liegt bei der Doppelwurzel.
            double sum = 0.0;
            int    cnt = 0;

            for (int i = 0; i < n; ++i)
            {
                bool wasThere = false;

                for (int j = 0; j < prevN; ++j)
                    if (std::abs (roots[i].t_e - prevRoots[j].t_e) < 0.05)
                        wasThere = true;

                if (! wasThere)
                {
                    sum += roots[i].t_e;
                    ++cnt;
                }
            }

            midTe = (cnt > 0) ? sum / (double) cnt : 0.0;
            break;
        }

        prevN = n;
        for (int i = 0; i < n; ++i)
            prevRoots[i] = roots[i];
    }

    char buf[256];

    if (tDetect < 0.0)
    {
        fail ("zusaetzliches Wurzelpaar entsteht", "kein Schritt mit >= 3 Wurzeln gefunden");
        return;
    }

    const double err = tDetect - tBoom;

    // Toleranz: 3 ms. Begründung - der Vollscan ist unterhalb von
    // |F| < Lip*minStep an das 1-ms-Trajektorienraster gebunden (minStep,
    // Plan 2.10). Die beiden neuen Wurzeln trennen sich mit
    // Δ ≈ sqrt(2*(t_h - t_boom)/g'') und werden vom Scan erst erfasst, wenn
    // sie mindestens ein Rasterintervall auseinander liegen. Mit g'' ≈ 8,9/s²
    // in dieser Geometrie ist das nach wenigen Mikrosekunden der Fall, 3 ms
    // sind also reichlich Luft und trotzdem weit unter dem Fehler, den ein
    // falscher Kegelwinkel machen würde (5 % Winkelfehler wären hier ~13 ms).
    const double tol = 3.0e-3;

    std::snprintf (buf, sizeof (buf), "t_erkannt = %.6f s, t_boom = %.6f s, dt = %+.3e s, n = %d",
                   tDetect, tBoom, err, nAtDetect);
    check (err >= -1.0e-4 && err < tol, "Paar entsteht am 30deg-Machkegel", buf);

    std::snprintf (buf, sizeof (buf), "Mitte t_e = %.6f s, erwartet %.6f s, dt = %+.3e s",
                   midTe, teFold, midTe - teFold);
    check (std::abs (midTe - teFold) < 5.0e-3, "Doppelwurzel liegt bei M_r = 1", buf);

    // Und danach muss das Paar auch wieder verschwinden: die Randwurzel aus
    // der Ruhephase vor Pufferbeginn erreicht den Hörer bei
    // t = 0 + |M(0) - L|/c, danach bleibt nur noch ein Zweig.
    {
        const double rStart = (listener - M0).length();
        const double tEnd   = rStart / c;   // Ende des Dreier-Fensters

        SourceTrajectory traj2;
        fillUniform (traj2, M0, v, 0.0, tEnd + 0.4);

        RetardedTimeSolver s2;
        s2.setMinScanStep (gridDt);

        Root roots[8];
        int  nBefore = 0, nAfter = 0;

        for (double t_h = tPass; t_h <= tEnd + 0.3; t_h += solverDt)
        {
            const int n = s2.solve (traj2, medium, listener, t_h, roots, 8);

            if (t_h < tEnd - 0.05) nBefore = n;
            if (t_h > tEnd + 0.05) nAfter  = n;
        }

        std::snprintf (buf, sizeof (buf), "vor %.4f s: %d Wurzeln, danach: %d",
                       tEnd, nBefore, nAfter);
        check (nBefore == 3 && nAfter == 1, "Wurzelpaar verschwindet wieder", buf);
    }
}

// -------------------------------------------------------------- Test 6
// Gedrosseltes Entdecken (solve(..., allowFullScan)): der teure Vollscan läuft
// nicht mehr an jedem Solver-Punkt, sondern höchstens alle discoverySeconds;
// nachgeführt wird weiter an jedem Punkt. Geprüft wird das, was dabei auf dem
// Spiel steht - dass die Kegelankunft trotzdem innerhalb derselben 3-ms-
// Toleranz bemerkt wird - und das, was es bringt: deutlich weniger
// Residuums-Auswertungen. Dieselbe Geometrie wie Test 5, damit beide Zahlen
// unmittelbar vergleichbar sind.
void testThrottledDiscovery()
{
    std::printf ("\nGedrosseltes Entdecken (Vollscan seltener als Nachfuehren)\n");

    const MediumState medium;
    const double c = medium.speedOfSound();

    const double speed = 2.0 * c;
    const double d     = 100.0;
    const double tPass = 1.0;

    const Vec3 M0 { -speed * tPass, 0.0, 0.0 };
    const Vec3 v  { speed, 0.0, 0.0 };
    const Vec3 listener { 0.0, d, 0.0 };

    const double tBoom    = tPass + d * std::sqrt (3.0) / speed;
    const double solverDt = 8.0 / 48000.0;

    // Der Wert, mit dem PropagationPath arbeitet.
    const double discoverySeconds = 0.5e-3;

    // throttled == false ist der alte Zustand (Vollscan an jedem Punkt) und
    // dient hier als Vergleichsmaßstab für beide Zahlen.
    auto run = [&] (bool throttled, double& outDetect, std::uint64_t& outEvals)
    {
        SourceTrajectory traj;
        fillUniform (traj, M0, v, 0.0, 1.6);

        RetardedTimeSolver solver;
        solver.setMinScanStep (gridDt);
        solver.clearResidualEvaluations();

        double lastDiscovery = tPass;
        outDetect = -1.0;

        for (double t_h = tPass; t_h <= 1.6; t_h += solverDt)
        {
            bool discover = true;

            if (throttled)
            {
                discover = (t_h - lastDiscovery) >= discoverySeconds;

                if (discover)
                    lastDiscovery = t_h;
            }

            Root roots[8];
            const int n = solver.solve (traj, medium, listener, t_h, roots, 8, discover);

            if (outDetect < 0.0 && n >= 3)
                outDetect = t_h;
        }

        outEvals = solver.residualEvaluations();
    };

    double        tFull = 0.0, tThrottled = 0.0;
    std::uint64_t evalsFull = 0, evalsThrottled = 0;

    run (false, tFull,      evalsFull);
    run (true,  tThrottled, evalsThrottled);

    char buf[256];

    if (tThrottled < 0.0)
    {
        fail ("Kegelankunft wird auch gedrosselt bemerkt", "kein Schritt mit >= 3 Wurzeln");
        return;
    }

    const double err = tThrottled - tBoom;

    std::snprintf (buf, sizeof (buf),
                   "t_erkannt = %.6f s (ungedrosselt %.6f s), t_boom = %.6f s, dt = %+.3e s",
                   tThrottled, tFull, tBoom, err);
    check (err >= -1.0e-4 && err < 3.0e-3, "Kegelankunft bleibt in der 3-ms-Toleranz", buf);

    // Die Drosselung darf nur SPÄTER erkennen, nie früher - alles andere wäre
    // Zufall und kein Verständnis der Mechanik.
    std::snprintf (buf, sizeof (buf), "gedrosselt %+.3e s gegenueber ungedrosselt",
                   tThrottled - tFull);
    check (tThrottled >= tFull - 1.0e-9, "Drosselung erkennt nicht frueher", buf);

    // Verspätung höchstens um ein Entdeckungsintervall (plus einen
    // Solver-Punkt Raster).
    std::snprintf (buf, sizeof (buf), "Verspaetung %.3e s, erlaubt %.3e s",
                   tThrottled - tFull, discoverySeconds + solverDt);
    check (tThrottled - tFull <= discoverySeconds + solverDt + 1.0e-9,
           "Verspaetung bleibt im Entdeckungsintervall", buf);

    const double ratio = evalsFull > 0 ? (double) evalsThrottled / (double) evalsFull : 1.0;

    std::snprintf (buf, sizeof (buf), "%llu statt %llu Auswertungen (%.0f %%)",
                   (unsigned long long) evalsThrottled, (unsigned long long) evalsFull,
                   100.0 * ratio);
    check (ratio < 0.75, "Drosselung spart messbar Loeserarbeit", buf);
}

// -------------------------------------------------------------- Test 7
// Spiegelebenen (PathTransform). Reine Geometrie, aber genau die Geometrie,
// auf der Boden- und Wandreflexion beruhen - ein Vorzeichenfehler hier wäre im
// Klang nur als "irgendwie falscher Hall" hörbar und sonst nirgends messbar.
void testMirrorPlanes()
{
    std::printf ("\nSpiegelebenen (Boden, Wand, beliebige Lage)\n");

    char buf[256];

    // 1. Der Boden muss weiterhin genau z umklappen.
    {
        const PathTransform ground = groundMirrorTransform();
        const Vec3 p { 3.0, -7.0, 1.75 };
        const Vec3 q = applyPathTransform (ground, p);
        const Vec3 v = applyPathTransformVelocity (ground, Vec3 { 1.0, 2.0, 3.0 });

        const double err = std::abs (q.x - 3.0) + std::abs (q.y + 7.0) + std::abs (q.z + 1.75)
                         + std::abs (v.x - 1.0) + std::abs (v.y - 2.0) + std::abs (v.z + 3.0);

        std::snprintf (buf, sizeof (buf), "Fehler %.3e", err);
        check (err < 1.0e-12, "Boden klappt z um, x/y bleiben", buf);
    }

    // 2. Eine senkrechte Wand, deren Linie entlang y läuft (Azimut 90°), steht
    //    bei x = w und muss x -> 2w - x abbilden.
    {
        const double w = 12.5;
        const PathTransform wall = wallMirrorTransform (Vec3 { w, 40.0, 0.0 },
                                                        3.14159265358979323846 * 0.5, 0.0);
        const Vec3 q = applyPathTransform (wall, Vec3 { 2.0, -3.0, 1.75 });

        const double err = std::abs (q.x - (2.0 * w - 2.0)) + std::abs (q.y + 3.0)
                         + std::abs (q.z - 1.75);

        std::snprintf (buf, sizeof (buf), "(%.3f, %.3f, %.3f), Fehler %.3e", q.x, q.y, q.z, err);
        check (err < 1.0e-12, "Wand bei x = w bildet x auf 2w - x ab", buf);
    }

    // 3. Grenzfall: eine um 90° gekippte Wand mit Fußpunkt auf z = 0 IST die
    //    Bodenebene. Trifft die allgemeine Formel diesen Fall nicht, stimmt die
    //    Herleitung der Normalen nicht.
    {
        const PathTransform tilted = wallMirrorTransform (Vec3 { 5.0, -2.0, 0.0 }, 0.7,
                                                          3.14159265358979323846 * 0.5);
        const Vec3 p { 4.0, 9.0, 2.5 };
        const Vec3 a = applyPathTransform (tilted, p);
        const Vec3 b = applyPathTransform (groundMirrorTransform(), p);

        const double err = std::abs (a.x - b.x) + std::abs (a.y - b.y) + std::abs (a.z - b.z);

        std::snprintf (buf, sizeof (buf), "Fehler %.3e", err);
        check (err < 1.0e-12, "um 90 Grad gekippte Wand == Bodenebene", buf);
    }

    // 4. Die tragende Eigenschaft der Methode: die Spiegelung ist eine
    //    Isometrie, deshalb ist es gleichgültig, ob Empfänger oder Quelle
    //    gespiegelt wird. Genau darauf beruht, dass alle Pfade dieselbe
    //    Trajektorie lesen dürfen (siehe PathTransform.h).
    {
        const PathTransform t = wallMirrorTransform (Vec3 { -3.0, 11.0, 0.0 }, 1.1, 0.35);

        double worst = 0.0;

        for (int i = 0; i < 40; ++i)
        {
            const double u = (double) i;

            const Vec3 L { 20.0 + u, -5.0 + 0.3 * u, 1.75 };
            const Vec3 M { -8.0 + 0.7 * u, 14.0 - 0.2 * u, 3.0 + 0.1 * u };

            const double lhs = (applyPathTransform (t, L) - M).length();
            const double rhs = (L - applyPathTransform (t, M)).length();

            worst = std::max (worst, std::abs (lhs - rhs));
        }

        std::snprintf (buf, sizeof (buf), "max |dR| = %.3e m", worst);
        check (worst < 1.0e-9, "Empfaenger spiegeln == Quelle spiegeln", buf);
    }

    // 5. Zweimal gespiegelt ist wieder der Ausgangspunkt - sonst wäre die
    //    Normale nicht normiert oder der Ebenenabstand falsch gesetzt.
    {
        const PathTransform t = wallMirrorTransform (Vec3 { 7.0, 3.0, 0.0 }, -0.4, 0.9);
        const Vec3 p { 1.0, 2.0, 3.0 };
        const Vec3 q = applyPathTransform (t, applyPathTransform (t, p));

        const double err = (q - p).length();

        std::snprintf (buf, sizeof (buf), "|dp| = %.3e m", err);
        check (err < 1.0e-12, "zweimal gespiegelt ist die Identitaet", buf);
    }

    // 6b. Verkettung zweier Spiegelungen (Mehrfachreflexion). Das Ergebnis ist
    //     KEINE Spiegelung mehr, sondern eine Drehung bzw. Verschiebung - aber
    //     immer noch eine Isometrie, und genau darauf beruht, dass ein Pfad
    //     zweiter Ordnung dieselbe Rechnung benutzen darf wie jeder andere.
    {
        const double quarterTurn = 3.14159265358979323846 * 0.5;

        const PathTransform a = wallMirrorTransform (Vec3 { -20.0, 0.0, 0.0 }, quarterTurn, 0.0);
        const PathTransform b = wallMirrorTransform (Vec3 {  20.0, 0.0, 0.0 }, quarterTurn, 0.0);
        const PathTransform ab = composeTransforms (a, b);

        // Reihenfolge: erst b, dann a.
        double worstOrder = 0.0;
        double worstIso   = 0.0;

        for (int i = 0; i < 40; ++i)
        {
            const double u = (double) i;

            const Vec3 L { 3.0 + 0.5 * u, -5.0 + 0.3 * u, 1.75 };
            const Vec3 M { -8.0 + 0.7 * u, 14.0 - 0.2 * u, 3.0 + 0.1 * u };

            const Vec3 viaCompose = applyPathTransform (ab, L);
            const Vec3 viaSteps   = applyPathTransform (a, applyPathTransform (b, L));

            worstOrder = std::max (worstOrder, (viaCompose - viaSteps).length());

            // Isometrie: der Abstand zur Quelle bleibt beim Spiegeln des
            // Empfängers derselbe wie beim (umgekehrten) Spiegeln der Quelle.
            const double lhs = (applyPathTransform (ab, L) - M).length();
            const double rhs = (L - applyPathTransform (composeTransforms (b, a), M)).length();

            worstIso = std::max (worstIso, std::abs (lhs - rhs));
        }

        std::snprintf (buf, sizeof (buf), "max |dp| = %.3e m", worstOrder);
        check (worstOrder < 1.0e-12, "Verkettung == zwei Einzelschritte", buf);

        std::snprintf (buf, sizeof (buf), "max |dR| = %.3e m", worstIso);
        check (worstIso < 1.0e-9, "verkettete Abbildung bleibt Isometrie", buf);

        // Zwei parallele Wände bei x = -20 und x = +20: erst an der rechten,
        // dann an der linken gespiegelt ergibt x -> (-40) - (40 - x) = x - 80.
        // Eine reine Verschiebung um den doppelten Wandabstand also - das ist
        // der Flatterecho-Fall und zugleich der Nachweis, dass die Verkettung
        // keine Spiegelung mehr ist (eine Spiegelung hat immer Fixpunkte,
        // diese Abbildung hat keinen).
        const Vec3 p { 5.0, 1.0, 2.0 };
        const Vec3 q = applyPathTransform (ab, p);

        std::snprintf (buf, sizeof (buf), "dx = %.6f m (erwartet -80), dy = %.3e, dz = %.3e",
                       q.x - p.x, q.y - p.y, q.z - p.z);
        check (std::abs ((q.x - p.x) + 80.0) < 1.0e-9
               && std::abs (q.y - p.y) < 1.0e-9 && std::abs (q.z - p.z) < 1.0e-12,
               "zwei parallele Waende ergeben eine Verschiebung", buf);
    }

    // 6. Ohne Normale (Direktschall) darf nichts passieren.
    {
        const PathTransform none;
        const Vec3 p { 1.0, -2.0, 3.0 };
        const Vec3 q = applyPathTransform (none, p);
        const Vec3 v = applyPathTransformVelocity (none, p);

        const double err = (q - p).length() + (v - p).length();

        std::snprintf (buf, sizeof (buf), "Fehler %.3e", err);
        check (err < 1.0e-15, "Direktschall bleibt die Identitaet", buf);
    }
}

} // namespace

// ------------------------------------------------- geschlossene Loesung
//
// Fuer die geradlinig gleichfoermige Quelle mit ruhendem Empfaenger gibt es die
// Retarded Time in geschlossener Form (StraightLineRetardedTime.h). Sie soll
// spaeter den Startwert fuer den numerischen Loeser liefern, damit an der
// Schallwand nicht mehr das ganze Fenster abgetastet werden muss.
//
// Geprueft wird gegen den numerischen Loeser selbst - der ist hier die
// Referenz, weil er auf derselben Trajektorie arbeitet, die spaeter auch
// tatsaechlich geschrieben wird.
void testClosedFormStraightLine()
{
    std::printf ("\nGeschlossene Loesung, gerade Bahn\n");

    const MediumState medium;
    const double      c = medium.speedOfSound();

    // Erst die Selbstpruefung: erfuellt jede gelieferte Wurzel die Gleichung?
    {
        double worstResidual = 0.0;
        int    subsonicOne   = 0;
        int    supersonicTwo = 0;
        int    samples       = 0;

        for (double mach : { 0.3, 0.8, 0.95, 1.05, 1.4, 2.0, 3.0 })
        {
            const Vec3 vel { mach * c, 0.0, 0.0 };
            const Vec3 origin { -4000.0, 0.0, 0.0 };
            const Vec3 receiver { 0.0, 150.0, 0.0 };

            for (double t_h = 0.0; t_h <= 12.0; t_h += 0.01)
            {
                const auto r = straightline::solve (origin, vel, receiver, c, t_h);

                for (int i = 0; i < r.count; ++i)
                {
                    const Vec3   m   = origin + vel * r.t_e[i];
                    const double res = c * (t_h - r.t_e[i]) - (receiver - m).length();

                    worstResidual = std::max (worstResidual, std::abs (res));
                }

                if (r.count > 0)
                {
                    ++samples;

                    if (mach < 1.0 && r.count == 1)
                        ++subsonicOne;

                    if (mach > 1.0 && r.count == 2)
                        ++supersonicTwo;
                }
            }
        }

        char buf[160];
        std::snprintf (buf, sizeof (buf), "groesstes Residuum %.3e m ueber %d Proben",
                       worstResidual, samples);
        check (worstResidual < 1.0e-6, "jede Wurzel erfuellt die Gleichung", buf);

        std::snprintf (buf, sizeof (buf), "Unterschall einfach %d, Ueberschall doppelt %d",
                       subsonicOne, supersonicTwo);
        check (subsonicOne > 0 && supersonicTwo > 0,
               "Unterschall eine Wurzel, im Kegel zwei", buf);
    }

    // Und der Abgleich mit dem numerischen Loeser auf derselben Geometrie.
    {
        constexpr double mach = 1.6;

        const Vec3 vel { mach * c, 0.0, 0.0 };
        const Vec3 origin { -4000.0, 0.0, 0.0 };
        const Vec3 receiver { 0.0, 150.0, 0.0 };

        SourceTrajectory traj;
        traj.prepare (trajRateHz, 30.0);
        traj.reset (origin, 0.0);
        traj.fillLinear (origin, vel, 0.0, 25.0);

        RetardedTimeSolver solver;
        solver.setMinScanStep (1.0 / trajRateHz);

        double worstDelta      = 0.0;
        int    compared        = 0;
        int    missedByNumeric = 0;
        int    steps           = 0;

        // Die Trajektorie LUECKENLOS fuellen, sonst vergleicht man den Loeser
        // gegen eine Bahn, die es so gar nicht gibt: fillLinear belegt nur die
        // Vorgeschichte bis t = 0, und ein Sprung direkt nach t = 8 s liesse
        // dazwischen ein Loch, in dem die Quelle stillsteht.
        const int totalTicks = (int) std::lround (11.0 * trajRateHz);

        for (int k = 1; k <= totalTicks; ++k)
        {
            const double t_h = (double) k / trajRateHz;

            traj.push (origin + vel * t_h, t_h);

            if (t_h < 8.0)
                continue;

            Root      roots[RetardedTimeSolver::maxBranches];
            const int n = solver.solve (traj, medium, receiver, t_h,
                                        roots, RetardedTimeSolver::maxBranches, true);

            const auto closed = straightline::solve (origin, vel, receiver, c, t_h);

            if (n == 0 || closed.count == 0)
                continue;

            ++steps;

            // Richtung des Vergleichs: zu jeder NUMERISCHEN Wurzel die
            // naechstgelegene geschlossene suchen, nicht umgekehrt.
            //
            // Andersherum misst man nicht die geschlossene Loesung, sondern die
            // bekannte Luecke des numerischen Loesers: der meldet im Kegel oft
            // nur eine der beiden Wurzeln (siehe Zweig-Zaehlwerk im
            // load_check), und die fehlende zweite erzeugt dann eine
            // Abweichung von fast einer Sekunde, ohne dass die geschlossene
            // Formel etwas dafuer koennte.
            for (int j = 0; j < n; ++j)
            {
                double best = 1.0e30;

                for (int i = 0; i < closed.count; ++i)
                    best = std::min (best, std::abs (roots[j].t_e - closed.t_e[i]));

                worstDelta = std::max (worstDelta, best);
                ++compared;
            }

            if (n < closed.count)
                ++missedByNumeric;
        }


        char buf[160];
        std::snprintf (buf, sizeof (buf), "groesste Abweichung %.3e s ueber %d Vergleiche",
                       worstDelta, compared);
        check (compared > 100 && worstDelta < 1.0e-4,
               "jede numerische Wurzel steht auch in der Formel", buf);

        // Der Gegenbefund, bewusst nur berichtet und nicht als Kriterium
        // gewertet: die geschlossene Formel sieht im Kegel zwei Wurzeln, der
        // numerische Loeser meldet oft nur eine. Das ist dieselbe Luecke, die
        // im load_check als Zweigtod bei vollem Pegel auftaucht - hier zum
        // ersten Mal gegen eine EXAKTE Referenz gemessen statt gegen sich
        // selbst.
        std::snprintf (buf, sizeof (buf),
                       "%d von %d Zeitpunkten, an denen die Formel mehr Wurzeln sieht",
                       missedByNumeric, steps);
        ok ("Referenzmessung: fehlende Wurzeln", buf);
    }
}

int main()
{
    std::printf ("solver_check - Abnahme H4 (Plan 2.3 bis 2.12)\n");

    const MediumState medium;
    std::printf ("c = %.4f m/s bei %.1f C\n", medium.speedOfSound(), medium.tempCelsius);

    testRestingSource();
    testUniformSubsonic();
    testUniformSupersonic();
    testCircularNoDropouts();
    testMachConeArrival();
    testThrottledDiscovery();
    testMirrorPlanes();
    testClosedFormStraightLine();

    std::printf ("\n");

    if (failures == 0)
    {
        std::printf ("alle Kriterien gruen\n");
        return 0;
    }

    std::printf ("%d Kriterium(en) NICHT erfuellt\n", failures);
    return 1;
}
