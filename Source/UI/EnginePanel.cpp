#include "EnginePanel.h"

void EnginePanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                              const juce::String& paramID, const juce::String& labelText)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
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
    setupKnob (rpmKnob, apvts, Params::rpm, "RPM");

    const std::array<const char*, 4> ratioIds  { Params::harmRatio1,  Params::harmRatio2,  Params::harmRatio3,  Params::harmRatio4 };
    const std::array<const char*, 4> detuneIds { Params::harmDetune1, Params::harmDetune2, Params::harmDetune3, Params::harmDetune4 };
    const std::array<const char*, 4> trackIds  { Params::harmTrack1,  Params::harmTrack2,  Params::harmTrack3,  Params::harmTrack4 };
    const std::array<const char*, 4> levelIds  { Params::harmLevel1,  Params::harmLevel2,  Params::harmLevel3,  Params::harmLevel4 };

    for (int i = 0; i < 4; ++i)
    {
        const juce::String n = juce::String (i + 1);
        setupKnob (harmonics[(size_t) i].ratio,  apvts, ratioIds[(size_t) i],  "Ratio "  + n);
        setupKnob (harmonics[(size_t) i].detune, apvts, detuneIds[(size_t) i], "Detune " + n);
        setupKnob (harmonics[(size_t) i].track,  apvts, trackIds[(size_t) i],  "Track "  + n);
        setupKnob (harmonics[(size_t) i].level,  apvts, levelIds[(size_t) i],  "Level "  + n);
    }

    setupKnob (noiseFcLoKnob,   apvts, Params::noiseFcLo,   "Noise Fc Lo");
    setupKnob (noiseFcHiKnob,   apvts, Params::noiseFcHi,   "Noise Fc Hi");
    setupKnob (noiseGainLoKnob, apvts, Params::noiseGainLo, "Noise Gain Lo");
    setupKnob (noiseGainHiKnob, apvts, Params::noiseGainHi, "Noise Gain Hi");
    setupKnob (noiseQKnob,      apvts, Params::noiseQ,      "Noise Q");

    setupKnob (jitterAmountKnob, apvts, Params::jitterAmount, "Jitter Amt");
    setupKnob (jitterRateKnob,   apvts, Params::jitterRateHz, "Jitter Rate");
    setupKnob (imbalanceKnob,    apvts, Params::imbalance,    "Imbalance");
}

void EnginePanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;
    auto area = getLocalBounds().reduced (8);

    // RPM oben, allein - der zentrale Parameter des Motors.
    layoutKnob (rpmKnob, area.removeFromTop (knobH).removeFromLeft (knobW));
    area.removeFromTop (6);

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

    // Jitter/Unwucht als letzte Reihe.
    auto miscRow = area.removeFromTop (knobH);
    for (auto* k : { &jitterAmountKnob, &jitterRateKnob, &imbalanceKnob })
    {
        layoutKnob (*k, miscRow.removeFromLeft (knobW));
        miscRow.removeFromLeft (4);
    }
}
