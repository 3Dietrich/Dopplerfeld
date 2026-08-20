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
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    // @dpas Text woertlich uebernommen, nur der Hinweis auf die States zeigt
    // jetzt ausdruecklich auf den Options-Knopf oben links statt auf ein Bild
    // ("s.B."), das es hier nicht gibt.
    bodyLabel.setText (
        "von DD, 3Dietrich und D.Pank\n"
        "\n"
        "Mehr fuer Standalone (als Plugin natuerlich auch, aber in v0.2.0 ungetestet).\n"
        "\n"
        "Die Einstellungen sind noch etwas cryptisch, deswegen empfehle ich, die\n"
        "Presets = States ueber den Options-Knopf oben links zu entdecken\n"
        "(\"Save current state...\" / \"Load a saved state...\").",
        juce::dontSendNotification);
    bodyLabel.setFont (juce::Font (juce::FontOptions (14.5f)));
    bodyLabel.setJustificationType (juce::Justification::topLeft);
    bodyLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd8d8d8));
    bodyLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (bodyLabel);

    okButton.onClick = [this] { okClicked(); };
    addAndMakeVisible (okButton);

    // "oeffne states" ergibt nur im Standalone einen Sinn - dort existiert der
    // Options-Knopf mit dem Ladedialog ueberhaupt. Im Plugin (VST3/AU) liefert
    // getInstance() nullptr, der Knopf wird dann gar nicht erst angelegt
    // (@dpa: "darf dann GAR NICHT erscheinen").
    if (juce::StandalonePluginHolder::getInstance() != nullptr)
    {
        openStatesButton.onClick = [this] { openStatesClicked(); };
        addAndMakeVisible (openStatesButton);
    }
}

void WelcomeOverlay::paint (juce::Graphics& g)
{
    // Halbtransparenter Schleier ueber der gesamten Editorflaeche, damit klar
    // ist, dass darunter noch etwas liegt - kein reines Blackout.
    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    constexpr float cornerRadius = 4.0f;   // @dpa mag kleine Eck-Radien

    auto card = getLocalBounds().withSizeKeepingCentre (cardWidth, cardHeight).toFloat();

    g.setColour (juce::Colour (0xff232323));   // etwas heller als der Editorhintergrund 0xff1a1a1a
    g.fillRoundedRectangle (card, cornerRadius);

    // Sanfter, kontrastarmer Rahmen (@dpa-Feedback: nicht wie Hover/e-Mode) -
    // dieselbe Machart wie das Settings-Fenster an anderer Stelle im Projekt.
    g.setColour (juce::Colour (0x22ffffff));
    g.drawRoundedRectangle (card, cornerRadius, 1.0f);
}

void WelcomeOverlay::resized()
{
    auto card = getLocalBounds().withSizeKeepingCentre (cardWidth, cardHeight);
    auto content = card.reduced (18);

    titleLabel.setBounds (content.removeFromTop (30));
    content.removeFromTop (6);
    bodyLabel.setBounds (content.removeFromTop (bodyHeight));
    content.removeFromTop (14);

    auto buttonRow = content.removeFromTop (buttonHeight);

    // Reihenfolge wie von @dpa vorgegeben: "[oeffne states] [OK]" - OK sitzt
    // deshalb rechts, oeffne states (falls vorhanden) direkt links daneben.
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
    markAsSeen();
    setVisible (false);
}

void WelcomeOverlay::openStatesClicked()
{
    // Sichtbar (und damit klickbar) ist dieser Knopf nur, wenn der
    // Konstruktor bereits getInstance() != nullptr gesehen hat - keine
    // erneute Pruefung noetig.
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        holder->askUserToLoadState();
}
