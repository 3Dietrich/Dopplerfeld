#include "EngineControlPanel.h"

void EngineControlPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                                     const juce::String& paramID, const char* labelText,
                                     Tooltips::Key tooltipKey)
{
    knob.tooltipKey = tooltipKey;
    const auto tooltip = Tooltips::text (tooltipKey);

    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.labelSource = labelText;
    knob.label.setText (Labels::text (labelText), juce::dontSendNotification);
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

    // Beschriftungen mit dem Sprachumschalter mitnehmen.
    motorGateButton.setButtonText (Labels::text ("Motor bei Griff"));

    for (auto* k : { &rpmKnob, &imbalanceKnob, &imbalanceOctaveKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);

        // Der Sprachumschalter nimmt die Beschriftung mit, nicht nur den
        // Hinweis (@dpa 20260824: "bitte auch alle deutschen Labels in EN
        // mode auf englisch").
        k->label.setText (Labels::text (k->labelSource), juce::dontSendNotification);
    }

    motorGateButton.setTooltip (Tooltips::text (Tooltips::Key::EngineMotorGate));
}

void EngineControlPanel::resized()
{
    // Nur das DREHRAD auf zwei Drittel (@dpa 20260823, Berichtigung: "NUR die
    // Knobs! Label und Value sollen so bleiben wie zuvor"). Beschriftung
    // (18 px) und Wertefeld (18 px) bleiben unveraendert, die Zellenhoehe
    // schrumpft genau um das Drittel, das dem Drehrad selbst gehoert: aus
    // 82 - 18 - 18 = 46 px Rad werden 31, also 31 + 36 = 67 px Zelle. Die
    // Zellenbreite bleibt ebenfalls, sonst wuerde das Wertefeld beschnitten -
    // JUCE zeichnet das Rad mit dem kleineren der beiden Masse, die Hoehe
    // allein macht es also klein.
    constexpr int knobW = 84;
    constexpr int knobH = 67;
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
