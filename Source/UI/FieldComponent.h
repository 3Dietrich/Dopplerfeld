#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Util/FieldSnapshot.h"
#include "../Physics/Vec3.h"
#include "../Physics/Listener.h"

#include <functional>

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

private:
    // -- Koordinatenumrechnung (Plan 2.1: px = position_m/n*700, isotrop) --
    float pixelsPerMetre() const;
    juce::Point<float> worldToScreen (Vec3 worldMetres) const;
    Vec3 screenToWorld (juce::Point<float> screenPx) const;
    double fieldHeightMetres() const; // aus fieldMetres + Seitenverhaeltnis der Flaeche

    // -- Zeichenteile --
    void drawGrid (juce::Graphics& g) const;
    void drawWavefronts (juce::Graphics& g) const;
    void drawTrail (juce::Graphics& g) const;
    void drawSource (juce::Graphics& g) const;
    void drawListener (juce::Graphics& g) const;

    // Screen-Blickwinkel des Hoerers: aus zwei mit worldToScreen projizierten
    // Punkten (Kopf, Kopf+Nasenrichtung) statt eines zweiten, redundanten
    // Vorzeichenwechsels - siehe Klassenkommentar oben.
    float listenerScreenYaw() const;

    // -- Maus / Drag --
    enum class DragTarget { none, source, listenerHead, listenerNose };
    DragTarget dragTargetAt (juce::Point<float> screenPx) const;
    void handleDragTo (juce::Point<float> screenPx);
    void reportNormalisedDrag (Vec3 worldPos, bool isSource) const;

    FieldSnapshot snapshot;
    double fieldMetres = 100.0;
    double speedOfSound = 343.2; // Plan 2.2: c(20 C) = 343,21 m/s

    static constexpr float headRadiusPx = 13.0f; // rein symbolische Groesse, nicht massstabsgetreu
    static constexpr float sourceRadiusPx = 6.0f;
    static constexpr float dragHitRadiusPx = 16.0f;

    DragTarget dragTarget = DragTarget::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldComponent)
};
