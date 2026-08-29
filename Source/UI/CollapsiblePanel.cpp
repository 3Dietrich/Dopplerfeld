#include "CollapsiblePanel.h"

CollapsiblePanel::CollapsiblePanel (const char* title) : panelTitle (title)
{
    headerButton.onClick = [this] { setExpanded (! expanded); };
    headerButton.setLookAndFeel (&headerLnf);
    addAndMakeVisible (headerButton);
    applyAccentToHeader();
    updateHeaderText();
}

CollapsiblePanel::~CollapsiblePanel()
{
    // Vor dem Abraeumen loesen: das LookAndFeel ist Member dieser Klasse und
    // darf nicht mehr angefasst werden, wenn der Knopf es noch kennt.
    headerButton.setLookAndFeel (nullptr);
}

void CollapsiblePanel::setAccentColour (juce::Colour newAccent)
{
    if (accent == newAccent)
        return;

    accent = newAccent;
    applyAccentToHeader();
    repaint();
}

void CollapsiblePanel::applyAccentToHeader()
{
    // Der Knopf malt keinen Hintergrund mehr (siehe HeaderLookAndFeel), nur
    // noch den Titel - der bekommt die Bereichsfarbe, gedaempft.
    headerButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    headerButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    headerButton.setColour (juce::TextButton::textColourOffId, Theme::headerText (accent));
    headerButton.setColour (juce::TextButton::textColourOnId, Theme::headerText (accent));
    headerButton.repaint();
}

void CollapsiblePanel::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // Flaeche: Panelgrund mit einem Hauch der Bereichsfarbe. Zugeklappt gibt
    // es keine Flaeche, dort steht nur die Kopfzeile.
    if (expanded)
    {
        g.setColour (Theme::panelBackground (accent));
        g.fillRoundedRectangle (area, Theme::cornerRadius);
    }

    // Kopfzeile: eine Spur kraeftiger, damit der Bereichsanfang auffaellt.
    // Oben gerundet, unten gerade - darum zweimal gemalt.
    auto header = area.withHeight ((float) headerHeight);

    g.setColour (Theme::headerBackground (accent));
    g.fillRoundedRectangle (header, Theme::cornerRadius);

    if (expanded)
        g.fillRect (header.withTop (header.getBottom() - Theme::cornerRadius));

    // Rahmen bewusst kontrastarm (siehe Theme::line).
    g.setColour (Theme::line);
    g.drawRoundedRectangle (area.reduced (0.5f), Theme::cornerRadius, 1.0f);
}

void CollapsiblePanel::setHeaderControl (juce::Component* control, int widthPx)
{
    if (headerControl != nullptr)
        removeChildComponent (headerControl);

    headerControl      = control;
    headerControlWidth = juce::jmax (0, widthPx);

    if (headerControl != nullptr)
        addAndMakeVisible (headerControl);

    resized();
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
    auto header = area.removeFromTop (headerHeight);

    // Die Kopfzeilen-Bedienung zuerst vom rechten Rand abziehen, damit der
    // Kopfknopf danach nur noch den Rest bekommt. Laege sie darueber, fiele
    // ihr Klick trotzdem an den Knopf und klappte das Panel um.
    if (headerControl != nullptr)
    {
        auto slot = header.removeFromRight (headerControlWidth + 6);
        slot.removeFromRight (6);

        headerControl->setBounds (slot.reduced (0, 3));
    }

    headerButton.setBounds (header);

    // Eigene Hoehe wird laut Klassenkommentar vom Aufrufer gesetzt; hier wird
    // nur das zur Verfuegung stehende Rechteck ausgefuellt. Im eingeklappten
    // Zustand ist `area` nach Abzug des Headers leer, sofern der Aufrufer die
    // Gesamthoehe korrekt auf headerHeight reduziert hat.
    if (contentComponent != nullptr && expanded)
        contentComponent->setBounds (area.reduced (contentPaddingX, 0)
                                         .withTrimmedBottom (contentPaddingBottom));
}

void CollapsiblePanel::updateHeaderText()
{
    // Unicode-Pfeil statt eigenem Icon-Asset - dreht Richtung je nach Zustand.
    const juce::String arrow = expanded ? juce::String::fromUTF8 ("\xE2\x96\xBC ") // ▼
                                         : juce::String::fromUTF8 ("\xE2\x96\xB6 "); // ▶
    headerButton.setButtonText (arrow + Labels::text (panelTitle));
}
