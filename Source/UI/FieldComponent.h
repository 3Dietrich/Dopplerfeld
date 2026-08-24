#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Util/FieldSnapshot.h"
#include "../Physics/Vec3.h"
#include "../Physics/Listener.h"

#include <functional>
#include <vector>

// Die 700x400-Feldanzeige (Plan 3.13): Gitter mit Meterbeschriftung,
// Wellenfronten, Spur, Quelle M und Hoerer L als Kopfsymbol. Zeichnet
// ausschliesslich einen zuvor per setSnapshot() gesetzten FieldSnapshot -
// unabhaengig davon, wer ihn befuellt (DopplerEngine::fillSnapshot kommt
// erst in H13). Der Editor ruft setSnapshot() typischerweise per Timer auf.
//
// Einzige Stelle im gesamten Projekt, an der die Welt<->Bildschirm-
// Vorzeichenumkehr (Plan 2.1: Welt-y nach oben, Bildschirm-y nach unten)
// auftauchen darf: worldToScreen()/screenToWorld(). Jede andere Rechnung
// hier (Nasenwinkel, Hit-Tests, Drag-Logik) leitet sich aus diesen beiden
// Funktionen ab, statt die Umkehr ein zweites Mal zu implementieren.
class FieldComponent : public juce::Component,
                       private juce::Timer,
                        public juce::SettableTooltipClient
{
public:
    FieldComponent();
    ~FieldComponent() override = default;

    // PluginEditor.cpp setzt hier den Basistext (Ziehen an M/Kopf/Nase, siehe
    // Konstruktoraufruf dort). Die Bedienung der Perspektive (Zoom, Horizont,
    // Hoerer-Sicht, Klick auf den gelben Marker) haengt sich in der .cpp
    // dahinter an, statt PluginEditor.cpp fuer diesen Zusatztext anfassen zu
    // muessen - der Basistext bleibt dort, wo die restlichen Tooltips auch
    // stehen.
    void setTooltip (const juce::String& newTooltip) override;

    // Kopiert die Daten (kein Alias auf den Aufrufer-Speicher) - Snapshot ist
    // klein und wertartig (FieldSnapshot.h), Kopieren ist hier unkritisch,
    // anders als im Audiothread.
    void setSnapshot (const FieldSnapshot& snapshotIn);

    // @dpa-Feedback ("Langsamkeit der Anzeigewahrnehmung"): das Cockpit-HUD
    // zeigt NICHT das Tempo aus dem 30Hz-Snapshot, sonst blinkert die Zahl bei
    // jeder kleinen Schwankung. Der Editor mittelt selbst ueber ein
    // 0.5s-Fenster (DopplerfeldEditor::updateDisplayAverages()) und reicht das
    // Ergebnis hier separat rein - getrennt von setSnapshot(), damit die
    // Feldgrafik (Position, Wellenfronten) weiter fluessig bei 30Hz bleibt.
    void setDisplaySpeed (double speedMps, double speedOfSoundMps);

    // Zwei Ansichten derselben Szene. Die Draufsicht ist die gewohnte
    // 700x400-Flaeche; die perspektivische blickt in die Tiefe (Welt-y in den
    // Bildschirm hinein) und macht damit die Hoehe z sichtbar, die in der
    // Draufsicht gar nicht vorkommt. Sie ERSETZT die Draufsicht nicht, sie
    // kommt per Umschalter dazu.
    enum class ViewMode
    {
        TopDown,
        Perspective
    };

    void setViewMode (ViewMode mode);
    ViewMode getViewMode() const { return viewMode; }

    // "Aus L Sicht" (@dpa-Feedback): zusaetzlicher Kameramodus INNERHALB der
    // Perspektive, kein eigener ViewMode - die Kamera steht dann direkt an
    // der Hoererposition und blickt entlang seiner Nase (Listener.h) statt
    // wie im Standardfall fest in Richtung Welt-+y hinter ihm. Oeffentliche
    // Methode statt Editor-Umschalter (der waere eine Panel-Aenderung),
    // zusaetzlich per Tastendruck ('L') oder Doppelklick erreichbar, siehe
    // .cpp.
    void setPerspectiveFromListener (bool shouldUseListenerView);
    bool isPerspectiveFromListener() const { return perspectiveFromListener; }

    // Tempo-Einheit fuer Umrechnung/Textdarstellung (convertSpeed/formatSpeed
    // unten). Das Cockpit-Display selbst zeigt seit 20260819 immer alle drei
    // Einheiten gleichzeitig nebeneinander (drawSpeedReadout) und braucht
    // diese Auswahl folglich nicht mehr - der Typ bleibt hier, weil
    // PluginEditor ihn als Alias fuer seinen eigenen speedUnitButton-Zustand
    // weiterverwendet (Statuszeile, Regler-Geschwindigkeitszeile).
    enum class SpeedUnit { KmH, Ms, Mach };

    // Reine Umrechnung ohne Text/Padding - von formatSpeed() UND vom
    // dreispaltigen Cockpit-Display (drawSpeedReadout) genutzt, damit die
    // Umrechnungsformel nur an einer Stelle steht.
    static double convertSpeed (double sourceSpeedMps, double speedOfSoundMps, SpeedUnit unit);

    // Textdarstellung eines Tempos in der gewaehlten Einheit, feste
    // Zeichenbreite, Wert und Einheit auf einer Zeile. Genutzt von der
    // Statuszeile (PluginEditor::statusText()).
    static juce::String formatSpeed (double sourceSpeedMps, double speedOfSoundMps, SpeedUnit unit);

    // Mausbewegung an die Bildrate koppeln (@dpa 20260819: "du musst die
    // glaettung der mausbewegung mit der framerate glaetten.. schaltbar").
    //
    // Mausereignisse kommen unregelmaessig: mal zwei in derselben Millisekunde,
    // mal keins fuer zwanzig. Wird jedes davon sofort als neues Ziel gemeldet,
    // steckt dieser Takt in der Bewegung - und damit im Doppler, denn dessen
    // Tonhoehe haengt an der Geschwindigkeit, nicht an der Position. Eingeschaltet
    // laeuft das Melden stattdessen auf einem festen Bildtakt und zieht die
    // zuletzt gesehene Mausposition gleichmaessig nach.
    void setMouseFrameSmoothing (bool shouldBeEnabled);

    // Klon-Schwarm anzeigen (@dpa 20260820: "Geht die Darstellung der Klone?
    // zuschaltbar?"). Zeigt, wo die echten Klone sitzen und wie sie einzeln
    // wackeln - klein und blass, damit die Quelle selbst erkennbar bleibt.
    void setShowClones (bool shouldShow) { showClones = shouldShow; repaint(); }
    bool isShowingClones() const { return showClones; }

    // Nur fuer den Test: wie viele Klon-Punkte das Feld gerade zeichnen wuerde.
    int clonePositionCountForTest() const { return showClones ? snapshot.clonePositionCount : 0; }


    // Nachlauf nach mouseUp() (@dpa-Feedback): Quelle/Hoerer laufen mit der
    // zuletzt gezogenen Geschwindigkeit noch kurz weiter und bremsen dann ab,
    // statt abrupt stehenzubleiben - "realer als nur STOP". Nur fuer
    // Positions-Drags in der Draufsicht (Quelle, Hoererposition); die
    // Perspektive und die Kopfdrehung bleiben aussen vor, siehe .cpp.
    // Reines Bedienungsgefuehl, keine Szenenphysik - deshalb zu-/abschaltbar
    // und kein Parameter.
    void setCoastEnabled (bool shouldCoast);
    bool isCoastEnabled() const { return coastEnabled; }

    // Feldbreite in Metern (Params::fieldMetres), fuer Gitter-Skalierung und
    // Umrechnung normierte <-> Meter-Koordinaten.
    void setFieldMetres (double metresIn);

    // Schallgeschwindigkeit fuer die Wellenfront-Radien (Plan 2.2: Default
    // 343,2 m/s bei 20 Grad C). Einstellbar, falls der Editor spaeter T
    // durchreichen will (siehe Params::airTempC, in Phase 1 nicht in der UI).
    void setSpeedOfSound (double metresPerSecond);

    void paint (juce::Graphics& g) override;
    void resized() override {}

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    // Nur in der Perspektive, Touchpad-Zwei-Finger-Geste mit Achsen-Lock
    // (wie ScopeComponent::mouseWheelMove(), s. dort - dasselbe Schema, "gut
    // und schoen" laut @dpa): 2 Finger senkrecht zoomt (perspectiveZoom), 2
    // Finger waagerecht verschiebt die Horizontlage (perspectiveHorizonFraction,
    // "ob (mit Boden) mehr oben oder mittig"). Begruendung fuer diese
    // Zuordnung statt eines Pans: in der Perspektive gibt es keine Zeitachse
    // zum Pannen wie im Scope, der Horizont ist die einzige zweite
    // verstellbare Groesse. Umschalt+Mausrad bleibt zusaetzlich als Weg fuer
    // reine Mausbenutzer (kein deltaX) bestehen, ist aber nicht mehr der
    // Hauptweg. Siehe auch mouseMagnify() unten fuer die Pinch-Geste.
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Pinch-Geste auf dem Trackpad, nur in der Perspektive - die
    // naheliegendste Zoomgeste auf dem Mac (@dpa-Feedback zur Referenz-Geste,
    // s. mouseWheelMove()), unabhaengig vom Achsen-Lock der Wheel-Gesten
    // oben. scaleFactor > 1 = Finger spreizen (reinzoomen/naeher), < 1 =
    // zusammenziehen (rauszoomen/weiter weg). Direkter, groeberer Faktor als
    // die Wheel-Zoomkurve, wie ScopeComponent::mouseMagnify().
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;

    // Tastatur-Zugang zur Hoerer-Sicht ('L'), siehe setPerspectiveFromListener().
    // '0' setzt Zoom und Horizontlage der Perspektive zurueck (Doppelklick
    // ist bereits mit dem Kamera-Wechsel oben belegt, deshalb ein eigener
    // Tastendruck - '0' wie in Browsern/Bildbetrachtern ueblich fuer "Zoom
    // zuruecksetzen").
    bool keyPressed (const juce::KeyPress& key) override;

    // Rueckmeldung nach aussen in normierten [0,1]-Koordinaten, passend zu
    // den APVTS-Parametern srcX/srcY/lisX/lisY (Params.h, normiert laut
    // Plan 3.11 bzw. 2.1). yawRadians folgt der ListenerState::yaw-Konvention
    // (0 = Nase in +y, siehe Listener.h).
    std::function<void (double normX, double normY)> onSourceDragged;
    std::function<void (double normX, double normY)> onListenerDragged;
    std::function<void (double yawRadians)> onListenerRotated;

    // Nur in der perspektivischen Ansicht: dort bedeutet Ziehen nach oben
    // "hoeher", nicht "weiter weg". Die Hoehe ist der einzige Freiheitsgrad,
    // den die Draufsicht ueberhaupt nicht anfassen kann - deshalb ist das kein
    // doppelter Weg zum selben Ziel, sondern der einzige mit der Maus.
    std::function<void (double metres)> onSourceHeightDragged;

    // Nur fuer die Quelle M, ausdruecklich getrennt vom Positions-Rueckkanal
    // oben (@dpa-Feedback: Motor-Gating - Klangfrage, nicht Positionsfrage,
    // der Aufrufer entscheidet selbst, ob/wie er reagiert). Feuert genau
    // einmal beim Greifen bzw. Loslassen von M, unabhaengig von Draufsicht/
    // Perspektive und unabhaengig davon, ob ueberhaupt etwas "gegated" wird -
    // FieldComponent kennt das Gating-Feature selbst nicht, sie meldet nur
    // das Ereignis.
    std::function<void()> onSourceGrabbed;
    std::function<void()> onSourceReleased;

private:
    // -- Koordinatenumrechnung (Plan 2.1: px = position_m/n*700, isotrop) --
    float pixelsPerMetre() const;
    juce::Point<float> worldToScreen (Vec3 worldMetres) const;
    Vec3 screenToWorld (juce::Point<float> screenPx) const;
    double fieldHeightMetres() const; // aus fieldMetres + Seitenverhaeltnis der Flaeche

    // -- Zeichenteile --
    void drawGrid (juce::Graphics& g) const;
    void drawWalls (juce::Graphics& g) const;

    // -- Perspektivische Ansicht --
    //
    // Ergebnis einer Projektion. visible ist false, wenn der Punkt hinter der
    // Kamera (oder zu dicht davor) liegt - dann ist px bedeutungslos. scale ist
    // der Abbildungsmasstab an dieser Tiefe in Pixeln je Meter; damit werden
    // Symbolgroessen mit der Entfernung kleiner, ohne dass jede Zeichenstelle
    // die Projektion noch einmal von Hand nachrechnet.
    struct Projected
    {
        juce::Point<float> px;
        bool  visible = false;
        float scale   = 0.0f;
    };

    Vec3  cameraPosition() const;

    // Blickrichtung/"rechts" der Kamera. Im Standardmodus fest die
    // Weltachsen (+y vorwaerts, +x rechts) - deshalb zerfaellt project()
    // unten in diesem Fall exakt in die feste x/y-Rechnung von vorher. "Aus
    // L Sicht" (perspectiveFromListener) sind es stattdessen die Hoerer-
    // Achsen aus Listener.h (Nase/rechts), dieselbe Konvention wie
    // listenerScreenYaw() in der Draufsicht.
    Vec3 cameraForward() const;
    Vec3 cameraRight() const;

    float focalPixels() const;
    float horizonYPx() const;

    Projected project (Vec3 worldMetres) const;

    // Bildposition + Radius des gelben Quellen-Markers in der Perspektive -
    // egal ob M gerade normal im Bild sitzt, an den Rand geklemmt ist
    // (ausserhalb des Sichtfelds, aber vor der Kamera) oder nur als fixer
    // Hinweis-Punkt hinter der Kamera steht. EINE Stelle fuer Zeichnen
    // (drawPerspectiveSource) UND Hit-Test (dragTargetAt) - ein Klick auf
    // den Marker trifft dadurch immer genau dort, wo er zu sehen ist, auch
    // wenn das nicht M's wahre Bildposition ist (@dpa: "auf dem gelben
    // Marker via Mausklick M an diese Stelle holen").
    struct SourceMarker { juce::Point<float> px; float radiusPx; };
    SourceMarker perspectiveSourceMarker() const;

    // Weltpunkte als Linienzug zeichnen, Teilstuecke hinter der Kamera
    // auslassen. Jede perspektivische Linie laeuft hierueber, statt die
    // Sichtbarkeitspruefung mehrfach hinzuschreiben.
    void strokeWorldPath (juce::Graphics& g, const std::vector<Vec3>& points,
                          juce::Colour colour, float thickness) const;

    void drawPerspective (juce::Graphics& g) const;
    void drawPerspectiveGround (juce::Graphics& g) const;
    void drawPerspectiveWalls (juce::Graphics& g) const;
    void drawPerspectiveWavefronts (juce::Graphics& g) const;
    void drawPerspectiveTrail (juce::Graphics& g) const;
    void drawPerspectiveSource (juce::Graphics& g) const;
    void drawPerspectiveListener (juce::Graphics& g) const;
    void drawWavefronts (juce::Graphics& g) const;
    void drawReflectionWavefronts (juce::Graphics& g) const;

    // Helligkeits-Faktor der Wellenfront-Ringe (@dpa 20260818: bei n = 6000 m
    // getuned, bei kleineren Feldern "voller heller Schallkreise" - siehe
    // .cpp). Reiner Anzeige-Faktor, unabhaengig von der Geometrie.
    float wavefrontBrightnessFactor() const;
    void drawTrail (juce::Graphics& g) const;

    // Vorbeiflug-Wegvorschau (@dpa-Feedback): geplante Reststrecke + Punkt
    // kuerzesten Abstands zu L. Nur Draufsicht - die Perspektive bekommt das
    // spaeter, wenn's noetig wird (siehe ARCHITEKTUR.md).
    void drawFlyByPreview (juce::Graphics& g) const;

    void drawSource (juce::Graphics& g) const;
    void drawListener (juce::Graphics& g) const;

    // Cockpit-Tempoanzeige oben rechts im Feld (@dpa-Feedback: "wie im
    // Cockpit"). Laeuft in beiden Ansichten (Draufsicht + Perspektive), weil
    // sie ein reines Anzeige-Overlay ist und nichts mit der Projektion zu tun
    // hat.
    void drawSpeedReadout (juce::Graphics& g) const;

    // Abstand Hoerer - Quelle, immer sichtbar (@dpa 20260824: "der Abstand
    // L .. M muss immer sichtbar sein. Ich schlage vor, wie die gelben speed
    // anzeigen, bloss links in ~meter"). Gleiche Machart wie die
    // Tempo-Anzeige, gleiche Farbe, nur eine Spalte und auf der anderen
    // Seite - so liest sich beides als ein Instrument, nicht als zwei.
    void drawDistanceReadout (juce::Graphics& g) const;

    // Screen-Blickwinkel des Hoerers: aus zwei mit worldToScreen projizierten
    // Punkten (Kopf, Kopf+Nasenrichtung) statt eines zweiten, redundanten
    // Vorzeichenwechsels - siehe Klassenkommentar oben.
    float listenerScreenYaw() const;

    // -- Maus / Drag --
    enum class DragTarget { none, source, listenerHead, listenerNose };
    DragTarget dragTargetAt (juce::Point<float> screenPx) const;
    void handleDragTo (juce::Point<float> screenPx);
    void reportNormalisedDrag (Vec3 worldPos, bool isSource) const;

    // -- Nachlauf nach mouseUp() (@dpa-Feedback, siehe setCoastEnabled) --
    //
    // KEIN eigener Simulations-Timer (erste Fassung hatte einen, siehe
    // git-history) - der lief 60x/s neu gegen den jeweils aktiven Smoother
    // an und kollidierte bei "Slew Limiter" mit dessen EIGENER Bremskurve
    // (siehe SlewLimiter::tick, sqrt(2*a_max*d)): der Limiter hing dem
    // ständig neu gesetzten, nahen Nachlauf-Ziel so eng auf den Fersen,
    // dass beim eigentlichen Stopp kein spuerbarer Restweg mehr uebrig war
    // - "läuft ein Stück, bremst nicht, bleibt stehen" (@dpa-Repro).
    //
    // Stattdessen EINMALIG der analytisch integrierte Endpunkt eines
    // exponentiell abklingenden Nachlaufs (Integral von v0*exp(-t/tau) über
    // t = v0*tau) als neues Ziel - genau wie ein Dreh am Regler oder ein
    // Automationswert. Welcher Smoother auch aktiv ist, er bekommt seine
    // eigene, dafuer gebaute Anfahrt-/Bremskurve zu sehen (One-Pole:
    // exponentiell, Critically Damped Spring: kein Ueberschwinger, Slew
    // Limiter: dessen eigene Beschleunigungsrampe+Bremskurve, One Euro:
    // Cutoff-Glaettung) - kein zweiter, konkurrierender Bremsmechanismus.
    bool coastEnabled = true;

    // Waehrend eines Drags fortlaufend geschaetzt (leicht geglaettet, siehe
    // mouseDrag()) - das ist die Anfangsgeschwindigkeit des Nachlaufs.
    Vec3   lastDragWorldPos;
    double lastDragTimeMs      = 0.0;
    Vec3   dragVelocityEstimate;
    bool   haveDragVelocity    = false;

    // Halbwertszeit des gedachten Abklingens - bestimmt nur, WIE WEIT der
    // projizierte Endpunkt liegt (Gesamtweg = v0*halfLife/ln(2)), nicht WIE
    // die Bewegung dorthin aussieht (das macht der aktive Smoother). @dpa:
    // "der Bremsweg könnte doppelt so lang sein" - entsprechend bemessen.
    static constexpr double coastHalfLifeSeconds = 0.3;
    static constexpr double coastMinSpeedSquared = 0.05 * 0.05;   // m/s, quadriert

    FieldSnapshot snapshot;
    double fieldMetres = 100.0;
    double speedOfSound = 343.2; // Plan 2.2: c(20 C) = 343,21 m/s

    // Fuers Cockpit-HUD (drawSpeedReadout), separat vom 30Hz-Snapshot oben -
    // s. setDisplaySpeed(). Startwert egal, wird vor dem ersten Zeichnen vom
    // Editor gesetzt.
    double displaySpeedMps = 0.0;
    double displaySpeedOfSoundMps = 343.2;

    static constexpr float headRadiusPx = 13.0f; // rein symbolische Groesse, nicht massstabsgetreu
    static constexpr float sourceRadiusPx = 6.0f;

    // Ob der Klon-Schwarm mitgezeichnet wird, siehe setShowClones().
    bool showClones = true;
    static constexpr float dragHitRadiusPx = 16.0f;

    // Grosszuegigerer Fangradius eigens fuer M (@dpa: "ich habe Schwierigkeiten,
    // M zu bewegen, wenn Jitter ueber seine Darstellung hinausgeht - man kann
    // es nicht mehr fangen"). Nur fuer die Quelle, nicht fuer Kopf/Nase - der
    // Hoerer L soll dadurch nicht schwerer greifbar werden, s. dragTargetAt().
    static constexpr float sourceDragHitRadiusPx = 28.0f;

    // Ruhende Ankerposition der Quelle OHNE das Jitter-Wackeln, nur fuers
    // Greifen per Mausklick (dragTargetAt()) - der gezeichnete Punkt darf
    // weiter zappeln, getroffen wird dort, wo M "eigentlich" ist. FieldSnapshot
    // liefert keine jitterfreie Position (nur die tatsaechliche, moeglicherweise
    // gewackelte sourcePos) - ohne Processor-Aenderung wird hier stattdessen
    // tiefpassgefiltert, mit einer Zeitkonstante deutlich ueber der ueblichen
    // Jitter-Periode (Hektik-Parameter typischerweise mehrere Hz): schnelle,
    // gewollte Bewegung (Drag, Vorbeiflug) bleibt erkennbar, das schnelle
    // Zappeln wird herausgemittelt. Siehe setSnapshot().
    Vec3   sourceAnchorWorld;
    bool   haveSourceAnchor          = false;
    double lastAnchorSnapshotTime    = 0.0;
    static constexpr double sourceAnchorSmoothTauSeconds = 0.25;

    // Waehrend M aktiv gezogen wird: die zuletzt an den Aufrufer gemeldete
    // Zielposition, fuers Zeichnen benutzt statt der naechsten (moeglicherweise
    // wieder gejitterten) Snapshot-Position - sonst kaempft der gezeichnete
    // Punkt waehrend des Ziehens sichtbar gegen die Maus an, obwohl er ihr
    // eigentlich 1:1 folgen soll (@dpa: "waehrend des Ziehens folgt M der Maus
    // ohne Wackel-Versatz"). Gilt fuer Draufsicht UND Perspektive (siehe
    // drawSource(), perspectiveSourceMarker()).
    Vec3 sourceDragWorldOverride;

    DragTarget dragTarget = DragTarget::none;

    ViewMode viewMode = ViewMode::TopDown;

    // "Aus L Sicht" (s. setPerspectiveFromListener()) - nur in der
    // Perspektive wirksam.
    bool perspectiveFromListener = false;

    // Perspektiv-Zoom (@dpa-Feedback). Multipliziert die Brennweite in
    // focalPixels(). Bedienung: 2 Finger senkrecht oder Pinch in der
    // Perspektive, s. mouseWheelMove()/mouseMagnify(); per '0' (keyPressed())
    // zurueck auf perspectiveZoomDefault.
    static constexpr float perspectiveZoomDefault = 1.0f;
    float perspectiveZoom = perspectiveZoomDefault;
    static constexpr float perspectiveZoomMin = 0.3f;
    static constexpr float perspectiveZoomMax = 4.0f;

    // Horizontlage als Anteil der Bildhoehe von oben (@dpa-Feedback: "ob mit
    // Boden mehr oben oder mittig"). Bedienung: 2 Finger waagerecht oder
    // Umschalt+Mausrad in der Perspektive, s. mouseWheelMove(); per '0'
    // (keyPressed()) zurueck auf perspectiveHorizonFractionDefault.
    static constexpr float perspectiveHorizonFractionDefault = 0.40f;
    float perspectiveHorizonFraction = perspectiveHorizonFractionDefault;
    static constexpr float perspectiveHorizonFractionMin = 0.15f;
    static constexpr float perspectiveHorizonFractionMax = 0.70f;

    // Empfindlichkeit der Wheel-Zoom-/Horizont-Kurven (s. mouseWheelMove()) -
    // 3.0 fuer den Zoom-Exponenten uebernommen aus dem bisherigen Verhalten
    // (frueher als 3.0f inline im Code), 0.3 fuer die Horizontverschiebung
    // gilt fuer beide Wege dorthin (2 Finger waagerecht UND Umschalt+Mausrad),
    // damit sich beide gleich anfuehlen.
    static constexpr double perspectiveZoomWheelSensitivity = 3.0;
    static constexpr double perspectiveHorizonWheelSensitivity = 0.3;

    // Achsen-Lock fuer die Zwei-Finger-Wheel-Geste (s. mouseWheelMove()) -
    // exakt wie ScopeComponent::WheelGestureAxis (s. dort): die erste
    // Bewegung einer Geste entscheidet per groesserem Delta, ob waagerecht
    // (Horizont) oder senkrecht (Zoom) gilt, und bleibt dabei, bis
    // wheelGestureGapMs lang kein Wheel-Event mehr kam (Finger abgehoben) -
    // verhindert Zittern zwischen Zoom und Horizontverschiebung mitten in
    // einer Geste. Gilt nicht fuer Umschalt+Mausrad (expliziter Zusatzweg,
    // s. mouseWheelMove()) und nicht fuer Pinch (mouseMagnify(), laeuft
    // parallel dazu).
    enum class WheelGestureAxis { none, horizontal, vertical };
    WheelGestureAxis wheelGestureAxis = WheelGestureAxis::none;
    juce::int64 lastWheelEventMs      = 0;
    static constexpr int wheelGestureGapMs = 140;

    // Naheste Tiefe, die noch abgebildet wird. Alles davor waechst ins
    // Unendliche und gehoert nicht ins Bild.
    static constexpr double nearPlaneMetres = 0.4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldComponent)

    // Bildtakt fuer das Melden der Mausposition, siehe setMouseFrameSmoothing().
    static constexpr int mouseFrameHz = 60;

    bool               mouseFrameSmoothing = true;
    bool               havePendingDrag     = false;
    juce::Point<float> pendingDragScreen;
    juce::Point<float> smoothedDragScreen;

    void timerCallback() override;
};
