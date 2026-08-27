#include "PresetBar.h"

namespace
{
// Dieselben Angaben wie in FieldComponent/WelcomeOverlay - eine
// Einstellungsdatei fuer alle Merkposten der Oberflaeche.
juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "Dopplerfeld";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Dopplerfeld";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

constexpr const char* folderKey = "presetFolder";

// Was als Zustand in Frage kommt. Kein Filter nach Endung: die vorhandenen
// Presets haben gar keine (die Standalone-App schreibt den Namen so, wie er
// eingetippt wurde). Versteckte Dateien und Ordner bleiben draussen, alles
// andere entscheidet sich beim Laden - ein Block ohne die Kennung von
// copyXmlToBinary() wird abgelehnt, nicht eingespielt.
bool looksLikePreset (const juce::File& f)
{
    return f.existsAsFile() && ! f.isHidden() && f.getSize() > 8;
}
}

PresetBar::PresetBar()
{
    folder = storedFolder();

    if (! folder.isDirectory())
        folder = defaultFolder();

    list.setTextWhenNothingSelected (Labels::text ("kein Zustand gewählt"));
    list.setTextWhenNoChoicesAvailable (Labels::text ("Ordner ist leer"));

    list.onChange = [this]
    {
        // Auswahl heisst laden. Genau das ist der Punkt der Zeile: einmal
        // klicken statt Menue, Dialog, Warten.
        if (list.getSelectedId() > 0)
            loadSelected();
    };

    // Der Inhalt dieser Liste sind DATEINAMEN, keine Beschriftungen: sie
    // werden nicht uebersetzt und duerfen im EN-Betrieb deutsch sein und
    // Umlaute tragen. Die Sprachpruefung in Tests/load_check.cpp laeuft ueber
    // jeden sichtbaren Text im Editor und muss das auslassen koennen - die
    // Kennung sagt ihr, dass hier Benutzerdaten stehen.
    list.setComponentID (userTextComponentId);
    status.setComponentID (userTextComponentId);

    addAndMakeVisible (list);

    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step ( 1); };

    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    saveButton.onClick   = [this] { saveOverSelected(); };
    saveAsButton.onClick = [this] { saveAsNew(); };
    folderButton.onClick = [this] { chooseFolder(); };

    addAndMakeVisible (saveButton);
    addAndMakeVisible (saveAsButton);
    addAndMakeVisible (folderButton);

    status.setJustificationType (juce::Justification::centredLeft);
    status.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (status);

    refreshTexts();
    refreshList();
}

juce::File PresetBar::defaultFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("Dopplerfeld");

    if (! f.isDirectory())
        f.createDirectory();

    return f;
}

juce::File PresetBar::storedFolder()
{
    juce::ApplicationProperties properties;
    properties.setStorageParameters (settingsOptions());

    if (auto* p = properties.getUserSettings())
        return juce::File (p->getValue (folderKey));

    return {};
}

void PresetBar::storeFolder (const juce::File& f)
{
    juce::ApplicationProperties properties;
    properties.setStorageParameters (settingsOptions());

    if (auto* p = properties.getUserSettings())
    {
        p->setValue (folderKey, f.getFullPathName());
        p->saveIfNeeded();
    }
}

void PresetBar::refreshTexts()
{
    saveButton.setButtonText   (Labels::text ("Sichern"));
    saveAsButton.setButtonText (Labels::text ("Neu..."));
    folderButton.setButtonText (Labels::text ("Ordner..."));

    list.setTooltip         (Tooltips::text (Tooltips::Key::PresetList));
    prevButton.setTooltip   (Tooltips::text (Tooltips::Key::PresetStep));
    nextButton.setTooltip   (Tooltips::text (Tooltips::Key::PresetStep));
    saveButton.setTooltip   (Tooltips::text (Tooltips::Key::PresetSave));
    saveAsButton.setTooltip (Tooltips::text (Tooltips::Key::PresetSaveAs));
    folderButton.setTooltip (Tooltips::text (Tooltips::Key::PresetFolder));
}

void PresetBar::refreshList()
{
    const int wasSelected = list.getSelectedId();
    const juce::String wasName = wasSelected > 0 && wasSelected <= files.size()
                               ? files[wasSelected - 1].getFileName()
                               : juce::String();

    files.clear();
    list.clear (juce::dontSendNotification);

    if (folder.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*",
                                                                juce::File::findFiles))
            if (looksLikePreset (entry.getFile()))
                files.add (entry.getFile());

    // Nach Namen, damit die Pfeile eine nachvollziehbare Reihenfolge haben
    // und nicht die des Dateisystems.
    std::sort (files.begin(), files.end(),
               [] (const juce::File& a, const juce::File& b)
               {
                   return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
               });

    for (int i = 0; i < files.size(); ++i)
        list.addItem (files[i].getFileName(), i + 1);

    // Dieselbe Datei wieder auswaehlen, wenn es sie noch gibt - ohne zu
    // laden, denn geladen ist sie ja bereits.
    if (wasName.isNotEmpty())
        for (int i = 0; i < files.size(); ++i)
            if (files[i].getFileName() == wasName)
            {
                list.setSelectedId (i + 1, juce::dontSendNotification);
                break;
            }
}

void PresetBar::step (int delta)
{
    if (files.isEmpty())
        return;

    // Ohne Auswahl faengt der Vorwaertsschritt beim ersten an, der
    // Rueckwaertsschritt beim letzten.
    const int current = list.getSelectedId();
    int next = current > 0 ? current + delta
                           : (delta > 0 ? 1 : files.size());

    next = juce::jlimit (1, files.size(), next);

    if (next != current)
        list.setSelectedId (next);   // loest onChange und damit das Laden aus
}

void PresetBar::loadSelected()
{
    const int index = list.getSelectedId() - 1;

    if (index < 0 || index >= files.size() || ! onLoad)
        return;

    const juce::File f = files[index];

    if (onLoad (f))
        say (f.getFileName());
    else
        say (Labels::text ("kein lesbarer Zustand") + ": " + f.getFileName(), true);
}

void PresetBar::saveOverSelected()
{
    const int index = list.getSelectedId() - 1;

    if (! onSave)
        return;

    if (index < 0 || index >= files.size())
    {
        // Nichts gewaehlt: dann ist "Sichern" dasselbe wie "Neu..." - besser
        // als eine Fehlermeldung, die nur sagt, was man haette tun sollen.
        saveAsNew();
        return;
    }

    const juce::File f = files[index];
    const juce::String problem = onSave (f);

    if (problem.isEmpty())
        say (Labels::text ("gesichert") + ": " + f.getFileName());
    else
        say (problem + ": " + f.getFileName(), true);
}

void PresetBar::saveAsNew()
{
    if (! onSave)
        return;

    // Bewusst KEIN Dateidialog, sondern nur die Frage nach dem Namen: der
    // Ordner steht ja fest, und der native Dialog ist genau das Langsame,
    // worum es hier geht.
    auto* window = new juce::AlertWindow (Labels::text ("Zustand sichern"),
                                          Labels::text ("Name für den neuen Zustand:"),
                                          juce::MessageBoxIconType::NoIcon);

    const int index = list.getSelectedId() - 1;
    const juce::String suggestion = index >= 0 && index < files.size()
                                  ? files[index].getFileName()
                                  : juce::String ("Zustand");

    window->addTextEditor ("name", suggestion, {});
    window->addButton (Labels::text ("Sichern"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (Labels::text ("Abbrechen"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window] (int result)
        {
            const juce::String name = window->getTextEditorContents ("name").trim();
            std::unique_ptr<juce::AlertWindow> owned (window);

            if (result == 0 || name.isEmpty() || ! folder.isDirectory())
                return;

            const juce::File target = folder.getChildFile (juce::File::createLegalFileName (name));

            const juce::String problem = onSave ? onSave (target)
                                                : Labels::text ("konnte nicht schreiben");

            if (problem.isEmpty())
            {
                refreshList();

                for (int i = 0; i < files.size(); ++i)
                    if (files[i] == target)
                    {
                        list.setSelectedId (i + 1, juce::dontSendNotification);
                        break;
                    }

                say (Labels::text ("gesichert") + ": " + target.getFileName());
            }
            else
                say (problem + ": " + target.getFileName(), true);
        }), false);
}

void PresetBar::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> (Labels::text ("Ordner mit Zuständen"), folder);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const juce::File picked = fc.getResult();

                              if (! picked.isDirectory())
                                  return;

                              folder = picked;
                              storeFolder (folder);
                              refreshList();
                              say (folder.getFileName());
                          });
}

void PresetBar::say (const juce::String& message, bool isError)
{
    status.setColour (juce::Label::textColourId,
                      isError ? juce::Colours::orangered
                              : findColour (juce::Label::textColourId));
    status.setText (message, juce::dontSendNotification);
}

void PresetBar::paint (juce::Graphics&)
{
}

void PresetBar::resized()
{
    auto r = getLocalBounds();

    // Kompakt gehalten (Panel-Platz): die Liste bekommt den Rest, die Knoepfe
    // nur so viel, wie ihr Text braucht.
    prevButton.setBounds (r.removeFromLeft (26));
    r.removeFromLeft (2);
    nextButton.setBounds (r.removeFromLeft (26));
    r.removeFromLeft (6);
    list.setBounds (r.removeFromLeft (220));
    r.removeFromLeft (8);
    saveButton.setBounds (r.removeFromLeft (80));
    r.removeFromLeft (4);
    saveAsButton.setBounds (r.removeFromLeft (70));
    r.removeFromLeft (4);
    folderButton.setBounds (r.removeFromLeft (80));
    r.removeFromLeft (10);
    status.setBounds (r);
}
