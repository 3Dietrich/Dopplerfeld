#include "WallPanel.h"

void WallPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
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

void WallPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

WallPanel::WallPanel (juce::AudioProcessorValueTreeState& apvts)
{
    const char* const onIds[wallCount]    { Params::wall1On,    Params::wall2On };
    const char* const xIds[wallCount]     { Params::wall1X,     Params::wall2X };
    const char* const yIds[wallCount]     { Params::wall1Y,     Params::wall2Y };
    const char* const angleIds[wallCount] { Params::wall1Angle, Params::wall2Angle };
    const char* const tiltIds[wallCount]  { Params::wall1Tilt,  Params::wall2Tilt };
    const char* const dampIds[wallCount]  { Params::wall1Damp,  Params::wall2Damp };
    const char* const gainIds[wallCount]  { Params::wall1Gain,  Params::wall2Gain };

    for (int w = 0; w < wallCount; ++w)
    {
        auto& wall = walls[(size_t) w];

        const juce::String nr (w + 1);

        wall.onButton.setButtonText ("Wand " + nr);
        wall.onButton.setTooltip (
            "Zusaetzlicher Ausbreitungsweg pro Ohr ueber eine unendlich grosse Ebene "
            "(Spiegelquelle wie beim Boden), mit eigener Laufzeit, eigenem Doppler und "
            "eigener Daempfung. Kostet ein weiteres Pfadpaar Loeserlast, deshalb "
            "standardmaessig aus. Die Wand ist im Feld als Linie eingezeichnet.");
        addAndMakeVisible (wall.onButton);
        wall.onAttachment = std::make_unique<ButtonAttachment> (apvts, onIds[w], wall.onButton);

        setupKnob (wall.x, apvts, xIds[w], "X " + nr,
                   "Fusspunkt der Wand, waagerecht - dieselbe normierte Feldkoordinate wie "
                   "Quelle und Hoerer. Die Wand ist unendlich gross, der Punkt legt nur "
                   "fest, wo sie durchlaeuft.");
        setupKnob (wall.y, apvts, yIds[w], "Y " + nr,
                   "Fusspunkt der Wand, in die Tiefe. Siehe X.");
        setupKnob (wall.angle, apvts, angleIds[w], "Winkel " + nr,
                   "Richtung der Wandlinie in der Draufsicht. 0 Grad = die Wand laeuft quer "
                   "von links nach rechts, 90 Grad = von vorn nach hinten.");
        setupKnob (wall.tilt, apvts, tiltIds[w], "Neigung " + nr,
                   "Neigung der Wand um genau ihre eigene Linie. 0 = senkrecht stehend, "
                   "+/-90 = flach liegend - dann ist sie eine zweite Bodenebene in der Hoehe "
                   "ihres Fusspunkts (also auf z = 0, deckungsgleich mit dem Boden).");
        setupKnob (wall.damp, apvts, dampIds[w], "Damp " + nr,
                   "Wie stark die Wand bei der Reflexion die Hoehen schluckt. 0 = ideal harte "
                   "Flaeche, 1 = weich/absorbierend. Wandflaechen sind in der Regel haerter "
                   "als Gras oder Erde, deshalb wirkt derselbe Reglerwert hier heller als "
                   "beim Boden.");
        setupKnob (wall.gain, apvts, gainIds[w], "Gain " + nr,
                   "Pegel der Reflexion in dB, unabhaengig von Damp. Damp ist ein Tiefpass "
                   "mit Gleichstromverstaerkung 1 (nimmt nur Hoehen, keinen Gesamtpegel) - "
                   "hoert man die Wand trotzdem zu leise, ist das hier der Regler dafuer.");
    }

    secondOrderButton.setTooltip (
        "Genau EINE zusaetzliche Reflexionsgeneration: Wege der Form Quelle -> Flaeche X "
        "-> Flaeche Y -> Ohr, mit X ungleich Y. Braucht mindestens zwei eingeschaltete "
        "Flaechen, sonst gibt es solche Wege gar nicht. Zwei parallele Waende ergeben so "
        "das typische Flatterecho. Kostet bis zu sechs weitere Pfadpaare - der CPU-Wert "
        "in der Statuszeile zeigt, was man sich einkauft.");
    addAndMakeVisible (secondOrderButton);
    secondOrderAttachment = std::make_unique<ButtonAttachment> (apvts, Params::reflect2ndOn,
                                                                secondOrderButton);

    setupKnob (bounceGainKnob, apvts, Params::bounceGain, "Bounce Gain",
               "Pegelfaktor je zusaetzlicher Reflexionsgeneration, immer unter 1. Die "
               "Flaechendaempfung allein reicht dafuer nicht: die ist ein Tiefpass mit "
               "Gleichstromverstaerkung 1 und nimmt nur Hoehen, keinen Pegel. Kleinere "
               "Werte = die zweite Reflexion tritt weiter zurueck.");
    setupKnob (bounceGainBoostKnob, apvts, Params::bounceGainDb, "Bounce Boost",
               "Zusaetzlicher, unabhaengiger Pegel-Boost (dB) obendrauf - anders als Bounce "
               "Gain darf dieser Regler auch ueber 0dB hinaus verstaerken, damit die "
               "zweifache Reflexion trotz Tiefpass hoerbar bleibt.");
}

void WallPanel::resized()
{
    // Sechs statt fuenf Regler pro Wandreihe (Gain kam dazu) - schmaler als
    // die 84px sonst ueblich, damit die Reihe in der Panel-Breite bleibt
    // (Kompaktheit vor gleicher Knopfbreite ueberall).
    constexpr int wallKnobW = 70;
    constexpr int knobW     = 84;
    constexpr int knobH     = 82;

    auto area = getLocalBounds().reduced (8);

    for (int w = 0; w < wallCount; ++w)
    {
        auto& wall = walls[(size_t) w];

        auto toggleRow = area.removeFromTop (26);
        wall.onButton.setBounds (toggleRow.removeFromLeft (120));
        area.removeFromTop (4);

        auto knobRow = area.removeFromTop (knobH);

        for (auto* k : { &wall.x, &wall.y, &wall.angle, &wall.tilt, &wall.damp, &wall.gain })
        {
            layoutKnob (*k, knobRow.removeFromLeft (wallKnobW));
            knobRow.removeFromLeft (4);
        }

        area.removeFromTop (8);
    }

    auto secondRow = area.removeFromTop (26);
    secondOrderButton.setBounds (secondRow.removeFromLeft (160));
    area.removeFromTop (4);

    auto gainRow = area.removeFromTop (knobH);
    layoutKnob (bounceGainKnob, gainRow.removeFromLeft (knobW));
    gainRow.removeFromLeft (4);
    layoutKnob (bounceGainBoostKnob, gainRow.removeFromLeft (knobW));
}
