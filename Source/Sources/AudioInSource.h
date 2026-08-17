#pragma once

#include "SoundSource.h"
#include <juce_core/juce_core.h>   // JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
#include <vector>

// Live-Audioeingang als dritte Klangquelle neben Motor und Sample
// (@dpa-Feedback: "Audio In" bei Quelle). SoundSource ist bewusst
// PULL-basiert (renderMono() liest nur) - der Host liefert das Signal aber
// PUSH-artig einmal pro processBlock(). pushBlock() ist die Brücke: der
// Processor kopiert den Eingang dort hinein, BEVOR die eigentliche
// Blockverarbeitung (samt buffer.clear(), siehe Kommentar dort) beginnt.
// SoundSourceHolder/DopplerEngine kennen weiterhin nur renderMono() - die
// Bauart der beiden anderen Quellen bleibt unangetastet.
//
// Kein Ringpuffer nötig: ein Host-Block wird noch innerhalb DESSELBEN
// processBlock()-Aufrufs vollständig konsumiert (renderMono() läuft je
// Teilblock, siehe DopplerfeldProcessor::motionChunkSamples).
class AudioInSource : public SoundSource
{
public:
    AudioInSource() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void renderMono (float* out, int numSamples) override;

    // Kein fester Ton - ein plausibler Platzhalter reicht der Fade-Policy
    // (Plan 3.7: Fadedauer aus der Periodendauer der Grundfrequenz).
    double dominantFrequencyHz() const override { return 200.0; }

    // Vom Processor genau einmal pro processBlock() gerufen, mit dem
    // rohen Host-Eingang (oder nullptr, wenn der Eingangsbus deaktiviert
    // ist - dann bleibt die Quelle still statt irgendetwas zu erfinden).
    void pushBlock (const float* in, int numSamples);

private:
    std::vector<float> buffered;
    int readPos   = 0;
    int available = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioInSource)
};
