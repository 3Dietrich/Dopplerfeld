#include "FieldPanel.h"

void FieldPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                             const juce::String& paramID, const juce::String& labelText)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (apvts, paramID, knob.slider);
}

void FieldPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

FieldPanel::FieldPanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (fieldMetresKnob, apvts, Params::fieldMetres,     "Field Size");
    setupKnob (boomLimitKnob,   apvts, Params::boomLimitDb,     "Boom Limit");
    setupKnob (airAbsorbKnob,   apvts, Params::airAbsorbAmount, "Air Absorb");
    setupKnob (fadeManualKnob,  apvts, Params::fadeManualMs,    "Fade Manual");
    setupKnob (outputGainKnob,  apvts, Params::outputGain,      "Output Gain");

    addAndMakeVisible (fadeAutoButton);
    fadeAutoAttachment = std::make_unique<ButtonAttachment> (apvts, Params::fadeAuto, fadeAutoButton);

    addAndMakeVisible (limiterOnButton);
    limiterOnAttachment = std::make_unique<ButtonAttachment> (apvts, Params::limiterOn, limiterOnButton);
}

void FieldPanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;
    auto area = getLocalBounds().reduced (8);

    auto toggleRow = area.removeFromTop (26);
    fadeAutoButton.setBounds (toggleRow.removeFromLeft (120));
    toggleRow.removeFromLeft (8);
    limiterOnButton.setBounds (toggleRow.removeFromLeft (100));
    area.removeFromTop (6);

    auto knobRow = area.removeFromTop (knobH);
    for (auto* k : { &fieldMetresKnob, &boomLimitKnob, &airAbsorbKnob, &fadeManualKnob, &outputGainKnob })
    {
        layoutKnob (*k, knobRow.removeFromLeft (knobW));
        knobRow.removeFromLeft (4);
    }
}
