// Messprogramm zur Frage, ob der Regler "Rueckwaerts" den zeitverkehrt
// gehoerten Zweig auch DORT absenkt, wo er laut ist.
//
// Nicht Teil der CMake-Tests - eigenstaendig zu bauen, weil es nur die
// Physik-Schicht braucht (keine JUCE-Abhaengigkeit):
//
//   clang++ -std=c++20 -O2 -I Source Tests/reverse_level_probe.cpp \
//     Source/Physics/RetardedTimeSolver.cpp Source/Physics/SourceTrajectory.cpp \
//     Source/Physics/PropagationPath.cpp Source/Physics/SourceSignalBuffer.cpp \
//     -o /tmp/reverse_level_probe && /tmp/reverse_level_probe
//
// Hintergrund: @dpa hoerte am 20260827 trotz -36 dB auf dem Regler einen
// lauten rueckwaerts laufenden Anteil ("und Rueckwaerts ist schon um -36dB
// abgesenkt! das ist gar nicht *das* Rueckwaerts..?").
//
// Gemessen wird derselbe Vorbeiflug zweimal - einmal mit 0 dB, einmal mit
// -36 dB - und verglichen wird der Spitzenpegel je 5-ms-Fenster.
//
// STAND DER MESSUNG (Mach 3,23, seitlich 300 m, danach ausrollend): der Regler
// wirkt, aber er kann den Ausgang nicht auf -36 dB bringen, weil nach der
// Kegelankunft ZWEI Zweige gleichzeitig tragen und nur einer von ihnen
// rueckwaerts laeuft. Das Verhaeltnis liegt zwischen 0,51 und 0,82, stellenweise
// ueber 1 (die beiden Zweige interferieren, das Absenken des einen kann die
// Summe kurzzeitig lauter machen). Ab der Zeit, zu der nur noch der vorwaerts
// laufende Zweig uebrig ist, steht es exakt auf 1,000.
//
// Damit ist die Frage, was @dpa als lautes Rueckwaerts hoert, NICHT geklaert -
// dieses Szenario reicht nicht heran. Der naechste Schritt waere, den
// dTau-Verlauf beider Zweige mitzuschreiben statt nur den Summenpegel.

#include "Physics/Medium.h"
#include "Physics/PropagationPath.h"
#include "Physics/SourceSignalBuffer.h"
#include "Physics/SourceTrajectory.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate   = 48000.0;
    constexpr int    blockSize    = 128;
    constexpr double trajRateHz   = 1000.0;
    constexpr double bufferSecs   = 30.0;

    // Geometrie aus @dpas Aufnahme: Mach 3,2 geradeaus, seitlicher Abstand
    // 300 m, Hoerer im Ursprung.
    constexpr double vFly     = 1107.0;   // m/s
    constexpr double sideDist = 300.0;    // m
    constexpr double xStart   = -2000.0;  // m, Anflugstrecke
    // Lang genug, dass der Schall des Ausrollens auch ankommt: die Quelle
    // steht bei der Bremsung ueber 1,3 km weit weg, das sind allein knapp
    // 4 s Laufzeit.
    constexpr double duration = 9.0;      // s

    // Der Flug ENDET, wie @dpas Vorbeiflug endet: nach der eingestellten
    // Strecke rollt die Quelle aus. Ohne dieses Ende bliebe M_r dauerhaft
    // ueber 1, die beiden Wurzeln blieben bis zum Schluss bestehen und der
    // Fall, um den es geht - das Zusammenlaufen an der Kaustik - traete gar
    // nicht ein.
    constexpr double tBrake    = 3.0;   // s, ab hier wird gebremst
    constexpr double brakeSecs = 0.4;   // s bis zum Stillstand

    Vec3 posAt (double t)
    {
        double x = xStart + vFly * t;

        if (t > tBrake)
        {
            const double dt = std::min (t - tBrake, brakeSecs);

            // Gleichmaessige Verzoegerung: zurueckgelegte Strecke ist die des
            // gleichfoermigen Flugs minus dem, was die Bremsung wegnimmt.
            x = xStart + vFly * tBrake + vFly * dt * (1.0 - 0.5 * dt / brakeSecs);

            if (t - tBrake > brakeSecs)
                x = xStart + vFly * tBrake + 0.5 * vFly * brakeSecs;
        }

        return Vec3 { x, sideDist, 0.0 };
    }

    // Spitzenpegel je Fenster, damit der Vergleich nicht an einer einzelnen
    // Nullstelle des Traegers haengt.
    constexpr double windowSecs = 0.005;

    std::vector<double> run (double reverseGain)
    {
        const MediumState medium;

        SourceTrajectory traj;
        traj.prepare (trajRateHz, bufferSecs);

        // Vorgeschichte gleichfoermig fliegend: eine kuenstliche Grenze
        // zwischen Ruhe und Bewegung wuerde selbst Zweige erzeugen und die
        // Messung verunreinigen.
        traj.fillLinear (posAt (0.0), Vec3 { vFly, 0.0, 0.0 }, 0.0, bufferSecs - 1.0);

        SourceSignalBuffer sig;
        sig.prepare (sampleRate, bufferSecs);

        PropagationPath path;
        path.prepare (sampleRate, blockSize);
        path.setReverseGain (reverseGain);

        // Ohne N-Welle: gemessen werden soll der Zweiginhalt, und die
        // Druckwelle laeuft ausdruecklich an ihm vorbei (siehe setNWave).
        path.setNWave (false, 15.0, 1.0, 0.5);

        const int    totalSamples  = (int) (duration * sampleRate);
        const int    windowSamples = (int) (windowSecs * sampleRate);
        const double phaseInc      = 2.0 * 3.141592653589793 * 300.0 / sampleRate;

        std::vector<float>  mono ((size_t) blockSize);
        std::vector<float>  out  ((size_t) blockSize);
        std::vector<double> peaks;

        double phase = 0.0;
        double peak  = 0.0;
        int    inWin = 0;

        for (int n = 0; n < totalSamples; n += blockSize)
        {
            const double tBlock = (double) n / sampleRate;

            // Trajektorie bis zum Blockende nachziehen.
            for (double t = traj.newestTime() + 1.0 / trajRateHz;
                 t <= tBlock + (double) blockSize / sampleRate;
                 t += 1.0 / trajRateHz)
                traj.push (posAt (t), t);

            for (int i = 0; i < blockSize; ++i)
            {
                mono[(size_t) i] = (float) std::sin (phase);
                phase += phaseInc;
            }

            sig.write (mono.data(), blockSize);

            std::fill (out.begin(), out.end(), 0.0f);
            path.process (traj, sig, medium, Vec3 { 0.0, 0.0, 0.0 }, Vec3 {},
                          tBlock, out.data(), blockSize);

            for (int i = 0; i < blockSize; ++i)
            {
                peak = std::max (peak, (double) std::abs (out[(size_t) i]));

                if (++inWin >= windowSamples)
                {
                    peaks.push_back (peak);
                    peak  = 0.0;
                    inWin = 0;
                }
            }
        }

        return peaks;
    }
}

int main()
{
    const double c = MediumState{}.speedOfSound();

    std::printf ("Vorbeiflug Mach %.2f, seitlich %.0f m, Traeger 300 Hz\n\n",
                 vFly / c, sideDist);

    const auto full = run (1.0);
    const auto damp = run (std::pow (10.0, -36.0 / 20.0));

    // Bezug ist der lauteste Augenblick des Vorbeiflugs, nicht ein absoluter
    // Pegel: die Amplitude faellt mit 1/R, und bei 2 km Anflug stuende ein
    // fester Schwellwert entweder ueber dem ganzen Signal oder unter dem
    // Rauschen.
    double loudest = 0.0;

    for (double v : full)
        loudest = std::max (loudest, v);

    const double floorLevel = 0.002 * loudest;

    std::printf ("  lautester Augenblick %.5f, betrachtet werden Fenster ab %.5f\n\n",
                 loudest, floorLevel);
    std::printf ("  t [s]   0 dB      -36 dB    Verhaeltnis\n");

    double worstRatio = 0.0;
    double worstTime  = 0.0;
    double worstLevel = 0.0;

    for (size_t i = 0; i < full.size() && i < damp.size(); ++i)
    {
        const double t = (double) i * windowSecs;

        // Nur Fenster, in denen ueberhaupt nennenswert Pegel steht - sonst
        // vergliche man Rauschen mit Rauschen.
        if (full[i] < floorLevel)
            continue;

        const double ratio = damp[i] / full[i];

        if (ratio > worstRatio)
        {
            worstRatio = ratio;
            worstTime  = t;
            worstLevel = full[i];
        }

        if (i % 40 == 0)
            std::printf ("  %6.3f  %8.5f  %8.5f   %6.3f\n", t, full[i], damp[i], ratio);
    }

    std::printf ("\n  Schlechtestes Verhaeltnis: %.3f (%.1f dB) bei t = %.3f s, "
                 "Pegel ungedaempft %.4f\n",
                 worstRatio, 20.0 * std::log10 (std::max (worstRatio, 1.0e-9)),
                 worstTime, worstLevel);
    std::printf ("  Erwartet: nahe 0,0158 (-36 dB) ueberall dort, wo der "
                 "rueckwaerts gehoerte Zweig traegt.\n");

    return 0;
}
