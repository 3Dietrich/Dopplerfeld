// Messprogramm zum Klon-Schwarm (@dpa 20260825: "nicht Koenig (orig) +
// Diener (Klone) sondern wie die Fliegen: jeder einzeln ueber den
// Jitterbereich" und "bei Jitter=0 sollten sich die Klones auch nicht um das
// Original bewegen").
//
// Zeigt, wo die Klone relativ zur Quelle stehen und wie weit sie sich
// gegeneinander bewegen - einmal mit Wackler, einmal ohne.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target swarm_probe && build/swarm_probe

#include "Params.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

void setParam (DopplerfeldProcessor& proc, const char* id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        std::printf ("  (Parameter %s gibt es nicht)\n", id);
}

void run (const char* label, float jitterAmount, float jitterSpeed, float spread, int clones)
{
    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    setParam (proc, Params::fieldMetres,     200.0f);
    setParam (proc, Params::srcJitterOn,     jitterAmount > 0.0f ? 1.0f : 0.0f);
    setParam (proc, Params::srcJitterAmount, jitterAmount);
    setParam (proc, Params::srcJitterSpeed,  jitterSpeed);
    setParam (proc, Params::cloneTotal,      (float) clones);
    setParam (proc, Params::cloneSpread,     spread);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    // Anfahren: der Ausschlag faehrt mit dem Tempo hoch, nicht sprunghaft.
    for (int i = 0; i < 200; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }

    std::vector<std::vector<Vec3>> rel;   // je Klon: Versatz zur Quelle ueber die Zeit

    for (int i = 0; i < 400; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        FieldSnapshot snap;
        proc.fillFieldSnapshot (snap);

        if (rel.empty())
            rel.resize ((size_t) snap.clonePositionCount);

        for (int c = 0; c < snap.clonePositionCount && c < (int) rel.size(); ++c)
            rel[(size_t) c].push_back (snap.clonePositions[(size_t) c] - snap.sourcePos);
    }

    std::printf ("\n=== %s  (Wackler %.1f m / %.0f m/s, Spread %.2f m, %d Klone)\n",
                 label, jitterAmount, jitterSpeed, spread, clones);

    if (rel.empty())
    {
        std::printf ("    keine Klone im Schnappschuss\n");
        return;
    }

    for (size_t c = 0; c < rel.size(); ++c)
    {
        const auto& r = rel[c];

        if (r.empty())
            continue;

        // Wie weit wandert dieser Klon relativ zur Quelle, und wie weit ist er
        // im Mittel von ihr weg?
        Vec3 mean;
        for (const auto& v : r)
            mean += v;
        mean *= 1.0 / (double) r.size();

        double wander = 0.0, minD = 1.0e30, maxD = 0.0;
        for (const auto& v : r)
        {
            wander = std::max (wander, (v - mean).length());
            minD   = std::min (minD, v.length());
            maxD   = std::max (maxD, v.length());
        }

        std::printf ("    Klon %zu: Abstand zur Quelle %.2f .. %.2f m, "
                     "Wanderung um die eigene Mittellage %.2f m\n",
                     c + 1, minD, maxD, wander);
    }

    // Bewegen sich die Klone GEGENEINANDER oder wie ein Koerper?
    if (rel.size() >= 2)
    {
        const auto& a = rel[0];
        const auto& b = rel[1];
        const size_t n = std::min (a.size(), b.size());

        double minGap = 1.0e30, maxGap = 0.0;

        for (size_t i = 0; i < n; ++i)
        {
            const double gap = (a[i] - b[i]).length();
            minGap = std::min (minGap, gap);
            maxGap = std::max (maxGap, gap);
        }

        std::printf ("    Abstand Klon 1 zu Klon 2: %.2f .. %.2f m %s\n",
                     minGap, maxGap,
                     (maxGap - minGap) < 0.01 ? "(starr zueinander)" : "(bewegt sich gegeneinander)");
    }
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    run ("Wackler aus, Spread aus",   0.0f,  0.0f, 0.0f, 4);
    run ("Wackler aus, Spread 10 m",  0.0f,  0.0f, 10.0f, 4);
    run ("Wackler 10 m, Spread aus", 10.0f, 20.0f, 0.0f, 4);
    run ("Wackler 10 m, Spread 10 m",10.0f, 20.0f, 10.0f, 4);

    return 0;
}
