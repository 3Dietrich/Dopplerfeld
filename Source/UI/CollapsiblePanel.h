#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Schlanke Klapp-Komponente fuer die Motor-/Sample-/Bewegungs-/Feld-Panels
// (Plan 3.13). Kein juce::ConcertinaPanel, weil der sein eigenes Layout fuer
// mehrere Panels gleichzeitig verwaltet - hier reicht ein einzelnes Panel,
// das Elternteil (PluginEditor) steckt mehrere davon selbst untereinander.
//
// WICHTIG - Anforderung an den Aufrufer: Diese Komponente aendert ihre
// eigene Groesse NICHT selbststaendig. Sie legt beim Aufklappen nur ihren
// Inhalt innerhalb der eigenen `getLocalBounds()` an; die Gesamthoehe (Header
// allein vs. Header+Inhalt) muss der Aufrufer selbst berechnen und per
// `setBounds()` setzen - z.B. im `resized()` des Editors, dort einmal pro
// Panel `isExpanded() ? headerHeight + contentHeight : headerHeight`
// aufsummieren, damit nachfolgende Panels beim Einklappen nach oben rutschen.
// `onExpandedChanged` feuert nach jedem Toggle, damit der Aufrufer sein
// eigenes `resized()` erneut anstossen kann.
class CollapsiblePanel : public juce::Component
{
public:
    // Hoehe des Headers in Pixeln - vom Aufrufer fuer die Hoehenberechnung
    // im eingeklappten Zustand benoetigt.
    static constexpr int headerHeight = 24;

    explicit CollapsiblePanel (const juce::String& title);
    ~CollapsiblePanel() override = default;

    // Setzt den Inhalt des Panels. Die Komponente wird NICHT uebernommen
    // (kein Besitz, kein delete) - Konvention wie bei addAndMakeVisible: der
    // Aufrufer bleibt Owner und muss die Lebensdauer selbst sicherstellen.
    // Ein erneuter Aufruf ersetzt den bisherigen Inhalt (der alte wird nur
    // als Kind entfernt, nicht geloescht).
    void setContent (juce::Component* content);

    bool isExpanded() const noexcept { return expanded; }
    void setExpanded (bool shouldBeExpanded);

    // Wird nach jedem Umschalten (Klick auf Header) aufgerufen - Signal an
    // den Aufrufer, das eigene Layout neu zu berechnen (siehe Klassenkommentar).
    std::function<void()> onExpandedChanged;

    void resized() override;

private:
    void updateHeaderText();

    juce::String panelTitle;
    juce::TextButton headerButton;
    juce::Component* contentComponent = nullptr;
    bool expanded = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollapsiblePanel)
};
