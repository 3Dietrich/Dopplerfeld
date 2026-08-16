#include "FieldComponent.h"
#include "HeadSymbol.h"

#include <cmath>

namespace
{
    // "Schoene" Gitterschrittweite (1-2-5-Stufung) fuer ungefaehr
    // targetDivisions Linien ueber die Feldbreite - Standardalgorithmus,
    // hier lokal statt einer eigenen Util-Datei, weil er nur hier gebraucht wird.
    double niceGridStep (double fieldMetresIn, double targetDivisions)
    {
        if (fieldMetresIn <= 0.0 || targetDivisions <= 0.0)
            return 1.0;

        const double roughStep = fieldMetresIn / targetDivisions;
        const double magnitude = std::pow (10.0, std::floor (std::log10 (roughStep)));
        const double residual = roughStep / magnitude;

        double niceResidual = 1.0;
        if (residual > 5.0)      niceResidual = 10.0;
        else if (residual > 2.0) niceResidual = 5.0;
        else if (residual > 1.0) niceResidual = 2.0;

        return niceResidual * magnitude;
    }

    juce::String formatMetres (double metres)
    {
        if (std::abs (metres - std::round (metres)) < 1.0e-6)
            return juce::String ((int) std::round (metres)) + "m";
        return juce::String (metres, 1) + "m";
    }
}

FieldComponent::FieldComponent()
{
    setSize (700, 400); // Plan 3.13: exakte Groesse
}

void FieldComponent::setSnapshot (const FieldSnapshot& snapshotIn)
{
    snapshot = snapshotIn; // Wertkopie, FieldSnapshot ist klein und allokationsfrei
    repaint();
}

void FieldComponent::setFieldMetres (double metresIn)
{
    fieldMetres = juce::jlimit (1.0, 10000.0, metresIn); // Plan 3.11: n in [1, 10000]
    repaint();
}

void FieldComponent::setSpeedOfSound (double metresPerSecond)
{
    speedOfSound = metresPerSecond;
    repaint();
}

// ---- Koordinatenumrechnung -------------------------------------------------
// Einzige Stelle mit dem Welt<->Bildschirm-Vorzeichenwechsel (Plan 2.1).
// Alles andere in dieser Datei (Nasenwinkel, Hit-Tests) geht ueber diese
// beiden Funktionen, statt die Umkehr ein zweites Mal zu implementieren.

float FieldComponent::pixelsPerMetre() const
{
    const double widthPx = (double) juce::jmax (1, getWidth());
    return (float) (widthPx / juce::jmax (1.0e-6, fieldMetres));
}

double FieldComponent::fieldHeightMetres() const
{
    const double widthPx  = (double) juce::jmax (1, getWidth());
    const double heightPx = (double) juce::jmax (1, getHeight());
    return fieldMetres * (heightPx / widthPx); // Plan 2.1: Hoehe = n*400/700 bei 700x400
}

juce::Point<float> FieldComponent::worldToScreen (Vec3 worldMetres) const
{
    const float scale = pixelsPerMetre();
    const float x = (float) worldMetres.x * scale;
    const float y = (float) getHeight() - (float) worldMetres.y * scale; // y-Flip: Welt hoch = Bildschirm hoch
    return { x, y };
}

Vec3 FieldComponent::screenToWorld (juce::Point<float> screenPx) const
{
    const float scale = pixelsPerMetre();
    if (scale <= 0.0f)
        return {};

    const double x = (double) screenPx.x / (double) scale;
    const double y = ((double) getHeight() - (double) screenPx.y) / (double) scale;
    return { x, y, 0.0 };
}

// Bildschirm-Blickwinkel des Hoerers: zwei Weltpunkte (Kopf, Kopf+Nase) durch
// worldToScreen schicken und die Bildschirm-Richtung daraus nehmen - so bleibt
// der Vorzeichenwechsel exklusiv in worldToScreen, statt hier ein zweites Mal
// per Hand nachgebildet zu werden (siehe Klassenkommentar in FieldComponent.h).
float FieldComponent::listenerScreenYaw() const
{
    const Vec3 head = snapshot.listener.head;
    const Vec3 noseWorld = head + listenerNose (snapshot.listener);
    const auto a = worldToScreen (head);
    const auto b = worldToScreen (noseWorld);
    return std::atan2 (b.y - a.y, b.x - a.x);
}

// ---- Zeichnen ---------------------------------------------------------------

void FieldComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    drawGrid (g);
    drawWavefronts (g);
    drawTrail (g);
    drawSource (g);
    drawListener (g);
}

void FieldComponent::drawGrid (juce::Graphics& g) const
{
    const auto bounds = getLocalBounds().toFloat();
    const double heightM = fieldHeightMetres();
    const double step = niceGridStep (fieldMetres, 7.0);
    if (step <= 0.0)
        return;

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    for (double xM = 0.0; xM <= fieldMetres + 1.0e-9; xM += step)
    {
        const float xPx = worldToScreen ({ xM, 0.0, 0.0 }).x;
        g.drawVerticalLine ((int) std::round (xPx), bounds.getY(), bounds.getBottom());
    }
    for (double yM = 0.0; yM <= heightM + 1.0e-9; yM += step)
    {
        const float yPx = worldToScreen ({ 0.0, yM, 0.0 }).y;
        g.drawHorizontalLine ((int) std::round (yPx), bounds.getX(), bounds.getRight());
    }

    // Beschriftung nur an X-Achse (unterer Rand) und Y-Achse (linker Rand) -
    // bei feinem Gitter waere ein Label pro Kreuzungspunkt unleserlich.
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (11.0f);
    for (double xM = 0.0; xM <= fieldMetres + 1.0e-9; xM += step)
    {
        const float xPx = worldToScreen ({ xM, 0.0, 0.0 }).x;
        g.drawText (formatMetres (xM), (int) xPx + 2, (int) bounds.getBottom() - 14, 60, 12,
                    juce::Justification::left);
    }
    for (double yM = step; yM <= heightM + 1.0e-9; yM += step) // 0 ist schon auf der X-Achse beschriftet
    {
        const float yPx = worldToScreen ({ 0.0, yM, 0.0 }).y;
        g.drawText (formatMetres (yM), 2, (int) yPx - 12, 60, 12, juce::Justification::left);
    }
}

void FieldComponent::drawWavefronts (juce::Graphics& g) const
{
    // Jede Front sitzt an der Quellposition zum EIGENEN Emissionszeitpunkt
    // M(t_k) (FieldSnapshot::wavefrontPositions), nicht an der aktuellen -
    // erst dieser Versatz erzeugt die vorne gestauchten Fronten und bei
    // Ueberschall die Einhuellende, die den Mach-Kegel bildet (Plan 3.12).
    const float pxPerM = pixelsPerMetre();

    for (int i = 0; i < snapshot.wavefrontCount; ++i)
    {
        const double age = snapshot.now - snapshot.wavefrontEmitTimes[(size_t) i];
        if (age <= 0.0)
            continue; // Emission liegt (noch) nicht in der Vergangenheit - nichts zu zeichnen

        const double radiusM = speedOfSound * age;
        const float radiusPx = (float) (radiusM * pxPerM);
        if (radiusPx <= 0.5f || radiusPx > 4000.0f) // 4000px: grosszuegige Deckelung gegen Ausreisser, kein fachliches Limit
            continue;

        const auto centrePx = worldToScreen (snapshot.wavefrontPositions[(size_t) i]);

        // Aeltere Fronten sind weiter aussen und blasser - macht die
        // Ausbreitungsrichtung sichtbar, ohne Pfeile zeichnen zu muessen.
        const float alpha = juce::jmap ((float) i, 0.0f,
                                         (float) juce::jmax (1, snapshot.wavefrontCount - 1),
                                         0.55f, 0.08f);
        g.setColour (juce::Colours::cyan.withAlpha (alpha));
        g.drawEllipse (juce::Rectangle<float> (radiusPx * 2.0f, radiusPx * 2.0f).withCentre (centrePx), 1.2f);
    }
}

void FieldComponent::drawTrail (juce::Graphics& g) const
{
    if (snapshot.trailCount < 2)
        return;

    juce::Path path;
    for (int i = 0; i < snapshot.trailCount; ++i)
    {
        const auto px = worldToScreen (snapshot.trail[(size_t) i]);
        if (i == 0)
            path.startNewSubPath (px);
        else
            path.lineTo (px);
    }

    g.setColour (juce::Colours::orange.withAlpha (0.6f));
    g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void FieldComponent::drawSource (juce::Graphics& g) const
{
    const auto centrePx = worldToScreen (snapshot.sourcePos);

    g.setColour (juce::Colours::yellow);
    g.fillEllipse (juce::Rectangle<float> (sourceRadiusPx * 2.0f, sourceRadiusPx * 2.0f).withCentre (centrePx));

    // Schallwellen-Deko: volle konzentrische Ringe um M, rein symbolisch -
    // eine punktfoermige Quelle strahlt nach allen Seiten, nicht nur in eine
    // Richtung (die echten Wellenfronten zeichnet drawWavefronts() separat
    // aus dem Snapshot, mit der tatsaechlichen Emissionsgeometrie).
    g.setColour (juce::Colours::yellow.withAlpha (0.55f));
    for (int ring = 1; ring <= 3; ++ring)
    {
        const float r = sourceRadiusPx + (float) ring * 5.0f;
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centrePx), 1.4f);
    }
}

void FieldComponent::drawListener (juce::Graphics& g) const
{
    const auto centrePx = worldToScreen (snapshot.listener.head);
    const float yaw = listenerScreenYaw();

    HeadSymbol::Style style;
    style.headColour = juce::Colours::white;
    style.earColour  = juce::Colours::white;
    style.fillColour = juce::Colours::white.withAlpha (0.08f);

    HeadSymbol::draw (g, centrePx, headRadiusPx, yaw, style);
}

// ---- Maus / Drag -------------------------------------------------------------

FieldComponent::DragTarget FieldComponent::dragTargetAt (juce::Point<float> screenPx) const
{
    const auto headPx = worldToScreen (snapshot.listener.head);
    const float yaw = listenerScreenYaw();
    const auto nosePx = HeadSymbol::noseTip (headPx, headRadiusPx, yaw);
    const auto sourcePx = worldToScreen (snapshot.sourcePos);

    // Nase zuerst pruefen: sie liegt oft innerhalb des grosszuegigen
    // Kopf-Fangradius, soll aber Vorrang vor "Kopf verschieben" haben.
    if (screenPx.getDistanceFrom (nosePx) <= dragHitRadiusPx)
        return DragTarget::listenerNose;

    if (screenPx.getDistanceFrom (headPx) <= headRadiusPx + dragHitRadiusPx * 0.6f)
        return DragTarget::listenerHead;

    if (screenPx.getDistanceFrom (sourcePx) <= sourceRadiusPx + dragHitRadiusPx)
        return DragTarget::source;

    return DragTarget::none;
}

void FieldComponent::handleDragTo (juce::Point<float> screenPx)
{
    switch (dragTarget)
    {
        case DragTarget::source:
            reportNormalisedDrag (screenToWorld (screenPx), true);
            break;

        case DragTarget::listenerHead:
            reportNormalisedDrag (screenToWorld (screenPx), false);
            break;

        case DragTarget::listenerNose:
        {
            const Vec3 headWorld = snapshot.listener.head;
            const Vec3 dir = screenToWorld (screenPx) - headWorld;
            if (dir.lengthSquared() > 1.0e-9)
            {
                // Umkehrung von listenerNose() (Listener.h): n = (sin(yaw), cos(yaw), 0).
                const double yaw = std::atan2 (dir.x, dir.y);
                if (onListenerRotated)
                    onListenerRotated (yaw);
            }
            break;
        }

        case DragTarget::none:
        default:
            break;
    }
}

void FieldComponent::reportNormalisedDrag (Vec3 worldPos, bool isSource) const
{
    const double normX = juce::jlimit (0.0, 1.0, worldPos.x / juce::jmax (1.0e-6, fieldMetres));
    const double normY = juce::jlimit (0.0, 1.0, worldPos.y / juce::jmax (1.0e-6, fieldHeightMetres()));

    if (isSource)
    {
        if (onSourceDragged)
            onSourceDragged (normX, normY);
    }
    else
    {
        if (onListenerDragged)
            onListenerDragged (normX, normY);
    }
}

void FieldComponent::mouseDown (const juce::MouseEvent& e)
{
    dragTarget = dragTargetAt (e.position);
    if (dragTarget != DragTarget::none)
        handleDragTo (e.position);
}

void FieldComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTarget != DragTarget::none)
        handleDragTo (e.position);
}

void FieldComponent::mouseUp (const juce::MouseEvent&)
{
    dragTarget = DragTarget::none;
}
