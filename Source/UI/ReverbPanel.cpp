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

    typeBox.addItemList ({ "Diffusor", "Schroeder", "FDN", Labels::text ("Draußen") }, 1);
    typeBox.setTooltip (Tooltips::text (Tooltips::Key::TapType));
    addAndMakeVisible (typeBox);

    typeLabel.setText (Labels::text ("Bauart"), juce::dontSendNotification);
    typeLabel.setJustificationType (juce::Justification::centredRight);
    typeLabel.setTooltip (Tooltips::text (Tooltips::Key::TapType));
    addAndMakeVisible (typeLabel);

    setupKnob (gain,  "Gain",      Tooltips::Key::TapGain);
    setupKnob (z,     "Höhe (Z)",  Tooltips::Key::TapZ);
    setupKnob (width, "LR-Breite", Tooltips::Key::TapWidth);
    setupKnob (room,  "Weite",     Tooltips::Key::TapRoom);
    setupKnob (early, "Energie",   Tooltips::Key::TapEarly);
    setupKnob (decay, "Abkling",   Tooltips::Key::TapDecay);
    setupKnob (damp,  "Damp",      Tooltips::Key::TapDamp);
    setupKnob (phase, "Phase",     Tooltips::Key::TapPhase);
    setupKnob (echoes, "Echos",    Tooltips::Key::TapEchoes);
    setupKnob (seed,   "Seed",     Tooltips::Key::TapSeed);
    setupKnob (direct, "Direkt",   Tooltips::Key::DirectGain);

    typeBox.onChange = [this] { updateTypeDependentControls(); };

    // Der Direktschall haengt fest am globalen Parameter und wird beim
    // Umschalten des Punktes nicht neu gebunden.
    direct.attachment = std::make_unique<SliderAttachment> (apvts, Params::directGain, direct.slider);

    copyButton.setTooltip (Tooltips::text (Tooltips::Key::TapCopy));
    copyButton.onClick = [this] { copyFromSelected(); };
    addAndMakeVisible (copyButton);

    pasteButton.setTooltip (Tooltips::text (Tooltips::Key::TapPaste));
    pasteButton.onClick = [this] { pasteToSelected(); };
    pasteButton.setEnabled (false);
    addAndMakeVisible (pasteButton);

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

    for (auto* k : { &z, &room, &early, &decay, &damp, &phase, &gain, &width, &echoes, &seed })
        k->attachment.reset();

    onAttachment = std::make_unique<ButtonAttachment> (
        state, Params::tapId (selected, on), onButton);

    predelayAttachment = std::make_unique<ButtonAttachment> (
        state, Params::tapId (selected, predelay), predelayButton);

    typeAttachment = std::make_unique<ComboAttachment> (
        state, Params::tapId (selected, type), typeBox);

    const std::pair<Knob*, const char*> bindings[] {
        { &z,     Params::TapPart::z },
        { &room,  Params::TapPart::room },
        { &early, Params::TapPart::early },
        { &decay, Params::TapPart::decay },
        { &damp,  Params::TapPart::damp },
        { &phase, Params::TapPart::phase },
        { &gain,  Params::TapPart::gain },
        { &width, Params::TapPart::width },
        { &echoes, Params::TapPart::echoes },
        { &seed,   Params::TapPart::seed }
    };

    for (const auto& b : bindings)
        b.first->attachment = std::make_unique<SliderAttachment> (
            state, Params::tapId (selected, b.second), b.first->slider);

    updateTypeDependentControls();
    refreshRunningMarks();
    updateGainLabel();
}

// "3 Gain" statt nur "Gain": daneben steht der Direktschall, und ohne die
// Nummer sieht man zwei Pegelregler, von denen einer unerklaerlich woanders
// hinwirkt.
void ReverbPanel::updateGainLabel()
{
    gain.label.setText (juce::String (selected + 1) + " " + Labels::text ("Gain"),
                        juce::dontSendNotification);
}

// Echos, Wuerfelbecher und der Phasenverdreher beschreiben, wie eine FLAECHE
// abgetastet wird - das gibt es nur bei Draussen. Ausgegraut statt versteckt,
// damit das Panel beim Umschalten nicht springt.
void ReverbPanel::updateTypeDependentControls()
{
    const bool isOpenAir = (typeBox.getSelectedItemIndex() == 3);

    for (auto* k : { &echoes, &seed, &phase })
    {
        k->slider.setEnabled (isOpenAir);
        k->label.setEnabled (isOpenAir);
        k->slider.setAlpha (isOpenAir ? 1.0f : 0.42f);
        k->label.setAlpha (isOpenAir ? 1.0f : 0.42f);
    }
}

void ReverbPanel::refreshRunningMarks()
{
    for (int t = 0; t < tapCount; ++t)
    {
        const auto* p = state.getRawParameterValue (Params::tapId (t, Params::TapPart::on));
        const bool running  = p != nullptr && p->load() > 0.5f;
        const bool isChosen = (t == selected);

        // BEIDE Farbkennungen setzen. Ein Knopf mit setClickingTogglesState()
        // zeichnet im eingeschalteten Zustand mit buttonOnColourId, sonst mit
        // buttonColourId - der gewaehlte Knopf ist immer eingeschaltet und
        // holte sich bisher eine Farbe, die nie gesetzt wurde. Genau daran lag
        // es, dass die Auswahl kaum zu sehen war.
        //
        // Drei Zustaende, drei klar verschiedene Flaechen:
        //   gewaehlt          voller Panelton, heller Text
        //   laeuft, nicht gewaehlt   gedaempfter Panelton
        //   aus               Kopfzeilengrund, gedaempfter Text
        const auto chosen  = Theme::Panel::wall.withAlpha (0.95f);
        const auto runs    = Theme::Panel::wall.withAlpha (0.30f);
        const auto idle    = Theme::panelHeader;

        const auto fill = isChosen ? chosen : (running ? runs : idle);

        selectButtons[t].setColour (juce::TextButton::buttonColourId,   fill);
        selectButtons[t].setColour (juce::TextButton::buttonOnColourId, fill);

        // Auf dem vollen Panelton braucht der Text den dunklen Grund als
        // Gegenfarbe, sonst steht Hell auf Hell.
        const auto ink = isChosen ? Theme::panel
                                  : (running ? Theme::text : Theme::muted);

        selectButtons[t].setColour (juce::TextButton::textColourOffId, ink);
        selectButtons[t].setColour (juce::TextButton::textColourOnId,  ink);
    }

    // Ein laufender Punkt zeigt sich zusaetzlich am Haken - und der gewaehlte
    // ist der einzige, dessen Werte in den Reglern darunter stehen.
    pasteButton.setEnabled (clipboard.valid);
}

void ReverbPanel::copyFromSelected()
{
    // Die Zwecke hier ausgeschrieben und nicht ueber "using namespace": die
    // Klasse hat Regler-Member, die genauso heissen (room, early, decay, ...),
    // und Member gehen einer using-Deklaration vor.
    namespace TP = Params::TapPart;

    auto read = [this] (const char* part, float fallback)
    {
        const auto* p = state.getRawParameterValue (Params::tapId (selected, part));
        return p != nullptr ? p->load() : fallback;
    };

    clipboard.type     = read (TP::type,  2.0f);
    clipboard.room     = read (TP::room,  30.0f);
    clipboard.early    = read (TP::early, 1.0f);
    clipboard.decay    = read (TP::decay, 2.0f);
    clipboard.damp     = read (TP::damp,  0.35f);
    clipboard.phase    = read (TP::phase, 35.0f);
    clipboard.gain     = read (TP::gain,  -6.0f);
    clipboard.width    = read (TP::width, 1.0f);
    clipboard.predelay = read (TP::predelay, 1.0f) > 0.5f;
    clipboard.echoes   = read (TP::echoes, 24.0f);
    clipboard.seed     = read (TP::seed,   137.0f);
    clipboard.valid    = true;

    pasteButton.setEnabled (true);
}

void ReverbPanel::pasteToSelected()
{
    if (! clipboard.valid)
        return;

    namespace TP = Params::TapPart;

    // Ueber den Parameter selbst und nicht ueber den Regler: nur so erfaehrt
    // der Host davon, und nur so landet es im Preset. Ein direkt gesetzter
    // Regler waere im naechsten Durchgang wieder ueberschrieben.
    auto write = [this] (const char* part, float value)
    {
        if (auto* p = state.getParameter (Params::tapId (selected, part)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    };

    write (TP::type,  clipboard.type);
    write (TP::room,  clipboard.room);
    write (TP::early, clipboard.early);
    write (TP::decay, clipboard.decay);
    write (TP::damp,  clipboard.damp);
    write (TP::phase, clipboard.phase);
    write (TP::gain,  clipboard.gain);
    write (TP::width, clipboard.width);
    write (TP::predelay, clipboard.predelay ? 1.0f : 0.0f);
    write (TP::echoes,   clipboard.echoes);
    write (TP::seed,     clipboard.seed);
}

void ReverbPanel::refreshTooltips()
{
    for (auto& b : selectButtons)
        b.setTooltip (Tooltips::text (Tooltips::Key::TapSelect));

    onButton.setButtonText (Labels::text ("an"));
    onButton.setTooltip (Tooltips::text (Tooltips::Key::TapOn));

    predelayButton.setButtonText (Labels::text ("Vorlauf"));
    predelayButton.setTooltip (Tooltips::text (Tooltips::Key::TapPredelay));

    copyButton.setTooltip (Tooltips::text (Tooltips::Key::TapCopy));
    pasteButton.setTooltip (Tooltips::text (Tooltips::Key::TapPaste));

    typeLabel.setText (Labels::text ("Bauart"), juce::dontSendNotification);
    typeLabel.setTooltip (Tooltips::text (Tooltips::Key::TapType));
    typeBox.setTooltip (Tooltips::text (Tooltips::Key::TapType));

    // Nur der TEXT des vierten Eintrags wechselt mit der Sprache; die Liste
    // neu aufzubauen wuerde die Auswahl mitreissen, an der das Attachment
    // haengt.
    typeBox.changeItemText (4, Labels::text ("Draußen"));

    for (auto* k : { &z, &room, &early, &decay, &damp, &phase, &gain, &width, &echoes, &seed, &direct })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);

        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
        k->label.setText (Labels::text (k->labelSource), juce::dontSendNotification);
    }

    // Muss NACH der Schleife stehen: die setzt "Gain" ohne Nummer.
    updateGainLabel();
}

void ReverbPanel::resized()
{
    constexpr int knobW = Theme::knobWidth;
    constexpr int knobH = Theme::knobHeight;

    auto area = getLocalBounds().reduced (8);

    groupRules.clear();

    // Der Direktschall steht VORN und allein, nicht am Ende der Reglerreihen
    // (@dpa 20260829: "der Regler soll nicht hinter den ganzen Reglern der 8
    // Reverbs stehen, sondern irgendwo zentral/vorne/unabhaengig"). Er ist der
    // einzige hier, der nicht zum gewaehlten Punkt gehoert, sondern fuers ganze
    // Plugin gilt - und wer ihn zwischen acht Punkt-Reglern sucht, haelt ihn
    // fuer einen davon.
    auto head = area.removeFromTop (knobH);

    layoutKnob (direct, head.removeFromLeft (knobW));
    head.removeFromLeft (10);

    // Rechts daneben, zweizeilig: Punktwahl oben, Bauart darunter. Beide
    // Zeilen sind nur 26 hoch und passen deshalb neben den Regler, statt eine
    // eigene Zeile zu kosten.
    auto pick = head.removeFromTop (26);

    for (auto& b : selectButtons)
        b.setBounds (pick.removeFromLeft (28));

    pick.removeFromLeft (10);
    onButton.setBounds (pick.removeFromLeft (56));

    // Uebernehmen und Einsetzen gehoeren zur Punktwahl und stehen deshalb
    // daneben (@dpa 20260829): kopiert wird von einem Punkt zum anderen, und
    // die Nummern sind das, worauf man dabei schaut.
    pick.removeFromLeft (4);
    copyButton.setBounds (pick.removeFromLeft (26));
    pick.removeFromLeft (4);
    pasteButton.setBounds (pick.removeFromLeft (26));

    head.removeFromTop (4);

    auto typeRow = head.removeFromTop (26);
    typeLabel.setBounds (typeRow.removeFromLeft (54));
    typeRow.removeFromLeft (4);
    typeBox.setBounds (typeRow.removeFromLeft (128));

    // Der Vorlauf steht hier oben, weil die untere Reihe die vier Hallregler
    // braucht und danach nur noch Platz fuer die zwei Uebertragungsknoepfe hat.
    typeRow.removeFromLeft (10);
    predelayButton.setBounds (typeRow.removeFromLeft (96));

    groupRules.push_back ({ typeRow, typeRow.getX() + 6 });

    area.removeFromTop (6);

    // Die Werte des gewaehlten Punktes, in zwei Reihen: oben wie er im Ausgang
    // steht, unten sein Hall.
    constexpr int perRow = 5;

    static_assert (perRow * (knobW + 4) < 470 - 16,
                   "Eine Reglerreihe muss in die Panelbreite passen (siehe "
                   "DopplerfeldEditor::panelColumnWidth).");

    auto placeRow = [&] (std::initializer_list<Knob*> knobs)
    {
        auto row = area.removeFromTop (knobH);

        for (auto* k : knobs)
        {
            layoutKnob (*k, row.removeFromLeft (knobW));
            row.removeFromLeft (4);
        }

        return row;
    };

    // Oben, was den Punkt im Ausgang beschreibt - vom Pegel ueber seinen Ort
    // bis zur Weite des Raums. Unten, was danach mit dem Hall passiert.
    placeRow ({ &gain, &z, &width, &room, &early });
    area.removeFromTop (4);

    placeRow ({ &decay, &damp, &phase, &echoes, &seed });
}

void ReverbPanel::paint (juce::Graphics& g)
{
    for (const auto& rule : groupRules)
        Theme::drawGroupRule (g, rule.row, rule.fromX);
}
