// Messprogramm zur Treppigkeit der Drehzahl (@dpa 20260830: "RPM drehen
// klingt immer treppig").
//
// Faehrt den Motorgenerator mit EINEM Sinus-Teilton und zieht die Drehzahl
// wie ein Regler unter der Maus: neue Werte nur alle paar Millisekunden, dazwischen
// steht der Wert still. Gemessen wird die Grundfrequenz Periode fuer Periode
// aus den Nulldurchgaengen; die Kennzahl ist der groesste Sprung zwischen zwei
// benachbarten Perioden in Cent.
//
// Zum Vergleich steht daneben, wie gross die Stufe ohne Gleitweg waere: das
// ist genau der Abstand zweier Rasterwerte des Reglers.
//
//   cmake --build build --target rpm_glide_probe && build/rpm_glide_probe [Sekunden] [Rastermillisekunden]

#include "Sources/EngineGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;   // 10,7 ms - die uebliche Blockgroesse

// Hoch genug, dass in der halben Sekunde genug Perioden liegen, um die
// Frequenz Periode fuer Periode ablesen zu koennen: 6000 RPM sind 100 Hz.
constexpr double rpmFrom = 6000.0;
constexpr double rpmTo   = 24000.0;

double centsBetween (double a, double b)
{
    return 1200.0 * std::log2 (b / a);
}
}

int main (int argc, char** argv)
{
    const double seconds  = argc > 1 ? std::atof (argv[1]) : 0.5;
    const double rasterMs = argc > 2 ? std::atof (argv[2]) : 16.0;   // Mausraster, rund 60 Hz

    EngineGenerator gen;
    gen.prepare (sampleRate, blockSize);

    // Nur ein Teilton, als Sinus, ohne alles andere: die Nulldurchgaenge
    // sollen die Grundfrequenz zeigen und sonst nichts.
    gen.setEngineKind (0);              // Frei
    gen.setHarmonicsOn (true);
    gen.setHarmonic (0, 1.0f, 0.0f, 1.0f, 0.0f);

    for (int i = 1; i < 4; ++i)
        gen.setHarmonic (i, 1.0f, 0.0f, 1.0f, -120.0f);

    for (int i = 0; i < 4; ++i)
        gen.setSineMode (i, true);

    gen.setJitter (0.0f, 8.0f);         // ohne Wackler, sonst misst man den
    gen.setImbalance (0.0f);
    gen.setNoiseParams (400.0f, 3000.0f, -120.0f, -120.0f, 1.2f);
    gen.setRpm ((float) rpmFrom);
    gen.reset();

    const int totalSamples = (int) (seconds * sampleRate);
    const int rasterSamples = std::max (1, (int) (rasterMs * 0.001 * sampleRate));

    std::vector<float> buffer ((size_t) blockSize, 0.0f);
    std::vector<float> signal;
    signal.reserve ((size_t) totalSamples);

    // Rasterwerte des Reglers mitschreiben, um daraus die Stufe OHNE Gleitweg
    // zu bekommen.
    double lastCommanded = rpmFrom;
    double rawStepCents  = 0.0;

    for (int pos = 0; pos < totalSamples; pos += blockSize)
    {
        // Der Regler steht auf dem Wert des zuletzt vergangenen Rasterpunkts.
        const int    rasterIndex = pos / rasterSamples;
        const double t = std::min (1.0, (double) (rasterIndex * rasterSamples) / (double) totalSamples);
        const double commanded = rpmFrom * std::pow (rpmTo / rpmFrom, t);

        if (std::abs (commanded - lastCommanded) > 1.0e-9)
        {
            rawStepCents = std::max (rawStepCents, std::abs (centsBetween (lastCommanded, commanded)));
            lastCommanded = commanded;
        }

        gen.setRpm ((float) commanded);

        const int n = std::min (blockSize, totalSamples - pos);
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        gen.renderMono (buffer.data(), n);
        signal.insert (signal.end(), buffer.begin(), buffer.begin() + n);
    }

    // Nulldurchgaenge aufwaerts, linear interpoliert: daraus je eine Periode.
    std::vector<double> crossings;

    for (size_t i = 1; i < signal.size(); ++i)
    {
        if (signal[i - 1] <= 0.0f && signal[i] > 0.0f)
        {
            const double d = (double) signal[i] - (double) signal[i - 1];
            const double frac = d > 0.0 ? (0.0 - (double) signal[i - 1]) / d : 0.0;
            crossings.push_back ((double) (i - 1) + frac);
        }
    }

    if (crossings.size() < 4)
    {
        std::printf ("zu wenige Nulldurchgaenge (%zu) - nichts zu messen\n", crossings.size());
        return 1;
    }

    std::vector<double> freqs;

    for (size_t i = 1; i < crossings.size(); ++i)
        freqs.push_back (sampleRate / (crossings[i] - crossings[i - 1]));

    double maxStep = 0.0;
    size_t maxAt = 0;

    for (size_t i = 1; i < freqs.size(); ++i)
    {
        const double step = std::abs (centsBetween (freqs[i - 1], freqs[i]));

        if (step > maxStep)
        {
            maxStep = step;
            maxAt = i;
        }
    }

    double sum = 0.0;

    for (size_t i = 1; i < freqs.size(); ++i)
        sum += std::abs (centsBetween (freqs[i - 1], freqs[i]));

    std::printf ("Zug %.0f -> %.0f RPM in %.2f s, Regler-Raster %.1f ms, Block %d Samples\n",
                 rpmFrom, rpmTo, seconds, rasterMs, blockSize);
    std::printf ("Perioden gemessen: %zu, Frequenz %.2f -> %.2f Hz\n",
                 freqs.size(), freqs.front(), freqs.back());
    std::printf ("Stufe OHNE Gleitweg (Abstand zweier Rasterwerte): %.1f Cent\n", rawStepCents);
    std::printf ("groesster Sprung zwischen zwei Perioden: %.1f Cent (Periode %zu)\n", maxStep, maxAt);
    std::printf ("mittlerer Sprung je Periode: %.2f Cent\n", sum / (double) (freqs.size() - 1));

    return 0;
}
