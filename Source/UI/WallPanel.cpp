#include "WallPanel.h"

void WallPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& paramID, const char* labelText,
                           Tooltips::Key tooltipKey, const juce::String& labelSuffix)
{
    knob.tooltipKey = tooltipKey;
    const auto tooltip = Tooltips::text (tooltipKey);

    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.labelSource = labelText;
    knob.labelSuffix = labelSuffix;
    knob.label.setText (Labels::text (labelText) + labelSuffix, juce::dontSendNotification);
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

        wall.onButton.setButtonText (Labels::text ("Wand ") + nr);
        wall.onButton.setTooltip (Tooltips::text (Tooltips::Key::WallOn));
        addAndMakeVisible (wall.onButton);
        wall.onAttachment = std::make_unique<ButtonAttachment> (apvts, onIds[w], wall.onButton);

        setupKnob (wall.x, apvts, xIds[w], "X ", Tooltips::Key::WallX, nr);
        setupKnob (wall.y, apvts, yIds[w], "Y ", Tooltips::Key::WallY, nr);
        setupKnob (wall.angle, apvts, angleIds[w], "Winkel ", Tooltips::Key::WallAngle, nr);
        setupKnob (wall.tilt, apvts, tiltIds[w], "Neigung ", Tooltips::Key::WallTilt, nr);
        setupKnob (wall.damp, apvts, dampIds[w], "Damp ", Tooltips::Key::WallDamp, nr);
        setupKnob (wall.gain, apvts, gainIds[w], "Gain ", Tooltips::Key::WallGain, nr);
    }

    secondOrderButton.setTooltip (Tooltips::text (Tooltips::Key::SecondOrder));
    addAndMakeVisible (secondOrderButton);
    secondOrderAttachment = std::make_unique<ButtonAttachment> (apvts, Params::reflect2ndOn,
                                                                secondOrderButton);

    setupKnob (bounceGainKnob, apvts, Params::bounceGain, "Bounce Gain", Tooltips::Key::BounceGain);
    setupKnob (bounceGainBoostKnob, apvts, Params::bounceGainDb, "Bounce Boost", Tooltips::Key::BounceGainBoost);
}

void WallPanel::refreshTooltips()
{
    for (int w = 0; w < wallCount; ++w)
        walls[w].onButton.setButtonText (Labels::text ("Wand ") + juce::String (w + 1));


    // Beschriftungen mit dem Sprachumschalter mitnehmen.
    secondOrderButton.setButtonText (Labels::text ("Mehrfachreflexion"));

    for (auto& wall : walls)
    {
        wall.onButton.setTooltip (Tooltips::text (Tooltips::Key::WallOn));

        for (auto* k : { &wall.x, &wall.y, &wall.angle, &wall.tilt, &wall.damp, &wall.gain })
        {
            const auto tooltip = Tooltips::text (k->tooltipKey);
            k->slider.setTooltip (tooltip);
            k->label.setTooltip (tooltip);
        }
    }

    secondOrderButton.setTooltip (Tooltips::text (Tooltips::Key::SecondOrder));

    for (auto* k : { &bounceGainKnob, &bounceGainBoostKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);

        // Der Sprachumschalter nimmt die Beschriftung mit, nicht nur den
        // Hinweis (@dpa 20260824: "bitte auch alle deutschen Labels in EN
        // mode auf englisch").
        k->label.setText (Labels::text (k->labelSource) + k->labelSuffix,
                          juce::dontSendNotification);
    }
}

void WallPanel::resized()
{
    // Sechs statt fuenf Regler pro Wandreihe (Gain kam dazu) - schmaler als
    // die 84px sonst ueblich, damit die Reihe in der Panel-Breite bleibt
    // (Kompaktheit vor gleicher Knopfbreite ueberall).
    constexpr int wallKnobW = 70;
    // Nur das DREHRAD auf zwei Drittel (@dpa 20260823, Berichtigung: "NUR die
    // Knobs! Label und Value sollen so bleiben wie zuvor"). Beschriftung
    // (18 px) und Wertefeld (18 px) bleiben unveraendert, die Zellenhoehe
    // schrumpft genau um das Drittel, das dem Drehrad selbst gehoert: aus
    // 82 - 18 - 18 = 46 px Rad werden 31, also 31 + 36 = 67 px Zelle. Die
    // Zellenbreite bleibt ebenfalls, sonst wuerde das Wertefeld beschnitten -
    // JUCE zeichnet das Rad mit dem kleineren der beiden Masse, die Hoehe
    // allein macht es also klein.
    constexpr int knobW     = 84;
    constexpr int knobH     = 67;

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
