#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

// Regler fuer alle Motor-Parameter (Plan 3.11, Gruppe "Motor"). Gedacht als
// Inhalt eines CollapsiblePanel (siehe CollapsiblePanel.h) - diese Klasse
// selbst weiss nichts von CollapsiblePanel, sie ist eine normale Component,
// die der Aufrufer per `panel.setContent (&enginePanel)` einhaengt.
class EnginePanel : public juce::Component
{
public:
    explicit EnginePanel (juce::AudioProcessorValueTreeState& apvts);
    ~EnginePanel() override = default;

    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    // Regler + Beschriftung + Attachment gebuendelt: die Attachment-Lebens-
    // dauer haengt so automatisch an der des Sliders (JUCE-Konvention, siehe
    // granular/Source/PluginEditor.h `gainAttachment`).
    struct Knob
    {
        RoundedSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID, const juce::String& labelText);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    Knob rpmKnob;

    // Je Harmonische: Verhaeltnis, Verstimmung, Tracking, Pegel - siehe
    // Params.h "--- Motor ---" (harmRatioN/harmDetuneN/harmTrackN/harmLevelN).
    struct HarmonicKnobs
    {
        Knob ratio, detune, track, level;
    };
    std::array<HarmonicKnobs, 4> harmonics;

    Knob noiseFcLoKnob, noiseFcHiKnob, noiseGainLoKnob, noiseGainHiKnob, noiseQKnob;
    Knob jitterAmountKnob, jitterRateKnob, imbalanceKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnginePanel)
};
