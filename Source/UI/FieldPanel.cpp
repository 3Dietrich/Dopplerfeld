#include "FieldPanel.h"

#include "../Util/Utf8.h"

void FieldPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
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
    setupKnob (fadeManualKnob,  apvts, Params::fadeManualMs,    "Fade Manual",   Tooltips::Key::FadeManual);
    setupKnob (outputGainKnob,  apvts, Params::outputGain,      "Output Gain",   Tooltips::Key::OutputGain);

    setupKnob (srcZKnob,        apvts, Params::srcZ,            "Source Z",      Tooltips::Key::SourceZ);
    setupKnob (lisZKnob,        apvts, Params::lisZ,            "Listener Z",    Tooltips::Key::ListenerZ);

    setupKnob (groundDampKnob,  apvts, Params::groundDampAmount, "Ground Damp",  Tooltips::Key::GroundDamp);
    setupKnob (groundGainKnob,  apvts, Params::groundGain,       "Ground Gain",  Tooltips::Key::GroundGain);
    setupKnob (airAltitudeKnob, apvts, Params::airAltitude,      Text::utf8 ("Höhe"), Tooltips::Key::AirAltitude);

    groundReflectionButton.setTooltip (Tooltips::text (Tooltips::Key::GroundReflection));
    addAndMakeVisible (groundReflectionButton);
    groundReflectionAttachment = std::make_unique<ButtonAttachment> (apvts, Params::groundReflectionOn, groundReflectionButton);

    setupKnob (nWaveSizeKnob, apvts, Params::nWaveSize, "N-Wave Size", Tooltips::Key::NWaveSize);
    setupKnob (nWaveGainKnob, apvts, Params::nWaveGainDb, "N-Wave Gain", Tooltips::Key::NWaveGain);
    setupKnob (distanceCurveKnob, apvts, Params::distanceCurve, "Distance Curve", Tooltips::Key::DistanceCurve);
    setupKnob (panAmountKnob,   apvts, Params::panAmount,       "Panning",       Tooltips::Key::Panning);
    setupKnob (airTempKnob,     apvts, Params::airTempC,        Text::utf8 ("Luft °C"), Tooltips::Key::AirTemperature);

    setupKnob (reverseGainKnob, apvts, Params::reverseGainDb,   Text::utf8 ("Rückwärts"), Tooltips::Key::ReverseGain);
    setupKnob (shockDuckKnob,   apvts, Params::shockDuckAmount, "Front-Duck",  Tooltips::Key::ShockDuck);
    setupKnob (shockDuckRangeKnob, apvts, Params::shockDuckRange, "Duck-Reichw.", Tooltips::Key::ShockDuckRange);
    setupKnob (shadowTailKnob,  apvts, Params::shadowTailMs,    "Schatten",    Tooltips::Key::ShadowTail);

    nWaveButton.setTooltip (Tooltips::text (Tooltips::Key::NWave));
    addAndMakeVisible (nWaveButton);
    nWaveAttachment = std::make_unique<ButtonAttachment> (apvts, Params::nWaveOn, nWaveButton);

    fadeAutoButton.setTooltip (Tooltips::text (Tooltips::Key::FadeAuto));
    addAndMakeVisible (fadeAutoButton);
    fadeAutoAttachment = std::make_unique<ButtonAttachment> (apvts, Params::fadeAuto, fadeAutoButton);

    limiterOnButton.setTooltip (Tooltips::text (Tooltips::Key::LimiterOn));
    addAndMakeVisible (limiterOnButton);
    limiterOnAttachment = std::make_unique<ButtonAttachment> (apvts, Params::limiterOn, limiterOnButton);

    levelMeter.setTooltip (Tooltips::text (Tooltips::Key::LevelMeter));
    addAndMakeVisible (levelMeter);
}

void FieldPanel::refreshTooltips()
{
    for (auto* k : { &fieldMetresKnob, &boomLimitKnob, &airAbsorbKnob, &fadeManualKnob, &outputGainKnob,
                      &panAmountKnob, &distanceCurveKnob, &srcZKnob, &lisZKnob,
                      &groundDampKnob, &groundGainKnob, &nWaveSizeKnob, &nWaveGainKnob,
                      &airTempKnob, &airAltitudeKnob,
                      &reverseGainKnob, &shockDuckKnob, &shockDuckRangeKnob, &shadowTailKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    groundReflectionButton.setTooltip (Tooltips::text (Tooltips::Key::GroundReflection));
    nWaveButton.setTooltip (Tooltips::text (Tooltips::Key::NWave));
    fadeAutoButton.setTooltip (Tooltips::text (Tooltips::Key::FadeAuto));
    limiterOnButton.setTooltip (Tooltips::text (Tooltips::Key::LimiterOn));
    levelMeter.setTooltip (Tooltips::text (Tooltips::Key::LevelMeter));
}

void FieldPanel::resized()
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

    auto toggleRow = area.removeFromTop (26);
    fadeAutoButton.setBounds (toggleRow.removeFromLeft (120));
    toggleRow.removeFromLeft (8);
    limiterOnButton.setBounds (toggleRow.removeFromLeft (100));
    toggleRow.removeFromLeft (8);
    groundReflectionButton.setBounds (toggleRow.removeFromLeft (140));
    toggleRow.removeFromLeft (8);
    nWaveButton.setBounds (toggleRow.removeFromLeft (100));
    area.removeFromTop (6);

    auto knobRow = area.removeFromTop (knobH);
    for (auto* k : { &fieldMetresKnob, &boomLimitKnob, &airAbsorbKnob, &fadeManualKnob, &outputGainKnob })
    {
        layoutKnob (*k, knobRow.removeFromLeft (knobW));
        knobRow.removeFromLeft (4);
    }

    // Levelmeter direkt neben Output Gain, gleiche Höhe wie die Regler
    // (ohne die Beschriftungszeile, die braucht das Meter nicht).
    knobRow.removeFromLeft (4);
    levelMeter.setBounds (knobRow.removeFromLeft (24));

    // Zweite Reihe: die Geometrie-Achse z, die daran hängende Bodendämpfung
    // und (aus Platzgruenden hier, thematisch aber unabhaengig von den
    // z-Positionen) die Hoehe ueber NN des Mediums.
    area.removeFromTop (6);

    auto geoRow = area.removeFromTop (knobH);
    for (auto* k : { &srcZKnob, &lisZKnob, &groundDampKnob, &groundGainKnob, &airAltitudeKnob })
    {
        layoutKnob (*k, geoRow.removeFromLeft (knobW));
        geoRow.removeFromLeft (4);
    }

    // Dritte Reihe: Amplituden-/Pegelthemen (N-Wave-Groesse, Amp-Verlauf,
    // Panning-Anteil) - seit Jitter/Hektik/Jitter An ins Bewegungs-Panel
    // gewandert sind (@dpa-Feedback), war diese Reihe frei; die Hoehe bleibt
    // exakt gleich (PluginEditor::fieldContentHeight unveraendert). Die
    // Lufttemperatur zieht hier aus Platzgruenden mit ein, obwohl sie
    // thematisch zu boomLimitKnob/airAbsorbKnob in Reihe 1 gehoert.
    area.removeFromTop (6);

    auto ampRow = area.removeFromTop (knobH);
    for (auto* k : { &nWaveSizeKnob, &nWaveGainKnob, &distanceCurveKnob, &panAmountKnob, &airTempKnob })
    {
        layoutKnob (*k, ampRow.removeFromLeft (knobW));
        ampRow.removeFromLeft (4);
    }

    // Vierte Reihe: was nach dem Knall passiert (siehe Header). Sie gehoert
    // zur N-Welle darueber und steht deshalb direkt darunter; die Panelhoehe
    // ist dafuer in PluginEditor::fieldContentHeight um eine Reglerreihe
    // gewachsen.
    area.removeFromTop (6);

    auto boomRow = area.removeFromTop (knobH);
    for (auto* k : { &reverseGainKnob, &shockDuckKnob, &shockDuckRangeKnob, &shadowTailKnob })
    {
        layoutKnob (*k, boomRow.removeFromLeft (knobW));
        boomRow.removeFromLeft (4);
    }
}
