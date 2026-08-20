#include "FieldPanel.h"

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

    setupKnob (panAmountKnob,   apvts, Params::panAmount,       "Panning",       Tooltips::Key::Panning);

    setupKnob (distanceCurveKnob, apvts, Params::distanceCurve, "Distance Curve", Tooltips::Key::DistanceCurve);

    setupKnob (srcZKnob,        apvts, Params::srcZ,            "Source Z",      Tooltips::Key::SourceZ);
    setupKnob (lisZKnob,        apvts, Params::lisZ,            "Listener Z",    Tooltips::Key::ListenerZ);
    setupKnob (srcJitterAmountKnob, apvts, Params::srcJitterAmount, "Jitter",    Tooltips::Key::SrcJitterAmount);
    setupKnob (srcJitterRateKnob,   apvts, Params::srcJitterRateHz, "Hektik",    Tooltips::Key::SrcJitterRate);

    srcJitterOnButton.setTooltip (Tooltips::text (Tooltips::Key::SrcJitterOn));
    addAndMakeVisible (srcJitterOnButton);
    srcJitterOnAttachment = std::make_unique<ButtonAttachment> (apvts, Params::srcJitterOn, srcJitterOnButton);
    // Klick UND Presetwechsel loesen onClick aus (ButtonAttachment schaltet
    // per sendNotificationSync um) - deshalb reicht dieser eine Ort, um die
    // Regler in beiden Faellen richtig auszugrauen.
    srcJitterOnButton.onClick = [this] { updateJitterEnabledState(); };

    setupKnob (groundDampKnob,  apvts, Params::groundDampAmount, "Ground Damp",  Tooltips::Key::GroundDamp);
    setupKnob (groundGainKnob,  apvts, Params::groundGain,       "Ground Gain",  Tooltips::Key::GroundGain);

    groundReflectionButton.setTooltip (Tooltips::text (Tooltips::Key::GroundReflection));
    addAndMakeVisible (groundReflectionButton);
    groundReflectionAttachment = std::make_unique<ButtonAttachment> (apvts, Params::groundReflectionOn, groundReflectionButton);

    setupKnob (nWaveSizeKnob, apvts, Params::nWaveSize, "N-Wave Size", Tooltips::Key::NWaveSize);

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

    // Einmal von Hand, denn das Attachment hat seinen Startwert schon vor der
    // Zuweisung von onClick durchgereicht - ohne diesen Aufruf waere der
    // Ausgrauzustand beim ersten Anzeigen falsch, bis man den Schalter selbst
    // anfasst.
    updateJitterEnabledState();
}

void FieldPanel::updateJitterEnabledState()
{
    // Aus heisst nur "steht still", nicht "Wert weg" - die Regler bleiben auf
    // ihrem Stand, damit beim Wiedereinschalten sofort der alte Ausschlag
    // greift statt bei null neu anzufangen (siehe Tooltips::Key::SrcJitterOn).
    const bool jitterOn = srcJitterOnButton.getToggleState();

    srcJitterAmountKnob.slider.setEnabled (jitterOn);
    srcJitterAmountKnob.label.setEnabled (jitterOn);
    srcJitterRateKnob.slider.setEnabled (jitterOn);
    srcJitterRateKnob.label.setEnabled (jitterOn);
}

void FieldPanel::refreshTooltips()
{
    for (auto* k : { &fieldMetresKnob, &boomLimitKnob, &airAbsorbKnob, &fadeManualKnob, &outputGainKnob,
                      &panAmountKnob, &distanceCurveKnob, &srcZKnob, &lisZKnob,
                      &srcJitterAmountKnob, &srcJitterRateKnob, &groundDampKnob, &groundGainKnob,
                      &nWaveSizeKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    groundReflectionButton.setTooltip (Tooltips::text (Tooltips::Key::GroundReflection));
    nWaveButton.setTooltip (Tooltips::text (Tooltips::Key::NWave));
    srcJitterOnButton.setTooltip (Tooltips::text (Tooltips::Key::SrcJitterOn));
    fadeAutoButton.setTooltip (Tooltips::text (Tooltips::Key::FadeAuto));
    limiterOnButton.setTooltip (Tooltips::text (Tooltips::Key::LimiterOn));
    levelMeter.setTooltip (Tooltips::text (Tooltips::Key::LevelMeter));
}

void FieldPanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;
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

    // Zweite Reihe: die Geometrie-Achse z und die daran hängende
    // Bodendämpfung.
    area.removeFromTop (6);

    auto geoRow = area.removeFromTop (knobH);
    for (auto* k : { &srcZKnob, &lisZKnob, &groundDampKnob, &groundGainKnob, &nWaveSizeKnob,
                     &distanceCurveKnob })
    {
        layoutKnob (*k, geoRow.removeFromLeft (knobW));
        geoRow.removeFromLeft (4);
    }

    // Dritte Reihe: M-Jitter, eigenstaendig statt in die schon volle
    // Hoehen-Reihe gequetscht. Eigener kleiner Schalter davor, gleicher
    // Aufbau wie je Wand in WallPanel (Schalter-Reihe direkt ueber der
    // zugehoerigen Knopf-Reihe).
    area.removeFromTop (6);

    auto jitterToggleRow = area.removeFromTop (26);
    srcJitterOnButton.setBounds (jitterToggleRow.removeFromLeft (120));
    area.removeFromTop (4);

    auto jitterRow = area.removeFromTop (knobH);
    for (auto* k : { &srcJitterAmountKnob, &srcJitterRateKnob, &panAmountKnob })
    {
        layoutKnob (*k, jitterRow.removeFromLeft (knobW));
        jitterRow.removeFromLeft (4);
    }
}
