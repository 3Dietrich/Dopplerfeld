#pragma once

#include "Labels.h"
#include "Theme.h"
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

    // Luft zwischen Panelrand und Inhalt. Ohne sie kleben die Regler an der
    // Kante, und die Bereiche laufen optisch ineinander. Der Aufrufer muss
    // contentPaddingBottom in seine Hoehenrechnung aufnehmen (siehe
    // DopplerfeldEditor::layoutPanels), sonst fehlt dem Inhalt unten Platz.
    static constexpr int contentPaddingX      = 5;
    static constexpr int contentPaddingBottom = 5;

    // Der Titel steht als DEUTSCHER Quelltext hier drin und wird ueber
    // Labels::text() angezeigt - genau wie jede andere Beschriftung.
    // Gemerkt wird er, damit refreshTitle() ihn beim Sprachwechsel neu
    // setzen kann.
    explicit CollapsiblePanel (const char* title);

    // Nach einem Sprachwechsel aufzurufen (siehe PluginEditor).
    void refreshTitle() { updateHeaderText(); }
    ~CollapsiblePanel() override;

    // Bereichsfarbe dieses Panels (siehe Theme.h). Sie faerbt nur die
    // Kopfzeile und - sehr schwach - die Flaeche darunter, damit sich die
    // sieben Panels der rechten Spalte auseinanderhalten lassen, ohne dass
    // die Spalte bunt wird. Panels, die inhaltlich zusammengehoeren, teilen
    // sich bewusst dieselbe Farbe.
    void setAccentColour (juce::Colour newAccent);

    // Setzt den Inhalt des Panels. Die Komponente wird NICHT uebernommen
    // (kein Besitz, kein delete) - Konvention wie bei addAndMakeVisible: der
    // Aufrufer bleibt Owner und muss die Lebensdauer selbst sicherstellen.
    // Ein erneuter Aufruf ersetzt den bisherigen Inhalt (der alte wird nur
    // als Kind entfernt, nicht geloescht).
    void setContent (juce::Component* content);

    // Eine kleine Bedienung RECHTS in der Kopfzeile, die auch im zugeklappten
    // Zustand sichtbar und bedienbar bleibt - gedacht fuer den einen Schalter,
    // den man erreichen will, ohne das Panel aufzuklappen.
    //
    // Sie wird als eigenes Kind gefuehrt und nicht in den Kopfknopf gelegt:
    // nur so faengt sie ihre eigenen Klicks ab, statt das Panel auf- und
    // zuzuklappen. Der Kopfknopf wird entsprechend schmaler.
    //
    // Kein Besitz, wie bei setContent().
    void setHeaderControl (juce::Component* control, int widthPx);

    bool isExpanded() const noexcept { return expanded; }
    void setExpanded (bool shouldBeExpanded);

    // Wird nach jedem Umschalten (Klick auf Header) aufgerufen - Signal an
    // den Aufrufer, das eigene Layout neu zu berechnen (siehe Klassenkommentar).
    std::function<void()> onExpandedChanged;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void updateHeaderText();
    void applyAccentToHeader();

    // Die Kopfzeile ist ein Knopf ueber die volle Breite. Der Hintergrund
    // wird in paint() des Panels gezeichnet, nicht vom Knopf - so bleiben
    // Rundung und Deckkraft an einer Stelle. Der Knopf steuert nur noch den
    // Text bei, und der steht links, wo das Auge ihn sucht.
    struct HeaderLookAndFeel : juce::LookAndFeel_V4
    {
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool, bool) override {}

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setColour (b.findColour (juce::TextButton::textColourOffId));
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (7),
                        juce::Justification::centredLeft, true);
        }
    };

    HeaderLookAndFeel headerLnf;

    const char* panelTitle = nullptr;
    juce::Colour accent = Theme::cyan;
    juce::TextButton headerButton;
    juce::Component* contentComponent = nullptr;

    juce::Component* headerControl      = nullptr;
    int              headerControlWidth = 0;

    // @dpa-Feedback: alle Panels starten zugeklappt, damit die Spalte beim
    // Oeffnen nicht sofort voll steht - der Aufrufer muss also NICHT mehr pro
    // Panel explizit setExpanded(false) rufen, nur noch fuer eine bewusste
    // Ausnahme setExpanded(true).
    bool expanded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollapsiblePanel)
};
