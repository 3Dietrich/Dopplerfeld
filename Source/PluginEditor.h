#pragma once

#include "PluginProcessor.h"

#include "UI/CollapsiblePanel.h"
#include "UI/EngineControlPanel.h"
#include "UI/EnginePanel.h"
#include "UI/FieldComponent.h"
#include "UI/FieldPanel.h"
#include "UI/MotionPanel.h"
#include "UI/SamplePanel.h"
#include "UI/ScopeComponent.h"
#include "UI/SwarmPanel.h"
#include "UI/WallPanel.h"
#include "UI/ToggleableTooltipWindow.h"

#include "Util/FieldSnapshot.h"

#include <array>

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

    // Scope ein-/ausblenden (@dpa-Feedback: "wegschaltbar") - setzt die
    // Sichtbarkeit von Scope/Freeze/Sync und rechnet die Fenstergroesse neu,
    // weil der Scope-Block zwischen Feld und Statuszeile eigenen Platz
    // braucht (siehe scopeBlockHeight).
    void updateScopeVisibility();

    // Schreibt einen Wert in den Bereich des Parameters und meldet ihn dem
    // Host. Über die Range des Parameters statt mit eigenen Grenzen, damit
    // Feld-Drag und Regler nie auseinanderlaufen.
    void setParameter (const char* paramID, double value);

    // Statuszeile unter dem Feld: Tempo, L-M-Abstand, CPU-Last, Reflexions-
    // und Aufnahme/Wiedergabe-Status. Liest die gemittelten Werte aus
    // displayAverages, nicht den rohen 30Hz-Snapshot (s. updateDisplayAverages()).
    juce::String statusText() const;

    // @dpa-Feedback ("Langsamkeit der Anzeigewahrnehmung", 20260818): Tempo,
    // L-M-Abstand und CPU-Last werden ueber ein 0.5s-Fenster gemittelt und nur
    // alle 0.5s aktualisiert, statt bei jedem 33ms-Tick den zappelnden Rohwert
    // zu zeigen - eine wechselnde Ziffernzahl (z.B. 0,0 -> 1013,7) sonst
    // "blinkert" bei jedem Tick, egal wie fest die Zeichenbreite ist. Die
    // Feldgrafik (Position, Wellenfronten) bleibt unabhaengig davon bei vollen
    // 30Hz, nur die Zahlen-Anzeigen (Statuszeile, Cockpit-HUD) sind betroffen.
    void updateDisplayAverages();

    struct DisplayAverages
    {
        double speedMps          = 0.0;
        double speedOfSoundMps   = 343.2;
        double listenerDistanceM = 0.0;
        double cpuPercent        = 0.0;
    };

    DisplayAverages displayAverages;

    // Laufende Summen fuers aktuelle Mittelungsfenster, s. updateDisplayAverages().
    struct DisplayAccumulator
    {
        double speedSum            = 0.0;
        double speedOfSoundSum     = 0.0;
        double listenerDistanceSum = 0.0;
        double cpuSum              = 0.0;
        int    sampleCount         = 0;
        double elapsedMs           = 0.0;
    };

    DisplayAccumulator displayAccumulator;

    static constexpr double displayAverageWindowMs = 500.0;

    DopplerfeldProcessor& dopplerfeldProcessor;

    FieldComponent field;
    FieldSnapshot  snapshot;

    // Oszilloskop (@dpa-Feedback: "gross genug zum analysieren, wegschaltbar
    // mit Freeze und Sync"). Sitzt unter dem Feld, volle Feldbreite. Der
    // Roh-Puffer wird bei jedem Timer-Tick frisch aus dem Processor gezogen
    // und an die Komponente gereicht (siehe refreshDisplay()) - Freeze/Sync
    // selbst leben in ScopeComponent, s. dortigen Klassenkommentar.
    ScopeComponent scope;
    bool scopeVisible = true;

    juce::TextButton scopeToggleButton;
    juce::TextButton scopeFreezeButton;
    juce::TextButton scopeSyncButton;

    // Zwischenspeicher fuer das Rohfenster aus dem Processor - Mitgliedsvariable
    // statt Stack-Array im Timer, damit pro Tick nicht neu allokiert wird.
    std::array<float, ScopeComponent::captureWindowSamples> scopeRawLeft {};
    std::array<float, ScopeComponent::captureWindowSamples> scopeRawRight {};

    juce::Viewport  panelViewport;
    juce::Component panelHolder;

    CollapsiblePanel engineControlPanelBox { "Motorsteuerung" };
    CollapsiblePanel enginePanelBox { "Motor" };
    CollapsiblePanel samplePanelBox { "Sample" };
    CollapsiblePanel motionPanelBox { "Bewegung" };
    CollapsiblePanel fieldPanelBox  { "Feld / Physik / Ausgang" };
    CollapsiblePanel wallPanelBox   { "Reflexionen / Waende" };
    CollapsiblePanel swarmPanelBox  { "Schwarm / Klone" };

    EngineControlPanel engineControlPanel;
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

    // Tempo-Anzeige in der Statuszeile UND im Cockpit-Display im Feld
    // (@dpa-Feedback), Einheit umschaltbar. Ebenfalls kein Parameter - reine
    // Anzeigefrage wie die Ansicht oben. Enum lebt in FieldComponent (das
    // Cockpit-Display braucht sie dort fuer formatSpeed()), hier nur ein
    // Alias statt einer zweiten Definition.
    using SpeedUnit = FieldComponent::SpeedUnit;
    SpeedUnit speedUnit = SpeedUnit::KmH;
    juce::TextButton speedUnitButton;

    // "Audiomotor neu anlassen" (@dpa-Feedback): allgemeiner Reset der
    // Engine, nicht nur des Schwarms (siehe SwarmPanel fuer dessen Notaus).
    // In der Kopfzeile statt in einem einklappbaren Panel, weil er genau
    // dann gebraucht wird, wenn der Ton schon weg ist - auffindbar muss er
    // sein, ohne erst ein Panel aufzuklappen.
    juce::TextButton engineResetButton;

    // Inhaltshöhen der vier Panels: was ihr resized() an Reihen und Reglern
    // unterbringt. Steht hier, weil nur der Aufrufer die Gesamthöhe eines
    // CollapsiblePanel setzen kann.
    static constexpr int engineControlContentHeight = 130;   // Reihe (RPM, Imbalance) + Gate-Schalter
    static constexpr int engineContentHeight = 520;         // 608 minus die ausgelagerte RPM-Reihe
    static constexpr int sampleContentHeight = 220;
    static constexpr int motionContentHeight = 384;
    static constexpr int fieldContentHeight  = 306;   // 218 + dritte Reihe (M-Jitter)
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

    // Scope-Block zwischen Feld und Statuszeile: Toolbar (Freeze/Sync) +
    // Scope-Flaeche, volle Feldbreite. scopeBlockHeight ist der Platz, den
    // updateScopeVisibility() der Fensterhoehe hinzufuegt/entzieht.
    static constexpr int scopeToolbarHeight = 26;
    static constexpr int scopeHeight        = 220;
    static constexpr int scopeBlockHeight   = 6 + scopeToolbarHeight + 4 + scopeHeight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopplerfeldEditor)
};
