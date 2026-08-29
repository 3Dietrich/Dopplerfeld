#include "ReverbPanel.h"

#include <initializer_list>

void ReverbPanel::setupKnob (Knob& knob, const char* labelText, Tooltips::Key tooltipKey)
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
}

void ReverbPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

ReverbPanel::ReverbPanel (juce::AudioProcessorValueTreeState& apvts)
    : state (apvts)
{
    for (int t = 0; t < tapCount; ++t)
    {
        auto& b = selectButtons[t];

        b.setButtonText (juce::String (t + 1));
        b.setTooltip (Tooltips::text (Tooltips::Key::TapSelect));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1);
        b.setConnectedEdges (  (t > 0 ? juce::Button::ConnectedOnLeft : 0)
                             | (t < tapCount - 1 ? juce::Button::ConnectedOnRight : 0));

        b.onClick = [this, t] { selectTap (t); };

        addAndMakeVisible (b);
    }

    onButton.setTooltip (Tooltips::text (Tooltips::Key::TapOn));

    // Der Rahmen der Nummernreihe haengt am An-Zustand, also muss er sich
    // mitbewegen, wenn hier geschaltet wird.
    onButton.onStateChange = [this] { refreshRunningMarks(); };
    addAndMakeVisible (onButton);

    predelayButton.setTooltip (Tooltips::text (Tooltips::Key::TapPredelay));
    addAndMakeVisible (predelayButton);

    typeBox.addItemList ({ "Diffusor", "Schroeder", "FDN" }, 1);
    typeBox.setTooltip (Tooltips::text (Tooltips::Key::TapType));
    addAndMakeVisible (typeBox);

    typeLabel.setText (Labels::text ("Bauart"), juce::dontSendNotification);
    typeLabel.setJustificationType (juce::Justification::centredRight);
    typeLabel.setTooltip (Tooltips::text (Tooltips::Key::TapType));
    addAndMakeVisible (typeLabel);

    setupKnob (x,     "X",       Tooltips::Key::TapX);
    setupKnob (y,     "Y",       Tooltips::Key::TapY);
    setupKnob (z,     "Hoehe",   Tooltips::Key::TapZ);
    setupKnob (room,  "Raum",    Tooltips::Key::TapRoom);
    setupKnob (decay, "Abkling", Tooltips::Key::TapDecay);
    setupKnob (damp,  "Damp",    Tooltips::Key::TapDamp);
    setupKnob (gain,  "Gain",    Tooltips::Key::TapGain);
    setupKnob (width, "Breite",  Tooltips::Key::TapWidth);

    selectButtons[0].setToggleState (true, juce::dontSendNotification);
    selectTap (0);
}

void ReverbPanel::selectTap (int index)
{
    selected = juce::jlimit (0, tapCount - 1, index);

    using namespace Params::TapPart;

    // Erst loesen, dann neu binden. Ein Attachment meldet sich beim Anlegen
    // sofort am Regler an und schreibt dessen Wert; blieben zwei auf demselben
    // Regler stehen, schriebe der eine in den Parameter des vorigen Punktes.
    onAttachment.reset();
    predelayAttachment.reset();
    typeAttachment.reset();

    for (auto* k : { &x, &y, &z, &room, &decay, &damp, &gain, &width })
        k->attachment.reset();

    onAttachment = std::make_unique<ButtonAttachment> (
        state, Params::tapId (selected, on), onButton);

    predelayAttachment = std::make_unique<ButtonAttachment> (
        state, Params::tapId (selected, predelay), predelayButton);

    typeAttachment = std::make_unique<ComboAttachment> (
        state, Params::tapId (selected, type), typeBox);

    const std::pair<Knob*, const char*> bindings[] {
        { &x,     Params::TapPart::x },
        { &y,     Params::TapPart::y },
        { &z,     Params::TapPart::z },
        { &room,  Params::TapPart::room },
        { &decay, Params::TapPart::decay },
        { &damp,  Params::TapPart::damp },
        { &gain,  Params::TapPart::gain },
        { &width, Params::TapPart::width }
    };

    for (const auto& b : bindings)
        b.first->attachment = std::make_unique<SliderAttachment> (
            state, Params::tapId (selected, b.second), b.first->slider);

    refreshRunningMarks();
}

void ReverbPanel::refreshRunningMarks()
{
    using namespace Params::TapPart;

    for (int t = 0; t < tapCount; ++t)
    {
        const auto* p = state.getRawParameterValue (Params::tapId (t, on));
        const bool running = p != nullptr && p->load() > 0.5f;

        // Laufende Punkte bekommen den Panelton, ruhende bleiben unauffaellig.
        // Der Unterschied muss ohne Beschriftung lesbar sein: welcher Punkt
        // gerade zu hoeren ist, ist die Frage, die man beim Einstellen am
        // haeufigsten stellt.
        selectButtons[t].setColour (juce::TextButton::buttonColourId,
                                    running ? Theme::Panel::wall.withAlpha (0.55f)
                                            : Theme::panelHeader);

        selectButtons[t].setColour (juce::TextButton::textColourOffId,
                                    running ? Theme::text : Theme::muted);
    }
}

void ReverbPanel::refreshTooltips()
{
    for (auto& b : selectButtons)
        b.setTooltip (Tooltips::text (Tooltips::Key::TapSelect));

    onButton.setButtonText (Labels::text ("an"));
    onButton.setTooltip (Tooltips::text (Tooltips::Key::TapOn));

    predelayButton.setButtonText (Labels::text ("Vorlauf"));
    predelayButton.setTooltip (Tooltips::text (Tooltips::Key::TapPredelay));

    typeLabel.setText (Labels::text ("Bauart"), juce::dontSendNotification);
    typeLabel.setTooltip (Tooltips::text (Tooltips::Key::TapType));
    typeBox.setTooltip (Tooltips::text (Tooltips::Key::TapType));

    for (auto* k : { &x, &y, &z, &room, &decay, &damp, &gain, &width })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);

        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
        k->label.setText (Labels::text (k->labelSource), juce::dontSendNotification);
    }
}

void ReverbPanel::resized()
{
    constexpr int knobW = Theme::knobWidth;
    constexpr int knobH = Theme::knobHeight;

    auto area = getLocalBounds().reduced (8);

    groupRules.clear();

    // Reihe 1: Nummernwahl, An-Schalter, Bauart.
    auto head = area.removeFromTop (26);

    for (auto& b : selectButtons)
        b.setBounds (head.removeFromLeft (28));

    head.removeFromLeft (10);
    onButton.setBounds (head.removeFromLeft (56));

    // Die Bauart rechts aussen: sie aendert den Klang am staerksten und soll
    // deshalb nicht zwischen den Zahlenreglern untergehen.
    typeBox.setBounds (head.removeFromRight (110));
    typeLabel.setBounds (head.removeFromRight (60));

    groupRules.push_back ({ head, head.getX() + 6 });

    area.removeFromTop (6);

    // Zwei Reglerreihen und nicht eine: acht Regler nebeneinander waeren bei
    // 470 px Panelbreite ueber 590 px breit. Die Aufteilung folgt dabei der
    // Sache statt nur dem Platz - oben steht, WO der Punkt ist und wie laut,
    // unten WIE sein Hall klingt.
    constexpr int perRow = 4;

    auto placeRow = [&] (std::initializer_list<Knob*> knobs)
    {
        auto row = area.removeFromTop (knobH);

        for (auto* k : knobs)
        {
            layoutKnob (*k, row.removeFromLeft (knobW));
            row.removeFromLeft (4);
        }
    };

    static_assert (perRow * (knobW + 4) < 470 - 16,
                   "Eine Reglerreihe muss in die Panelbreite passen (siehe "
                   "DopplerfeldEditor::panelColumnWidth).");

    placeRow ({ &x, &y, &z, &gain });
    area.removeFromTop (4);
    placeRow ({ &room, &decay, &damp, &width });

    area.removeFromTop (6);

    // Reihe 4: der Vorlauf. Er ist ein Schalter und kein Regler, deshalb steht
    // er nicht in einer Reglerreihe.
    auto footRow = area.removeFromTop (26);
    predelayButton.setBounds (footRow.removeFromLeft (110));
    groupRules.push_back ({ footRow, footRow.getX() + 6 });
}

void ReverbPanel::paint (juce::Graphics& g)
{
    for (const auto& rule : groupRules)
        Theme::drawGroupRule (g, rule.row, rule.fromX);
}
