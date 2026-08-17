#include "EngineControlPanel.h"

void EngineControlPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                                     const juce::String& paramID, const juce::String& labelText,
                                     const juce::String& tooltip)
{
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
    setupKnob (rpmKnob, apvts, Params::rpm, "RPM",
               "Drehzahl des Motors. Treibt die Grundfrequenz (f = RPM/60) und faerbt "
               "Rauschband + Jitter mit ein - der zentrale Regler des Motorklangs.");
    setupKnob (imbalanceKnob, apvts, Params::imbalance, "Imbalance",
               "Zusaetzliche Amplitudenmodulation bei der halben Grundfrequenz - simuliert "
               "den Zuendtakt eines Viertakters. 0 = aus.");
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
}
