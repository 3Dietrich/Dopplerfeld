#include "SwarmPanel.h"

void SwarmPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
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
    setupKnob (realLevelKnob, apvts, Params::cloneRealLevel, "Gain", Tooltips::Key::CloneRealLevel);

    showButton.setTooltip (Tooltips::text (Tooltips::Key::CloneShow));
    showButton.setToggleState (true, juce::dontSendNotification);
    showButton.onClick = [this] { if (onShowClonesToggled != nullptr) onShowClonesToggled (showButton.getToggleState()); };
    addAndMakeVisible (showButton);

    panicButton.setTooltip (Tooltips::text (Tooltips::Key::Panic));
    panicButton.onClick = [this] { if (onPanic) onPanic(); };
    addAndMakeVisible (panicButton);

    updateEnabledState();
}

void SwarmPanel::updateEnabledState()
{
    // Steht die Gesamtzahl auf null, gibt es nichts zu verteilen - dann muss
    // der Regler auch grau sein, statt einen Wert zu zeigen, der nichts tut.
    const bool anyClones = totalKnob.slider.getValue() > 0.5;

    spreadKnob.slider.setEnabled (anyClones);
    spreadKnob.label.setEnabled (anyClones);
    realLevelKnob.slider.setEnabled (anyClones);
    realLevelKnob.label.setEnabled (anyClones);
    showButton.setEnabled (anyClones);
}

void SwarmPanel::refreshTooltips()
{
    for (auto* k : { &totalKnob, &spreadKnob, &realLevelKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    panicButton.setTooltip (Tooltips::text (Tooltips::Key::Panic));
}

void SwarmPanel::resized()
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

    auto knobRow = area.removeFromTop (knobH);

    for (auto* k : { &totalKnob, &spreadKnob, &realLevelKnob })
    {
        layoutKnob (*k, knobRow.removeFromLeft (knobW));
        knobRow.removeFromLeft (4);
    }

    area.removeFromTop (6);
    {
        auto row = area.removeFromTop (26);
        showButton.setBounds (row.removeFromLeft (110));
    }

    // Kein Platz mehr fuer den CPU-Balken hier reserviert - der sitzt jetzt
    // in der Statuszeile des Editors (siehe Klassenkommentar in SwarmPanel.h).
    area.removeFromTop (8);
    panicButton.setBounds (area.removeFromTop (28).removeFromLeft (240));
}
