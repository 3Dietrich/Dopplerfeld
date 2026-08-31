#pragma once

#include "../Params.h"
#include "LevelMeter.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
#include "Theme.h"
#include "Labels.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
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

    // Zeichnet die Gruppenkoepfe (Raum / Luft / Boden / Knall / Ausgang) -
    // der Panelgrund kommt vom CollapsiblePanel.
    void paint (juce::Graphics& g) override;

    // Weiterleitung an das Levelmeter (@dpa-Feedback) - der Aufrufer
    // (Editor-Timer) kennt den Processor, dieses Panel nicht.
    void pushLevels (float peakLLinear, float peakRLinear, double callIntervalMs,
                     bool limiterActive = false)
    {
        levelMeter.pushLevels (peakLLinear, peakRLinear, callIntervalMs, limiterActive);
    }

    // Setzt die Tooltips aller Regler/Schalter dieses Panels neu, in der
    // aktuell an Tooltips::currentLanguage() gewaehlten Sprache - fuer den
    // Sprachumschalter in der Kopfzeile (siehe PluginEditor).
    void refreshTooltips();

private:
    // Ein Gruppenkopf: deutscher Text (wie jede Beschriftung, siehe Labels)
    // und die Zeile, in der er steht. In resized() gefuellt, in paint()
    // gezeichnet - als Komponenten waeren es fuenf weitere Kinder fuer je
    // eine Textzeile.
    struct GroupHeader
    {
        const char* title = nullptr;
        juce::Rectangle<int> bounds;
    };

    std::vector<GroupHeader> groupHeaders;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

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

        Tooltips::Key tooltipKey = Tooltips::Key::FieldSize;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID, const char* labelText,
                     Tooltips::Key tooltipKey);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    Knob fieldMetresKnob, boomLimitKnob, airAbsorbKnob, outputGainKnob;

    // Höhe über dem Boden. x/y stellt man mit der Maus im Feld ein, für z gibt
    // es dort keine Achse - deshalb sind das die einzigen Positionsregler.
    Knob srcZKnob, lisZKnob;

    // Höhendämpfung der Bodenreflexion - steht neben den z-Reglern, weil sie
    // ohne Höhenunterschied nichts zu tun hat.
    Knob groundDampKnob;

    // Pegel der Bodenreflexion, siehe Params::groundGain.
    Knob groundGainKnob;

    // Höhe über dem Meeresspiegel (Params::airAltitude) - steht bei den anderen
    // Höhenreglern dieser Reihe (Zeile aus Platzgruenden, siehe
    // FieldPanel::resized()), wirkt aber unabhaengig von ihnen ueber die
    // Luftdichte auf den Ausgangspegel (Physics/Medium.h).
    Knob airAltitudeKnob;

    // Druckwellen-/N-Wellen-Schicht: eigener Schalter und eigene Groesse,
    // bewusst neben (nicht in) Boom Limit - das eine ist eine Pulsform, das
    // andere eine reine Amplitudendeckelung. Steht (mit distanceCurveKnob und
    // panAmountKnob) in der dritten Reihe, wo Platz ist, weil Jitter/Hektik/
    // Jitter An im Bewegungs-Panel liegen (nicht hier) -
    // alle drei sind Amplituden-/Pegelthemen, keine Positionsregler wie Reihe 2.
    Knob nWaveSizeKnob;

    // Sperrzeit nach einem Knall (@dpa 20260830), steht in der Knall-Gruppe
    // neben der Fahne - beides betrifft, was NACH der Stossfront passiert.
    Knob boomHoldKnob;
    Knob nWaveGainKnob;

    // Vierte Reihe: die Form des Knalls und was nach ihm passiert. Die
    // Schaerfe seiner Stossfronten, der Pegel des zeitverkehrt gehoerten
    // Anteils und die Absenkung des uebrigen Schalls waehrend der Stossfront
    // samt ihrer Rueckkehrzeit.
    //
    // Die Schaerfe steht hier und nicht neben nWaveSizeKnob, obwohl sie
    // thematisch dort hingehoert: die dritte Reihe ist mit fuenf Reglern auf
    // der Breite der Panelspalte (PluginEditor::panelColumnWidth) voll.
    Knob nWaveEdgeKnob;

    // Staerke der Nulllinien-Auslenkung (Params::nWavePressure). Steht neben
    // der Knall-Kante, weil beide dieselbe Welle formen - die eine ihre
    // Fronten, die andere den Teil dazwischen.
    Knob nWavePressureKnob;
    Knob shockDuckRangeKnob;

    // Rollen nach dem Knall: Pegel gegenueber der Stossfront und Abklingzeit
    // (Params::rumbleGainDb, Params::rumbleSeconds). Steht in der Knall-Gruppe,
    // weil es der Nachhall genau dieser Stossfront ist und sonst nichts.
    Knob rumbleKnob;
    Knob rumbleTimeKnob;
    Knob rumbleEdgeLoKnob;
    Knob rumbleEdgeHiKnob;
    Knob rumbleToneKnob;

    // Kante eines Bewegungssprungs durchlassen statt interpolieren - der

    // Entfernung -> Amplitude schaerfer/flacher als das physikalische 1/R
    // (@dpa-Skizze "Amp-Verlauf"). Steht in der dritten Reihe, s. nWaveSizeKnob
    // oben - thematisch naeher an N-Wave/Panning (Amplitude) als an Reihe 2.
    Knob distanceCurveKnob;

    // Anteil des gewoehnlichen Pegel-Pannings (@dpa-Feedback) - steht in der
    // dritten Reihe, s. nWaveSizeKnob oben.
    Knob panAmountKnob;

    // Lufttemperatur (Params::airTempC) - bestimmt c(T) und damit die
    // Mach-Schwelle (Physics/Medium.h). Steht aus Platzgruenden in der
    // dritten Reihe (s. FieldPanel::resized()), thematisch aber Physik wie
    // boomLimitKnob/airAbsorbKnob, nicht Amplitude.
    Knob airTempKnob;

    // Neben Output Gain: -6dB-Marke, Clip-Anzeige mit 500ms-Halt (@dpa).
    LevelMeter levelMeter;

    juce::ToggleButton limiterOnButton  { "Limiter" };
    juce::ToggleButton groundReflectionButton { "Bodenreflexion" };
    juce::ToggleButton nWaveButton { "N-Welle" };

    std::unique_ptr<ButtonAttachment> limiterOnAttachment;
    std::unique_ptr<ButtonAttachment> groundReflectionAttachment;
    std::unique_ptr<ButtonAttachment> nWaveAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldPanel)
};
