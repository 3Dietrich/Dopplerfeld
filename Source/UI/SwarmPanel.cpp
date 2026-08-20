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

void SwarmPanel::setLoad (float cpu, int realClones, int cheapClones, bool limiterActive)
{
    cpuPercent = cpu;
    realCount  = realClones;
    cheapCount = cheapClones;
    limiting   = limiterActive;
    repaint();
}

void SwarmPanel::paint (juce::Graphics& g)
{
    if (meterArea.isEmpty())
        return;

    auto bar = meterArea.toFloat();

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (bar, 2.0f);

    // Der Balken zeigt bis 150 %, nicht bis 100: der interessante Bereich
    // beginnt dort, wo es knapp wird, und ein Balken, der bei 100 % einfach
    // anschlaegt, verschweigt genau das.
    constexpr float fullScale = 150.0f;

    const float filled = juce::jlimit (0.0f, 1.0f, cpuPercent / fullScale);

    // Ueber 100 % ist es hoerbar, nicht nur eine Zahl - deshalb dieselbe Farbe
    // wie in der Statuszeile des Editors.
    const juce::Colour colour = cpuPercent > 100.0f ? juce::Colours::orangered
                              : cpuPercent >  70.0f ? juce::Colours::orange
                                                    : juce::Colours::limegreen;

    g.setColour (colour.withAlpha (0.75f));
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * filled), 2.0f);

    // Marke bei 100 %, damit der Balken eine Bezugsgroesse hat.
    const float markX = bar.getX() + bar.getWidth() * (100.0f / fullScale);

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawLine (markX, bar.getY(), markX, bar.getBottom(), 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              11.0f, juce::Font::plain)));
    // Der Begrenzer gehoert hier hin: laeuft er, klingt ein Schwarm nach einer
    // einzigen Stimme, weil alles auf dieselbe Obergrenze zusammengefahren wird.
    // Ohne diese Anzeige sieht man dem Ausgang das nicht an.
    // cheapCount ist seit der Entfernung der billigen Klone immer 0 - kommt
    // aber ueber setLoad() unveraendert herein, siehe deren Kommentar oben.
    g.drawText (juce::String::formatted ("CPU %4.0f %%   Klone: %d%s",
                                         (double) cpuPercent, realCount + cheapCount,
                                         limiting ? "   BEGRENZER AKTIV" : ""),
                meterArea.getX(), meterArea.getBottom() + 2, meterArea.getWidth(), 16,
                juce::Justification::centredLeft);
}

void SwarmPanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;

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

    area.removeFromTop (8);
    meterArea = area.removeFromTop (14);
    area.removeFromTop (18);   // Platz fuer die Zeile unter dem Balken

    area.removeFromTop (8);
    panicButton.setBounds (area.removeFromTop (28).removeFromLeft (240));
}
