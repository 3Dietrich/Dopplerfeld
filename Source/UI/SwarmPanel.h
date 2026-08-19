#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Regler fuer das "Schrot"-Muster: mehrere Klone der Quelle, davon ein Teil mit
// voller Loeserphysik und der Rest als billige Nachbildung.
//
// Dieses Panel traegt bewusst auch den CPU-Balken und den Notaus. Das ist keine
// Willkuer, sondern der Grund, aus dem es die Regler ueberhaupt gibt: die
// Loeserlast waechst linear mit der Zahl der echten Klone, und @dpa will sehen,
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
    // Echtzeit-Budgets und was gerade tatsaechlich gerechnet wird. Beides
    // gehoert zusammen - die Zahl der echten Klone kann bei eingeschalteter
    // Automatik unter dem Regler liegen, und genau das soll ablesbar sein.
    void setLoad (float cpuPercent, int realClones, int cheapClones);

    // Notaus. Kein Parameter, weil er mehrere auf einmal zurueckstellt.
    std::function<void()> onPanic;

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
        Tooltips::Key tooltipKey = Tooltips::Key::CloneTotal;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID, const juce::String& labelText,
                    Tooltips::Key tooltipKey);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    Knob totalKnob, realKnob, spreadKnob, levelKnob;

    juce::ToggleButton autoButton { "Automatik" };
    std::unique_ptr<ButtonAttachment> autoAttachment;

    juce::TextButton panicButton { "Notaus: minimale Konfiguration" };

    // Bereich, in den paint() den CPU-Balken zeichnet.
    juce::Rectangle<int> meterArea;

    float cpuPercent = 0.0f;
    int   realCount  = 0;
    int   cheapCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmPanel)
};
