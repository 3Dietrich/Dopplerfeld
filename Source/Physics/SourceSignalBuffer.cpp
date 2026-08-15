#include "SourceSignalBuffer.h"
#include "Interpolation.h"
#include <algorithm>
#include <cassert>
#include <cmath>

void SourceSignalBuffer::prepare (double sampleRate, double maxSeconds)
{
    assert (sampleRate > 0.0 && maxSeconds > 0.0);

    currentSampleRate = sampleRate;

    // +3 Rand für den 4-Punkt-Lagrange-Stencil (eine Stützstelle davor, zwei
    // danach werden gebraucht), damit readAt() nahe am Rand des gerade
    // geschriebenen Bereichs nicht extra behandelt werden muss - dort
    // liefert es stattdessen einfach 0.0f.
    capacity = std::max (8, (int) std::ceil (maxSeconds * sampleRate) + 3);

    ring.assign ((size_t) capacity, 0.0f);
    writePos = 0;
}

int SourceSignalBuffer::ringSlot (std::int64_t idx) const
{
    const std::int64_t m = idx % capacity;
    return (int) (m < 0 ? m + capacity : m);
}

void SourceSignalBuffer::write (const float* mono, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
        ring[(size_t) ringSlot (writePos + i)] = mono[i];

    writePos += numSamples;
}

float SourceSignalBuffer::readAt (double absoluteSampleIndex) const
{
    // Stützstellen bei floorIdx-1 .. floorIdx+2 (4 Punkte, kubisch), frac
    // liegt zwischen floorIdx (Knoten 0) und floorIdx+1 (Knoten 1).
    const auto floorIdx = (std::int64_t) std::floor (absoluteSampleIndex);
    const double frac = absoluteSampleIndex - (double) floorIdx;

    const std::int64_t i0 = floorIdx - 1;
    const std::int64_t i3 = floorIdx + 2;

    const std::int64_t lower = writePos - capacity;
    const std::int64_t upper = writePos - 1;

    if (i0 < lower || i3 > upper)
        return 0.0f;

    const float y0 = ring[(size_t) ringSlot (i0)];
    const float y1 = ring[(size_t) ringSlot (i0 + 1)];
    const float y2 = ring[(size_t) ringSlot (i0 + 2)];
    const float y3 = ring[(size_t) ringSlot (i0 + 3)];

    return lagrange4<float> (y0, y1, y2, y3, frac);
}
