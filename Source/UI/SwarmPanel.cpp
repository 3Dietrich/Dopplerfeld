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
    setupKnob (realKnob, apvts, Params::cloneReal, "davon echt", Tooltips::Key::CloneReal);
    setupKnob (spreadKnob, apvts, Params::cloneSpread, "Streuung", Tooltips::Key::CloneSpread);
    setupKnob (levelKnob, apvts, Params::cloneLevel, "Pegel billig", Tooltips::Key::CloneLevel);

    autoButton.setTooltip (Tooltips::text (Tooltips::Key::CloneAuto));
    addAndMakeVisible (autoButton);
    autoAttachment = std::make_unique<ButtonAttachment> (apvts, Params::cloneAuto, autoButton);

    panicButton.setTooltip (Tooltips::text (Tooltips::Key::Panic));
    panicButton.onClick = [this] { if (onPanic) onPanic(); };
    addAndMakeVisible (panicButton);
}

void SwarmPanel::refreshTooltips()
{
    for (auto* k : { &totalKnob, &realKnob, &spreadKnob, &levelKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    autoButton.setTooltip (Tooltips::text (Tooltips::Key::CloneAuto));
    panicButton.setTooltip (Tooltips::text (Tooltips::Key::Panic));
}

void SwarmPanel::setLoad (float cpuPercentIn, int realClones, int cheapClones)
{
    // Nur zeichnen, wenn sich etwas sichtbar geaendert hat - der Timer laeuft
    // mit 30 Hz, und ein Balken, der sich um ein Zehntelprozent bewegt, ist
    // kein Grund fuer eine Neuzeichnung.
    const bool changed = std::abs (cpuPercentIn - cpuPercent) > 0.5f
                      || realClones != realCount
                      || cheapClones != cheapCount;

    cpuPercent = cpuPercentIn;
    realCount  = realClones;
    cheapCount = cheapClones;

    if (changed)
        repaint (meterArea.expanded (0, 20));
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
    g.drawText (juce::String::formatted ("CPU %4.0f %%   Klone: %d echt / %d billig",
                                         (double) cpuPercent, realCount, cheapCount),
                meterArea.getX(), meterArea.getBottom() + 2, meterArea.getWidth(), 16,
                juce::Justification::centredLeft);
}

void SwarmPanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;

    auto area = getLocalBounds().reduced (8);

    auto knobRow = area.removeFromTop (knobH);

    for (auto* k : { &totalKnob, &realKnob, &spreadKnob, &levelKnob })
    {
        layoutKnob (*k, knobRow.removeFromLeft (knobW));
        knobRow.removeFromLeft (4);
    }

    area.removeFromTop (6);
    autoButton.setBounds (area.removeFromTop (26).removeFromLeft (140));

    area.removeFromTop (8);
    meterArea = area.removeFromTop (14);
    area.removeFromTop (18);   // Platz fuer die Zeile unter dem Balken

    area.removeFromTop (8);
    panicButton.setBounds (area.removeFromTop (28).removeFromLeft (240));
}
