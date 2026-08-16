#include "SamplePanel.h"

void SamplePanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                              const juce::String& paramID, const juce::String& labelText,
                              const juce::String& tooltip)
{
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

void SamplePanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

SamplePanel::SamplePanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (gainKnob,      apvts, Params::sampleGain,  "Gain",
               "Lautstaerke des geladenen Samples (dB).");
    setupKnob (pitchKnob,     apvts, Params::samplePitch, "Pitch",
               "Tonhoehenverschiebung des Samples in Halbtoenen (Resampling).");
    setupKnob (loopStartKnob, apvts, Params::loopStart,   "Loop Start",
               "Loop-Anfang, normiert 0-1 der geladenen Datei.");
    setupKnob (loopEndKnob,   apvts, Params::loopEnd,     "Loop End",
               "Loop-Ende, normiert 0-1 der geladenen Datei.");
    setupKnob (loopXfadeKnob, apvts, Params::loopXfadeMs, "Loop Xfade",
               "Ueberblendzeit an der Loop-Naht (ms) - verhindert einen hoerbaren Klick "
               "beim Sprung von Loop-Ende zurueck zu Loop-Anfang.");
    setupKnob (eqLowKnob,     apvts, Params::eqLowGain,   "EQ Low",
               "Bass-Anhebung/Absenkung (Low-Shelf, feste Eckfrequenz 200 Hz).");
    setupKnob (eqMidGainKnob, apvts, Params::eqMidGain,   "EQ Mid",
               "Anhebung/Absenkung im Mittenband (Glockenfilter).");
    setupKnob (eqMidFreqKnob, apvts, Params::eqMidFreq,   "EQ Mid Freq",
               "Mittenfrequenz des Glockenfilters (Hz).");
    setupKnob (eqHighKnob,    apvts, Params::eqHighGain,  "EQ High",
               "Hoehen-Anhebung/Absenkung (High-Shelf, feste Eckfrequenz 8 kHz).");

    loadButton.setTooltip ("Audiodatei laden (WAV/AIFF/FLAC/MP3) - wird als endlos "
                           "loopende Klangquelle verwendet, ersetzt bei Bedarf per "
                           "Quelle-Knopf oben den Motor-Generator.");
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

    fileNameLabel.setText ("(kein Sample geladen)", juce::dontSendNotification);
    fileNameLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (fileNameLabel);
}

void SamplePanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;
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
