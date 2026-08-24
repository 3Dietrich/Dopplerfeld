#include "EnginePanel.h"

#include <algorithm>

void EnginePanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
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

    // paramID kommt ausschliesslich aus Params.h-Konstanten (Aufrufer) - eine
    // falsche ID wuerde hier nicht crashen, sondern das Attachment still
    // wirkungslos lassen; der Test-Editor prueft das beim H12-Abnahmelauf.
    knob.attachment = std::make_unique<SliderAttachment> (apvts, paramID, knob.slider);
}

void EnginePanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

void EnginePanel::populateChoices (juce::ComboBox& combo, juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
{
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
    {
        int itemId = 1;
        for (auto& choice : choiceParam->choices)
            combo.addItem (choice, itemId++);
    }
    // Kein Parameter oder falscher Typ unter dieser ID - waere ein Tippfehler
    // in paramID; Dropdown bliebe dann leer statt still falsch zu wirken.
}

EnginePanel::EnginePanel (juce::AudioProcessorValueTreeState& apvts)
{
    for (int i = 0; i < 4; ++i)
    {
        harmSineButtons[(size_t) i].setButtonText ("Sin");
        harmSineButtons[(size_t) i].setTooltip (Tooltips::text (Tooltips::Key::EngineSine));
        addAndMakeVisible (harmSineButtons[(size_t) i]);
        harmSineAttachments[(size_t) i] = std::make_unique<ButtonAttachment> (
            apvts, Params::harmSine[i], harmSineButtons[(size_t) i]);
    }

    const std::array<const char*, 4> ratioIds  { Params::harmRatio1,  Params::harmRatio2,  Params::harmRatio3,  Params::harmRatio4 };
    const std::array<const char*, 4> detuneIds { Params::harmDetune1, Params::harmDetune2, Params::harmDetune3, Params::harmDetune4 };
    const std::array<const char*, 4> trackIds  { Params::harmTrack1,  Params::harmTrack2,  Params::harmTrack3,  Params::harmTrack4 };
    const std::array<const char*, 4> levelIds  { Params::harmLevel1,  Params::harmLevel2,  Params::harmLevel3,  Params::harmLevel4 };

    for (int i = 0; i < 4; ++i)
    {
        const juce::String n = juce::String (i + 1);
        setupKnob (harmonics[(size_t) i].ratio,  apvts, ratioIds[(size_t) i],  "Ratio "  + n,  Tooltips::Key::HarmRatio);
        setupKnob (harmonics[(size_t) i].detune, apvts, detuneIds[(size_t) i], "Detune " + n,  Tooltips::Key::HarmDetune);
        setupKnob (harmonics[(size_t) i].track,  apvts, trackIds[(size_t) i],  "Track "  + n,  Tooltips::Key::HarmTrack);
        setupKnob (harmonics[(size_t) i].level,  apvts, levelIds[(size_t) i],  "Level "  + n,  Tooltips::Key::HarmLevel);
    }

    setupKnob (noiseFcLoKnob,   apvts, Params::noiseFcLo,   "Noise Fc Lo",   Tooltips::Key::NoiseFcLo);
    setupKnob (noiseFcHiKnob,   apvts, Params::noiseFcHi,   "Noise Fc Hi",   Tooltips::Key::NoiseFcHi);
    setupKnob (noiseGainLoKnob, apvts, Params::noiseGainLo, "Noise Gain Lo", Tooltips::Key::NoiseGainLo);
    setupKnob (noiseGainHiKnob, apvts, Params::noiseGainHi, "Noise Gain Hi", Tooltips::Key::NoiseGainHi);
    setupKnob (noiseQKnob,      apvts, Params::noiseQ,      "Noise Q",       Tooltips::Key::NoiseQ);

    setupKnob (jitterAmountKnob, apvts, Params::jitterAmount, "Jitter Amt",  Tooltips::Key::JitterAmount);
    setupKnob (jitterRateKnob,   apvts, Params::jitterRateHz, "Jitter Rate", Tooltips::Key::JitterRate);

    // --- Betriebsart (@dpa 20260824) ---

    engineKindLabel.setText ("Betriebsart", juce::dontSendNotification);
    engineKindLabel.setJustificationType (juce::Justification::centredLeft);
    engineKindLabel.setTooltip (Tooltips::text (Tooltips::Key::EngineKind));
    addAndMakeVisible (engineKindLabel);
    populateChoices (engineKindCombo, apvts, Params::engineKind);
    engineKindCombo.setTooltip (Tooltips::text (Tooltips::Key::EngineKind));
    addAndMakeVisible (engineKindCombo);
    engineKindAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::engineKind, engineKindCombo);
    engineKindCombo.onChange = [this] { updateHeliControlsEnabled(); };

    setupKnob (kindLevelKnob,   apvts, Params::engineLevelDb, "Pegel",     Tooltips::Key::EngineLevel);
    setupKnob (rocketShockKnob, apvts, Params::rocketShock,   "Druckstoss", Tooltips::Key::RocketShock);
    setupKnob (rotorSlapKnob,   apvts, Params::rotorSlap,     "Knattern",   Tooltips::Key::RotorSlap);

    setupKnob (propSpanKnob,  apvts, Params::propSpan,    "Spannweite", Tooltips::Key::PropSpan);
    setupKnob (propLevelKnob, apvts, Params::propLevelDb, "Prop Pegel", Tooltips::Key::PropLevel);

    setupKnob (heliRotorHzKnob,    apvts, Params::heliRotorHz,    "Rotor Hz",  Tooltips::Key::HeliRotorHz);
    setupKnob (heliBladeCountKnob, apvts, Params::heliBladeCount, "Blaetter",  Tooltips::Key::HeliBladeCount);

    // Anfangszustand passend zur tatsaechlich geladenen Betriebsart setzen -
    // sonst stuenden die Rotor-Regler nach dem Oeffnen des Editors aktiv,
    // obwohl der geladene Zustand z.B. "Duesenantrieb" waehlt.
    updateHeliControlsEnabled();
}

std::vector<const EnginePanel::Knob*> EnginePanel::kindKnobs() const
{
    // Reihenfolge = Anzeigereihenfolge. Indizes wie in Params::engineKind:
    // 0=Frei, 1=Düsenantrieb, 2=Raketenantrieb, 3=Hubschrauber, 4=Propeller.
    switch (engineKindCombo.getSelectedItemIndex())
    {
        case 1:  return { &kindLevelKnob };
        case 2:  return { &kindLevelKnob, &rocketShockKnob };
        case 3:  return { &kindLevelKnob, &rotorSlapKnob, &heliRotorHzKnob, &heliBladeCountKnob };
        case 4:  return { &kindLevelKnob, &rotorSlapKnob, &heliRotorHzKnob, &heliBladeCountKnob,
                          &propSpanKnob, &propLevelKnob };
        default: return {};   // Frei: die vier Teiltöne machen den Klang
    }
}

std::vector<EnginePanel::Knob*> EnginePanel::kindKnobs()
{
    // Dieselbe Liste, nur nicht konstant - ohne zweite Aufzählung, damit die
    // beiden nicht auseinanderlaufen können.
    std::vector<Knob*> result;

    for (auto* k : const_cast<const EnginePanel*> (this)->kindKnobs())
        result.push_back (const_cast<Knob*> (k));

    return result;
}

bool EnginePanel::kindUsesHarmonics() const
{
    const int kind = engineKindCombo.getSelectedItemIndex();

    // Frei und Hubschrauber: beide haben einen Verbrennermotor, und der sind
    // die vier Teiltöne samt Rauschband (@dpa: "Hubschrauber ... hat einen
    // Verbrennermotor (+andere, also 4 Osc sind gut)").
    return kind == 0 || kind == 3;
}

int EnginePanel::preferredContentHeight() const
{
    constexpr int knobH = 67;

    // Betriebsart-Zeile plus Abstand - die steht immer.
    int height = 8 + 44 + 6;

    const int kindCount = (int) kindKnobs().size();

    if (kindCount > 0)
    {
        const int rows = (kindCount + knobColumns - 1) / knobColumns;
        height += rows * (knobH + 6);
    }

    if (kindUsesHarmonics())
    {
        // Schalterzeile, Teilton-Matrix, Rauschband.
        height += 18 + 4 + 4 * knobH + 6;
        height += knobH + 6;
    }

    // Jitter steht immer: er verstimmt die Drehzahl selbst und wirkt damit in
    // jeder Betriebsart.
    height += knobH + 8;

    return height;
}

void EnginePanel::updateHeliControlsEnabled()
{
    // Die Betriebsart entscheidet, was überhaupt DA ist, nicht nur was
    // bedienbar ist (@dpa 20260824: "mach die Einstellungen schmal, so dass
    // nur das nötigste da ist"). Ein Düsenantrieb hat keine vier Teiltöne,
    // also stehen sie dort auch nicht herum.
    const auto active = kindKnobs();

    for (auto* k : { &kindLevelKnob, &rocketShockKnob, &rotorSlapKnob,
                      &heliRotorHzKnob, &heliBladeCountKnob,
                      &propSpanKnob, &propLevelKnob })
    {
        const bool visible = std::find (active.begin(), active.end(), k) != active.end();

        k->slider.setVisible (visible);
        k->label.setVisible (visible);
    }

    const bool harmonicsAudible = kindUsesHarmonics();

    for (auto& h : harmonics)
        for (auto* k : { &h.ratio, &h.detune, &h.track, &h.level })
        {
            k->slider.setVisible (harmonicsAudible);
            k->label.setVisible (harmonicsAudible);
        }

    for (auto& b : harmSineButtons)
        b.setVisible (harmonicsAudible);

    for (auto* k : { &noiseFcLoKnob, &noiseFcHiKnob, &noiseGainLoKnob, &noiseGainHiKnob, &noiseQKnob })
    {
        k->slider.setVisible (harmonicsAudible);
        k->label.setVisible (harmonicsAudible);
    }

    resized();

    // Die Panelhöhe hängt an der Betriebsart, der Editor muss sie neu
    // einsetzen (siehe preferredContentHeight()).
    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

void EnginePanel::refreshTooltips()
{
    for (auto& h : harmonics)
        for (auto* k : { &h.ratio, &h.detune, &h.track, &h.level })
        {
            const auto tooltip = Tooltips::text (k->tooltipKey);
            k->slider.setTooltip (tooltip);
            k->label.setTooltip (tooltip);
        }

    for (auto& b : harmSineButtons)
        b.setTooltip (Tooltips::text (Tooltips::Key::EngineSine));

    for (auto* k : { &propSpanKnob, &propLevelKnob,
                      &kindLevelKnob, &rocketShockKnob, &rotorSlapKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    for (auto* k : { &noiseFcLoKnob, &noiseFcHiKnob, &noiseGainLoKnob, &noiseGainHiKnob, &noiseQKnob,
                      &jitterAmountKnob, &jitterRateKnob, &heliRotorHzKnob, &heliBladeCountKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    engineKindLabel.setTooltip (Tooltips::text (Tooltips::Key::EngineKind));
    engineKindCombo.setTooltip (Tooltips::text (Tooltips::Key::EngineKind));
}

void EnginePanel::resized()
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

    // Die Betriebsart steht ganz oben: sie entscheidet, was darunter kommt.
    auto kindRow = area.removeFromTop (44);
    auto kindArea = kindRow.removeFromLeft (juce::jmin (200, kindRow.getWidth()));
    engineKindLabel.setBounds (kindArea.removeFromTop (18));
    engineKindCombo.setBounds (kindArea);
    area.removeFromTop (6);

    // Die Regler, die diese Betriebsart braucht - nicht mehr. Mehr als
    // knobColumns nebeneinander passen nicht in die Panelbreite, der Rest
    // rutscht in eine zweite Zeile.
    {
        auto active = kindKnobs();
        int  placed = 0;

        while (placed < (int) active.size())
        {
            auto row = area.removeFromTop (knobH);

            for (int i = 0; i < knobColumns && placed < (int) active.size(); ++i, ++placed)
            {
                layoutKnob (*active[(size_t) placed], row.removeFromLeft (knobW));
                row.removeFromLeft (4);
            }

            area.removeFromTop (6);
        }
    }

    // Teiltoene samt Rauschband nur dort, wo sie zu hoeren sind.
    if (kindUsesHarmonics())
    {
        // Eine Schalterzeile ueber der Teilton-Matrix: je Spalte ein "Sin",
        // senkrecht ueber dem Teilton, zu dem er gehoert.
        {
            auto sineRow = area.removeFromTop (18);

            for (int i = 0; i < 4; ++i)
            {
                harmSineButtons[(size_t) i].setBounds (sineRow.removeFromLeft (knobW));
                sineRow.removeFromLeft (4);
            }
        }
        area.removeFromTop (4);

        // 4 Harmonische als Spalten, je Spalte Ratio/Detune/Track/Level
        // untereinander (Plan 3.11: "pro Teilton" vier Werte).
        auto harmonicsArea = area.removeFromTop (4 * knobH);

        for (auto& h : harmonics)
        {
            auto column = harmonicsArea.removeFromLeft (knobW);
            layoutKnob (h.ratio,  column.removeFromTop (knobH));
            layoutKnob (h.detune, column.removeFromTop (knobH));
            layoutKnob (h.track,  column.removeFromTop (knobH));
            layoutKnob (h.level,  column.removeFromTop (knobH));
            harmonicsArea.removeFromLeft (4); // Spaltenabstand
        }

        area.removeFromTop (6);

        // Rauschband darunter, eine Reihe (Plan 3.10: fc/gain je Lo/Hi + Q).
        auto noiseRow = area.removeFromTop (knobH);

        for (auto* k : { &noiseFcLoKnob, &noiseFcHiKnob, &noiseGainLoKnob, &noiseGainHiKnob, &noiseQKnob })
        {
            layoutKnob (*k, noiseRow.removeFromLeft (knobW));
            noiseRow.removeFromLeft (4);
        }

        area.removeFromTop (6);
    }

    // Jitter zum Schluss: er verstimmt die Drehzahl selbst und wirkt damit in
    // jeder Betriebsart.
    auto miscRow = area.removeFromTop (knobH);

    for (auto* k : { &jitterAmountKnob, &jitterRateKnob })
    {
        layoutKnob (*k, miscRow.removeFromLeft (knobW));
        miscRow.removeFromLeft (4);
    }
}
