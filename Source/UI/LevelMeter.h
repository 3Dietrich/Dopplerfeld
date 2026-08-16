#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// Kompakter Stereo-Pegelmesser (@dpa-Feedback): -6dB-Marke, Clip-Anzeige mit
// 500ms-Haltezeit. Hat keinen eigenen Timer - der Aufrufer (Editor-Timer,
// ~30Hz) pusht periodisch den linearen Spitzenwert seit dem letzten Aufruf
// (siehe DopplerfeldProcessor::consumeOutputPeakL/R) und das seither
// vergangene Intervall in ms, daraus macht die Komponente Anzeige-Decay und
// Clip-Haltezeit selbst.
class LevelMeter : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    void pushLevels (float peakLLinear, float peakRLinear, double callIntervalMs);

    void paint (juce::Graphics& g) override;

private:
    struct Channel
    {
        float  displayDb  = minDb;   // geglätteter Anzeigewert
        double clipHoldMs = 0.0;     // > 0 = Clip-Anzeige noch aktiv
    };

    static constexpr float  minDb              = -60.0f;
    static constexpr float  maxDb              = 3.0f;
    static constexpr float  markerDb           = -6.0f;   // @dpa-Vorgabe
    static constexpr float  clipThresholdDb    = 0.0f;
    static constexpr double clipHoldDurationMs = 500.0;   // @dpa-Vorgabe
    static constexpr double releaseDbPerMs     = 20.0 / 400.0;   // ~20dB/400ms Fallzeit

    static double linearToDb (float v)
    {
        return v > 1.0e-5f ? 20.0 * std::log10 ((double) v) : (double) minDb - 20.0;
    }

    void updateChannel (Channel& c, float peakLinear, double dtMs);
    void drawChannel (juce::Graphics& g, juce::Rectangle<float> area, const Channel& c) const;

    Channel left, right;
};
