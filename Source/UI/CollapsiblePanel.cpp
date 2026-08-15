#include "CollapsiblePanel.h"

CollapsiblePanel::CollapsiblePanel (const juce::String& title) : panelTitle (title)
{
    headerButton.onClick = [this] { setExpanded (! expanded); };
    addAndMakeVisible (headerButton);
    updateHeaderText();
}

void CollapsiblePanel::setContent (juce::Component* content)
{
    if (contentComponent != nullptr)
        removeChildComponent (contentComponent);

    contentComponent = content;

    if (contentComponent != nullptr)
    {
        addAndMakeVisible (contentComponent);
        contentComponent->setVisible (expanded);
    }

    resized();
}

void CollapsiblePanel::setExpanded (bool shouldBeExpanded)
{
    if (expanded == shouldBeExpanded)
        return;

    expanded = shouldBeExpanded;
    updateHeaderText();

    if (contentComponent != nullptr)
        contentComponent->setVisible (expanded);

    resized();

    // Nach resized() aufrufen: der Aufrufer soll die bereits aktualisierten
    // eigenen Bounds sehen koennen, falls er im Callback etwas davon abliest.
    if (onExpandedChanged != nullptr)
        onExpandedChanged();
}

void CollapsiblePanel::resized()
{
    auto area = getLocalBounds();
    headerButton.setBounds (area.removeFromTop (headerHeight));

    // Eigene Hoehe wird laut Klassenkommentar vom Aufrufer gesetzt; hier wird
    // nur das zur Verfuegung stehende Rechteck ausgefuellt. Im eingeklappten
    // Zustand ist `area` nach Abzug des Headers leer, sofern der Aufrufer die
    // Gesamthoehe korrekt auf headerHeight reduziert hat.
    if (contentComponent != nullptr && expanded)
        contentComponent->setBounds (area);
}

void CollapsiblePanel::updateHeaderText()
{
    // Unicode-Pfeil statt eigenem Icon-Asset - dreht Richtung je nach Zustand.
    const juce::String arrow = expanded ? juce::String::fromUTF8 ("\xE2\x96\xBC ") // ▼
                                         : juce::String::fromUTF8 ("\xE2\x96\xB6 "); // ▶
    headerButton.setButtonText (arrow + panelTitle);
}
