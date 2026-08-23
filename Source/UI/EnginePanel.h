#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
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

    // Setzt die Tooltips aller Regler dieses Panels neu, in der aktuell an
    // Tooltips::currentLanguage() gewaehlten Sprache - fuer den Sprach-
    // umschalter in der Kopfzeile (siehe PluginEditor).
    void refreshTooltips();

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // Regler + Beschriftung + Attachment gebuendelt: die Attachment-Lebens-
    // dauer haengt so automatisch an der des Sliders (JUCE-Konvention, siehe
    // granular/Source/PluginEditor.h `gainAttachment`).
    struct Knob
    {
        RoundedSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
        Tooltips::Key tooltipKey = Tooltips::Key::HarmRatio;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID, const juce::String& labelText,
                     Tooltips::Key tooltipKey);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    // Combo-Items werden aus der tatsaechlichen AudioParameterChoice-Liste
    // uebernommen, nicht hier erneut als String-Literale getippt (Muster aus
    // MotionPanel::populateChoices) - sonst waere ein zweiter Ort fuer
    // Tippfehler bei den Choice-Texten offen.
    static void populateChoices (juce::ComboBox& combo, juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID);

    // Wellenform der vier Teiltoene: aus = Saegezahn wie bisher, an = reiner
    // Sinus (@dpa 20260823). Steht rechts neben der Teilton-Matrix, dort ist
    // ohnehin Platz - eine eigene Zeile dafuer wuerde das Panel nur hoeher
    // machen, ohne mehr zu zeigen.
    juce::ToggleButton engineSineButton { "Sinus" };
    std::unique_ptr<ButtonAttachment> engineSineAttachment;

    // Je Harmonische: Verhaeltnis, Verstimmung, Tracking, Pegel - siehe
    // Params.h "--- Motor ---" (harmRatioN/harmDetuneN/harmTrackN/harmLevelN).
    // RPM und Imbalance sitzen NICHT hier, sondern im eigenen EngineControlPanel
    // ("Motorsteuerung", @dpa-Feedback) - das sind die live/oft angefassten
    // Regler, hier bleibt das Klang-DESIGN (Harmonische, Rauschband, Jitter).
    struct HarmonicKnobs
    {
        Knob ratio, detune, track, level;
    };
    std::array<HarmonicKnobs, 4> harmonics;

    Knob noiseFcLoKnob, noiseFcHiKnob, noiseGainLoKnob, noiseGainHiKnob, noiseQKnob;
    Knob jitterAmountKnob, jitterRateKnob;

    // Betriebsart des Motors (@dpa 20260824: "in 'Motor' mehrere umschaltbar
    // machen") - ueberschreibt keinen der Regler oben, gewichtet nur im
    // Generator, siehe EngineGenerator::setEngineKind(). "Frei"/"Propeller"
    // verhalten sich wie bisher.
    juce::Label engineKindLabel;
    juce::ComboBox engineKindCombo;
    std::unique_ptr<ComboBoxAttachment> engineKindAttachment;

    // Nur in Betriebsart "Hubschrauber" wirksam (Rotordrehzahl, Blattzahl) -
    // bleiben in den anderen Betriebsarten sichtbar, aber ausgegraut statt zu
    // verschwinden (Muster: MotionPanel::updateJitterEnabledState()), damit
    // die Panelhoehe unabhaengig von der Betriebsart konstant bleibt.
    Knob heliRotorHzKnob, heliBladeCountKnob;

    // Nur in Betriebsart "Propeller" wirksam: Fluegelspanne und Pegel des
    // Propellerpaars. Sie stehen hier bei den anderen Betriebsart-Reglern,
    // obwohl sie in der Geometrie umgesetzt sind und nicht im Generator -
    // bedient wird beides zusammen.
    Knob propSpanKnob, propLevelKnob;

    void updateHeliControlsEnabled();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnginePanel)
};
