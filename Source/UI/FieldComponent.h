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
                        public juce::SettableTooltipClient
{
public:
    FieldComponent();
    ~FieldComponent() override = default;

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

    // Tempo-Einheit fuer das Cockpit-Display (@dpa-Feedback), dieselbe Auswahl
    // wie der speedUnitButton in der Statuszeile - der Editor haelt die
    // Auswahl (kein Parameter, reine Anzeigefrage), reicht sie hier nur durch.
    enum class SpeedUnit { KmH, Ms, Mach };
    void setSpeedUnit (SpeedUnit unit);

    // Textdarstellung eines Tempos in der gewaehlten Einheit - gemeinsam
    // genutzt von der Statuszeile (PluginEditor::statusText()) und dem
    // Cockpit-Display hier, damit es nur eine Formel/Einheiten-Zuordnung gibt.
    static juce::String formatSpeed (double sourceSpeedMps, double speedOfSoundMps, SpeedUnit unit);

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
    float focalPixels() const;
    float horizonYPx() const;

    Projected project (Vec3 worldMetres) const;

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
    static constexpr float dragHitRadiusPx = 16.0f;

    DragTarget dragTarget = DragTarget::none;

    ViewMode viewMode = ViewMode::TopDown;
    SpeedUnit speedUnit = SpeedUnit::KmH;

    // Naheste Tiefe, die noch abgebildet wird. Alles davor waechst ins
    // Unendliche und gehoert nicht ins Bild.
    static constexpr double nearPlaneMetres = 0.4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldComponent)
};
