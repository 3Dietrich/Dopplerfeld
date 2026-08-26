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
#include "UI/Tooltips.h"
#include "UI/WelcomeOverlay.h"

#include "Util/FieldSnapshot.h"

#include <array>
#include <vector>

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

    // Nur fuer den Test: was der Einheitenschalter gerade zeigt, was im
    // Textfeld von Fly Speed steht, und ein Klick auf den Schalter. Ohne diese
    // drei Griffe laesst sich die Einheitenumschaltung nur mit einem echten
    // Fenster pruefen, und genau dieser Weg war schon einmal still kaputt.
    juce::String speedUnitLabelForTest() const { return speedUnitButton.getButtonText(); }
    juce::String flySpeedTextForTest() { return motionPanel.flySpeedTextForTest(); }

    // Nur fuer den Test: was nach einem Anzeige-Durchlauf wirklich im Feld
    // ankommt. Der Weg Processor -> Editor -> FieldComponent ist der, den das
    // Plugin geht; ihn direkt am Processor zu pruefen laesst genau die Stelle
    // aus, an der es klemmen kann.
    // Nur fuer den Test: das Feld allein als Bild, um nachzusehen, was
    // tatsaechlich gezeichnet wird.
    juce::Image fieldSnapshotImageForTest()
    {
        refreshDisplay();
        field.setBounds (0, 0, 900, 600);
        return field.createComponentSnapshot (field.getLocalBounds(), false);
    }

    int clonesInFieldForTest()
    {
        refreshDisplay();
        return field.clonePositionCountForTest();
    }
    void         cycleSpeedUnitForTest() { speedUnitButton.onClick(); }

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

    // Statuszeile unter dem Feld: Tempo, L-M-Abstand, Reflexions- und
    // Aufnahme/Wiedergabe-Status. Liest die gemittelten Werte aus
    // displayAverages, nicht den rohen 30Hz-Snapshot (s. updateDisplayAverages()).
    // Die CPU-Last steht NICHT mehr in diesem Text, sondern in der eigenen
    // Zeile direkt darueber (siehe cpuMeterBlockHeight, gezeichnet in
    // paint()) - eine einzelne Anzeige statt zweier, die durch
    // unterschiedliche Mittelung auseinanderlaufen koennten.
    juce::String statusText() const;

    // Zweite, kleinere Zeile unter der Statuszeile (@dpa-Feedback 20260819:
    // "ich kann mit m/s schlecht rechnen") - zeigt Fly Speed/Max Speed/Slew
    // Vmax (die drei Bewegungs-Regler in m/s) in der am speedUnitButton
    // gewaehlten Einheit, gekoppelt an denselben Schalter wie statusText()
    // und das Cockpit-Display im Feld. Die Regler selbst (MotionPanel) zeigen
    // weiterhin ihren m/s-Rohwert im eigenen Textfeld - diese Zeile ist die
    // gewaehlte Variante der Umrechnung, ohne MotionPanel anzufassen.

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

    // Knall-Trigger (@dpa 20260824: der Nulldurchgangs-Sync taugt fuer
    // periodische Signale, nicht fuer Knalle). Schalter plus Haltezeit -
    // beides gehoert in die Scope-Werkzeugleiste und nicht in die
    // Parameterliste: es ist eine Anzeige-Einstellung, kein Klang.
    juce::TextButton scopeEventButton;
    juce::ComboBox   scopeHoldCombo;

    // Zoom (@dpa-Feedback: "zoombar", brauchbar auch ohne verlaessliche
    // Scroll-/Pinch-Geste) - Klick-Knoepfe als garantiert funktionierender
    // Weg, zusaetzlich zu Mausrad/Pinch direkt auf dem Scope (siehe
    // ScopeComponent).
    juce::TextButton scopeZoomInButton  { "+" };
    juce::TextButton scopeZoomOutButton { "-" };

    // Sichtbaren Scope-Ausschnitt als WAV sichern (@dpa-Feedback: "fuer Dich,
    // debuggen" - erst als CSV gebaut, @dpa-Korrektur: "doch nicht als csv",
    // ein echtes Audioformat zum Abspielen). scopeSaveStatusUntilMs merkt
    // sich, bis wann der Bestaetigungstext im Label stehen bleibt (siehe
    // refreshDisplay()).
    juce::TextButton scopeSaveButton { "Speichern" };
    juce::Label      scopeSaveStatusLabel;
    juce::uint32     scopeSaveStatusUntilMs = 0;

    // Play-Toggle (@dpa: "Play an schalten: es spielt die Scopeansicht von
    // vorn bis hinten ab und geht dann auf null ... Klick startet an
    // bestimmten Stellen"). Der An/Aus-Zustand steht in ScopeComponent (wie
    // bei Freeze/Sync), dieser Knopf spiegelt ihn nur - siehe scopePlayButton
    // .onClick im Konstruktor: er schaltet BEIDES, scope.setPlaybackEnabled()
    // fuer Klick-Erkennung/Cursor UND dopplerfeldProcessor.
    // setScopePlaybackModeEnabled() fuer die eigentliche Audio-Ersetzung.
    juce::TextButton scopePlayButton;

    // Zwischenspeicher fuer das Rohfenster aus dem Processor - Mitgliedsvariable
    // statt Stack-Array im Timer. Groesse folgt der aktuellen Zoomstufe
    // (siehe ScopeComponent::captureWindowSampleCount()), deshalb ein Vektor
    // statt eines Arrays fester Groesse - wird nur bei Zoomaenderung neu
    // dimensioniert, nicht bei jedem Tick.
    std::vector<float> scopeRawLeft, scopeRawRight;

    // Letzte an den Scope gemeldete Samplerate (0 = noch keine gemeldet) -
    // nur bei Aenderung wird setMaxDisplaySampleCount() neu gerechnet, siehe
    // refreshDisplay().
    double lastKnownScopeSampleRate = 0.0;

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
// Hauptschalter ganz links, siehe Params::masterOn.
    juce::ToggleButton masterOnButton { "An" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> masterOnAttachment;

        juce::ToggleButton tooltipsButton { "Hilfehinweise" };

    // Sprache der Hilfehinweise (deutsch/englisch, siehe Tooltips.h) - der
    // Text selbst liegt zentral in Tooltips.h, hier nur der Umschalter.
    // setTooltip() speichert den Text einmalig im Component, ein spaeterer
    // Sprachwechsel muss ihn deshalb an jeder betroffenen Komponente neu
    // setzen - siehe refreshAllTooltips().
    juce::TextButton languageButton;
    void refreshAllTooltips();

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
    //
    // Oeffentlich, damit der load_check-Abschnitt "Bedienelemente" die Panels
    // mit GENAU den Massen pruefen kann, die sie im Editor bekommen. Mit
    // abgeschriebenen Zahlen liefe die Pruefung sonst irgendwann gegen eine
    // Groesse, die es nicht mehr gibt - und meldete dann entweder nichts oder
    // Falsches.
public:
    static constexpr int engineControlContentHeight = 115;   // Reihe (RPM, Imbalance) + Gate-Schalter
    // Das Motor-Panel fuehrt seine Hoehe selbst: sie haengt an der gewaehlten
    // Betriebsart, siehe EnginePanel::preferredContentHeight(). Hier steht
    // darum keine Konstante mehr.
    static constexpr int sampleContentHeight = 190;   // Dateizeile + zwei Reglerreihen
    // Reiter Vorbeiflug/Record-Play + eine gemeinsame Zeile fuer Tempo-Deckel
    // und Jitter, s. MotionPanel::resized(). Gerechnet mit den Reglermassen
    // des Motor-Panels (84 x 67): Reiterzeile 28 + 6, Reiterinhalt
    // 28 + 6 + 44 + 6 + 26 + 6 + 67 = 183, Abstand 6, gemeinsame Zeile 67,
    // dazu oben und unten je 8 Rand.
    static constexpr int motionContentHeight = 274;
    static constexpr int fieldContentHeight  = 352;   // vier Reglerreihen, die vierte ist Rueckwaerts/Front-Duck/Schatten (s. FieldPanel::resized())
    static constexpr int wallContentHeight   = 315;   // zwei Waende plus die gemeinsame Reflexionsreihe
    // 226 minus die 40px, die frueher fuer den CPU-Balken reserviert waren -
    // der sitzt jetzt in der eigenen Zeile am unteren Fensterrand statt hier
    // (siehe cpuMeterBlockHeight), das Panel braucht darum weniger Hoehe.
    static constexpr int swarmContentHeight  = 171;

    // Breite der Panelspalte - ebenfalls oeffentlich, aus demselben Grund wie
    // die Hoehen darueber.
    static constexpr int panelColumnWidth = 470;   // breitestes Panel (Sample) plus Scrollbalken
private:

    // Das Feld bleibt exakt 700x400 (Plan 3.13). Die Größe ist nicht nur
    // Optik: FieldComponent rechnet die Feldhöhe in Metern aus seinem
    // Seitenverhältnis, und der Processor benutzt dafür dieselbe Konstante
    // (DopplerfeldProcessor::fieldAspect). Ein anderes Verhältnis würde
    // gezogene und gerechnete y-Position auseinanderlaufen lassen.
    static constexpr int fieldWidth  = 700;
    static constexpr int fieldHeight = 400;

    static constexpr int margin       = 10;
    static constexpr int topBarHeight = 26;
    // Zeitbasis, mit der der Scope startet (@dpa: "default auf 10s").
    static constexpr double scopeDefaultSeconds = 10.0;

    // Loeserlast-Zeile direkt ueber der Statuszeile, ueber die gesamte
    // Fenster-Laufzeit hinweg sichtbar - unabhaengig davon, ob ein Panel
    // (insbesondere das Schwarm-Panel) auf- oder zugeklappt ist, denn sie ist
    // die Warnung vor hoerbaren Aussetzern (@dpa 20260820: "auch ohne Klone
    // immer sichtbar"). Frueher steckte dieser Balken in SwarmPanel::paint
    // und war darum unsichtbar, sobald das Panel zu war.
    static constexpr int cpuMeterBarHeight   = 8;
    static constexpr int cpuMeterLabelHeight = 16;
    // Balken + kleiner Abstand + Beschriftungszeile + Abstand zur Statuszeile darunter.
    static constexpr int cpuMeterBlockHeight = cpuMeterBarHeight + 2 + cpuMeterLabelHeight + 6;

    static constexpr int statusHeight = 44;

    // Scope-Block zwischen Feld und Statuszeile: Toolbar (Freeze/Sync) +
    // Scope-Flaeche, volle Feldbreite. scopeBlockHeight ist der Platz, den
    // updateScopeVisibility() der Fensterhoehe hinzufuegt/entzieht.
    static constexpr int scopeToolbarHeight = 26;
    static constexpr int scopeHeight        = 220;
    static constexpr int scopeBlockHeight   = 6 + scopeToolbarHeight + 4 + scopeHeight;

    // Begruessungsfenster (@dpa-Feedback 20260821) - als LETZTES Kind
    // angelegt (siehe Konstruktor) und in resized() ueber die volle
    // Editorflaeche gelegt, damit es wirklich alles darunter verdeckt und
    // dessen Klicks abfaengt. Bleibt bestehen (nur unsichtbar), wenn es schon
    // einmal gesehen wurde - WelcomeOverlay::hasBeenSeen() entscheidet beim
    // Start, ob es sichtbar startet.
    WelcomeOverlay welcomeOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopplerfeldEditor)
};
