#include "SoundSourceHolder.h"

#include <algorithm>

void SoundSourceHolder::prepare (double sampleRate, int maxBlockSize)
{
    sr            = sampleRate > 0.0 ? sampleRate : 48000.0;
    preparedBlock = std::max (1, maxBlockSize);

    // Mono: die Quelle ist ein Punkt im Feld, Stereo entsteht erst durch die
    // zwei Ohren in DopplerEngine.
    xfade.prepare (sr, preparedBlock, 1);

    SoundSource* a = xfade.active().src;
    SoundSource* b = xfade.pending().src;

    if (a != nullptr)
        a->prepare (sr, preparedBlock);

    if (b != nullptr && b != a)
        b->prepare (sr, preparedBlock);
}

void SoundSourceHolder::reset()
{
    SoundSource* a = xfade.active().src;
    SoundSource* b = xfade.pending().src;

    if (a != nullptr)
        a->reset();

    if (b != nullptr && b != a)
        b->reset();

    // Der Fade selbst überlebt ein reset() nicht: nach einem Transportstop
    // gibt es nichts mehr zu überblenden, und die Zielquelle soll sofort
    // stehen.
    xfade.reset();
    xfade.active().src  = targetSource;
    xfade.pending().src = nullptr;
    queuedSource        = nullptr;
}

void SoundSourceHolder::setSource (SoundSource* src)
{
    targetSource        = src;
    queuedSource        = nullptr;

    xfade.reset();
    xfade.active().src  = src;
    xfade.pending().src = nullptr;
}

int SoundSourceHolder::fadeSamplesFor (const SoundSource* newSource) const
{
    FadeContext ctx;
    ctx.reason          = FadeReason::SourceTimbre;
    ctx.sampleRate      = sr;
    ctx.baseFrequencyHz = newSource != nullptr ? newSource->dominantFrequencyHz() : 0.0;
    ctx.manualSeconds   = manualSeconds;
    ctx.useManual       = useManualFade;

    return computeFadeSamples (ctx);
}

void SoundSourceHolder::switchTo (SoundSource* newSource)
{
    // Auf dieselbe Quelle nicht überblenden - sonst löst schon das erneute
    // Setzen desselben Parameterwerts einen Fade aus.
    if (newSource == targetSource)
        return;

    targetSource = newSource;

    const int fadeSamples = fadeSamplesFor (newSource);

    if (xfade.isFading() || xfade.queuedSwitchDue())
    {
        // Läuft schon einer: nur anmelden, der letzte gewinnt.
        queuedSource = newSource;
        xfade.beginSwitch (fadeSamples);
        return;
    }

    xfade.pending().src = newSource;
    xfade.beginSwitch (fadeSamples);
}

void SoundSourceHolder::setManualFade (bool shouldUseManual, double seconds)
{
    useManualFade = shouldUseManual;
    manualSeconds = seconds;
}

double SoundSourceHolder::dominantFrequencyHz() const
{
    if (targetSource != nullptr)
        return targetSource->dominantFrequencyHz();

    return 0.0;
}

void SoundSourceHolder::renderMono (float* out, int numSamples)
{
    if (out == nullptr || numSamples <= 0)
        return;

    // Angemeldeter Wechsel am Blockanfang, bevor gerendert wird.
    if (xfade.queuedSwitchDue())
    {
        xfade.pending().src = queuedSource;
        queuedSource        = nullptr;
        xfade.startQueuedSwitch();
    }

    // Sollte der Host größere Blöcke schicken als in prepare() angekündigt,
    // rendert der Crossfader nur bis preparedBlock - der Rest muss still
    // sein, sonst stünde dort alter Pufferinhalt.
    const int n = std::min (numSamples, xfade.maxBlockSize());

    if (n < numSamples)
        juce::FloatVectorOperations::clear (out + n, numSamples - n);

    // Blickfenster auf den fremden Puffer, keine Allokation.
    float* channels[1] = { out };
    juce::AudioBuffer<float> view (channels, 1, n);

    xfade.process (view);
}
