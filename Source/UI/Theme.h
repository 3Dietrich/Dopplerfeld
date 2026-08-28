#pragma once

#include <juce_graphics/juce_graphics.h>

// Farbwelt des Editors an EINER Stelle.
//
// Quelle: die Palette, die @dpa fuer seine eigenen Seiten und Bewerbungen
// nutzt (pankd.de/webseite/synths.html, dunkle Variante) - dunkler Grund,
// gedaempfte Flaechen, kontrastarme Linien, dazu ein kleiner Satz kraeftiger
// Akzentfarben, von denen jeder Bereich genau einen bekommt.
//
// Die Akzentfarbe faerbt nie die Flaeche selbst, sondern wird nur in sehr
// geringer Deckkraft ueber den Panelgrund gelegt (siehe panelBackground) -
// @dpa 20260828: "Nur getoente BGs". Rahmen bleiben kontrastarm, Radien klein.
namespace Theme
{
    // --- Grundflaechen ------------------------------------------------
    inline const juce::Colour editorBackground { 0xff1a1a1a }; // Grund des Editors
    inline const juce::Colour panel            { 0xff15161d }; // Panelflaeche
    inline const juce::Colour panelHeader      { 0xff1a1c25 }; // Kopfzeile eines Panels
    inline const juce::Colour text             { 0xffeef0f4 };
    inline const juce::Colour muted            { 0xffbec5d7 };

    // Kontrastarme Trennlinie - bewusst schwach (siehe Settings-Fenster).
    inline const juce::Colour line = juce::Colours::white.withAlpha (0.09f);

    // Trennlinie zwischen Reglergruppen INNERHALB eines Panels. Etwas
    // kraeftiger als der Panelrahmen, sonst verschwindet sie zwischen den
    // Reglern - aber immer noch weit unter dem Kontrast einer Beschriftung.
    inline const juce::Colour separator = juce::Colours::white.withAlpha (0.16f);

    // Ecken: klein halten, nicht rund.
    inline constexpr float cornerRadius = 3.0f;

    // --- Akzente ------------------------------------------------------
    inline const juce::Colour cyan      { 0xff52d3e6 };
    inline const juce::Colour pink      { 0xffef5fa6 };
    inline const juce::Colour amber     { 0xfff2a94e };
    inline const juce::Colour violet    { 0xff8f7ff0 };
    inline const juce::Colour mustard   { 0xffc99a4a };
    inline const juce::Colour tealgreen { 0xff55c99a };

    // Zuordnung der Bereiche zu Farben, an einer Stelle. Die Spalte laeuft von
    // oben nach unten durch den Signalweg, und die Farbe folgt ihm: warm, wo
    // der Klang entsteht, violett fuer die Bewegung, kuehl fuer alles, was mit
    // der Ausbreitung im Raum zu tun hat, ein eigener Ton fuer den Schwarm.
    // Motorsteuerung und Motor teilen sich bewusst denselben Ton.
    namespace Panel
    {
        inline const juce::Colour engineControl = amber;
        inline const juce::Colour engine        = amber;
        inline const juce::Colour sample        = mustard;
        inline const juce::Colour motion        = violet;
        inline const juce::Colour field         = cyan;
        inline const juce::Colour wall          = tealgreen;
        inline const juce::Colour swarm         = pink;
    }

    // Ein aktiver Reiter innerhalb eines Panels: derselbe Ton wie der Bereich,
    // nur kraeftig genug, dass man ihn von den unbenutzten Reitern daneben
    // unterscheidet.
    inline juce::Colour activeTab (juce::Colour accent)
    {
        return accent.withAlpha (0.32f);
    }

    // Panelgrund mit einem Hauch der Bereichsfarbe. Der Wert ist absichtlich
    // klein: die Flaeche soll sich unterscheiden lassen, ohne farbig zu wirken.
    inline juce::Colour panelBackground (juce::Colour accent)
    {
        return panel.overlaidWith (accent.withAlpha (0.05f));
    }

    // Kopfzeile: eine Spur kraeftiger als die Flaeche darunter, damit der
    // Bereichsanfang das Auge faengt.
    inline juce::Colour headerBackground (juce::Colour accent)
    {
        return panelHeader.overlaidWith (accent.withAlpha (0.11f));
    }

    // Titeltext in der Kopfzeile: die Bereichsfarbe, aber entsaettigt und
    // aufgehellt, damit sie neben dem Fliesstext nicht schreit.
    inline juce::Colour headerText (juce::Colour accent)
    {
        return accent.withMultipliedSaturation (0.35f).brighter (0.5f);
    }
}
