#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Regler fuer das "Schrot"-Muster: mehrere Klone der Quelle, alle mit voller
// Loeserphysik (@dpa: "nur echte Klones, alles andere weg, keine 'billigen',
// die bringen nichts" - eine billige Nachbildung gibt es deshalb nicht mehr).
//
// Dieses Panel traegt bewusst auch den CPU-Balken und den Notaus. Das ist keine
// Willkuer, sondern der Grund, aus dem es die Regler ueberhaupt gibt: die
// Loeserlast waechst linear mit der Zahl der Klone, und @dpa will sehen,
// was er sich einkauft, statt einen stillen Deckel zu bekommen. Wer hier die
// Zahl hochdreht, soll die Folge im selben Blickfeld haben - und einen Weg
// zurueck, ohne das Plugin neu zu laden.
class SwarmPanel : public juce::Component
{
public:
    explicit SwarmPanel (juce::AudioProcessorValueTreeState& apvts);
    ~SwarmPanel() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Vom Editor-Timer nachgefuehrt: Auslastung in Prozent des
    // Echtzeit-Budgets und was gerade tatsaechlich gerechnet wird.
    // cheapClones bleibt in der Signatur stehen, obwohl es die billige
    // Nachbildung nicht mehr gibt (@dpa: "nur echte Klones, alles andere
    // weg") - der Aufrufer in PluginEditor.cpp darf hier nicht angefasst
    // werden, er uebergibt seitdem schlicht 0.
    void setLoad (float cpuPercent, int realClones, int cheapClones, bool limiterActive);

    // Notaus. Kein Parameter, weil er mehrere auf einmal zurueckstellt.
    std::function<void()> onPanic;
    std::function<void (bool)> onShowClonesToggled;

    // Setzt die Tooltips aller Regler/Schalter dieses Panels neu, in der
    // aktuell an Tooltips::currentLanguage() gewaehlten Sprache - fuer den
    // Sprachumschalter in der Kopfzeile (siehe PluginEditor).
    void refreshTooltips();
    // Graut die abhaengigen Regler aus, solange die Gesamtzahl auf null steht.
    void updateEnabledState();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        RoundedSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
        Tooltips::Key tooltipKey = Tooltips::Key::CloneTotal;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID, const juce::String& labelText,
                    Tooltips::Key tooltipKey);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    Knob totalKnob, spreadKnob;
    // Gain der Klone in dB, siehe Params::cloneRealLevel.
    Knob realLevelKnob;

    // Klon-Schwarm im Feld anzeigen, siehe FieldComponent::setShowClones().
    juce::ToggleButton showButton { "Zeigen" };

    juce::TextButton panicButton { "Notaus: minimale Konfiguration" };

    // Bereich, in den paint() den CPU-Balken zeichnet.
    juce::Rectangle<int> meterArea;

    float cpuPercent = 0.0f;
    int   realCount  = 0;
    int   cheapCount = 0;

    // Ob der Begrenzer gerade eingreift, siehe paint().
    bool  limiting   = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmPanel)
};
