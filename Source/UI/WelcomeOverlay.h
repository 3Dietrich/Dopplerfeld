#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Begruessungsfenster (@dpa-Feedback 20260821): "Der User muss unbedingt von
// den Presets/States/Snapshots wissen" - liegt als LETZTES Kind im Editor
// ueber allem, nimmt die volle Editorflaeche ein und faengt damit Klicks
// darunter ab, bis es geschlossen wird. Erscheint nur beim allerersten Start
// (siehe hasBeenSeen()/markAsSeen()), danach nie wieder.
class WelcomeOverlay : public juce::Component
{
public:
    WelcomeOverlay();

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Enter, Escape und der OK-Knopf sind gleichwertige Wege, das Fenster zu
    // schliessen (@dpa-Nachtrag) - alle drei laufen ueber dieselbe okClicked().
    bool keyPressed (const juce::KeyPress& key) override;

    // Wird sichtbar gemacht, statt neu angelegt zu werden (siehe PluginEditor)
    // - beim Einblenden muss der Tastaturfokus aktiv geholt werden, sonst
    // kommen Enter/Escape nicht an. Verzoegert ueber MessageManager::callAsync,
    // weil die Component beim Aufruf von visibilityChanged() teils noch nicht
    // wirklich auf dem Bildschirm sichtbar ist.
    void visibilityChanged() override;

    // Dauerhafter Merker in einer eigenen PropertiesFile (User-Verzeichnis,
    // Schluessel "welcomeSeen") - gilt gleichermassen fuer Plugin und
    // Standalone, weil beide denselben Nutzer-Ordner benutzen.
    static bool hasBeenSeen();
    static void markAsSeen();

    // Setzt den Merker zurueck, das Fenster erscheint danach wieder. Fuer den
    // Testablauf gedacht, siehe tools/welcome-reset.sh.
    static void forgetSeen();

private:
    // Schliesst das Fenster und setzt "welcomeSeen" - der gemeinsame Weg fuer
    // Knopf, Enter und Escape.
    // Zeichnet die Wellenfronten einer bewegten Quelle: dieselbe Sache, die das
    // Plugin macht, als stehendes Bild.
    void drawDopplerFigure (juce::Graphics& g, juce::Rectangle<float> area) const;

    void okClicked();
    void dontShowClicked();

    // Oeffnet den Standalone-Ladedialog fuer States (nur erreichbar, wenn der
    // Knopf ueberhaupt sichtbar ist, siehe Konstruktor).
    void openStatesClicked();

    juce::Label titleLabel;
    juce::Label creatorLabel;

    // Von resized() gesetzt, von paint() bemalt.
    juce::Rectangle<int> figureArea;
    juce::Label bodyLabel;
    juce::TextButton okButton { "OK" };
    // Klein und zurueckhaltend: es ist der einzige Weg, das Fenster dauerhaft
    // loszuwerden, soll aber nicht mit OK um Aufmerksamkeit streiten.
    juce::TextButton dontShowButton { "nicht mehr zeigen" };
    juce::TextButton openStatesButton;   // Beschriftung im Konstruktor, siehe dort (UTF-8)

    // Kartengroesse - bewusst kompakt (@dpa: "nicht ausladend"), aber breit
    // genug fuer die drei Textabsaetze ohne haessliche Umbrueche.
    static constexpr int cardWidth   = 560;
    static constexpr int cardHeight  = 452;
    static constexpr int bodyHeight  = 148;

    // Hoehe der Doppler-Zeichnung zwischen Kopf und Text.
    static constexpr int figureHeight = 104;
    static constexpr int buttonHeight = 34;
    static constexpr int buttonWidth  = 104;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WelcomeOverlay)
};
