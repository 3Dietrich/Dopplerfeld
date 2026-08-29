// Messprogramm zur Frage, WOMIT ein Abgriffpunkt gespeist wird (@dpa
// 20260829: die Waende "muessen die Reverbs korrekt beliefern").
//
// Der Direktschall wird dabei stummgeschaltet (-60 dB), es bleibt also nur,
// was ueber die Abgriffpunkte und ihren Hall herauskommt. Gemessen wird
// derselbe Vorbeiflug zweimal - einmal ohne Boden und Waende, einmal mit -
// und beide Ausgaenge werden verglichen. Kommt dasselbe heraus, erreicht die
// Wand den Punkt nicht.
//
// Dazu die Rechenzeit mit acht laufenden Punkten, weil jeder Reflexionsweg zu
// einem Punkt ein eigener Loeser ist.
//
//   cmake --build build --target tapfeed_probe && build/tapfeed_probe

#include "Params.h"
#include "PluginProcessor.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

struct Run
{
    std::vector<float> out;
    double blockAverageUs = 0.0;
    double blockMaxUs     = 0.0;
};

Run render (bool surfacesOn, int tapsOn, double seconds)
{
    DopplerfeldProcessor proc;

    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    auto set = [&proc] (const juce::String& id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    set (Params::fieldMetres, 2000.0f);
    set (Params::flyKind,     1.0f);
    set (Params::flySpeed,    240.0f);
    set (Params::flyDistance, 200.0f);
    set (Params::flyApproach, 600.0f);

    // Nur der Hall ist zu hoeren: der Direktschall gilt bei -60 dB als stumm.
    set (Params::directGain,   -60.0f);
    set (Params::reverbBypass,   0.0f);

    set (Params::groundReflectionOn, surfacesOn ? 1.0f : 0.0f);
    set (Params::wall1On,            surfacesOn ? 1.0f : 0.0f);
    set (Params::wall2On,            surfacesOn ? 1.0f : 0.0f);

    for (int t = 0; t < tapsOn; ++t)
    {
        using namespace Params::TapPart;

        set (Params::tapId (t, on),    1.0f);
        set (Params::tapId (t, gain), -6.0f);
        set (Params::tapId (t, x),     0.15f + 0.1f * (float) t);
        set (Params::tapId (t, y),     0.2f  + 0.08f * (float) t);
        set (Params::tapId (t, room),  40.0f);
        set (Params::tapId (t, decay),  2.5f);
    }

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }

    proc.triggerFlyBy();

    Run run;

    const int blocks = (int) (seconds * sampleRate / blockSize);

    run.out.reserve ((size_t) (blocks * blockSize));

    double totalUs = 0.0;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();

        const auto t0 = std::chrono::high_resolution_clock::now();
        proc.processBlock (buffer, midi);
        const auto t1 = std::chrono::high_resolution_clock::now();

        const double us = std::chrono::duration<double, std::micro> (t1 - t0).count();

        totalUs += us;
        run.blockMaxUs = std::max (run.blockMaxUs, us);

        for (int i = 0; i < blockSize; ++i)
            run.out.push_back (0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)));
    }

    run.blockAverageUs = totalUs / std::max (1, blocks);

    return run;
}

double rmsDb (const std::vector<float>& x)
{
    double sum = 0.0;

    for (float v : x)
        sum += (double) v * (double) v;

    return 20.0 * std::log10 (std::max (1.0e-12, std::sqrt (sum / (double) std::max<size_t> (1, x.size()))));
}

double correlation (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size());

    double sab = 0.0, saa = 0.0, sbb = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        sab += (double) a[i] * (double) b[i];
        saa += (double) a[i] * (double) a[i];
        sbb += (double) b[i] * (double) b[i];
    }

    if (saa <= 0.0 || sbb <= 0.0)
        return 1.0;

    return sab / std::sqrt (saa * sbb);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("tapfeed_probe - was einen Abgriffpunkt speist\n\n");

    const double seconds = 6.0;

    // Drei Laeufe, damit wirklich der Punkt gemessen wird und nicht die
    // Waende am Ohr: der Lauf ohne Punkt ist der Ohr-Anteil, und die Differenz
    // dazu ist genau das, was der Punkt beitraegt. Alles addiert sich linear,
    // die Subtraktion ist also zulaessig.
    const Run bare    = render (false, 1, seconds);
    const Run full    = render (true,  1, seconds);
    const Run earOnly = render (true,  0, seconds);

    std::vector<float> tapOnly (full.out.size(), 0.0f);

    for (size_t i = 0; i < tapOnly.size() && i < earOnly.out.size(); ++i)
        tapOnly[i] = full.out[i] - earOnly.out[i];

    std::printf ("Was der Abgriffpunkt beitraegt (Direktschall stumm):\n");
    std::printf ("  ohne Boden/Waende   %7.1f dB\n", rmsDb (bare.out));
    std::printf ("  mit  Boden/Waenden  %7.1f dB\n", rmsDb (tapOnly));
    std::printf ("  Korrelation der beiden: %.3f\n", correlation (bare.out, tapOnly));
    std::printf ("  (1,000 hiesse: die Flaechen erreichen den Punkt ueberhaupt nicht)\n\n");

    const Run load1 = render (true, 1, seconds);
    const Run load8 = render (true, 8, seconds);

    std::printf ("Rechenzeit (Budget %.0f us je Block):\n", blockSize / sampleRate * 1.0e6);
    std::printf ("  1 Punkt,  Boden + 2 Waende   Block Oe %6.1f us  max %7.1f us\n",
                 load1.blockAverageUs, load1.blockMaxUs);
    std::printf ("  8 Punkte, Boden + 2 Waende   Block Oe %6.1f us  max %7.1f us\n",
                 load8.blockAverageUs, load8.blockMaxUs);

    return 0;
}
