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

    setupKnob (groundDampKnob,  apvts, Params::groundDampAmount, "Ground Damp",  Tooltips::Key::GroundDamp);

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
}

void FieldPanel::refreshTooltips()
{
    for (auto* k : { &fieldMetresKnob, &boomLimitKnob, &airAbsorbKnob, &fadeManualKnob, &outputGainKnob,
                      &panAmountKnob, &distanceCurveKnob, &srcZKnob, &lisZKnob,
                      &srcJitterAmountKnob, &srcJitterRateKnob, &groundDampKnob, &nWaveSizeKnob })
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
    for (auto* k : { &srcZKnob, &lisZKnob, &groundDampKnob, &nWaveSizeKnob, &distanceCurveKnob })
    {
        layoutKnob (*k, geoRow.removeFromLeft (knobW));
        geoRow.removeFromLeft (4);
    }

    // Dritte Reihe: M-Jitter, eigenstaendig statt in die schon volle
    // Hoehen-Reihe gequetscht.
    area.removeFromTop (6);

    auto jitterRow = area.removeFromTop (knobH);
    for (auto* k : { &srcJitterAmountKnob, &srcJitterRateKnob, &panAmountKnob })
    {
        layoutKnob (*k, jitterRow.removeFromLeft (knobW));
        jitterRow.removeFromLeft (4);
    }
}
