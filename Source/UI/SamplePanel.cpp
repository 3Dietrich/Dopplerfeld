#include "SamplePanel.h"

void SamplePanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
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

void SamplePanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

SamplePanel::SamplePanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (gainKnob,      apvts, Params::sampleGain,  "Gain",        Tooltips::Key::SampleGain);
    setupKnob (pitchKnob,     apvts, Params::samplePitch, "Pitch",       Tooltips::Key::SamplePitch);
    setupKnob (loopStartKnob, apvts, Params::loopStart,   "Loop Start",  Tooltips::Key::LoopStart);
    setupKnob (loopEndKnob,   apvts, Params::loopEnd,     "Loop End",    Tooltips::Key::LoopEnd);
    setupKnob (loopXfadeKnob, apvts, Params::loopXfadeMs, "Loop Xfade",  Tooltips::Key::LoopXfade);
    setupKnob (eqLowKnob,     apvts, Params::eqLowGain,   "EQ Low",      Tooltips::Key::EqLow);
    setupKnob (eqMidGainKnob, apvts, Params::eqMidGain,   "EQ Mid",      Tooltips::Key::EqMid);
    setupKnob (eqMidFreqKnob, apvts, Params::eqMidFreq,   "EQ Mid Freq", Tooltips::Key::EqMidFreq);
    setupKnob (eqHighKnob,    apvts, Params::eqHighGain,  "EQ High",     Tooltips::Key::EqHigh);

    loadButton.setTooltip (Tooltips::text (Tooltips::Key::LoadSample));
    addAndMakeVisible (loadButton);
    loadButton.onClick = [this]
    {
        // Async statt modal - auf macOS/Sandbox-Hosts ist der modale Dialog
        // teils blockiert bzw. abgekuendigt.
        fileChooser = std::make_unique<juce::FileChooser> ("Sample laden...", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        constexpr auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return; // Abbruch

            fileNameLabel.setText (file.getFileName(), juce::dontSendNotification);

            if (onFileSelected != nullptr)
                onFileSelected (file);
        });
    };

    fileNameLabel.setText (Labels::text ("(kein Sample geladen)"), juce::dontSendNotification);
    fileNameLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (fileNameLabel);
}

void SamplePanel::refreshTooltips()
{
    for (auto* k : { &gainKnob, &pitchKnob, &loopStartKnob, &loopEndKnob, &loopXfadeKnob,
                      &eqLowKnob, &eqMidGainKnob, &eqMidFreqKnob, &eqHighKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);

        // Der Sprachumschalter nimmt die Beschriftung mit, nicht nur den
        // Hinweis (@dpa 20260824: "bitte auch alle deutschen Labels in EN
        // mode auf englisch").
        k->label.setText (Labels::text (k->labelSource), juce::dontSendNotification);
    }

    loadButton.setTooltip (Tooltips::text (Tooltips::Key::LoadSample));
}

void SamplePanel::resized()
{
    // Zellenmasse siehe Theme::knobWidth/knobHeight.
    constexpr int knobW = Theme::knobWidth;
    constexpr int knobH = Theme::knobHeight;
    auto area = getLocalBounds().reduced (8);

    // Datei-Auswahl oben - logisch der erste Schritt vor allen Reglern.
    auto fileRow = area.removeFromTop (28);
    loadButton.setBounds (fileRow.removeFromLeft (140));
    fileRow.removeFromLeft (8);
    fileNameLabel.setBounds (fileRow);
    area.removeFromTop (6);

    auto row1 = area.removeFromTop (knobH);
    for (auto* k : { &gainKnob, &pitchKnob, &loopStartKnob, &loopEndKnob, &loopXfadeKnob })
    {
        layoutKnob (*k, row1.removeFromLeft (knobW));
        row1.removeFromLeft (4);
    }
    area.removeFromTop (6);

    auto row2 = area.removeFromTop (knobH);
    for (auto* k : { &eqLowKnob, &eqMidGainKnob, &eqMidFreqKnob, &eqHighKnob })
    {
        layoutKnob (*k, row2.removeFromLeft (knobW));
        row2.removeFromLeft (4);
    }
}
