#pragma once

#include "../Params.h"
#include "Labels.h"
#include "RoundedSlider.h"
#include "Theme.h"
#include "Tooltips.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

// Regler fuer die Abgriffpunkte: Orte im Feld, die hoeren, was dort ankommt,
// und es durch einen Hall schicken.
//
// Acht Punkte mit je elf Werten waeren als volle Tafel 88 Regler und damit
// unbedienbar. Stattdessen steht oben eine Reihe von acht Nummern, und darunter
// nur die Werte des GEWAEHLTEN Punktes. Das heisst, dass die Reglerbindungen
// beim Umschalten neu gesetzt werden - dafuer ist selectTap() da.
//
// Der Schalter "an" gehoert zum gewaehlten Punkt und nicht zur Nummernreihe:
// die Nummer waehlt aus, was man einstellt, der Schalter sagt, ob es laeuft.
// Beides in einen Knopf zu legen waere kuerzer und im Betrieb staendig
// missverstaendlich - ein Klick auf "5" wuerde dann Punkt 5 abschalten, statt
// ihn zu zeigen.
class ReverbPanel : public juce::Component
{
public:
    explicit ReverbPanel (juce::AudioProcessorValueTreeState& apvts);
    ~ReverbPanel() override = default;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void refreshTooltips();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        RoundedSlider slider;
        juce::Label   label;
        std::unique_ptr<SliderAttachment> attachment;

        const char*   labelSource = nullptr;
        Tooltips::Key tooltipKey  = Tooltips::Key::TapX;
    };

    void setupKnob (Knob& knob, const char* labelText, Tooltips::Key tooltipKey);
    void layoutKnob (Knob& knob, juce::Rectangle<int> cell);

    // Bindet alle Regler an die Parameter des gewaehlten Punktes. Die alten
    // Attachments werden dabei zuerst geloest: ein Attachment haelt eine
    // Rueckmeldung auf seinen Parameter, zwei gleichzeitig auf demselben Regler
    // wuerden sich gegenseitig ueberschreiben.
    void selectTap (int index);

    // Der Rahmen der Nummernknoepfe zeigt, welche Punkte laufen. Er wird
    // nachgezogen, wenn sich der An-Schalter aendert - auch dann, wenn die
    // Aenderung nicht von hier kommt, sondern aus einem geladenen Preset.
    void refreshRunningMarks();

    juce::AudioProcessorValueTreeState& state;

    static constexpr int tapCount = Params::tapCount;

    juce::TextButton selectButtons[tapCount];

    juce::ToggleButton onButton { "an" };
    std::unique_ptr<ButtonAttachment> onAttachment;

    juce::ComboBox typeBox;
    juce::Label    typeLabel;
    std::unique_ptr<ComboAttachment> typeAttachment;

    juce::ToggleButton predelayButton { "Vorlauf" };
    std::unique_ptr<ButtonAttachment> predelayAttachment;

    // X und Y stehen bewusst NICHT hier: der Ort wird im Feld gezogen
    // (@dpa 20260829: "x und Y braucht es nicht wenn man es so cool auf dem
    // Panel verschieben kann"). Die Hoehe bleibt, die hat im Feld keine Achse.
    Knob z, room, early, decay, damp, gain, width;

    // Nur fuer die Bauart Draussen. Bei den Raumbauarten steht die
    // Leitungszahl fest, dort sind sie ausgegraut statt versteckt: ein Regler,
    // der beim Umschalten verschwindet, laesst das Panel springen.
    Knob echoes, seed;

    void updateTypeDependentControls();

    // Direktschall: der einzige Regler hier, der NICHT zum gewaehlten Punkt
    // gehoert, sondern fuers ganze Plugin gilt. Er bekommt deshalb auch keine
    // Neubindung beim Umschalten.
    Knob direct;

    juce::TextButton copyButton  { "Kop" };
    juce::TextButton pasteButton { "Einf" };

    // Was ein Kopieren mitnimmt: alles ausser dem ORT. Zwei Punkte
    // uebereinander waeren beim Einfuegen die haeufigste Ueberraschung, und
    // kopiert werden soll der Hall, nicht die Stelle.
    struct Clipboard
    {
        bool   valid    = false;
        float  type     = 2.0f;
        float  room     = 30.0f;
        float  early    = 1.0f;
        float  decay    = 2.0f;
        float  damp     = 0.35f;
        float  gain     = -6.0f;
        float  width    = 1.0f;
        bool   predelay = true;
        float  echoes   = 24.0f;
        float  seed     = 137.0f;
    };

    Clipboard clipboard;

    void copyFromSelected();
    void pasteToSelected();

    int selected = 0;

    // Die Zeile der Nummernknoepfe samt der Stelle, ab der rechts daneben die
    // Linie laeuft - dieselbe Sprache wie in den anderen Panels.
    struct GroupRule { juce::Rectangle<int> row; int fromX = 0; };

    std::vector<GroupRule> groupRules;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbPanel)
};
