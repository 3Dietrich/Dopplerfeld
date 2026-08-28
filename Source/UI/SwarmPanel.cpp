#include "SwarmPanel.h"

void SwarmPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
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

void SwarmPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

SwarmPanel::SwarmPanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (totalKnob, apvts, Params::cloneTotal, "Klone", Tooltips::Key::CloneTotal);

    // Beim Bewegen der Gesamtzahl sofort nachziehen, damit sichtbar ist, ab
    // wann die anderen Regler ueberhaupt etwas bewirken.
    totalKnob.slider.onValueChange = [this] { updateEnabledState(); };
    setupKnob (spreadKnob, apvts, Params::cloneSpread, "Streuung", Tooltips::Key::CloneSpread);
    setupKnob (zAmountKnob, apvts, Params::srcJitterZAmount, "Z-Anteil", Tooltips::Key::SrcJitterZAmount);
    setupKnob (realLevelKnob, apvts, Params::cloneRealLevel, "Gain", Tooltips::Key::CloneRealLevel);

    showButton.setTooltip (Tooltips::text (Tooltips::Key::CloneShow));
    showButton.setToggleState (true, juce::dontSendNotification);
    showButton.onClick = [this] { if (onShowClonesToggled != nullptr) onShowClonesToggled (showButton.getToggleState()); };
    addAndMakeVisible (showButton);

    updateEnabledState();
}

void SwarmPanel::updateEnabledState()
{
    // Steht die Gesamtzahl auf null, gibt es nichts zu verteilen - dann muss
    // der Regler auch grau sein, statt einen Wert zu zeigen, der nichts tut.
    const bool anyClones = totalKnob.slider.getValue() > 0.5;

    spreadKnob.slider.setEnabled (anyClones);
    spreadKnob.label.setEnabled (anyClones);
    zAmountKnob.slider.setEnabled (anyClones);
    zAmountKnob.label.setEnabled (anyClones);
    realLevelKnob.slider.setEnabled (anyClones);
    realLevelKnob.label.setEnabled (anyClones);
    showButton.setEnabled (anyClones);
}

void SwarmPanel::refreshTooltips()
{

    // Beschriftungen mit dem Sprachumschalter mitnehmen.
    showButton.setButtonText (Labels::text ("Zeigen"));

    for (auto* k : { &totalKnob, &spreadKnob, &zAmountKnob, &realLevelKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);

        // Der Sprachumschalter nimmt die Beschriftung mit, nicht nur den
        // Hinweis (@dpa 20260824: "bitte auch alle deutschen Labels in EN
        // mode auf englisch").
        k->label.setText (Labels::text (k->labelSource), juce::dontSendNotification);
    }
}

void SwarmPanel::resized()
{
    // Zellenmasse siehe Theme::knobWidth/knobHeight.
    constexpr int knobW = Theme::knobWidth;
    constexpr int knobH = Theme::knobHeight;

    auto area = getLocalBounds().reduced (8);

    auto knobRow = area.removeFromTop (knobH);

    for (auto* k : { &totalKnob, &spreadKnob, &zAmountKnob, &realLevelKnob })
    {
        layoutKnob (*k, knobRow.removeFromLeft (knobW));
        knobRow.removeFromLeft (4);
    }

    area.removeFromTop (6);
    {
        auto row = area.removeFromTop (26);
        showButton.setBounds (row.removeFromLeft (110));
    }
}
