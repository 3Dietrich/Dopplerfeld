#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include <vector>

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

    // Höhe, die dieses Panel bei der GERADE gewählten Betriebsart braucht.
    //
    // Sie ist nicht konstant, und das ist der Zweck (@dpa 20260824: "mach die
    // Einstellungen schmal, so dass nur das nötigste da ist"): ein
    // Düsenantrieb hat keine vier Teiltöne, also steht dort auch keine
    // Teilton-Matrix, und das Panel ist entsprechend kürzer. Der Editor fragt
    // diesen Wert ab, statt eine feste Zahl zu führen.
    int preferredContentHeight() const;

    // Ruft der Editor, um bei einem Wechsel der Betriebsart neu zu setzen -
    // die Panelhöhe ändert sich dabei (siehe preferredContentHeight()).
    std::function<void()> onLayoutChanged;

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
    // Wellenform JE Teilton (@dpa 20260824: "der sinus soll ... für jeden osc
    // setzbar sein"). Ein Schalter je Spalte, direkt über dem Teilton, zu dem
    // er gehört.
    std::array<juce::ToggleButton, 4> harmSineButtons;
    std::array<std::unique_ptr<ButtonAttachment>, 4> harmSineAttachments;

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

    // Pegel der Betriebsart (gilt fuer alles ausser "Frei") sowie die beiden
    // Groessen, die nur in je einer Betriebsart etwas tun: die Staerke der
    // Druckstoesse aus der Raketenduese und des Blattknallens am Rotor.
    Knob kindLevelKnob, rocketShockKnob, rotorSlapKnob;

    // Klangformung der beiden Rausch-Betriebsarten (@dpa 20260824: Duese und
    // Rakete brauchen "einen Klangveraenderungsknob und/oder eine Auswahl an
    // vorgefertigten (multiband?) Filtern (am besten beides)").
    //
    // Beides also: je Betriebsart eine Vorlagenliste (Choice) und ein
    // stufenloser Regler darueber. Getrennte Listen, weil ein Duesenstrahl
    // und ein Raketenbruellen nicht dieselben Klangfarben haben.
    Knob jetToneKnob, rocketToneKnob;

    // Form der Druckstoesse aus der Raketenduese: Laenge einer Stosswelle und
    // ihre Folge. Erst mit diesen beiden ist der Stoss einstellbar - vorher
    // steckten beide Groessen als Festwerte im Generator.
    Knob rocketShockSizeKnob, rocketShockRateKnob;

    // Beschriftete Auswahlliste, gebuendelt wie Knob - dieselbe Begruendung:
    // die Lebensdauer des Attachments haengt so an der der ComboBox.
    struct Choice
    {
        juce::Label label;
        juce::ComboBox combo;
        std::unique_ptr<ComboBoxAttachment> attachment;
        Tooltips::Key tooltipKey = Tooltips::Key::JetVoice;
    };

    void setupChoice (Choice& choice, juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& paramID, const juce::String& labelText,
                       Tooltips::Key tooltipKey);

    Choice jetVoiceChoice, rocketVoiceChoice;

    // Welche Vorlagenliste die gewaehlte Betriebsart hat - oder nullptr, wenn
    // sie keine hat. Wie kindKnobs() die EINE Quelle fuer Sichtbarkeit und
    // Layout, damit die beiden nicht auseinanderlaufen.
    Choice* kindChoice();
    const Choice* kindChoice() const;

    // Welche Modus-Regler die gewaehlte Betriebsart ueberhaupt braucht, in
    // Anzeigereihenfolge. EINE Quelle fuer Sichtbarkeit, Layout und
    // Panelhoehe - drei getrennte Listen liefen sonst auseinander.
    std::vector<Knob*> kindKnobs();
    std::vector<const Knob*> kindKnobs() const;

    // Hat die gewaehlte Betriebsart die vier Teiltoene samt Rauschband? Bei
    // Duese und Rakete nicht: dort steckt der eine Oszillator fest in der
    // Betriebsart (@dpa: "braucht es hoechstens 1 Oscillator").
    bool kindUsesHarmonics() const;

    void updateHeliControlsEnabled();

    // Wieviele Modus-Regler nebeneinander passen, bevor eine zweite Zeile
    // beginnt. Aus der Panelbreite, nicht geraten.
    static constexpr int knobColumns = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnginePanel)
};
