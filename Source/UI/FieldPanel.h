#pragma once

#include "../Params.h"
#include "LevelMeter.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Regler fuer Feldgroesse, Physik-Limits, Crossfade und Ausgang (Plan 3.11,
// Gruppen "Feld"/"Physik"/"Crossfade"/"Ausgang" - fuer die UI zu einem Panel
// zusammengefasst, weil es nur wenige Regler je Gruppe sind). Gedacht als
// Inhalt eines CollapsiblePanel.
class FieldPanel : public juce::Component
{
public:
    explicit FieldPanel (juce::AudioProcessorValueTreeState& apvts);
    ~FieldPanel() override = default;

    void resized() override;

    // Weiterleitung an das Levelmeter (@dpa-Feedback) - der Aufrufer
    // (Editor-Timer) kennt den Processor, dieses Panel nicht.
    void pushLevels (float peakLLinear, float peakRLinear, double callIntervalMs)
    {
        levelMeter.pushLevels (peakLLinear, peakRLinear, callIntervalMs);
    }

    // Setzt die Tooltips aller Regler/Schalter dieses Panels neu, in der
    // aktuell an Tooltips::currentLanguage() gewaehlten Sprache - fuer den
    // Sprachumschalter in der Kopfzeile (siehe PluginEditor).
    void refreshTooltips();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        RoundedSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
        Tooltips::Key tooltipKey = Tooltips::Key::FieldSize;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID, const juce::String& labelText,
                     Tooltips::Key tooltipKey);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    Knob fieldMetresKnob, boomLimitKnob, airAbsorbKnob, fadeManualKnob, outputGainKnob;

    // Anteil des gewoehnlichen Pegel-Pannings (@dpa-Feedback) - steht in der
    // dritten Reihe bei den Jitter-Reglern, wo Platz ist; in der ersten Reihe
    // reicht die Fensterbreite nicht.
    Knob panAmountKnob;

    // Entfernung -> Amplitude schaerfer/flacher als das physikalische 1/R
    // (@dpa-Skizze "Amp-Verlauf"). Steht bei den Hoehe-Reglern statt in der
    // ersten Reihe, weil dort kein Platz mehr frei ist - thematisch gehoert
    // er eher zu Boom Limit/Air Absorb.
    Knob distanceCurveKnob;

    // Höhe über dem Boden. x/y stellt man mit der Maus im Feld ein, für z gibt
    // es dort keine Achse - deshalb sind das die einzigen Positionsregler.
    Knob srcZKnob, lisZKnob;

    // Additive Mikrobewegung der Quelle M ("echter Chorus" bei Stillstand,
    // @dpa 20260818) - eigene Reihe, weil in der Höhen-Reihe kein Platz mehr
    // frei ist.
    Knob srcJitterAmountKnob, srcJitterRateKnob;

    // Höhendämpfung der Bodenreflexion - steht neben den z-Reglern, weil sie
    // ohne Höhenunterschied nichts zu tun hat.
    Knob groundDampKnob;

    // Druckwellen-/N-Wellen-Schicht: eigener Schalter und eigene Groesse,
    // bewusst neben (nicht in) Boom Limit - das eine ist eine Pulsform, das
    // andere eine reine Amplitudendeckelung.
    Knob nWaveSizeKnob;

    // Neben Output Gain: -6dB-Marke, Clip-Anzeige mit 500ms-Halt (@dpa).
    LevelMeter levelMeter;

    juce::ToggleButton fadeAutoButton   { "Fade Auto" };
    juce::ToggleButton limiterOnButton  { "Limiter" };
    juce::ToggleButton groundReflectionButton { "Bodenreflexion" };
    juce::ToggleButton nWaveButton { "N-Welle" };
    std::unique_ptr<ButtonAttachment> fadeAutoAttachment;
    std::unique_ptr<ButtonAttachment> limiterOnAttachment;
    std::unique_ptr<ButtonAttachment> groundReflectionAttachment;
    std::unique_ptr<ButtonAttachment> nWaveAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldPanel)
};
