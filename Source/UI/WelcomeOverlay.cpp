#include "WelcomeOverlay.h"

// juce_StandaloneFilterWindow.h setzt AudioProcessor (juce_audio_processors)
// und AudioDeviceManager/AudioProcessorPlayer (juce_audio_utils) bereits als
// bekannt voraus, bindet sie selbst aber nicht ein - ohne diese beiden Header
// vorher bricht der Standalone-Header mit "undeclared identifier" ab.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

// Fuer den Standalone-Ladedialog (askUserToLoadState()) und die Erkennung,
// ob wir ueberhaupt als Standalone laufen (StandalonePluginHolder::getInstance()
// != nullptr). Dieser Header laesst sich unabhaengig vom Zielformat einbinden -
// er definiert nur eine Klasse, keine der enthaltenen Methoden wird hier
// aufgerufen, ausser den beiden statischen/instanzgebundenen, die wir
// tatsaechlich brauchen. Im VST3-/AU-Build liefert getInstance() schlicht
// nullptr, weil dort nie eine StandalonePluginHolder-Instanz entsteht.
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
    // Eigene ApplicationProperties nur fuer den einen "welcomeSeen"-Schalter.
    // Function-lokales static: legt die Storage-Parameter genau einmal fest,
    // bevor irgendein Zugriff stattfindet (ApplicationProperties liefert erst
    // nach setStorageParameters() eine echte PropertiesFile zurueck).
    juce::ApplicationProperties& welcomeProperties()
    {
        static juce::ApplicationProperties properties;
        static bool initialised = false;

        if (! initialised)
        {
            juce::PropertiesFile::Options options;
            options.applicationName     = "Dopplerfeld";
            options.filenameSuffix      = ".settings";
            options.folderName          = "Dopplerfeld";
            // Apple will Einstellungen inzwischen in "Application Support"
            // statt "Preferences" - siehe Kommentar in juce_PropertiesFile.h.
            options.osxLibrarySubFolder = "Application Support";

            properties.setStorageParameters (options);
            initialised = true;
        }

        return properties;
    }
}

bool WelcomeOverlay::hasBeenSeen()
{
    return welcomeProperties().getUserSettings()->getBoolValue ("welcomeSeen", false);
}

void WelcomeOverlay::forgetSeen()
{
    auto* settings = welcomeProperties().getUserSettings();

    settings->removeValue ("welcomeSeen");
    settings->saveIfNeeded();
}

void WelcomeOverlay::markAsSeen()
{
    auto* settings = welcomeProperties().getUserSettings();
    settings->setValue ("welcomeSeen", true);
    settings->saveIfNeeded();
}

WelcomeOverlay::WelcomeOverlay()
{
    // Faengt Klicks auf allem darunter ab, solange das Overlay sichtbar ist -
    // ohne setWantsKeyboardFocus haette Enter/Escape (s.u.) keinen Empfaenger.
    setWantsKeyboardFocus (true);

    titleLabel.setText ("Dopplerfeld", juce::dontSendNotification);
    // Eine Schrift mit Charakter statt der Systemvorgabe, aber nur solche, die
    // auf dem System auch wirklich liegen - JUCE faellt sonst wortlos auf die
    // Standardschrift zurueck, und dann sieht es aus wie ein Versehen. Die
    // Reihenfolge ist die Rangfolge; Helvetica Neue gibt es auf jedem Mac.
    const juce::StringArray schoeneSchriften { "Avenir Next", "Avenir", "Optima",
                                               "Helvetica Neue" };
    const juce::StringArray vorhanden = juce::Font::findAllTypefaceNames();

    juce::String anzeigeSchrift;

    for (const auto& name : schoeneSchriften)
        if (vorhanden.contains (name))
        {
            anzeigeSchrift = name;
            break;
        }

    auto schrift = [&anzeigeSchrift] (float hoehe, int stil)
    {
        return anzeigeSchrift.isNotEmpty()
             ? juce::Font (juce::FontOptions (anzeigeSchrift, hoehe, stil))
             : juce::Font (juce::FontOptions (hoehe, stil));
    };

    titleLabel.setFont (schrift (38.0f, juce::Font::plain));
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    creatorLabel.setText ("von DD, 3Dietrich und D.Pank", juce::dontSendNotification);
    creatorLabel.setFont (schrift (19.0f, juce::Font::plain));
    creatorLabel.setJustificationType (juce::Justification::centred);
    creatorLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb0b0b0));
    addAndMakeVisible (creatorLabel);

    // @dpas Text woertlich uebernommen, nur der Hinweis auf die States zeigt
    // jetzt ausdruecklich auf den Options-Knopf oben links statt auf ein Bild
    // ("s.B."), das es hier nicht gibt.
    // Der Text wird ausdruecklich als UTF-8 uebergeben. Ohne das liest JUCE die
    // Bytes eines Umlauts einzeln und zeigt "fÃ¼r" statt "für" - genau deshalb
    // schreibt der Rest des Projekts ae/oe/ue aus. Hier steht der Text vor dem
    // Benutzer, also gehoeren echte Umlaute hin.
    //
    // Umgebrochen wird NICHT von Hand: juce::Label bricht selbst an Wortgrenzen
    // um, sobald der Text breiter als sein Bereich ist. Feste Umbrueche landen
    // sonst mitten im Satz, sobald sich Breite oder Schriftgroesse aendern.
    bodyLabel.setText (
        juce::String::fromUTF8 (
            "Ein akustisches Dopplerfeld: eine Schallquelle bewegt sich durch den "
            "Raum, und du hörst, was davon an deinem Ohr ankommt.\n"
            "\n"
            "Gedacht ist es vor allem als eigenständiges Programm. Als Plugin läuft "
            "es ebenso, dort ist es in Version 0.2.0 aber noch nicht erprobt.\n"
            "\n"
            "Die Einstellungen sind noch etwas kryptisch. Am schnellsten kommst du "
            "über die mitgelieferten Presets hinein, die hier States heißen: zu "
            "finden über den Knopf Options oben links, unter \"Load a saved "
            "state...\"."),
        juce::dontSendNotification);
    bodyLabel.setFont (schrift (17.5f, juce::Font::plain));
    bodyLabel.setJustificationType (juce::Justification::topLeft);
    bodyLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd8d8d8));
    bodyLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (bodyLabel);

    okButton.setLookAndFeel (nullptr);
    okButton.onClick = [this] { okClicked(); };
    addAndMakeVisible (okButton);

    // Bewusst zurueckhaltend gestaltet: kleinere Schrift, kein Fuellton, nur
    // gedaempfte Beschriftung. Er erledigt etwas Endgueltiges und soll deshalb
    // nicht der Knopf sein, den man aus Versehen zuerst trifft.
    dontShowButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    dontShowButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff8a8a8a));
    dontShowButton.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    dontShowButton.onClick = [this] { dontShowClicked(); };
    addAndMakeVisible (dontShowButton);

    // "oeffne states" ergibt nur im Standalone einen Sinn - dort existiert der
    // Options-Knopf mit dem Ladedialog ueberhaupt. Im Plugin (VST3/AU) liefert
    // getInstance() nullptr, der Knopf wird dann gar nicht erst angelegt
    // (@dpa: "darf dann GAR NICHT erscheinen").
    if (juce::StandalonePluginHolder::getInstance() != nullptr)
    {
        openStatesButton.setButtonText (juce::String::fromUTF8 ("\xc3\xb6" "ffne states"));
        openStatesButton.onClick = [this] { openStatesClicked(); };
        addAndMakeVisible (openStatesButton);
    }

    // Erst jetzt, wo Text und Schrift feststehen: nachmessen, wie hoch der
    // Fliesstext wird, und die Karte danach richten. Eine feste Zahl haette
    // hier gestanden und beim naechsten Satz oder auf einem System mit anderer
    // Schrift wieder zu wenig Platz gelassen.
    bodyHeight = gemesseneTexthoehe (cardWidth - 2 * padding);

    cardHeight = 2 * padding
               + titleHeight + creatorHeight
               + gapAroundFigure + figureHeight + gapAroundFigure
               + bodyHeight
               + gapBeforeButtons + buttonHeight;
}

int WelcomeOverlay::gemesseneTexthoehe (int breite) const
{
    // Gemessen wird mit demselben Mittel, mit dem spaeter gezeichnet wird:
    // GlyphArrangement bricht an denselben Wortgrenzen um und benutzt denselben
    // Zeilenabstand wie das Label. Ohne Hoehenvorgabe umgebrochen ergibt sich
    // die volle Hoehe - genau die, die das Label bekommen muss, damit nichts
    // abgeschnitten wird.
    const juce::Font schrift = bodyLabel.getFont();

    juce::GlyphArrangement anordnung;
    anordnung.addJustifiedText (schrift, bodyLabel.getText(),
                                0.0f, schrift.getAscent(),
                                (float) breite, juce::Justification::left);

    const auto umriss = anordnung.getBoundingBox (0, -1, true);

    // Eine halbe Zeile Luft: die Unterlaengen der letzten Zeile zaehlen nur
    // mit, wenn dort tatsaechlich eine steht, und eine punktgenau passende
    // Hoehe kostet sonst gelegentlich die letzte Zeile.
    return (int) std::ceil (umriss.getBottom() + schrift.getHeight() * 0.5f);
}

juce::Rectangle<int> WelcomeOverlay::kartenFlaeche() const
{
    // Nie hoeher als der Editor: sonst stuenden Knopfreihe oder Titel ausserhalb
    // des Fensters. Was dann fehlt, holt sich resized() bei der Zeichnung.
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (cardWidth, getWidth()),
                                                   juce::jmin (cardHeight, getHeight()));
}

void WelcomeOverlay::paint (juce::Graphics& g)
{
    // Halbtransparenter Schleier ueber der gesamten Editorflaeche, damit klar
    // ist, dass darunter noch etwas liegt - kein reines Blackout.
    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    constexpr float cornerRadius = 4.0f;   // @dpa mag kleine Eck-Radien

    auto card = kartenFlaeche().toFloat();

    g.setColour (juce::Colour (0xff232323));   // etwas heller als der Editorhintergrund 0xff1a1a1a
    g.fillRoundedRectangle (card, cornerRadius);

    // Sanfter, kontrastarmer Rahmen (@dpa-Feedback: nicht wie Hover/e-Mode) -
    // dieselbe Machart wie das Settings-Fenster an anderer Stelle im Projekt.
    g.setColour (juce::Colour (0x22ffffff));
    g.drawRoundedRectangle (card, cornerRadius, 1.0f);

    drawDopplerFigure (g, figureArea.toFloat());
}

void WelcomeOverlay::drawDopplerFigure (juce::Graphics& g, juce::Rectangle<float> area) const
{
    // Das Bild zum Namen: eine Quelle, die nach rechts zieht, und ihre
    // Wellenfronten. Jede Front wurde an einem anderen Ort abgestrahlt und
    // waechst seither gleichmaessig weiter - vorne draengen sie sich deshalb
    // zusammen (hoeher), hinten ziehen sie sich auseinander (tiefer). Genau
    // das rechnet das Plugin, hier steht es still.
    //
    // Flach gekippt statt frontal: als Kreise waere es ein Zielscheiben-Symbol,
    // als Ellipsen liest es sich als Flaeche, ueber die etwas hinwegzieht.
    const float cx = area.getCentreX();
    const float cy = area.getCentreY();
    const float kippung = 0.42f;        // Hoehe zu Breite, also der Blickwinkel

    constexpr int fronts = 6;

    for (int i = fronts; i >= 1; --i)
    {
        // Alter der Front: die aelteste ist am weitesten gewachsen und wurde am
        // weitesten links abgestrahlt.
        const float alter  = (float) i / (float) fronts;
        const float radius = 24.0f + alter * 118.0f;
        const float quelleDamals = cx - alter * 66.0f;

        // Aeltere Fronten verblassen - sonst wird das Bild vorne dicht und
        // hinten genauso laut, und die Bewegungsrichtung geht verloren.
        const float deckkraft = 0.10f + 0.34f * (1.0f - alter);

        g.setColour (juce::Colour (0xff4ec9c9).withAlpha (deckkraft));
        g.drawEllipse (quelleDamals - radius, cy - radius * kippung,
                       radius * 2.0f, radius * 2.0f * kippung, 1.4f);
    }

    // Die Quelle selbst, dort wo sie JETZT ist: hell, damit klar ist, welcher
    // Punkt die Fronten ausgesendet hat. Gelb wie im Feld.
    const juce::Colour quellFarbe (0xffe8d44a);

    g.setColour (quellFarbe.withAlpha (0.25f));
    g.fillEllipse (cx - 9.0f, cy - 9.0f * kippung - 1.0f, 18.0f, 18.0f * kippung + 2.0f);

    g.setColour (quellFarbe);
    g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);

    // Bewegungsrichtung als kurzer Schweif hinter der Quelle - ohne ihn
    // koennte das Bild auch eine ruhende Quelle mit Ringen sein.
    juce::Path schweif;
    schweif.startNewSubPath (cx - 74.0f, cy);
    schweif.lineTo (cx - 16.0f, cy);

    g.setColour (quellFarbe.withAlpha (0.30f));
    g.strokePath (schweif, juce::PathStrokeType (1.6f));
}

void WelcomeOverlay::resized()
{
    auto content = kartenFlaeche().reduced (padding);

    titleLabel.setBounds (content.removeFromTop (titleHeight));
    creatorLabel.setBounds (content.removeFromTop (creatorHeight));

    // Von unten her aufgeteilt: Knopfreihe und Fliesstext bekommen ihren Platz
    // zuerst, die Zeichnung nimmt, was uebrig bleibt. Wird der Editor einmal zu
    // niedrig, schrumpft also das Bild - der Text bleibt vollstaendig, denn er
    // ist der Grund, warum dieses Fenster ueberhaupt aufgeht.
    auto buttonRow = content.removeFromBottom (buttonHeight);
    content.removeFromBottom (gapBeforeButtons);

    bodyLabel.setBounds (content.removeFromBottom (juce::jmin (bodyHeight, content.getHeight())));

    content.removeFromBottom (gapAroundFigure);
    content.removeFromTop (gapAroundFigure);
    figureArea = content;

    // Reihenfolge wie von @dpa vorgegeben: "[oeffne states] [OK]" - OK sitzt
    // deshalb rechts, oeffne states (falls vorhanden) direkt links daneben.
    // Links in derselben Reihe, weit weg von OK: der endgueltige Knopf soll
    // nicht direkt neben dem liegen, den man staendig drueckt.
    dontShowButton.setBounds (buttonRow.removeFromLeft (130));

    okButton.setBounds (buttonRow.removeFromRight (buttonWidth));

    if (openStatesButton.isVisible())
    {
        buttonRow.removeFromRight (10);
        openStatesButton.setBounds (buttonRow.removeFromRight (buttonWidth + 20));
    }
}

bool WelcomeOverlay::keyPressed (const juce::KeyPress& key)
{
    // Enter und Escape sind gleichwertig zum OK-Knopf (@dpa-Nachtrag) - beide
    // schliessen das Fenster ueber denselben Weg, damit "welcomeSeen" so
    // sicher gesetzt wird wie bei einem Klick.
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::escapeKey)
    {
        okClicked();
        return true;
    }

    return false;
}

void WelcomeOverlay::visibilityChanged()
{
    if (isVisible())
    {
        // Verzoegert, weil die Component beim Feuern von visibilityChanged()
        // noch nicht sicher auf dem Bildschirm/im Fokuszyklus haengt - erst
        // im naechsten Nachrichtendurchlauf ist grabKeyboardFocus() verlaesslich.
        juce::Component::SafePointer<WelcomeOverlay> safeThis (this);

        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr && safeThis->isVisible())
                safeThis->grabKeyboardFocus();
        });
    }
}

void WelcomeOverlay::okClicked()
{
    // Schliesst nur diese Sitzung. Das Fenster kommt beim naechsten Oeffnen
    // wieder, denn es ist der Hinweis auf die States, und der ist beim zweiten
    // Mal nicht weniger wert als beim ersten (@dpa 20260821: "das
    // Begruessungsfenster bei jedem Oeffnen anzeigen"). Dauerhaft loswerden
    // laesst es sich nur ueber "nicht mehr zeigen".
    setVisible (false);
}

void WelcomeOverlay::dontShowClicked()
{
    markAsSeen();
    setVisible (false);
}

void WelcomeOverlay::openStatesClicked()
{
    // Sichtbar (und damit klickbar) ist dieser Knopf nur, wenn der
    // Konstruktor bereits getInstance() != nullptr gesehen hat - keine
    // erneute Pruefung noetig.
    // Zuerst aus dem Weg, dann laden: wer hier klickt, hat den Hinweis
    // verstanden und will das geladene State sehen, nicht weiter den Hinweis
    // darauf. Geschlossen wird wie bei OK nur fuer diese Sitzung.
    setVisible (false);

    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        holder->askUserToLoadState();
}
