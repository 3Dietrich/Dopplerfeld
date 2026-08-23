#include "EngineControlPanel.h"

void EngineControlPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                                     const juce::String& paramID, const juce::String& labelText,
                                     Tooltips::Key tooltipKey)
{
    knob.tooltipKey = tooltipKey;
    const auto tooltip = Tooltips::text (tooltipKey);

    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 12);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::Font (juce::FontOptions (11.0f)));
    knob.label.setTooltip (tooltip);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (apvts, paramID, knob.slider);
}

void EngineControlPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (12));
    knob.slider.setBounds (cell);
}

EngineControlPanel::EngineControlPanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (rpmKnob, apvts, Params::rpm, "RPM", Tooltips::Key::EngineRpm);
    setupKnob (imbalanceKnob, apvts, Params::imbalance, "Imbalance", Tooltips::Key::EngineImbalance);
    setupKnob (imbalanceOctaveKnob, apvts, Params::imbalanceOctave, "Imb Octave",
               Tooltips::Key::EngineImbalanceOctave);

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
    for (auto* k : { &rpmKnob, &imbalanceKnob, &imbalanceOctaveKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    motorGateButton.setTooltip (Tooltips::text (Tooltips::Key::EngineMotorGate));
}

void EngineControlPanel::resized()
{
    // Regler auf zwei Drittel der frueheren Groesse (@dpa 20260823: "mach die
    // knobs 2/3 so gross wie jetzt"). Beschriftungszeile und Wertefeld
    // schrumpfen mit, sonst bliebe fuer den Drehknopf selbst fast nichts
    // uebrig: von 82 px Zellenhoehe gingen sonst 36 an Text.
    constexpr int knobW = 56;
    constexpr int knobH = 55;
    auto area = getLocalBounds().reduced (8);

    auto row = area.removeFromTop (knobH);
    for (auto* k : { &rpmKnob, &imbalanceKnob, &imbalanceOctaveKnob })
    {
        layoutKnob (*k, row.removeFromLeft (knobW));
        row.removeFromLeft (4);
    }

    area.removeFromTop (6);
    motorGateButton.setBounds (area.removeFromTop (26).removeFromLeft (160));
}
