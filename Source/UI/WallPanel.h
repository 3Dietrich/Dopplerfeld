#pragma once

#include "../Params.h"
#include "RoundedSlider.h"
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

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        RoundedSlider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID, const juce::String& labelText,
                    const juce::String& tooltip);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    // Eine Wand: Schalter plus fünf Regler.
    struct Wall
    {
        juce::ToggleButton onButton;
        std::unique_ptr<ButtonAttachment> onAttachment;

        Knob x, y, angle, tilt, damp;
    };

    static constexpr int wallCount = 2;

    Wall walls[wallCount];

    // Mehrfachreflexion: gilt fuer alle Flaechen zusammen, deshalb nicht je
    // Wand, sondern einmal unten.
    juce::ToggleButton secondOrderButton { "Mehrfachreflexion" };
    std::unique_ptr<ButtonAttachment> secondOrderAttachment;
    Knob bounceGainKnob;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WallPanel)
};
