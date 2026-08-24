#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include "FieldComponent.h"
#include "Tooltips.h"
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

    // Nachlauf nach mouseUp() im Feld (@dpa-Feedback) - lebt eigentlich in
    // FieldComponent (das zieht ja), hier nur der Schalter dazu, weil er
    // inhaltlich zur Bewegung gehoert. Kein Parameter, siehe FieldComponent::
    // setCoastEnabled().
    void setCoastEnabled (bool shouldCoast);
    std::function<void (bool)> onCoastToggled;
    std::function<void (bool)> onMouseFrameToggled;

    // Setzt die Tooltips aller Regler/Schalter dieses Panels neu, in der
    // aktuell an Tooltips::currentLanguage() gewaehlten Sprache - fuer den
    // Sprachumschalter in der Kopfzeile (siehe PluginEditor).
    void refreshTooltips();

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        RoundedSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
        Tooltips::Key tooltipKey = Tooltips::Key::SmootherTau;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID, const juce::String& labelText,
                     Tooltips::Key tooltipKey);

public:
    // Einheit der drei Tempo-Regler (@dpa 20260819: "ich kann mit m/s schlecht
    // rechnen"). Sie haengen am selben Hauptschalter wie die Anzeige im Feld,
    // damit es nur EINE Stelle gibt, an der die Einheit umgestellt wird.
    // Gespeichert wird weiterhin in m/s - umgerechnet wird nur die Beschriftung.
    void setSpeedUnit (FieldComponent::SpeedUnit unit, double speedOfSoundMps);

    // Was im Textfeld von Fly Speed steht, fuer den Test der Einheiten-
    // umschaltung (siehe DopplerfeldEditor::flySpeedTextForTest).
    juce::String flySpeedTextForTest() { return flySpeedKnob.slider.getTextFromValue (flySpeedKnob.slider.getValue()); }
    juce::String slewAmaxTextForTest() { return slewAmaxKnob.slider.getTextFromValue (slewAmaxKnob.slider.getValue()); }

private:
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    // Slew Vmax/Amax sind nur bei Smoother "Slew Limiter" ueberhaupt wirksam
    // (siehe Params::slewVmax/slewAmax) - bei jedem anderen Verfahren
    // ausgegraut statt weiter bedienbar, aber wirkungslos, herumzustehen.
    void updateSlewControlsVisibility();

    // Combo-Items werden aus der tatsaechlichen AudioParameterChoice-Liste
    // uebernommen statt hier erneut als String-Literale getippt zu werden -
    // sonst waere ein zweiter Ort fuer Tippfehler bei den Choice-Texten offen.
    static void populateChoices (juce::ComboBox& combo, juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID);

    Knob smootherTauKnob, slewVmaxKnob, slewAmaxKnob, playSpeedKnob;

    // Gemeinsamer Tempo-Deckel fuer jede Bewegungsquelle (Maus/Automation UND
    // Vorbeiflug) - siehe Params::globalMaxSpeed. Steht darum IMMER sichtbar
    // (nicht in einem der beiden Reiter unten), zusammen mit dem Jitter -
    // beides gilt unabhaengig davon, welche Bewegungsart gerade angezeigt wird.
    Knob globalMaxSpeedKnob;

    // Additive Mikrobewegung der Quelle M ("echter Chorus" bei Stillstand,
    // @dpa 20260818) - hierher gewandert aus dem Feld-Panel (@dpa-Feedback:
    // "Jitter, Hektik, Jitter An sollen in Bewegung"), da sie eine Bewegung
    // sind, keine Feldgeometrie. Wie globalMaxSpeedKnob immer sichtbar.
    Knob srcJitterAmountKnob, srcJitterRateKnob;

    // Zweite Betriebsart des Wacklers (@dpa 20260821: "statt Jitter
    // Rotoren"): gleichmaessige Kreisbahn statt Zufallswackeln. Die beiden
    // Hauptregler bleiben dieselben - der Ausschlag wird zum Radius, "Hektik"
    // heisst dann "Speed". Nur diese beiden Regler kommen hinzu und sind
    // ausschliesslich im Rotoren-Modus sichtbar (Kompaktheit: im
    // Wackel-Modus stuenden sie ohne Wirkung herum).

    // Eigener Tempo-Deckel des Wacklers. Steht bei den Jitter-Reglern und
    // nicht beim Bahn-Deckel daneben, weil er zum Wackler gehoert - das war
    // vorher ein Regler fuer beides und hat den Wackler still abgewuergt.
    Knob srcJitterMaxSpeedKnob;


    // Ganz-Aus fuer den Jitter, ohne die Regler Jitter/Hektik zurueckzusetzen
    // - deren Wert bleibt erhalten, siehe updateJitterEnabledState().
    juce::ToggleButton srcJitterOnButton { "Jitter An" };
    std::unique_ptr<ButtonAttachment> srcJitterOnAttachment;

    // Graut Jitter/Hektik aus, solange srcJitterOnButton aus ist - auch nach
    // dem Laden eines Presets, nicht nur beim Klicken (siehe Konstruktor).
    void updateJitterEnabledState();

    // Reiter-Umschalter (@dpa-Feedback: "entweder Vorbeiflug ODER Record/Play
    // anzeigen, jew. mit allen Controls - spart Platz und bringt Uebersicht").
    // Klassisches Segmented-Control-Paar per setRadioGroupId(): klickbar wie
    // TextButton, aber nur einer der beiden bleibt gedrueckt.
    // Dauerschleife des Vorbeifluges (Params::flyLoop) - steht neben dem
    // Ausloeser, weil beides zusammen bedient wird.
    juce::ToggleButton flyLoopButton { "Loop" };
    std::unique_ptr<ButtonAttachment> flyLoopAttachment;

    juce::TextButton flyTabButton    { "Vorbeiflug" };
    juce::TextButton recordTabButton { "Record/Play" };
    void updateTabVisibility();

    // Vorbeiflug: Bahnart, Startvariante, Abstand, Tempo.
    juce::Label flyKindLabel;
    juce::ComboBox flyKindCombo;
    std::unique_ptr<ComboBoxAttachment> flyKindAttachment;

    juce::Label flyStartLabel;
    juce::ComboBox flyStartCombo;
    std::unique_ptr<ComboBoxAttachment> flyStartAttachment;

    Knob flyDistanceKnob, flyApproachKnob, flySpeedKnob;

    // Was den Knall-Start hoerbar macht (@dpa 20260824: "Soll wie der
    // Raketen-Stoss hoerbar sein, ist es aber nicht"). Beides stand vorher im
    // Feld/Physik-Panel - drei Panels entfernt von der Startvariante, die es
    // ausloest. Hier steht es neben seiner Ursache.
    Knob flyJumpBoomKnob;


    juce::TextButton flyButton { "Vorbeiflug" };

    juce::Label smootherTypeLabel;
    juce::ComboBox smootherTypeCombo;
    std::unique_ptr<ComboBoxAttachment> smootherTypeAttachment;

    juce::Label playInterpLabel;
    juce::ComboBox playInterpCombo;
    std::unique_ptr<ComboBoxAttachment> playInterpAttachment;

    juce::ToggleButton playLoopButton { "Loop" };
    std::unique_ptr<ButtonAttachment> playLoopAttachment;

    juce::ToggleButton coastButton { "Nachlauf" };

    // Mausbewegung auf den Bildtakt legen, siehe
    // FieldComponent::setMouseFrameSmoothing().
    juce::ToggleButton mouseFrameButton { "Maus glatt" };

    juce::TextButton recordButton { "Record" };
    juce::TextButton playButton   { "Play" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MotionPanel)
};
