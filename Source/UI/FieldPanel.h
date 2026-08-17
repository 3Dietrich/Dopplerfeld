#pragma once

#include "../Params.h"
#include "LevelMeter.h"
#include "RoundedSlider.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Regler fuer Feldgroesse, Physik-Limits, Crossfade und Ausgang (Plan 3.11,
// Gruppen "Feld"/"Physik"/"Crossfade"/"Ausgang" - fuer die UI zu einem Panel
// zusammengefasst, weil es nur wenige Regler je Gruppe sind). Gedacht als
// Inhalt eines CollapsiblePanel.
class FieldPanel : public juce::Component
{
public:
    explicit FieldPanel (juce::AudioProcessorValueTreeState& apvts);
    ~FieldPanel() override = default;

    void resized() override;

    // Weiterleitung an das Levelmeter (@dpa-Feedback) - der Aufrufer
    // (Editor-Timer) kennt den Processor, dieses Panel nicht.
    void pushLevels (float peakLLinear, float peakRLinear, double callIntervalMs)
    {
        levelMeter.pushLevels (peakLLinear, peakRLinear, callIntervalMs);
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        RoundedSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID, const juce::String& labelText,
                     const juce::String& tooltip = {});
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    Knob fieldMetresKnob, boomLimitKnob, airAbsorbKnob, fadeManualKnob, outputGainKnob;

    // Höhe über dem Boden. x/y stellt man mit der Maus im Feld ein, für z gibt
    // es dort keine Achse - deshalb sind das die einzigen Positionsregler.
    Knob srcZKnob, lisZKnob;

    // Neben Output Gain: -6dB-Marke, Clip-Anzeige mit 500ms-Halt (@dpa).
    LevelMeter levelMeter;

    juce::ToggleButton fadeAutoButton   { "Fade Auto" };
    juce::ToggleButton limiterOnButton  { "Limiter" };
    std::unique_ptr<ButtonAttachment> fadeAutoAttachment;
    std::unique_ptr<ButtonAttachment> limiterOnAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldPanel)
};
