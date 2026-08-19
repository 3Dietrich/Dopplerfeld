#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Regler fuer die Sample-Quelle (Plan 3.11, Gruppe "Sample") plus Datei-
// Auswahl. Kennt SampleSource NICHT - das eigentliche Laden macht der
// Aufrufer (PluginProcessor via SampleSource::loadFile) im `onFileSelected`-
// Callback. Gedacht als Inhalt eines CollapsiblePanel.
class SamplePanel : public juce::Component
{
public:
    explicit SamplePanel (juce::AudioProcessorValueTreeState& apvts);
    ~SamplePanel() override = default;

    void resized() override;

    // Wird mit der gewaehlten Datei aufgerufen, sobald der FileChooser-Dialog
    // erfolgreich geschlossen wurde. Bei Abbruch erfolgt kein Aufruf.
    std::function<void (const juce::File&)> onFileSelected;

    // Setzt die Tooltips aller Regler/Schalter dieses Panels neu, in der
    // aktuell an Tooltips::currentLanguage() gewaehlten Sprache - fuer den
    // Sprachumschalter in der Kopfzeile (siehe PluginEditor).
    void refreshTooltips();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        RoundedSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
        Tooltips::Key tooltipKey = Tooltips::Key::SampleGain;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID, const juce::String& labelText,
                     Tooltips::Key tooltipKey);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    Knob gainKnob, pitchKnob, loopStartKnob, loopEndKnob, loopXfadeKnob;
    Knob eqLowKnob, eqMidGainKnob, eqMidFreqKnob, eqHighKnob;

    juce::TextButton loadButton { "Load Sample..." };
    juce::Label fileNameLabel;

    // Muss laenger leben als der asynchrone Dialog - deshalb Member statt
    // lokaler Variable (Standard-JUCE-Muster fuer juce::FileChooser).
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePanel)
};
