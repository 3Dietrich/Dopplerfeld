#include "FieldPanel.h"

#include "../Util/Utf8.h"

void FieldPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
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

void FieldPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

FieldPanel::FieldPanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (fieldMetresKnob, apvts, Params::fieldMetres,     "Field Size",    Tooltips::Key::FieldSize);
    setupKnob (boomLimitKnob,   apvts, Params::boomLimitDb,     "Boom Limit",    Tooltips::Key::BoomLimit);
    setupKnob (airAbsorbKnob,   apvts, Params::airAbsorbAmount, "Air Absorb",    Tooltips::Key::AirAbsorb);
    setupKnob (outputGainKnob,  apvts, Params::outputGain,      "Output Gain",   Tooltips::Key::OutputGain);

    setupKnob (srcZKnob,        apvts, Params::srcZ,            "Source Z",      Tooltips::Key::SourceZ);
    setupKnob (lisZKnob,        apvts, Params::lisZ,            "Listener Z",    Tooltips::Key::ListenerZ);

    setupKnob (groundDampKnob,  apvts, Params::groundDampAmount, "Ground Damp",  Tooltips::Key::GroundDamp);
    setupKnob (groundGainKnob,  apvts, Params::groundGain,       "Ground Gain",  Tooltips::Key::GroundGain);
    setupKnob (airAltitudeKnob, apvts, Params::airAltitude,      "Meereshöhe", Tooltips::Key::AirAltitude);

    groundReflectionButton.setTooltip (Tooltips::text (Tooltips::Key::GroundReflection));
    addAndMakeVisible (groundReflectionButton);
    groundReflectionAttachment = std::make_unique<ButtonAttachment> (apvts, Params::groundReflectionOn, groundReflectionButton);

    setupKnob (nWaveSizeKnob, apvts, Params::nWaveSize, "N-Wave Size", Tooltips::Key::NWaveSize);
    setupKnob (nWaveGainKnob, apvts, Params::nWaveGainDb, "N-Wave Gain", Tooltips::Key::NWaveGain);
    setupKnob (distanceCurveKnob, apvts, Params::distanceCurve, "Distance Curve", Tooltips::Key::DistanceCurve);
    setupKnob (panAmountKnob,   apvts, Params::panAmount,       "Panning",       Tooltips::Key::Panning);
    setupKnob (airTempKnob,     apvts, Params::airTempC,        "Luft °C", Tooltips::Key::AirTemperature);

    setupKnob (extraPathKnob,   apvts, Params::extraPathGainDb, "Fahne",     Tooltips::Key::ExtraPaths);
    setupKnob (shockDuckRangeKnob, apvts, Params::shockDuckRange, "Duck-Reichw.", Tooltips::Key::ShockDuckRange);
    setupKnob (nWaveEdgeKnob,   apvts, Params::nWaveEdge,       "Knall-Kante", Tooltips::Key::NWaveEdge);
    setupKnob (nWavePressureKnob, apvts, Params::nWavePressure,  "Druckwelle",  Tooltips::Key::NWavePressure);

    nWaveButton.setTooltip (Tooltips::text (Tooltips::Key::NWave));
    addAndMakeVisible (nWaveButton);
    nWaveAttachment = std::make_unique<ButtonAttachment> (apvts, Params::nWaveOn, nWaveButton);

    limiterOnButton.setTooltip (Tooltips::text (Tooltips::Key::LimiterOn));
    addAndMakeVisible (limiterOnButton);
    limiterOnAttachment = std::make_unique<ButtonAttachment> (apvts, Params::limiterOn, limiterOnButton);

    levelMeter.setTooltip (Tooltips::text (Tooltips::Key::LevelMeter));
    addAndMakeVisible (levelMeter);
}

void FieldPanel::refreshTooltips()
{

    // Beschriftungen mit dem Sprachumschalter mitnehmen.
    groundReflectionButton.setButtonText (Labels::text ("Bodenreflexion"));
    nWaveButton.setButtonText (Labels::text ("N-Welle"));

    for (auto* k : { &fieldMetresKnob, &boomLimitKnob, &airAbsorbKnob, &outputGainKnob,
                      &panAmountKnob, &distanceCurveKnob, &srcZKnob, &lisZKnob,
                      &groundDampKnob, &groundGainKnob, &nWaveSizeKnob, &nWaveGainKnob,
                      &airTempKnob, &airAltitudeKnob,
                      &nWaveEdgeKnob, &nWavePressureKnob, &extraPathKnob,
                      &shockDuckRangeKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);

        // Der Sprachumschalter nimmt die Beschriftung mit, nicht nur den
        // Hinweis (@dpa 20260824: "bitte auch alle deutschen Labels in EN
        // mode auf englisch").
        k->label.setText (Labels::text (k->labelSource), juce::dontSendNotification);
    }

    groundReflectionButton.setTooltip (Tooltips::text (Tooltips::Key::GroundReflection));
    nWaveButton.setTooltip (Tooltips::text (Tooltips::Key::NWave));
    limiterOnButton.setTooltip (Tooltips::text (Tooltips::Key::LimiterOn));
    levelMeter.setTooltip (Tooltips::text (Tooltips::Key::LevelMeter));
}

void FieldPanel::resized()
{
    // Sortiert nach dem, was ein Regler tut, nicht danach, was gerade in eine
    // Reihe passte (@dpa 20260828: "bei anderen sind die Regler durcheinander
    // und uncool verteilt"). Fuenf Gruppen, jede mit eigener Kopfzeile, und
    // der Schalter, der zur Gruppe gehoert, steht rechts in dieser Kopfzeile
    // statt in einer eigenen Schalterreihe ganz oben.
    //
    // Zellenmasse siehe Theme::knobWidth/knobHeight.
    constexpr int knobW       = Theme::knobWidth;
    constexpr int knobH       = Theme::knobHeight;
    constexpr int headerH     = 20;  // Kopfzeile einer Gruppe
    constexpr int groupGap    = 8;   // Luft zwischen zwei Gruppen
    constexpr int afterHeader = 2;

    auto area = getLocalBounds().reduced (8);

    groupHeaders.clear();

    // Kopfzeile einer Gruppe: Text links (in paint()), Schalter rechts.
    auto groupHeader = [&] (const char* title, juce::Button* toggle, int toggleWidth)
    {
        auto row = area.removeFromTop (headerH);

        // Der Schalter zuerst: was danach von der Zeile uebrig ist, gehoert
        // der Ueberschrift - so laeuft die Linie bis zum Schalter und nicht
        // durch ihn hindurch.
        if (toggle != nullptr)
        {
            toggle->setBounds (row.removeFromRight (toggleWidth));
            row.removeFromRight (8);
        }

        groupHeaders.push_back ({ title, row });

        area.removeFromTop (afterHeader);
    };

    // Eine Reglerreihe von links nach rechts.
    auto knobRow = [&] (std::initializer_list<Knob*> knobs) -> juce::Rectangle<int>
    {
        auto row = area.removeFromTop (knobH);

        for (auto* k : knobs)
        {
            layoutKnob (*k, row.removeFromLeft (knobW));
            row.removeFromLeft (4);
        }

        return row; // was rechts uebrig bleibt (fuer das Meter)
    };

    // Raum: wie gross das Feld ist und wo Quelle und Hoerer darin stehen.
    groupHeader ("Raum", nullptr, 0);
    knobRow ({ &fieldMetresKnob, &srcZKnob, &lisZKnob });

    // Luft: das Medium, durch das der Schall laeuft - Daempfung, Temperatur,
    // Hoehe ueber dem Meer und wie schnell der Pegel mit der Entfernung faellt.
    area.removeFromTop (groupGap);
    groupHeader ("Luft", nullptr, 0);
    knobRow ({ &airAbsorbKnob, &airTempKnob, &airAltitudeKnob, &distanceCurveKnob });

    // Boden: der zweite Weg, ueber den der Schall ankommt.
    area.removeFromTop (groupGap);
    groupHeader ("Boden", &groundReflectionButton, 140);
    knobRow ({ &groundDampKnob, &groundGainKnob });

    // Knall: alles, was zur Ueberschall-Stossfront gehoert - ihre Form, ihre
    // Groesse, ihr Deckel und was nach ihr passiert.
    area.removeFromTop (groupGap);
    groupHeader ("Knall", &nWaveButton, 100);
    knobRow ({ &nWaveSizeKnob, &nWaveGainKnob, &nWaveEdgeKnob, &nWavePressureKnob, &boomLimitKnob });
    area.removeFromTop (4);
    knobRow ({ &extraPathKnob, &shockDuckRangeKnob });

    // Ausgang: was das Plugin am Ende abgibt.
    area.removeFromTop (groupGap);
    groupHeader ("Ausgang", &limiterOnButton, 100);

    auto rest = knobRow ({ &outputGainKnob, &panAmountKnob });

    // Levelmeter direkt neben dem Ausgangspegel, gleiche Hoehe wie die Regler
    // (ohne die Beschriftungszeile, die braucht das Meter nicht).
    levelMeter.setBounds (rest.removeFromLeft (24));
}

void FieldPanel::paint (juce::Graphics& g)
{
    for (const auto& header : groupHeaders)
    {
        const auto title = Labels::text (header.title);

        g.setColour (Theme::muted);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));

        const int textWidth = juce::roundToInt (
                                  juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 8;

        g.drawText (title, header.bounds.withWidth (textWidth),
                    juce::Justification::centredLeft, false);

        // Die Linie endet vor einem Schalter, falls die Zeile einen traegt -
        // dort ist die Zeile schon schmaler (siehe resized()).
        Theme::drawGroupRule (g, header.bounds, header.bounds.getX() + textWidth);
    }
}
