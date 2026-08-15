#pragma once

// Gemeinsame Schnittstelle aller Klangquellen (Plan 3.10) - EngineGenerator
// und SampleSource sind austauschbar dahinter, über den DualPathCrossfader
// aus Plan 3.7 gegeneinander überblendbar.
//
// Mono ist Pflicht: die Quelle ist ein Punkt im Feld, hat also kein
// Stereobild. Stereo entsteht erst durch die zwei Ohren in PropagationPath/
// DopplerEngine (H5).
//
// Bewusst nur auf rohem float* statt juce::AudioBuffer, damit dieses
// Interface selbst JUCE-frei bleibt, auch wenn die Implementierungen
// (EngineGenerator, SampleSource) intern JUCE-DSP-Klassen benutzen dürfen.
class SoundSource
{
public:
    virtual ~SoundSource() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    // Schreibt numSamples Mono-Samples nach out (überschreibt, addiert nicht).
    virtual void renderMono (float* out, int numSamples) = 0;

    // Für die Fade-Policy (Plan 3.7: SourceTimbre-Fadedauer aus der
    // Periodendauer der Grundfrequenz) und den späteren Nahfeldterm
    // (Plan 2.7: nearFieldGain(R, dominantFreq)).
    virtual double dominantFrequencyHz() const = 0;
};
