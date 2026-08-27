#pragma once

#include "Labels.h"
#include "Tooltips.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Schmale Zeile zum Umschalten zwischen gespeicherten Zustaenden, ohne
// Dateidialog (@dpa 20260827: "immer wenn ich states/Snapshots lade oder
// speicher muss ich das kleine options clicken dann load oder save clicken,
// dann denkt es lange nach (bis zu 2s?) dann ist der Filedialog da .. gibt's
// fuer das schnelle 'laden, durchhoeren, anpassen, speichern' irgendwas
// fixeres?").
//
// Der langsame Weg ist der der Standalone-App: ihr Optionen-Menue oeffnet den
// nativen Dateidialog des Systems, und der braucht beim ersten Mal ueber eine
// Sekunde. Daran laesst sich von hier aus nichts aendern - wohl aber daran,
// dass man ihn ueberhaupt braucht.
//
// Die Dateien sind dieselben: was die Standalone-App als Zustand schreibt,
// ist der rohe Block aus AudioProcessor::getStateInformation(). Diese Zeile
// liest und schreibt genau den, die vorhandenen Presets bleiben also
// unveraendert brauchbar.
//
// Der Dateidialog kommt nur noch an einer Stelle vor: beim Waehlen des
// Ordners, und das einmal. Danach laeuft alles ueber die Liste und die zwei
// Pfeile - laden, durchhoeren, anpassen, sichern, ohne einen einzigen Dialog.
class PresetBar : public juce::Component
{
public:
    PresetBar();

    void resized() override;
    void paint (juce::Graphics&) override;

    // Der Editor haengt sich hier ein: laden heisst den Block der Datei an
    // setStateInformation() geben, sichern heisst getStateInformation() in
    // die Datei schreiben. Beides kennt diese Zeile nicht selbst - sie kennt
    // nur Dateien.
    std::function<bool (const juce::File&)> onLoad;

    // Leere Antwort heisst geschrieben, sonst steht darin, was dagegen sprach.
    // Kein blosses bool: "konnte nicht schreiben" waere die falsche Auskunft,
    // wenn in Wahrheit nur ein gerade geladener Zustand noch nicht durch ist.
    std::function<juce::String (const juce::File&)> onSave;

    // Nach einem Sprachwechsel: Beschriftungen und Hinweise neu setzen.
    void refreshTexts();

    // Ordnerinhalt neu einlesen (nach dem Sichern einer neuen Datei).
    void refreshList();

private:
    // Ordner merken, damit er nur einmal gewaehlt werden muss. Dieselbe
    // Einstellungsdatei wie fuer die uebrigen Oberflaechen-Merkposten (siehe
    // FieldComponent, WelcomeOverlay).
    static juce::File storedFolder();
    static void       storeFolder (const juce::File&);

    // Legt bei Bedarf den Standardordner an. Erst wenn @dpa "Ordner..."
    // benutzt, zeigt die Zeile woandershin.
    static juce::File defaultFolder();

    void chooseFolder();
    void step (int delta);
    void loadSelected();
    void saveOverSelected();
    void saveAsNew();
    void say (const juce::String& message, bool isError = false);

    juce::File              folder;
    juce::Array<juce::File> files;

    juce::ComboBox   list;
    juce::TextButton prevButton { "<" };
    juce::TextButton nextButton { ">" };
    juce::TextButton saveButton;
    juce::TextButton saveAsButton;
    juce::TextButton folderButton;
    juce::Label      status;

    // Der FileChooser muss den Aufruf ueberleben, siehe SamplePanel.
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
