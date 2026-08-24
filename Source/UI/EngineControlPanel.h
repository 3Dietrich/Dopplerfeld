#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
#include "Labels.h"
#include <juce_audio_processors/juce_audio_processors.h>

// "Motorsteuerung" (@dpa-Feedback): RPM und Imbalance aus dem Motor-Panel
// herausgezogen - das sind die beiden Regler, die man live/oft anfasst,
// waehrend der Rest von "Motor" (Harmonische, Rauschband, Jitter) Klang-
// DESIGN ist, das man einmal einstellt und dann selten wieder anruehrt.
// Eigenes Panel statt eines Extra-Abschnitts in EnginePanel, damit es
// schnell erreichbar ist, ohne "Motor" aufklappen zu muessen.
class EngineControlPanel : public juce::Component
{
public:
    explicit EngineControlPanel (juce::AudioProcessorValueTreeState& apvts);
    ~EngineControlPanel() override = default;

    void resized() override;

    // Motor-Gating (@dpa-Feedback): Motor klingt nur, waehrend/nachdem M
    // gegriffen ist. Lebt eigentlich im Processor (DopplerfeldProcessor::
    // setMotorGateEnabled()), hier nur der Schalter dazu - kein Parameter.
    void setMotorGateEnabled (bool shouldGate);
    std::function<void (bool)> onMotorGateToggled;

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

        // Der DEUTSCHE Beschriftungstext, so wie er im Quelltext steht.
        // Angezeigt wird daraus Labels::text() - im EN-Betrieb also die
        // Uebersetzung. Gemerkt wird er, damit ein Sprachwechsel die
        // Beschriftung mitnehmen kann (siehe refreshTooltips()).
        const char* labelSource = nullptr;

        Tooltips::Key tooltipKey = Tooltips::Key::EngineRpm;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID, const char* labelText,
                     Tooltips::Key tooltipKey);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    Knob rpmKnob, imbalanceKnob;

    // Oktavlage der Unwucht, siehe Params::imbalanceOctave.
    Knob imbalanceOctaveKnob;

    juce::ToggleButton motorGateButton { "Motor bei Griff" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineControlPanel)
};
