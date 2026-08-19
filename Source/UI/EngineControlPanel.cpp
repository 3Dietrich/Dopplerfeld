#include "EngineControlPanel.h"

void EngineControlPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                                     const juce::String& paramID, const juce::String& labelText,
                                     Tooltips::Key tooltipKey)
{
    knob.tooltipKey = tooltipKey;
    const auto tooltip = Tooltips::text (tooltipKey);

    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setTooltip (tooltip);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (apvts, paramID, knob.slider);
}

void EngineControlPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

EngineControlPanel::EngineControlPanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (rpmKnob, apvts, Params::rpm, "RPM", Tooltips::Key::EngineRpm);
    setupKnob (imbalanceKnob, apvts, Params::imbalance, "Imbalance", Tooltips::Key::EngineImbalance);

    motorGateButton.setTooltip (Tooltips::text (Tooltips::Key::EngineMotorGate));
    motorGateButton.onClick = [this]
    {
        if (onMotorGateToggled != nullptr)
            onMotorGateToggled (motorGateButton.getToggleState());
    };
    addAndMakeVisible (motorGateButton);
}

void EngineControlPanel::setMotorGateEnabled (bool shouldGate)
{
    motorGateButton.setToggleState (shouldGate, juce::dontSendNotification);
}

void EngineControlPanel::refreshTooltips()
{
    for (auto* k : { &rpmKnob, &imbalanceKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    motorGateButton.setTooltip (Tooltips::text (Tooltips::Key::EngineMotorGate));
}

void EngineControlPanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;
    auto area = getLocalBounds().reduced (8);

    auto row = area.removeFromTop (knobH);
    for (auto* k : { &rpmKnob, &imbalanceKnob })
    {
        layoutKnob (*k, row.removeFromLeft (knobW));
        row.removeFromLeft (4);
    }

    area.removeFromTop (6);
    motorGateButton.setBounds (area.removeFromTop (26).removeFromLeft (160));
}
