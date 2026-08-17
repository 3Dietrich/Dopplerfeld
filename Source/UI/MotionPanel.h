#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Regler fuer Bewegungs-Aufnahme/-Wiedergabe (Plan 3.11, Gruppe "Bewegung").
// Kennt MotionRecorder/MotionPlayer NICHT - Record/Play loesen nur die
// Callbacks nach aussen aus, die eigentliche Verdrahtung macht der Aufrufer
// (PluginProcessor). Gedacht als Inhalt eines CollapsiblePanel.
class MotionPanel : public juce::Component
{
public:
    explicit MotionPanel (juce::AudioProcessorValueTreeState& apvts);
    ~MotionPanel() override = default;

    void resized() override;

    // Beschriftung des Play-Knopfs auf "Play"/"Stop" umschalten - der Aufrufer
    // kennt den tatsaechlichen Wiedergabezustand (MotionPlayer gehoert dem
    // Audiothread), hier wird nur die Anzeige nachgefuehrt.
    void setPlaying (bool isPlaying);

    std::function<void()> onRecordClicked;
    std::function<void()> onPlayClicked;   // Aufrufer entscheidet: je nach Zustand trigger() oder stop()

    // Vorbeiflug-Generator. Beschriftung wie beim Play-Knopf vom Aufrufer
    // nachgefuehrt, weil nur er den tatsaechlichen Zustand kennt.
    void setFlying (bool isFlying);
    std::function<void()> onFlyClicked;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        RoundedSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID, const juce::String& labelText,
                     const juce::String& tooltip = {});
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    // Combo-Items werden aus der tatsaechlichen AudioParameterChoice-Liste
    // uebernommen statt hier erneut als String-Literale getippt zu werden -
    // sonst waere ein zweiter Ort fuer Tippfehler bei den Choice-Texten offen.
    static void populateChoices (juce::ComboBox& combo, juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID);

    Knob smootherTauKnob, slewVmaxKnob, slewAmaxKnob, playSpeedKnob;

    // Gemeinsamer Tempo-Deckel fuer jede Bewegungsquelle (Maus/Automation UND
    // Vorbeiflug) - siehe Params::globalMaxSpeed.
    Knob globalMaxSpeedKnob;

    // Vorbeiflug: Bahnart, Startvariante, Abstand, Tempo.
    juce::Label flyKindLabel;
    juce::ComboBox flyKindCombo;
    std::unique_ptr<ComboBoxAttachment> flyKindAttachment;

    juce::Label flyStartLabel;
    juce::ComboBox flyStartCombo;
    std::unique_ptr<ComboBoxAttachment> flyStartAttachment;

    Knob flyDistanceKnob, flySpeedKnob;

    juce::TextButton flyButton { "Vorbeiflug" };

    juce::Label smootherTypeLabel;
    juce::ComboBox smootherTypeCombo;
    std::unique_ptr<ComboBoxAttachment> smootherTypeAttachment;

    juce::Label playInterpLabel;
    juce::ComboBox playInterpCombo;
    std::unique_ptr<ComboBoxAttachment> playInterpAttachment;

    juce::ToggleButton playLoopButton { "Loop" };
    std::unique_ptr<ButtonAttachment> playLoopAttachment;

    juce::TextButton recordButton { "Record" };
    juce::TextButton playButton   { "Play" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MotionPanel)
};
