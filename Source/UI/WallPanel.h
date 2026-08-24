#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
#include "Tooltips.h"
#include "Labels.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Regler für die frei platzierbaren Wände (unendliche Ebenen) und den
// Notausschalter für alle Reflexionen. Gedacht als Inhalt eines
// CollapsiblePanel.
//
// Zwei feste Plätze statt einer dynamischen Liste: die Pfade müssen im
// Audiothread allokationsfrei bereitliegen, und jede eingeschaltete Fläche
// kostet ein weiteres Pfadpaar Löserlast. Der Aufbau je Wand ist bewusst
// derselbe wie beim Boden - Schalter plus Dämpfung, hier zusätzlich die Lage.
class WallPanel : public juce::Component
{
public:
    explicit WallPanel (juce::AudioProcessorValueTreeState& apvts);
    ~WallPanel() override = default;

    void resized() override;

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

        // Der DEUTSCHE Beschriftungstext, so wie er im Quelltext steht.
        // Angezeigt wird daraus Labels::text() - im EN-Betrieb also die
        // Uebersetzung. Gemerkt wird er, damit ein Sprachwechsel die
        // Beschriftung mitnehmen kann (siehe refreshTooltips()).
        const char* labelSource = nullptr;

        // Anhang hinter der Beschriftung, der NICHT uebersetzt wird - die
        // laufende Nummer einer Wand oder eines Teiltons.
        juce::String labelSuffix;

        Tooltips::Key tooltipKey = Tooltips::Key::WallX;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID, const char* labelText,
                    Tooltips::Key tooltipKey, const juce::String& labelSuffix = {});
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    // Eine Wand: Schalter plus fünf Regler.
    struct Wall
    {
        juce::ToggleButton onButton;
        std::unique_ptr<ButtonAttachment> onAttachment;

        Knob x, y, angle, tilt, damp, gain;
    };

    static constexpr int wallCount = 2;

    Wall walls[wallCount];

    // Mehrfachreflexion: gilt fuer alle Flaechen zusammen, deshalb nicht je
    // Wand, sondern einmal unten.
    juce::ToggleButton secondOrderButton { "Mehrfachreflexion" };
    std::unique_ptr<ButtonAttachment> secondOrderAttachment;
    Knob bounceGainKnob;
    Knob bounceGainBoostKnob;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WallPanel)
};
