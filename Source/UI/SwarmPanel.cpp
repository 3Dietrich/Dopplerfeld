#include "SwarmPanel.h"

void SwarmPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
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

void SwarmPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

SwarmPanel::SwarmPanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (totalKnob, apvts, Params::cloneTotal, "Klone",
               "Gesamtzahl der Klone. Ein Klon ist eine zweite Quelle, deren Route um "
               "einen kleinen Betrag von der echten abweicht - zusammen ergibt das ein "
               "Schrotmuster statt eines Einzelobjekts. 0 = aus, kostet dann auch nichts.");

    setupKnob (realKnob, apvts, Params::cloneReal, "davon echt",
               "Wie viele der Klone volle Loeserphysik bekommen: eigene Laufzeit, eigener "
               "Doppler, eigener Ueberschall. Jeder davon kostet genau ein Pfadpaar - die "
               "Loeserlast waechst also linear mit dieser Zahl, siehe CPU-Balken darunter. "
               "Der Rest laeuft ueber die billige Nachbildung: leicht versetzte, in der "
               "Verzoegerung langsam wandernde Kopien des fertigen Signals, ohne einen "
               "einzigen Loeseraufruf.");

    setupKnob (spreadKnob, apvts, Params::cloneSpread, "Streuung",
               "Wie weit die Klon-Routen von der echten abweichen, in Metern. Bei den "
               "billigen Klonen wird derselbe Wert ueber die Schallgeschwindigkeit in "
               "Laufzeit umgerechnet - drei Meter sind also knapp neun Millisekunden, "
               "genau wie bei einem echten Klon in dieser Entfernung.");

    setupKnob (levelKnob, apvts, Params::cloneLevel, "Pegel billig",
               "Pegel der billigen Klone, relativ zum Original. Wirkt nur auf die "
               "Nachbildung - die echten Klone haben ihren Pegel aus der Physik (1/R) und "
               "brauchen keinen Regler.");

    autoButton.setTooltip (
        "Zieht die Zahl der ECHTEN Klone bei hoher Auslastung selbsttaetig zurueck und "
        "holt sie zurueck, wenn wieder Luft ist. Der Regler bleibt dabei die Obergrenze. "
        "Bewusst nur ein Angebot und nicht der Standard: was gerechnet wird, soll man "
        "einstellen koennen, nicht erraten muessen. Was die Automatik daraus macht, steht "
        "unter dem CPU-Balken.");
    addAndMakeVisible (autoButton);
    autoAttachment = std::make_unique<ButtonAttachment> (apvts, Params::cloneAuto, autoButton);

    panicButton.setTooltip (
        "Sofort zurueck auf die minimale sichere Konfiguration: nur der Direktpfad pro Ohr, "
        "keine Bodenreflexion, keine Waende, keine Mehrfachreflexion, keine Klone. Wirkt im "
        "Audiothread beim naechsten Block und haengt nicht daran, dass die Oberflaeche noch "
        "durchkommt. Gedacht fuer den Fall, dass die Auslastung hochgeht und der Ton "
        "wegbleibt - dann muss ein Weg zurueck da sein, ohne das Plugin neu zu laden.");
    panicButton.onClick = [this] { if (onPanic) onPanic(); };
    addAndMakeVisible (panicButton);
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
