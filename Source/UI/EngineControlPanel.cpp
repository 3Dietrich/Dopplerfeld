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

    motorGateButton.setTooltip ("Motor klingt nur, waehrend/nachdem M gegriffen ist: Start beim "
                                "Greifen, nach dem Loslassen erst zur Ruhe kommen (Nachlauf), "
                                "dann in Ruhe ausfaden (~2,5s). Wirkt nur bei Quelle 'Motor'.");
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
