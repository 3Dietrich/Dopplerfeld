#include "EnginePanel.h"

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

EnginePanel::EnginePanel (juce::AudioProcessorValueTreeState& apvts)
{
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

    for (auto* k : { &noiseFcLoKnob, &noiseFcHiKnob, &noiseGainLoKnob, &noiseGainHiKnob, &noiseQKnob,
                      &jitterAmountKnob, &jitterRateKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }
}

void EnginePanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;
    auto area = getLocalBounds().reduced (8);

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

    // Jitter als letzte Reihe.
    auto miscRow = area.removeFromTop (knobH);
    for (auto* k : { &jitterAmountKnob, &jitterRateKnob })
    {
        layoutKnob (*k, miscRow.removeFromLeft (knobW));
        miscRow.removeFromLeft (4);
    }
}
