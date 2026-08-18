#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

// Oszilloskop fuer den Ausgang (@dpa-Feedback, "Scope einbauen ... gross
// genug zum analysieren ... zoombar ... im freezed Scope frei herumsuchen").
// Zeitbereichs-Darstellung von L/R. Hat wie LevelMeter keinen eigenen Timer -
// der Aufrufer (Editor-Timer) pusht periodisch ein Rohfenster aus dem
// Ringpuffer (siehe ScopeRingBuffer), die Komponente entscheidet daraus
// selbst, was angezeigt wird (Freeze/Sync/Zoom/Pan leben deshalb hier, nicht
// im Editor).
//
// Zwei Betriebsarten:
//  - Live (Normalfall): feed() bekommt bei jedem Tick ein frisches
//    Rohfenster, zeigt entweder die juengste Haelfte oder (bei Sync) ein um
//    einen Trigger zentriertes Fenster. Kein Pan - "jetzt" aendert sich ja
//    laufend von selbst.
//  - History (nach Freeze, @dpa: "im freezed Scope frei herumsuchen"): der
//    Editor uebergibt einmalig die KOMPLETTE bisherige Ringpuffer-Historie
//    (siehe enterHistoryMode()), danach ist das Bild statisch und der
//    sichtbare Ausschnitt laesst sich per Wheel/Drag frei verschieben
//    (panBy()) und weiter zoomen, bis exitHistoryMode() zurueck in den
//    Live-Betrieb schaltet.
//
// Zoom laeuft in Samples, nicht in Sekunden - die Komponente kennt die
// Samplerate nicht (die haengt an dem, was gerade geladen ist, und aendert
// sich mit dem Host/Projekt). Der Editor rechnet die Samplerate-abhaengige
// Obergrenze um (siehe setMaxDisplaySampleCount()) und reicht ausserdem
// einen reinen Anzeige-Hinweis fuer die Zeit-Beschriftung durch
// (setSampleRateHint()) - beides bewusst getrennt vom DSP-Pfad.
class ScopeComponent : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    // Untere Zoom-Grenze in Samples, samplerate-unabhaengig - darunter waeren
    // nur noch ein paar Punkte zu sehen, das bringt nichts mehr.
    static constexpr int minDisplaySamples = 128;

    ScopeComponent();

    // Neues Rohfenster von genau captureWindowSampleCount() Samples
    // (chronologisch, aeltestes zuerst). Wird ignoriert, solange isFrozen()
    // (Live-Feed pausiert waehrend Freeze/History, s. Klassenkommentar) - die
    // zuletzt angezeigten Kurven bleiben stehen, wie am echten Geraet.
    void feed (const float* rawLeft, const float* rawRight);

    // Schaltet in den History-Modus: fullLeft/fullRight (Laenge fullLength,
    // chronologisch, aeltestes zuerst) ist die KOMPLETTE Ringpuffer-Historie
    // zum Freeze-Zeitpunkt. Sucht bei aktivem Sync einmalig einen Trigger
    // nahe dem Ende (wie im Live-Betrieb), sonst zeigt sie den juengsten
    // Ausschnitt - von da an frei verschiebbar per Wheel/Drag.
    void enterHistoryMode (const float* fullLeft, const float* fullRight, int fullLength);

    // Zurueck in den Live-Betrieb: naechster feed()-Aufruf zeigt wieder
    // frische Daten, die History-Kopie wird freigegeben.
    void exitHistoryMode();

    bool isFrozen() const { return frozen; }

    // Sync: sucht im Rohfenster (Live) bzw. beim Einfrieren einmalig in der
    // Historie einen steigenden Nulldurchgang von L nahe der gewuenschten
    // Stelle und richtet die Anzeige daran aus - der Trigger-Moment landet
    // dadurch in der Mitte des Scopes (@dpa-Vorgabe). Ohne Sync wird einfach
    // der juengste Ausschnitt gezeigt.
    void setSyncEnabled (bool shouldSync) { syncEnabled = shouldSync; }
    bool isSyncEnabled() const { return syncEnabled; }

    // Obere Zoom-Grenze in Samples (Editor: DopplerfeldProcessor::
    // scopeMaxDisplaySeconds * Samplerate). Klemmt den aktuellen Zoom mit,
    // falls der gerade darueber liegt (z.B. nach einem Samplerate-Wechsel
    // auf einen kleineren Wert).
    void setMaxDisplaySampleCount (int maxSamples);

    // Reiner Anzeige-Wert fuer die Zeit-Beschriftung in paint() - siehe
    // Klassenkommentar.
    void setSampleRateHint (double sr) { sampleRateHint = sr; }

    int displaySampleCount() const { return displaySamples; }
    int captureWindowSampleCount() const { return displaySamples * 2; }

    // Ein Zoom-Schritt, multiplikativ: factor < 1 verkuerzt die Zeitbasis
    // (reinzoomen), factor > 1 verlaengert sie (rauszoomen). Oeffentlich,
    // damit der Editor zusaetzliche +/- Knoepfe daran haengen kann - Wheel
    // und Pinch (unten) rufen intern dasselbe.
    void zoomStep (float factor);

    // Speichert den aktuell sichtbaren Ausschnitt (genau das, was gerade
    // gezeichnet wird - Live oder History) als CSV nach `file`: eine
    // Kommentarzeile mit Metadaten (Samplerate, Zeitfenster, Modus), dann
    // eine Zeile je Sample (Index, Zeit in ms relativ zum Fensteranfang, L,
    // R). @dpa-Feedback: "fuer Dich, debuggen" - reiner Text statt Audio-
    // Format, damit sich die Werte direkt lesen/greppen lassen.
    bool exportVisibleWindow (const juce::File& file) const;

    void paint (juce::Graphics& g) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // Pinch-Geste auf dem Trackpad (@dpa-Feedback: "fuer Mac mit Touchpad") -
    // die naheliegendste Zoom-Geste auf dem Mac, unabhaengig davon, ob/wie
    // horizontales/vertikales Scrollen gerade belegt ist. scaleFactor > 1 =
    // Finger spreizen (reinzoomen), < 1 = zusammenziehen (rauszoomen).
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    // Sucht in [searchLo, searchHi) den steigenden Nulldurchgang von left,
    // der `target` am naechsten liegt. Liefert -1, wenn keiner gefunden
    // wurde (z.B. Stille oder reiner Gleichanteil).
    static int findTriggerIndexNear (const float* left, int searchLo, int searchHi, int target);

    // Setzt eine neue Zoomstufe (Samples), klemmt auf [minDisplaySamples,
    // maxDisplaySamples]. Im History-Modus bleibt dabei die Bildmitte
    // (Sample-Position) stehen, im Live-Modus passt sich einfach die
    // naechste feed()-Anzeige an.
    void setDisplaySampleCount (int newCount);

    // Verschiebt den sichtbaren Ausschnitt in der History um deltaSamples
    // (positiv = weiter in die Vergangenheit/nach links), geklemmt auf
    // [0, frozenLength - displaySamples]. Ohne Wirkung ausserhalb des
    // History-Modus.
    void panBy (int deltaSamples);

    // Liefert Zeiger auf den gerade sichtbaren Ausschnitt - entweder in die
    // Live-Puffer (shownLeft/shownRight) oder mit Offset in die History
    // (frozenLeft/frozenRight). Zentral an einer Stelle, damit paint() und
    // exportVisibleWindow() nicht zwei verschiedene Fallunterscheidungen
    // pflegen.
    const float* visibleLeft() const;
    const float* visibleRight() const;

    bool frozen      = false;
    bool syncEnabled = false;

    int displaySamples    = 4096;          // Default bis der Editor die Samplerate kennt
    int maxDisplaySamples = 1 << 20;        // vorlaeufig grosszuegig, s. setMaxDisplaySampleCount()
    double sampleRateHint = 48000.0;

    std::vector<float> shownLeft, shownRight;   // Live-Anzeige

    // History-Modus (s. Klassenkommentar) - komplette Ringpuffer-Kopie zum
    // Freeze-Zeitpunkt plus Scroll-Position darin.
    bool historyMode = false;
    std::vector<float> frozenLeft, frozenRight;
    int frozenLength = 0;
    int panOffset    = 0;
    int triggerAbsoluteIndex = -1;   // Position in frozenLeft, -1 = kein Trigger markiert

    // Ob gerade eine Sync-Ausrichtung gelungen ist (fuer die Trigger-Linie
    // in paint() - bei fehlgeschlagener Suche wuerde sonst eine Trigger-
    // Linie ueber einem gar nicht ausgerichteten Bild stehen). Nur im
    // Live-Modus relevant, im History-Modus uebernimmt triggerAbsoluteIndex
    // dieselbe Rolle.
    bool lastFrameWasSynced = false;

    // Klick-Ziehen zum Pannen (nur History-Modus, s. mouseDown/mouseDrag).
    int dragStartX          = 0;
    int dragStartPanOffset  = 0;

    static constexpr float amplitudeRange = 1.2f;   // etwas ueber Vollausschlag, Clipping bleibt sichtbar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeComponent)
};
