#pragma once

#include "Tooltips.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <functional>
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
    void feed (const float* rawLeft, const float* rawRight, std::uint32_t windowEndSample);

    //------------------------------------------------------------------
    // Ereignis-Trigger (@dpa 20260824: "Der vorhandene Sync richtet an einem
    // Nulldurchgang aus - fuer periodische Signale sinnvoll, fuer Knalle
    // nutzlos, deshalb sehe ich nichts").
    //
    // Ein Knall hat keine Periode, an der man ausrichten koennte, sondern
    // einen Anfang: der Pegel steigt schlagartig. Genau darauf triggert diese
    // Betriebsart - ein schneller Huellkurvenfolger gegen einen langsamen.
    // Uebersteigt der schnelle den langsamen um riseFactor, ist das der
    // Einsatz. Danach steht das Bild fuer holdSeconds still und schaltet sich
    // von selbst wieder scharf.
    //
    // Das Ereignis landet in der MITTE des Bildes, nicht am rechten Rand:
    // gesucht wird deshalb nur dort, wo hinter dem Fund noch ein halbes
    // Anzeigefenster an Nachlauf im Rohfenster steht.
    //
    // Der manuelle Freeze hat Vorrang - er haelt das Bild unabhaengig davon,
    // was der Trigger gerade tut (feed() steigt bei frozen ohnehin sofort
    // aus).
    void setEventTriggerEnabled (bool shouldTrigger);
    bool isEventTriggerEnabled() const { return eventTriggerEnabled; }

    void setHoldSeconds (double seconds) { holdSeconds = juce::jmax (0.0, seconds); }
    double getHoldSeconds() const { return holdSeconds; }

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

    // Zeitbasis direkt setzen, in Samples. Oeffentlich, damit der Editor die
    // Voreinstellung in Sekunden umrechnen kann - die Komponente selbst kennt
    // die Abtastrate nicht (siehe Klassenkommentar).
    void setDisplaySeconds (double seconds, double sampleRate)
    {
        if (seconds > 0.0 && sampleRate > 0.0)
            setDisplaySampleCount ((int) (seconds * sampleRate));
    }

    int displaySampleCount() const { return displaySamples; }
    int captureWindowSampleCount() const { return displaySamples * 2; }

    // Ein Zoom-Schritt, multiplikativ: factor < 1 verkuerzt die Zeitbasis
    // (reinzoomen), factor > 1 verlaengert sie (rauszoomen). Oeffentlich,
    // damit der Editor zusaetzliche +/- Knoepfe daran haengen kann - Wheel
    // und Pinch (unten) rufen intern dasselbe.
    void zoomStep (float factor);

    // Speichert den aktuell sichtbaren Ausschnitt (genau das, was gerade
    // gezeichnet wird - Live oder History) als WAV-Datei nach `file`
    // (@dpa-Korrektur: erst CSV gebaut, "doch nicht als csv" - ein echtes
    // Audioformat, damit man es abspielen kann). Stereo, 32-bit-Float, mit
    // sampleRateHint als Samplerate, damit Tonhoehe/Tempo stimmen.
    bool exportVisibleWindow (const juce::File& file) const;

    //------------------------------------------------------------------
    // Play-Toggle (@dpa: "Play an schalten: es spielt die Scopeansicht von
    // vorn bis hinten ... wenn Play an bleibt, kann man ... an bestimmten
    // Stellen starten"). Die Komponente kennt nur die Anfrage-Seite - WAS
    // mit den Samples passiert (Puffer fuellen, an den Audiothread
    // uebergeben, Anti-Klick-Rampen), liegt beim Processor, s. dortigen
    // Kommentar zu requestScopePlayback(). Hier wird nur entschieden, WANN
    // eine Wiedergabe angestossen wird und WOMIT (welcher Ausschnitt des
    // gerade sichtbaren Bilds).
    //
    // Einschalten spielt sofort das GANZE sichtbare Fenster von vorn.
    // Danach startet nur noch ein echter Klick (kein Ziehen, s. mouseUp())
    // eine neue Wiedergabe, ab der geklickten Stelle bis zum rechten Rand.
    void setPlaybackEnabled (bool shouldEnable);
    bool isPlaybackEnabled() const { return playbackEnabled; }

    // Fortschritt der gerade laufenden Wiedergabe fuer den Cursor in
    // paint(): progressFraction 0..1 relativ zum ABGESPIELTEN Puffer (der
    // bei einem Klick kuerzer ist als das ganze Fenster, s.o.), active =
    // false blendet den Cursor aus. Vom Editor bei jedem Timer-Tick aus
    // DopplerfeldProcessor::scopePlaybackProgress()/isScopePlaybackAudible()
    // nachgefuehrt - die Komponente fragt den Processor nie selbst.
    void setPlaybackProgress (float progressFraction, bool active);

    // Feuert, sobald eine neue Wiedergabe angestossen werden soll (s.o.).
    // left/right zeigen in visibleLeft()/visibleRight() (nur fuer die Dauer
    // des Aufrufs gueltig - der Empfaenger kopiert sofort, s. Processor).
    std::function<void (const float* left, const float* right, int length)> onPlaybackRequested;

    void paint (juce::Graphics& g) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // Klick-Erkennung fuer den Play-Toggle (s.o.) - nur wenn playbackEnabled
    // UND die Maus sich seit mouseDown() kaum bewegt hat (sonst war es ein
    // Ziehen zum Pannen, s. mouseDrag()). Der ganz normale Panning-Zug bleibt
    // dadurch unangetastet, egal ob Play gerade an ist oder nicht.
    void mouseUp (const juce::MouseEvent&) override;

    // Pinch-Geste auf dem Trackpad (@dpa-Feedback: "fuer Mac mit Touchpad") -
    // die naheliegendste Zoom-Geste auf dem Mac, unabhaengig davon, ob/wie
    // horizontales/vertikales Scrollen gerade belegt ist. scaleFactor > 1 =
    // Finger spreizen (reinzoomen), < 1 = zusammenziehen (rauszoomen).
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    // Setzt den eigenen Tooltip neu, in der aktuell an
    // Tooltips::currentLanguage() gewaehlten Sprache - fuer den Sprach-
    // umschalter in der Kopfzeile (siehe PluginEditor).
    void refreshTooltips() { setTooltip (Tooltips::text (Tooltips::Key::Scope)); }

    // Fuer load_check: wieviele Samples zuletzt tatsaechlich gezeichnet
    // wurden und wo sie stehen. Bei aktivem Sync ist das eine ganze Zahl von
    // Perioden und damit weniger als displaySampleCount() - siehe
    // shownSampleCount weiter unten.
    int shownSampleCountForTest() const
    {
        return shownSampleCount > 0 ? shownSampleCount : displaySamples;
    }

    const float* shownLeftForTest() const { return shownLeft.data(); }

    // Zuletzt gemessene Periodenlaenge der Grundwelle, in Samples. 0 = keine
    // erkannt.
    double periodSamplesForTest() const { return lastPeriodSamples; }

private:
    // Sucht in [searchLo, searchHi) den steigenden Nulldurchgang von left,
    // der `target` am naechsten liegt. Liefert -1, wenn keiner gefunden
    // wurde (z.B. Stille oder reiner Gleichanteil).
    //
    // Zweistufig (@dpa 20260824: "Scope Sync springt noch sehr zwischen den
    // schwingungen. kannst Du einen Lopass einbauen, der dann den (ungenauen)
    // Sync angibt, woraus Du dann den korrekten Sync errechnen kannst?"):
    //
    //   1. Das Fenster wird tiefpassgefiltert. Uebrig bleibt die Grundwelle,
    //      und die hat je Periode genau EINEN steigenden Nulldurchgang. Im
    //      Rohsignal sind es so viele, wie das Signal Obertoene hat - deshalb
    //      sprang das Bild zwischen ihnen hin und her.
    //   2. Der grobe Fund wird im ROHSIGNAL nachgezogen: der steigende
    //      Nulldurchgang, der ihm am naechsten liegt. Damit steht das Bild auf
    //      der Flanke, die man wirklich sieht, aber immer auf derselben.
    //
    // Gefiltert wird vorwaerts UND rueckwaerts. Ein einfacher Durchlauf
    // verschoebe die Grundwelle um seine eigene Gruppenlaufzeit, und genau um
    // die laege der grobe Fund daneben - bei tiefen Grenzfrequenzen um mehr
    // als eine halbe Periode, womit Stufe 2 wieder auf der falschen Flanke
    // landete. Zwei Durchlaeufe in entgegengesetzter Richtung heben die
    // Phasendrehung exakt auf; das geht hier, weil das ganze Fenster schon
    // vorliegt.
    int findTriggerIndexNear (const float* left, int searchLo, int searchHi, int target) const;

    // Wie oben, aber ohne Tiefpass - der zweite Schritt fuer sich.
    static int nearestRisingZero (const float* left, int searchLo, int searchHi, int target);

    // Mittlere Periodenlaenge der Grundwelle in Samples, aus den steigenden
    // Nulldurchgaengen des tiefpassgefilterten Fensters. 0 = keine erkennbar.
    //
    // Setzt voraus, dass buildSyncLowpass() fuer dieses Fenster bereits
    // gelaufen ist - beides passiert in findTriggerIndexNear(), und der
    // gemessene Wert bleibt bis zum naechsten Bild in lastPeriodSamples
    // stehen.
    double measurePeriodSamples (int searchLo, int searchHi) const;

    // Rundet `requested` auf das naechste ganzzahlige Vielfache der zuletzt
    // gemessenen Periode. Ohne erkennbare Periode, bei weniger als einer
    // vollen Periode im Bild oder bei sehr kurzen Perioden (dort sind es
    // ohnehin Dutzende und der Rest faellt nicht auf) bleibt `requested`
    // unveraendert.
    int periodAlignedLength (int requested) const;

    // Fuellt syncScratch mit dem nullphasig tiefpassgefilterten Fenster.
    // Grenzfrequenz aus der Nulldurchgangsrate des Fensters selbst, siehe
    // Kommentar in der .cpp.
    void buildSyncLowpass (const float* left, int length) const;

    // Sucht im Rohfenster den ersten Pegelanstieg in [searchLo, searchHi).
    // Beide Huellkurvenfolger laufen ab Index 0 an, damit der langsame beim
    // Erreichen des Suchbereichs schon eingeschwungen ist. -1 = kein Anstieg.
    int findLevelRise (const float* left, const float* right,
                       int searchLo, int searchHi) const;

    // Setzt eine neue Zoomstufe (Samples), klemmt auf [minDisplaySamples,
    // maxDisplaySamples]. Im History-Modus bleibt dabei die Bildmitte
    // (Sample-Position) stehen, im Live-Modus passt sich einfach die
    // naechste feed()-Anzeige an. Fuer Aufrufer ohne Mausposition (die
    // +/- Knoepfe im Editor) - Wheel/Pinch nutzen stattdessen
    // zoomAroundFraction() unten, das den Cursor als Anker nimmt.
    void setDisplaySampleCount (int newCount);

    // Wie setDisplaySampleCount(), haelt aber im History-Modus den Sample
    // unter anchorFraction (0 = linker, 1 = rechter Bildrand) an seiner
    // Bildschirmposition fest statt der Fenstermitte - macht "Zoom um den
    // Mauszeiger" moeglich, wie im Vorbild (@dpa: ~/hass/sensor-archive/mac/
    // index.html, gesturePlugin(): cursorVal bleibt fix, die Distanzen
    // links/rechts skalieren beide mit factor). Im Live-Modus ist der
    // rechte Rand immer "jetzt" und damit fix - da gibt es keinen Anker,
    // s. Klassenkommentar oben, faellt also auf setDisplaySampleCount()
    // zurueck.
    void zoomAroundFraction (float factor, float anchorFraction);

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

    //------------------------------------------------------------------
    // Ereignis-Trigger, siehe setEventTriggerEnabled().
    bool   eventTriggerEnabled = false;
    double holdSeconds         = 1.0;

    // Zeitkonstanten der beiden Huellkurvenfolger. Der schnelle muss einer
    // Stossfront folgen koennen (Anstiegszeit einer N-Welle liegt im
    // Millisekundenbereich), der langsame darf ihr gerade nicht folgen -
    // sonst gaebe es nie einen Abstand zwischen beiden.
    static constexpr double envFastSeconds = 0.002;
    static constexpr double envSlowSeconds = 0.150;

    // Wie weit der schnelle ueber dem langsamen liegen muss. 4 = 12 dB.
    static constexpr double riseFactor = 4.0;

    // Unter diesem Pegel wird gar nicht getriggert - sonst feuert in der
    // Stille jedes Rauschen, weil dort auch der langsame Folger fast null ist
    // und jedes Verhaeltnis gross wird.
    static constexpr float riseFloor = 1.0e-4f;

    // Bild steht, bis diese Zeit erreicht ist (juce::Time::getMillisecond-
    // CounterHiRes()). 0 = nicht am Halten.
    double holdUntilMs = 0.0;

    // Absolute Position des zuletzt ausgeloesten Ereignisses im Ringpuffer.
    // Verhindert, dass dasselbe Ereignis nach Ablauf der Haltezeit erneut
    // feuert - die Anzeigefenster ueberlappen stark, und ein Knall steht
    // deshalb in mehreren aufeinanderfolgenden Rohfenstern.
    std::uint32_t lastTriggerAbsolute = 0;
    bool          hasTriggeredOnce    = false;

    // Nur fuer die Statuszeile in paint(): scharf oder gerade am Halten.
    bool holding = false;

    int displaySamples    = 4096;          // Default bis der Editor die Samplerate kennt
    int maxDisplaySamples = 1 << 20;        // vorlaeufig grosszuegig, s. setMaxDisplaySampleCount()
    double sampleRateHint = 48000.0;

    std::vector<float> shownLeft, shownRight;   // Live-Anzeige

    // Arbeitsspeicher des Sync-Tiefpasses (siehe findTriggerIndexNear).
    // mutable, weil die Suche selbst nichts am sichtbaren Zustand aendert.
    mutable std::vector<float> syncScratch;

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

    // --- Periodenrasten (@dpa 20260825: "die Wellen sind oft 2 geteilt ...
    // egal wo es synct - der naechste sync soll 2n spaeter sein oder so") ---
    //
    // Ein Oszilloskop mit fester Zeitbasis zeigt fast nie eine ganze Zahl von
    // Perioden: bei anderthalb sieht die Welle aus, als waere sie in der Mitte
    // durchgeschnitten. Bei aktivem Sync wird die gezeichnete Laenge deshalb
    // auf das naechste Vielfache der Periode gerundet - das Bild fuellt sich
    // dann mit ganzen Wellen, und aufeinanderfolgende Bilder sehen gleich aus.
    //
    // Der Zoomregler bleibt, was er ist: er sagt weiterhin, wieviel Zeit
    // ungefaehr ins Bild soll. Gerastet wird nur um bis zu eine halbe Periode
    // nach oben oder unten, und wenn keine Grundwelle erkennbar ist, gar
    // nicht.
    // mutable, weil findTriggerIndexNear() const ist und den Wert nebenbei
    // mitnimmt - genau wie syncScratch darunter.
    mutable double lastPeriodSamples = 0.0;

    // Wieviele der displaySamples tatsaechlich gezeichnet werden. Ohne Sync
    // oder ohne erkannte Periode ist das displaySamples selbst.
    int shownSampleCount = 0;

    // Klick-Ziehen zum Pannen (nur History-Modus, s. mouseDown/mouseDrag) UND
    // Klick-Erkennung fuer den Play-Toggle (jeder Modus, s. mouseUp) - beide
    // messen denselben Weg seit mouseDown() und teilen sich darum dragStartX.
    int dragStartX          = 0;
    int dragStartPanOffset  = 0;

    // Play-Toggle, siehe setPlaybackEnabled()/setPlaybackProgress() im
    // Header. playbackStartFraction ist die Stelle im sichtbaren Fenster
    // (0 = linker, 1 = rechter Rand), ab der zuletzt losgespielt wurde -
    // der Cursor in paint() setzt sich daraus UND aus playbackProgress
    // zusammen: cursorFraction = start + progress * (1 - start). Als
    // FRAKTION statt als Sample-Index, weil sich die Fraktion 1:1 auf die
    // Bildbreite abbildet, egal wie seither ge-/entzoomt wurde - ein reiner
    // Sample-Index waere nach einem Zoomschritt an der falschen Stelle.
    bool  playbackEnabled        = false;
    float playbackStartFraction  = 0.0f;
    float playbackProgress       = 0.0f;
    bool  playbackCursorActive   = false;

    // Toleranz fuer die Klick-vs-Zieh-Unterscheidung in mouseUp() - kleine
    // Zittertoleranz, damit ein leicht wackeliger Klick nicht faelschlich als
    // Drag zaehlt (JUCEs eigene Klick-Toleranz liegt in derselben Groessenordnung).
    static constexpr int clickDragThresholdPixels = 4;

    // Achsen-Lock fuer zwei-Finger-Wheel-Gesten, wie im Vorbild (@dpa:
    // ~/hass/sensor-archive/mac/index.html, gesturePlugin()): die Achse
    // wird beim ersten Event einer Geste per groesserem Delta entschieden
    // und bleibt dann fix, bis wheelGestureGapMs lang kein Wheel-Event mehr
    // kam (Finger abgehoben) - verhindert, dass eine eigentlich waagerechte
    // oder senkrechte Geste durch ein kleines Gegen-Delta mittendrin
    // zwischen Pan und Zoom hin- und herspringt. Statt eines echten Timers
    // (JS setTimeout) reicht hier ein fauler Check am naechsten Event: liegt
    // der schon laenger als wheelGestureGapMs zurueck, ist die alte Geste
    // vorbei und wird neu entschieden.
    enum class WheelGestureAxis { none, horizontal, vertical };
    WheelGestureAxis wheelGestureAxis = WheelGestureAxis::none;
    juce::int64 lastWheelEventMs      = 0;
    static constexpr int wheelGestureGapMs = 140;

    // Zoom-Empfindlichkeit fuers Mausrad-/Trackpad-deltaY (s.
    // mouseWheelMove): kalibriert auf JUCEs macOS-Wheel-Skala (0.5/256, s.
    // juce_NSViewComponentPeer_mac.mm redirectMouseWheel()) - angelehnt ans
    // Vorbild (~/hass/sensor-archive/mac/index.html: dort 0.006 auf rohe
    // Browser-Pixel-Deltas; JUCEs Deltas sind ca. 40-50x kleiner, deshalb
    // hier entsprechend groesser).
    static constexpr double zoomWheelSensitivity = 3.0;

    // Rueckrechnung von JUCEs (macOS-)Wheel-Delta auf echte Bildschirm-
    // Pixel (s. juce_NSViewComponentPeer_mac.mm redirectMouseWheel(): dort
    // deltaX/deltaY = 0.5/256 * echte Scroll-Pixel) - damit sich waagerecht
    // Scrollen genauso 1:1 anfuehlt wie Ziehen (mouseDrag), das dieselbe
    // deltaPixels/width*displaySamples-Rechnung nutzt.
    static constexpr double wheelPixelDeltaScale = 0.5 / 256.0;

    static constexpr float amplitudeRange = 1.2f;   // etwas ueber Vollausschlag, Clipping bleibt sichtbar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeComponent)
};
