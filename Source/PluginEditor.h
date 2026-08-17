#pragma once

#include "PluginProcessor.h"

#include "UI/CollapsiblePanel.h"
#include "UI/EnginePanel.h"
#include "UI/FieldComponent.h"
#include "UI/FieldPanel.h"
#include "UI/MotionPanel.h"
#include "UI/SamplePanel.h"
#include "UI/SwarmPanel.h"
#include "UI/WallPanel.h"
#include "UI/ToggleableTooltipWindow.h"

#include "Util/FieldSnapshot.h"

// Oberfläche nach Plan 3.13: links das Feld mit M und L, rechts die
// einklappbaren Regler-Panels in einem Viewport.
//
// Der Timer holt den Anzeige-Snapshot aus dem Audiothread ab - der Editor
// greift auf keine Engine-Daten direkt zu, alles läuft über die
// doppelgepufferte Übergabe in DopplerEngine::fillSnapshot.
class DopplerfeldEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit DopplerfeldEditor (DopplerfeldProcessor&);
    ~DopplerfeldEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Holt den Anzeige-Snapshot aus dem Audiothread und fuehrt alle
    // Anzeigeelemente nach. Der Timer ruft genau das - herausgezogen, damit es
    // auch ohne laufende Nachrichtenschleife aufgerufen werden kann (der
    // Rauchtest in load_check zeichnet sonst einen genullten Snapshot und
    // prueft die Projektion der perspektivischen Ansicht praktisch nicht, weil
    // alle Punkte im Ursprung laegen).
    void refreshDisplay();

private:
    void timerCallback() override;

    // Stapelt die Panels im Viewport-Inhalt und setzt dessen Gesamthöhe.
    // CollapsiblePanel ändert seine eigene Größe nicht (siehe dortiger
    // Klassenkommentar), das gehört hierher.
    void layoutPanels();

    // Schreibt einen Wert in den Bereich des Parameters und meldet ihn dem
    // Host. Über die Range des Parameters statt mit eigenen Grenzen, damit
    // Feld-Drag und Regler nie auseinanderlaufen.
    void setParameter (const char* paramID, double value);

    // Feldgröße und Kanalstatus als Text unter dem Feld - das ist die
    // schnellste Kontrolle, ob die Physik läuft (Laufzeit pro Ohr, M_r,
    // Anzahl der Wurzelzweige).
    juce::String statusText() const;

    DopplerfeldProcessor& dopplerfeldProcessor;

    FieldComponent field;
    FieldSnapshot  snapshot;

    juce::Viewport  panelViewport;
    juce::Component panelHolder;

    CollapsiblePanel enginePanelBox { "Motor" };
    CollapsiblePanel samplePanelBox { "Sample" };
    CollapsiblePanel motionPanelBox { "Bewegung" };
    CollapsiblePanel fieldPanelBox  { "Feld / Physik / Ausgang" };
    CollapsiblePanel wallPanelBox   { "Reflexionen / Waende" };
    CollapsiblePanel swarmPanelBox  { "Schwarm / Klone" };

    EnginePanel enginePanel;
    SamplePanel samplePanel;
    MotionPanel motionPanel;
    FieldPanel  fieldPanel;
    WallPanel   wallPanel;
    SwarmPanel  swarmPanel;

    // Quellwahl ist kein Parameter (siehe DopplerfeldProcessor), deshalb ein
    // gewöhnlicher Knopf statt eines Attachments.
    juce::TextButton sourceButton;

    // @dpa-Feedback: Hilfehinweise für alle Regler, in den Einstellungen
    // abschaltbar. Die Regler selbst tragen ihren Tooltip-Text bereits
    // (setTooltip in den Panels) - hier sitzt nur der globale Schalter.
    ToggleableTooltipWindow tooltipWindow { this };
    juce::ToggleButton tooltipsButton { "Hilfehinweise" };

    // Umschalter Draufsicht <-> perspektivische Ansicht. Kein Parameter: das
    // ist eine Frage der Ansicht, nicht des Klangs, und gehoert damit nicht in
    // den gespeicherten Zustand des Hosts.
    juce::TextButton viewButton;

    // Inhaltshöhen der vier Panels: was ihr resized() an Reihen und Reglern
    // unterbringt. Steht hier, weil nur der Aufrufer die Gesamthöhe eines
    // CollapsiblePanel setzen kann.
    static constexpr int engineContentHeight = 608;
    static constexpr int sampleContentHeight = 220;
    static constexpr int motionContentHeight = 384;
    static constexpr int fieldContentHeight  = 218;
    static constexpr int wallContentHeight   = 360;
    static constexpr int swarmContentHeight  = 226;

    // Das Feld bleibt exakt 700x400 (Plan 3.13). Die Größe ist nicht nur
    // Optik: FieldComponent rechnet die Feldhöhe in Metern aus seinem
    // Seitenverhältnis, und der Processor benutzt dafür dieselbe Konstante
    // (DopplerfeldProcessor::fieldAspect). Ein anderes Verhältnis würde
    // gezogene und gerechnete y-Position auseinanderlaufen lassen.
    static constexpr int fieldWidth  = 700;
    static constexpr int fieldHeight = 400;

    static constexpr int margin       = 10;
    static constexpr int topBarHeight = 26;
    static constexpr int statusHeight = 44;
    static constexpr int panelColumnWidth = 470;   // breitestes Panel (Sample) plus Scrollbalken

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopplerfeldEditor)
};
