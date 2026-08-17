#include "CloneSpray.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double twoPi = 6.283185307179586476925;

// Die längste Verzögerung, die eine Kopie bekommen kann, plus Reserve für das
// Wandern. Mehr als eine Zehntelsekunde wäre kein Schwarm mehr, sondern ein
// Echo - und der Puffer soll klein genug bleiben, um im Cache zu liegen.
constexpr double maxDelaySeconds = 0.15;
}

void CloneSpray::prepare (double sampleRate, int numChannels)
{
    sr       = sampleRate > 0.0 ? sampleRate : 48000.0;
    channels = std::max (1, numChannels);
    capacity = std::max (16, (int) std::ceil (maxDelaySeconds * sr) + 4);

    ring.assign ((size_t) (capacity * channels), 0.0f);

    // Verzögerungen und Wanderraten fest je Index, nicht zufällig gezogen:
    // derselbe Regelweg muss zweimal dasselbe ergeben, sonst ist kein Vergleich
    // zweier Durchläufe möglich (siehe load_check). Die Faktoren sind bewusst
    // irrational zueinander, damit sich die Kopien nicht periodisch treffen.
    for (int i = 0; i < maxClones; ++i)
    {
        const double u = (double) (i + 1) / (double) maxClones;

        clones[i].baseDelaySamples = 0.0;   // in setSpreadMs() gesetzt
        clones[i].phaseA = u * twoPi * 0.6180339887;
        clones[i].phaseB = u * twoPi * 0.4142135624;
        clones[i].rateA  = 0.17 + 0.11 * u;
        clones[i].rateB  = 0.23 + 0.19 * u;
    }

    setSpreadMs (spreadMs);
    reset();
}

void CloneSpray::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writePos = 0;
}

void CloneSpray::setCount (int newCount)
{
    count = std::min (maxClones, std::max (0, newCount));
}

void CloneSpray::setSpreadMs (double milliseconds)
{
    spreadMs = std::max (0.0, milliseconds);

    for (int i = 0; i < maxClones; ++i)
    {
        // Ungleichmäßig verteilt (quadratisch), damit die Kopien nicht in
        // gleichen Abständen liegen - gleichmäßige Abstände klängen als
        // Kammfilter, nicht als Schwarm.
        const double u = (double) (i + 1) / (double) maxClones;

        clones[i].baseDelaySamples = spreadMs * 0.001 * sr * u * u;
    }
}

void CloneSpray::setJitterMs (double milliseconds)
{
    jitterMs = std::max (0.0, milliseconds);
}

void CloneSpray::setLevel (double level01)
{
    level = std::min (1.0, std::max (0.0, level01));
}

double CloneSpray::readInterpolated (int channel, double delaySamples) const
{
    const double maxDelay = (double) (capacity - 2);
    const double d        = std::min (maxDelay, std::max (0.0, delaySamples));

    const double readPos = (double) writePos - d;
    const int    i0      = (int) std::floor (readPos);
    const double frac    = readPos - (double) i0;

    auto at = [this, channel] (int index)
    {
        int wrapped = index % capacity;

        if (wrapped < 0)
            wrapped += capacity;

        return (double) ring[(size_t) (wrapped * channels + channel)];
    };

    // Linear reicht: die Kopien sind ohnehin nur eine Andeutung, und eine
    // höhere Interpolation würde genau die Rechenzeit kosten, um die es hier
    // geht.
    return at (i0) + frac * (at (i0 + 1) - at (i0));
}

void CloneSpray::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = std::min (buffer.getNumChannels(), channels);

    if (numSamples <= 0 || numCh <= 0 || capacity <= 0)
        return;

    float* const* data = buffer.getArrayOfWritePointers();

    const double jitterSamples = jitterMs * 0.001 * sr;
    const double dt            = 1.0 / sr;

    for (int n = 0; n < numSamples; ++n)
    {
        // Erst schreiben, dann lesen: eine Kopie mit Verzögerung 0 wäre sonst
        // ein Sample zu alt.
        for (int ch = 0; ch < numCh; ++ch)
            ring[(size_t) (writePos * channels + ch)] = data[ch][n];

        if (count > 0)
        {
            double sum[2] { 0.0, 0.0 };

            for (int k = 0; k < count; ++k)
            {
                Clone& c = clones[k];

                c.phaseA += twoPi * c.rateA * dt;
                c.phaseB += twoPi * c.rateB * dt;

                if (c.phaseA > twoPi) c.phaseA -= twoPi;
                if (c.phaseB > twoPi) c.phaseB -= twoPi;

                const double wander = jitterSamples
                                    * (0.6 * std::sin (c.phaseA) + 0.4 * std::sin (c.phaseB));

                const double delay = c.baseDelaySamples + jitterSamples + wander;

                for (int ch = 0; ch < numCh && ch < 2; ++ch)
                    sum[ch] += readInterpolated (ch, delay);
            }

            // Pegel je Kopie durch die Wurzel der Anzahl teilen: bei
            // unkorrelierten Kopien wächst die Summe mit sqrt(n), nicht mit n.
            // Ohne das würde der Schwarm mit jeder Kopie lauter, und der
            // Regler wäre in Wahrheit ein Lautstärkeregler.
            const double norm = level / std::sqrt ((double) count);

            for (int ch = 0; ch < numCh && ch < 2; ++ch)
                data[ch][n] += (float) (sum[ch] * norm);
        }

        if (++writePos >= capacity)
            writePos = 0;
    }
}
