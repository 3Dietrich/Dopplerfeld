#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include <juce_audio_processors/juce_audio_processors.h>

// "Motorsteuerung" (@dpa-Feedback): RPM und Imbalance aus dem Motor-Panel
// herausgezogen - das sind die beiden Regler, die man live/oft anfasst,
// waehrend der Rest von "Motor" (Harmonische, Rauschband, Jitter) Klang-
// DESIGN ist, das man einmal einstellt und dann selten wieder anruehrt.
// Eigenes Panel statt eines Extra-Abschnitts in EnginePanel, damit es
// schnell erreichbar ist, ohne "Motor" aufklappen zu muessen.
class EngineControlPanel : public juce::Component
{
public:
    explicit EngineControlPanel (juce::AudioProcessorValueTreeState& apvts);
    ~EngineControlPanel() override = default;

    void resized() override;

    // Motor-Gating (@dpa-Feedback): Motor klingt nur, waehrend/nachdem M
    // gegriffen ist. Lebt eigentlich im Processor (DopplerfeldProcessor::
    // setMotorGateEnabled()), hier nur der Schalter dazu - kein Parameter.
    void setMotorGateEnabled (bool shouldGate);
    std::function<void (bool)> onMotorGateToggled;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

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

    Knob rpmKnob, imbalanceKnob;

    juce::ToggleButton motorGateButton { "Motor bei Griff" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineControlPanel)
};
