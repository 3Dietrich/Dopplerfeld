#include "AudioInSource.h"

#include <algorithm>

void AudioInSource::prepare (double, int maxBlockSize)
{
    buffered.assign ((size_t) std::max (1, maxBlockSize), 0.0f);
    readPos   = 0;
    available = 0;
}

void AudioInSource::reset()
{
    std::fill (buffered.begin(), buffered.end(), 0.0f);
    readPos   = 0;
    available = 0;
}

void AudioInSource::pushBlock (const float* in, int numSamples)
{
    const int n = std::min (numSamples, (int) buffered.size());

    if (in != nullptr)
        std::copy (in, in + n, buffered.begin());
    else
        std::fill (buffered.begin(), buffered.begin() + n, 0.0f);

    readPos   = 0;
    available = n;
}

void AudioInSource::renderMono (float* out, int numSamples)
{
    if (out == nullptr || numSamples <= 0)
        return;

    int i = 0;
    for (; i < numSamples && readPos < available; ++i, ++readPos)
        out[i] = buffered[(size_t) readPos];

    // Puffer erschoepft (Host lieferte weniger als angefordert, oder kein
    // Eingang) - Stille statt altem Pufferinhalt.
    for (; i < numSamples; ++i)
        out[i] = 0.0f;
}
