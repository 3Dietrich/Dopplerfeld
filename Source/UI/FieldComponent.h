#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Util/FieldSnapshot.h"
#include "../Physics/Vec3.h"
#include "../Physics/Listener.h"

#include <array>
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
    // statt abrupt stehenzubleiben - "realer als nur STOP", und zwar in JEDER
    // Perspektive, nicht nur der Draufsicht (@dpa: "es soll sich einfach
    // langsam zur Ruhe bewegen... nicht auf einmal stoppen"). Nur fuer
    // Positions-Drags (Quelle in beiden Ansichten, Hoererposition nur in der
    // Draufsicht, weil sie nur dort ueberhaupt greifbar ist); die Kopfdrehung
    // bleibt aussen vor, siehe .cpp. Reines Bedienungsgefuehl, keine
    // Szenenphysik - deshalb zu-/abschaltbar
    // und kein Parameter. Der Zustand bleibt trotzdem nicht folgenlos: er
    // wird bei jedem Aufruf in einer eigenen ApplicationProperties-Datei
    // gemerkt (nicht im Host-Preset) und beim naechsten Start wieder geholt
    // - siehe coastProperties() in FieldComponent.cpp.
    void setCoastEnabled (bool shouldCoast);
    bool isCoastEnabled() const { return coastEnabled; }

    // Feldbreite in Metern (Params::fieldMetres), fuer Gitter-Skalierung und
    // Umrechnung normierte <-> Meter-Koordinaten.
    void setFieldMetres (double metresIn);

    // Schallgeschwindigkeit fuer die Wellenfront-Radien (Plan 2.2: Default
    // 343,2 m/s bei 20 Grad C). Einstellbar, falls der Editor spaeter T
    // durchreichen will (siehe Params::airTempC, in Phase 1 nicht in der UI).
    void setSpeedOfSound (double metresPerSecond);

    // Kettenziel je Abgriffpunkt (@dpa 20260830: Reverbs koennen hintereinander
    // geschaltet werden). Der FieldSnapshot fuehrt das nicht mit - er ist
    // audiothread-berechneter Klangzustand, die Kette ist ein reiner
    // APVTS-Parameter (Params::TapPart::chain) ohne Rueckwirkung auf die
    // Physik-Groessen dort. Deshalb ein eigener, schlanker Kanal statt einer
    // Snapshot-Erweiterung.
    //
    // rawChainChoice[t] ist der ROHE Wert des Chain-Parameters von Punkt t:
    // 0 = "aus", 1 = der unmittelbar folgende Punkt, 2 = der uebernaechste,
    // und so weiter (Params.h) - dieselbe Zahl, die
    // ReverbPanel::refreshRunningMarks() schon zu chainTargetOf() verrechnet.
    // Die Umrechnung auf den tatsaechlichen Zielindex (oder -1, falls aus
    // oder ausserhalb) passiert hier drin, nicht beim Aufrufer.
    void setTapChainTargets (std::array<int, FieldSnapshot::maxTaps> rawChainChoice);

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

    // Ein Abgriffpunkt wurde im Feld verschoben. x/y auf die Feldflaeche
    // normiert wie bei Quelle und Hoerer, die Hoehe bleibt unberuehrt: sie
    // ist in der Draufsicht nicht bedienbar, und ein Zug nach oben wuerde
    // sonst unbeabsichtigt die Hoehe mitnehmen.
    std::function<void (int index, double normX, double normY)> onTapDragged;
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

    // Die Quelle wurde in Bewegung losgelassen: Geschwindigkeit in Metern je
    // Sekunde, Weltkoordinaten. Der Nachlauf selbst gehoert nicht hierher,
    // sondern in die Bewegungskette (DopplerfeldProcessor::startSourceCoast) -
    // hier wird nur gemessen, wie schnell die Maus zuletzt war.
    std::function<void (Vec3 velocity)> onSourceCoast;

private:
    // -- Koordinatenumrechnung (Plan 2.1: px = position_m/n*700, isotrop) --
    float pixelsPerMetre() const;
    juce::Point<float> worldToScreen (Vec3 worldMetres) const;
    Vec3 screenToWorld (juce::Point<float> screenPx) const;
    double fieldHeightMetres() const; // aus fieldMetres + Seitenverhaeltnis der Flaeche

    // Umkehrung von project() bei FESTGEHALTENER Tiefe (an snapshot.sourcePos
    // festgemacht) - die perspektivische Entsprechung von screenToWorld()
    // oben, siehe .cpp fuer die Herleitung. EINE Stelle fuer diese Rechnung:
    // handleDragTo() (eigentliches Ziehen) UND dragScreenToWorld() (Nachlauf-
    // Geschwindigkeitsschaetzung) nutzen sie gemeinsam.
    Vec3 perspectiveScreenToWorld (juce::Point<float> screenPx) const;

    // Weltposition unter der Maus, wie sie die jeweils aktive Ansicht
    // versteht - screenToWorld() in der Draufsicht, perspectiveScreenToWorld()
    // in der Perspektive. Damit die Nachlauf-Geschwindigkeitsschaetzung
    // (mouseDrag()/mouseDown()) exakt dieselbe Umrechnung sieht wie
    // handleDragTo() beim eigentlichen Ziehen - sonst bekaeme sie in der
    // Perspektive eine falsche (flache) Geschwindigkeit und der Nachlauf
    // bliebe auf die Draufsicht beschraenkt.
    Vec3 dragScreenToWorld (juce::Point<float> screenPx) const;

    // -- Zeichenteile --
    void drawGrid (juce::Graphics& g) const;
    void drawWalls (juce::Graphics& g) const;
    void drawTaps (juce::Graphics& g) const;

    // Halbebene auf der Hoererseite einer Wandebene, in Bildschirmkoordinaten
    // (fuer g.reduceClipRegion()) - was jenseits davon liegt, ist die dem
    // Hoerer abgewandte Seite und gehoert nicht ins Bild (@dpa: "hinter den
    // Waenden die Spiegelungen nicht anzeigen"). Genutzt von
    // drawReflectionWavefronts(), das die Bildquellen-Kreise/-Boegen der
    // Wandreflexionen zeichnet. Wie bei drawWalls() aendert die Neigung
    // nichts an der Lage der Wandgeraden in der Draufsicht - nur der Azimut
    // zaehlt.
    juce::Path wallListenerSideClip (const FieldSnapshot::WallInfo& wall) const;

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
    // unten in diesem Fall exakt in eine feste x/y-Rechnung. "Aus
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

    // Grundlage des Anfass-Versatzes (siehe grabOffsetPx). Zwei Punkte, weil
    // es zwei Fragen sind: hitPx ist die Stelle, gegen die dragTargetAt()
    // geprueft hat - also die gezeichnete, moeglicherweise gewackelte -, und
    // an ihr entscheidet sich, ob der Klick das Symbol selbst getroffen hat.
    // anchorPx ist die Ruhelage, von der aus danach gerechnet wird; nur so
    // meldet der Klick eine Bewegung von genau null, statt den Wackel-Versatz
    // des Augenblicks als Zielposition festzuschreiben.
    //
    // valid = false heisst: keine anfassbare Stelle, sondern eine Randmarke
    // oder ein Hinweispunkt - dort ist der Sprung zur geklickten Stelle
    // ausdruecklich gewollt.
    struct GrabAnchor
    {
        juce::Point<float> hitPx;
        juce::Point<float> anchorPx;
        float radiusPx;
        bool  valid;
    };

    GrabAnchor grabAnchorPx() const;

    // Dieselbe Idee wie perspectiveSourceMarker() oben, nur fuer die
    // Draufsicht: Bildposition von M ueber worldToScreen(), an den Rand
    // geklemmt (6 px Abstand), wenn M ausserhalb des sichtbaren Feldes liegt
    // (@dpa 20260825: "manchmal fliegt M weiter als das Feld gross... dann
    // will man ihn irgendwann wieder haben"). EINE Stelle fuer Zeichnen
    // (drawSource) UND Hit-Test (dragTargetAt) - ein Klick/Zug auf die
    // Randmarke trifft dadurch immer genau dort, wo sie zu sehen ist, und
    // holt M ueber den normalen Drag-Pfad zurueck (handleDragTo() ->
    // reportNormalisedDrag() klemmt dabei wie gewohnt auf [0,1] des Feldes).
    SourceMarker topDownSourceMarker() const;

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

    // -- Ueberschall-Frontlinien -------------------------------------------
    //
    // Bei Ueberschall bilden alle Kugelwellen zusammen EINE Front, die mit der
    // Quelle durchs Feld wandert. Wo sie einen Hoerer ueberstreicht, hoert der
    // in diesem Augenblick den Knall (die N-Welle, s. PropagationPath) - die
    // Linie ist also nicht Deko, sondern genau die Stelle, an der es knallt.
    //
    // Die Front ist eine Flaeche im Raum, keine Linie: auf Ohrhoehe laeuft sie
    // woanders als am Boden oder auf halber Flughoehe. Gezeichnet wird deshalb
    // ihr Schnitt mit mehreren waagerechten Ebenen, gestaffelt nach Hoehe
    // (@dpa: "das kann man farblich oder strichfarb-deckungsmaessig andeuten").
    //
    // Gebaut wird sie NICHT aus einem gedachten Kegel, sondern aus genau den
    // Kugelwellen, die ohnehin schon als cyane Kreise im Bild stehen: die
    // Front ist deren gemeinsame aeussere Tangente. Dadurch kann sie
    // grundsaetzlich nicht weiter reichen als der aeusserste Kreis und nie vor
    // der Quelle liegen, egal wie krumm die Bahn ist. Liegen zwei Kreise
    // ineinander, gab es zwischen ihren Emissionszeiten keinen Ueberschall,
    // und dort ist die Linie unterbrochen - dieselbe Bedingung wie Mach 1,
    // nur ohne eine Geschwindigkeit zu schaetzen.
    std::vector<std::vector<Vec3>> machFrontAtHeight (double height) const;

    // Welche Hoehen gezeichnet werden und wie kraeftig - an einer Stelle,
    // damit Draufsicht und Perspektive dieselbe Staffelung zeigen.
    struct MachFrontLayer { double height; float alpha; float thickness; };
    std::vector<MachFrontLayer> machFrontLayers() const;

    void drawMachFronts (juce::Graphics& g) const;
    void drawPerspectiveMachFronts (juce::Graphics& g) const;

    // Wie viele Hoehenstufen zwischen Boden und Flughoehe liegen (siehe
    // machFrontLayers()).
    static constexpr int machFrontHeightSteps = 5;
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
    enum class DragTarget { none, source, listenerHead, listenerNose, tap };
    DragTarget dragTargetAt (juce::Point<float> screenPx) const;
    void handleDragTo (juce::Point<float> screenPx);
    void reportNormalisedDrag (Vec3 worldPos, bool isSource) const;

    // -- Nachlauf nach mouseUp() (@dpa-Feedback, siehe setCoastEnabled) --
    //
    // KEIN eigener Simulations-Timer: der liefe 60x/s neu gegen den jeweils
    // aktiven Smoother an und kollidiert bei "Slew Limiter" mit dessen
    // EIGENER Bremskurve (siehe SlewLimiter::tick, sqrt(2*a_max*d)) - der
    // Limiter haengt dem staendig neu gesetzten, nahen Nachlauf-Ziel dann so
    // eng auf den Fersen, dass beim eigentlichen Stopp kein spuerbarer
    // Restweg mehr uebrig bleibt.
    //
    // Stattdessen EINMALIG der analytisch integrierte Endpunkt eines
    // exponentiell abklingenden Nachlaufs (Integral von v0*exp(-t/tau) über
    // t = v0*tau) als neues Ziel - genau wie ein Dreh am Regler oder ein
    // Automationswert. Welcher Smoother auch aktiv ist, er bekommt seine
    // eigene, dafuer gebaute Anfahrt-/Bremskurve zu sehen (One-Pole:
    // exponentiell, Critically Damped Spring: kein Ueberschwinger, Slew
    // Limiter: dessen eigene Beschleunigungsrampe+Bremskurve, One Euro:
    // Cutoff-Glaettung) - kein zweiter, konkurrierender Bremsmechanismus.
    //
    // Default true nur als Fallback fuer den allerersten Start - der
    // Konstruktor ueberschreibt ihn sofort mit dem zuletzt gewaehlten,
    // dauerhaft gemerkten Stand (siehe coastProperties()/setCoastEnabled()
    // in FieldComponent.cpp).
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

    // Zielindex (0-basiert) je Abgriffpunkt, oder -1 ohne Kette - vom Editor
    // ueber setTapChainTargets() gefuellt, ausserhalb des Snapshots (siehe
    // dort). -1 vorbelegt, bevor der erste Aufruf kommt.
    std::array<int, FieldSnapshot::maxTaps> tapChainTargets { -1, -1, -1, -1, -1, -1, -1, -1 };

    // Fuers Cockpit-HUD (drawSpeedReadout), separat vom 30Hz-Snapshot oben -
    // s. setDisplaySpeed(). Startwert egal, wird vor dem ersten Zeichnen vom
    // Editor gesetzt.
    double displaySpeedMps = 0.0;
    double displaySpeedOfSoundMps = 343.2;

    static constexpr float headRadiusPx = 13.0f; // rein symbolische Groesse, nicht massstabsgetreu
    static constexpr float sourceRadiusPx = 6.0f;

    // Bildgroesse je Tiefe in der Perspektive: Radius = scale * dieser Wert,
    // also Pixel je Meter mal Meter. Gilt fuer M (perspectiveSourceMarker());
    // der Hoererkopf nimmt denselben Wert im selben Verhaeltnis, in dem er
    // auch in der Draufsicht groesser ist als M (headRadiusPx zu
    // sourceRadiusPx) - sonst waere ausgerechnet das groesste Symbol der
    // Draufsicht in der Perspektive das kleinste.
    static constexpr float perspectiveSourceScale = 0.4f;

    // Oberer Randabstand eigens fuer die Randmarke der Draufsicht
    // (topDownSourceMarker()) - deutlich mehr als der uebliche 6px-Rand
    // (rechts/links/unten), weil oben die Tempo-/Entfernungsanzeige liegt
    // (drawSpeedReadout()/drawDistanceReadout(), beide bis y=69 hoch,
    // drawSource() zeichnet VOR ihnen). Ohne diesen groesseren Abstand
    // verschwindet die Randmarke bei M noerdlich des Feldes teilweise unter
    // der HUD-Flaeche - gefunden beim Verifizieren mit Tests/field_shot.cpp.
    static constexpr float topDownMarkerTopMarginPx = 74.0f;

    // Ob der Klon-Schwarm mitgezeichnet wird, siehe setShowClones().
    bool showClones = true;
    static constexpr float dragHitRadiusPx = 16.0f;

    // Grosszuegigerer Fangradius eigens fuer M (@dpa: "ich habe Schwierigkeiten,
    // M zu bewegen, wenn Jitter ueber seine Darstellung hinausgeht - man kann
    // es nicht mehr fangen"). Nur fuer die Quelle, nicht fuer Kopf/Nase - der
    // Hoerer L soll dadurch nicht schwerer greifbar werden, s. dragTargetAt().
    static constexpr float sourceDragHitRadiusPx = 28.0f;

    // Fangradius eines Abgriffpunkts. Kleiner als der der Quelle: acht davon
    // im Feld mit je 28 px Fangradius wuerden einander und alles andere
    // zudecken.
    static constexpr float tapDragHitRadiusPx = 16.0f;

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

    // Welcher Abgriffpunkt gerade gezogen wird. Der Zielbezeichner allein
    // reicht bei ihnen nicht - es gibt acht davon, und sie unterscheiden sich
    // nur durch den Index.
    int dragTapIndex = -1;

    // Welcher Abgriffpunkt an dieser Bildstelle liegt, oder -1. Eigene
    // Funktion und kein Nebenbei-Ergebnis von dragTargetAt(): die ist const
    // und darf sich nichts merken.
    int tapIndexAt (juce::Point<float> screenPx) const;

    // Ob Punkt tapIndex das Ziel einer AKTIVEN Kette ist: der Geber existiert
    // und ist an, das Ziel ist an, und die Verkettung steht (Params::TapPart::
    // chain). Dann hoert das Ziel nur noch seinen Vorgaenger statt des Felds -
    // seine eigene Lage ist bedeutungslos, deshalb bekommt es in drawTaps()
    // keine eigene Marke und ist in tapIndexAt() nicht greifbar.
    bool isActiveChainTarget (int tapIndex) const;

    // Versatz zwischen Mauszeiger und der Stelle, an der das gegriffene
    // Symbol tatsaechlich steht. Beim Anfassen einmal gemerkt und danach bei
    // jedem Ziehschritt wieder aufgeschlagen (@dpa 20260826: "wenn man den M
    // anfasst springt er meist ... es soll sich durchs click 0 bewegen. Erst
    // dragging zaehlt dann von der Klickposition aus.. ohne sprung, einfach
    // ein mausversatz"). Ohne ihn wuerde schon der blosse Klick die Position
    // auf den Mauszeiger setzen, und M spraenge um bis zu einen Fangradius -
    // beim Wackeln um mehr, weil man dann ohnehin nie genau auf seine Mitte
    // trifft.
    //
    // Ausgenommen bleiben die Randmarken (M ausserhalb des Bildes oder
    // hinter der Kamera): dort ist der Sprung zur geklickten Stelle
    // ausdruecklich der Zweck, s. mouseDown() und handleDragTo().
    juce::Point<float> grabOffsetPx;

    ViewMode viewMode = ViewMode::TopDown;

    // "Aus L Sicht" (s. setPerspectiveFromListener()) - nur in der
    // Perspektive wirksam.
    bool perspectiveFromListener = false;

    // Perspektiv-Zoom (@dpa-Feedback). Multipliziert die Brennweite in
    // focalPixels(). Bedienung: 2 Finger senkrecht oder Pinch in der
    // Perspektive, s. mouseWheelMove()/mouseMagnify(); per '0' (keyPressed())
    // zurueck auf perspectiveZoomDefault.
    //
    // Die Untergrenze steht bei 0,04: das ist rund ein Fuenfundzwanzigstel der
    // Brennweite von 0,7 * Breite, also ein Blickwinkel, in dem auch ein
    // 10-km-Feld als Ganzes ins Bild passt (@dpa 20260825: "soll noch weiter
    // rauszoomen koennen"). Kein versteckter Deckel, sondern nur die Stelle,
    // an der die Perspektive rechnerisch in eine Parallelprojektion kippt.
    static constexpr float perspectiveZoomDefault = 1.0f;
    float perspectiveZoom = perspectiveZoomDefault;
    static constexpr float perspectiveZoomMin = 0.04f;
    static constexpr float perspectiveZoomMax = 16.0f;

    // Horizontlage als Anteil der Bildhoehe von oben (@dpa-Feedback: "ob mit
    // Boden mehr oben oder mittig"). Bedienung: 2 Finger waagerecht oder
    // Umschalt+Mausrad in der Perspektive, s. mouseWheelMove(); per '0'
    // (keyPressed()) zurueck auf perspectiveHorizonFractionDefault.
    //
    // Der Regelweg reicht von "fast nur Boden" bis "fast nur Himmel", geht
    // aber an keinem Ende auf null (@dpa 20260825: "die Ansicht weiter auf,
    // also weniger (nicht unsichtbar!) Boden, und zu stellen"). Bei 0,94
    // bleiben sechs Prozent der Bildhoehe Boden - ein schmaler Streifen, der
    // die Weite noch lesbar macht; bei 0,04 bleibt derselbe Streifen Himmel.
    static constexpr float perspectiveHorizonFractionDefault = 0.40f;
    float perspectiveHorizonFraction = perspectiveHorizonFractionDefault;
    static constexpr float perspectiveHorizonFractionMin = 0.04f;
    static constexpr float perspectiveHorizonFractionMax = 0.94f;

    // Empfindlichkeit der Wheel-Zoom-/Horizont-Kurven (s. mouseWheelMove()) -
    // 3.0 fuer den Zoom-Exponenten, 0.3 fuer die Horizontverschiebung. Die
    // 0.3 gelten fuer beide Wege dorthin (2 Finger waagerecht UND
    // Umschalt+Mausrad), damit sich beide gleich anfuehlen.
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
