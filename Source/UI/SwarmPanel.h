#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
#include "Labels.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Regler fuer das "Schrot"-Muster: mehrere Klone der Quelle, alle mit voller
// Loeserphysik (@dpa: "nur echte Klones, alles andere weg, keine 'billigen',
// die bringen nichts" - eine billige Nachbildung gibt es deshalb nicht mehr).
//
// Dieses Panel traegt bewusst den Notaus: die Loeserlast waechst linear mit
// der Zahl der Klone, und wer hier die Zahl hochdreht, soll im selben
// Blickfeld einen Weg zurueck haben, ohne das Plugin neu zu laden.
//
// Den CPU-Balken zeigt dieses Panel NICHT (mehr) - er sitzt in der
// Statuszeile am unteren Fensterrand (PluginEditor::paint()). Die Auslastung
// ist die Warnung vor hoerbaren Aussetzern und muss deshalb sichtbar bleiben,
// egal ob dieses Panel gerade auf- oder zugeklappt ist.
class SwarmPanel : public juce::Component
{
public:
    explicit SwarmPanel (juce::AudioProcessorValueTreeState& apvts);
    ~SwarmPanel() override = default;

    void resized() override;

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

        // Der DEUTSCHE Beschriftungstext, so wie er im Quelltext steht.
        // Angezeigt wird daraus Labels::text() - im EN-Betrieb also die
        // Uebersetzung. Gemerkt wird er, damit ein Sprachwechsel die
        // Beschriftung mitnehmen kann (siehe refreshTooltips()).
        const char* labelSource = nullptr;

        Tooltips::Key tooltipKey = Tooltips::Key::CloneTotal;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID, const char* labelText,
                    Tooltips::Key tooltipKey);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    Knob totalKnob, spreadKnob;

    // Anteil der Hoehe an der Streuung. Haengt am SELBEN Parameter wie der
    // "Z-Anteil" im Bewegungs-Panel (Params::srcJitterZAmount) - @dpa
    // 20260826: "zwei mal den Control scheint zuviel, aber in beiden ist er
    // wichtig.. vielleicht gegenseitig ferngesteuert/gleich geschaltet?".
    // Zwei SliderAttachments auf denselben Parameter sind genau diese
    // Gleichschaltung, ohne dass irgendwer die beiden Regler von Hand
    // synchron halten muesste.
    Knob zAmountKnob;
    // Gain der Klone in dB, siehe Params::cloneRealLevel.
    Knob realLevelKnob;

    // Klon-Schwarm im Feld anzeigen, siehe FieldComponent::setShowClones().
    juce::ToggleButton showButton { "Zeigen" };

    juce::TextButton panicButton { "Notaus: minimale Konfiguration" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmPanel)
};
