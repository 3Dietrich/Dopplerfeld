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

void FieldComponent::setDisplaySpeed (double speedMps, double speedOfSoundMps)
{
    displaySpeedMps = speedMps;
    displaySpeedOfSoundMps = speedOfSoundMps;
    // Kein eigener repaint() noetig - setSnapshot() wird ohnehin bei jedem
    // 30Hz-Tick aufgerufen und zeichnet neu; hier wuerde ein zusaetzlicher
    // repaint() nur denselben Frame doppelt anstossen.
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

void FieldComponent::setSpeedUnit (SpeedUnit unit)
{
    if (speedUnit == unit)
        return;

    speedUnit = unit;
    repaint();
}

juce::String FieldComponent::formatSpeed (double sourceSpeedMps, double speedOfSoundMps, SpeedUnit unit)
{
    double value = 0.0;
    const char* label = "";

    switch (unit)
    {
        case SpeedUnit::KmH:  value = sourceSpeedMps * 3.6;                                    label = "km/h"; break;
        case SpeedUnit::Ms:   value = sourceSpeedMps;                                          label = "m/s";  break;
        case SpeedUnit::Mach: value = sourceSpeedMps / juce::jmax (1.0, speedOfSoundMps);       label = "Mach"; break;
    }

    // Feste Breite (@dpa-Feedback "Langsamkeit der Anzeigewahrnehmung"): egal
    // ob 0 oder 100 km/h, Zahl und Einheit duerfen im laufenden Betrieb nicht
    // seitlich wandern - nur die Ziffern selbst duerfen sich aendern. Beide
    // Abnehmer (Cockpit-Display hier und PluginEditor::statusText()) bekommen
    // deshalb schon hier eine auf Zeichenbreite feste Zeichenkette statt sie
    // selbst nachtraeglich padden zu muessen.
    return juce::String::formatted ("%6.1f %-4s", value, label);
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

void FieldComponent::setViewMode (ViewMode mode)
{
    if (mode == viewMode)
        return;

    viewMode = mode;
    repaint();
}

void FieldComponent::paint (juce::Graphics& g)
{
    if (viewMode == ViewMode::Perspective)
    {
        drawPerspective (g);
        drawSpeedReadout (g);
        return;
    }

    g.fillAll (juce::Colours::black);
    drawGrid (g);
    drawWalls (g);
    drawWavefronts (g);
    drawTrail (g);
    drawFlyByPreview (g);
    drawSource (g);
    drawListener (g);
    drawSpeedReadout (g);
}

void FieldComponent::drawSpeedReadout (juce::Graphics& g) const
{
    // Cockpit-Tempoanzeige (@dpa-Feedback: "wie im Cockpit, aber nicht
    // uebertrieben") - dieselbe Einheit wie die Statuszeile, hier aber gross
    // und direkt im schwarzen Feld statt nur klein unten im Text. Alpha-Gelb
    // #ffff0055, oben rechts, ~140x50px. Kontrastarmer Rahmen statt eines
    // hellen (siehe Sanfte-Rahmen-Konvention) - das soll ins Bild einsinken,
    // nicht draufkleben.
    constexpr int w = 140;
    constexpr int h = 50;
    const juce::Rectangle<int> box (getWidth() - w - 10, 10, w, h);

    const juce::Colour hudYellow (0xffffff00);

    g.setColour (hudYellow.withAlpha (0.06f));
    g.fillRoundedRectangle (box.toFloat(), 4.0f);
    g.setColour (hudYellow.withAlpha (0.22f));
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);

    const juce::String text = formatSpeed (displaySpeedMps, displaySpeedOfSoundMps, speedUnit);

    // Monospace statt Proportionalschrift (@dpa-Feedback "Langsamkeit der
    // Anzeigewahrnehmung"): erst bei fester Zeichenbreite pro Glyphe haelt die
    // feste Zeichenbreite aus formatSpeed() die Anzeige auch pixelgenau
    // stehen - in einer Proportionalschrift waeren "1" und "0" unterschiedlich
    // breit und die Zahl würde bei jeder Ziffernaenderung minimal ruckeln.
    g.setColour (hudYellow.withAlpha (0x55 / 255.0f));
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 26.0f, juce::Font::bold)));
    g.drawText (text, box.reduced (6), juce::Justification::centred, false);
}

void FieldComponent::drawWalls (juce::Graphics& g) const
{
    // Eine Wand ist eine unendlich grosse Ebene; in der Draufsicht ist sie
    // eine Gerade durch den Fusspunkt mit Richtung (cos a, sin a). Gezeichnet
    // wird sie deshalb ueber das ganze Bild hinaus, nicht als Strecke - eine
    // begrenzte Linie wuerde eine Kante suggerieren, die es nicht gibt.
    //
    // Die Neigung aendert an der Draufsicht nichts (eine gekippte Wand steht
    // an derselben Stelle), macht die Reflexion aber schwaecher bis
    // wirkungslos - deshalb wird sie als Strichstaerke sichtbar gemacht: eine
    // flach liegende Wand faellt in der Draufsicht mit dem Boden zusammen und
    // wird zur duennen Linie.
    const double diagonal = std::hypot ((double) getWidth(), (double) getHeight());

    for (const auto& wall : snapshot.walls)
    {
        if (! wall.on)
            continue;

        const Vec3 dir { std::cos (wall.azimuthRad), std::sin (wall.azimuthRad), 0.0 };

        // Über die Bildecke hinaus verlaengern: die Umrechnung ist isotrop,
        // deshalb reicht die Bilddiagonale in Metern.
        const double reach = diagonal / (double) juce::jmax (1.0e-6f, pixelsPerMetre());

        const auto a = worldToScreen (wall.anchor - dir * reach);
        const auto b = worldToScreen (wall.anchor + dir * reach);

        const float upright = (float) std::abs (std::cos (wall.tiltRad));

        g.setColour (juce::Colours::skyblue.withAlpha (0.20f + 0.35f * upright));
        g.drawLine (juce::Line<float> (a, b), 1.0f + 2.0f * upright);

        // Fusspunkt markieren, sonst ist beim Ziehen nicht erkennbar, worauf
        // sich X und Y beziehen.
        const auto anchor = worldToScreen (wall.anchor);
        g.setColour (juce::Colours::skyblue.withAlpha (0.55f));
        g.fillEllipse (anchor.x - 2.5f, anchor.y - 2.5f, 5.0f, 5.0f);
    }
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

void FieldComponent::drawFlyByPreview (juce::Graphics& g) const
{
    if (! snapshot.flyByActive)
        return;

    // Geplante Reststrecke: aktuelle Position bis Streckenende, duenn und
    // gestrichelt - anders als die Spur (durchgezogen, Vergangenheit).
    const auto fromPx = worldToScreen (snapshot.sourcePos);
    const auto toPx   = worldToScreen (snapshot.flyByPlannedEnd);

    juce::Path path;
    path.startNewSubPath (fromPx);
    path.lineTo (toPx);

    const float dashLengths[] { 4.0f, 4.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, path, dashLengths, 2);

    g.setColour (juce::Colours::orange.withAlpha (0.35f));
    g.fillPath (dashed);

    // Punkt kuerzesten Abstands zu L, mit Zahl (@dpa: "kuerzsten Abstand des
    // gesamten laufs zum L einzeichnen und angeben").
    const auto nearestPx = worldToScreen (snapshot.flyByNearestPoint);

    g.setColour (juce::Colours::orange.withAlpha (0.8f));
    g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (nearestPx), 1.5f);

    g.setFont (11.0f);
    g.drawText (formatMetres (snapshot.flyByNearestDistance) + " min",
               (int) nearestPx.x + 6, (int) nearestPx.y - 14, 90, 12,
               juce::Justification::left);
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

// ---- Perspektivische Ansicht -------------------------------------------------
//
// Blick in die Tiefe: Welt-y zeigt in den Bildschirm hinein, Welt-z nach oben,
// Welt-x nach rechts. Die optische Achse liegt waagerecht, deshalb laeuft die
// Bodenebene z = 0 auf eine Horizontlinie zu - das ist das Fluchtpunkt-Trapez
// "wie eine Strasse in die Ferne".
//
// Die Kamera steht hinter und ueber dem Hoerer, damit er selbst im Bild ist;
// beide Abstaende wachsen mit der Feldgroesse, sonst waere die Ansicht bei
// n = 10000 m ein Blick auf die eigene Schuhspitze und bei n = 5 m ein Blick
// aus dem Weltall. Das sind Gestaltungswerte, keine gemessenen Groessen.

Vec3 FieldComponent::cameraPosition() const
{
    const Vec3 head = snapshot.listener.head;

    // Der Abstand nach hinten waechst mit der Feldgroesse: sonst waere die
    // Ansicht bei n = 10000 m ein Blick auf die eigene Schuhspitze und bei
    // n = 5 m einer aus dem Weltall.
    const double back = 3.0 + 0.06 * fieldMetres;

    // Die Kamerahoehe haengt fest am Abstand, nicht eigenstaendig an der
    // Feldgroesse. Der Grund ist rein geometrisch: der Fusspunkt des Hoerers
    // erscheint bei horizon + focal * camZ / back. Nur wenn camZ/back konstant
    // ist, liegt er unabhaengig von der Feldgroesse immer an derselben Stelle im
    // Bild - mit zwei unabhaengigen Formeln wandert er heraus.
    const double height = 0.35 * back;

    return { head.x, head.y - back, height };
}

float FieldComponent::focalPixels() const
{
    // Brennweite in Pixeln. 0,7 * Breite entspricht einem halben
    // Oeffnungswinkel von rund 35 Grad - weit genug, dass ein Vorbeiflug seitlich
    // noch ins Bild passt, und eng genug, dass die Tiefe nicht flach aussieht.
    return 0.7f * (float) juce::jmax (1, getWidth());
}

float FieldComponent::horizonYPx() const
{
    // Horizont ueber der Bildmitte: unterhalb spielt sich der Boden ab, und der
    // braucht mehr Platz als der leere Himmel darueber.
    return 0.40f * (float) juce::jmax (1, getHeight());
}

FieldComponent::Projected FieldComponent::project (Vec3 worldMetres) const
{
    const Vec3   cam   = cameraPosition();
    const double depth = worldMetres.y - cam.y;

    Projected out;

    if (depth < nearPlaneMetres)
        return out;   // hinter der Kamera oder zu dicht davor

    const float focal = focalPixels();
    const float scale = (float) ((double) focal / depth);

    out.px = { (float) getWidth() * 0.5f + scale * (float) (worldMetres.x - cam.x),
               horizonYPx()               - scale * (float) (worldMetres.z - cam.z) };
    out.visible = true;
    out.scale   = scale;

    return out;
}

void FieldComponent::strokeWorldPath (juce::Graphics& g, const std::vector<Vec3>& points,
                                      juce::Colour colour, float thickness) const
{
    if (points.size() < 2)
        return;

    juce::Path path;
    bool penDown = false;

    for (const auto& p : points)
    {
        const auto pr = project (p);

        if (! pr.visible)
        {
            // Teilstueck hinter der Kamera: Stift heben, statt quer durchs Bild
            // zu einem falschen Punkt zu ziehen.
            penDown = false;
            continue;
        }

        if (! penDown)
        {
            path.startNewSubPath (pr.px);
            penDown = true;
        }
        else
        {
            path.lineTo (pr.px);
        }
    }

    if (path.isEmpty())
        return;

    // Bewusst OHNE curved-Verbindungen: die Linienzuege laufen hier auf den
    // Fluchtpunkt zu, ihre letzten Teilstuecke sind kuerzer als ein Pixel, und
    // eine glaettende Verbindung schiesst dort ueber das letzte Stueck hinaus -
    // sichtbar als Linie, die ueber den Horizont hinausragt, wo eine Bodenlinie
    // nichts zu suchen hat.
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (thickness));
}

void FieldComponent::drawPerspective (juce::Graphics& g) const
{
    // Himmel und Boden getrennt einfaerben, damit der Horizont auch ohne Gitter
    // ablesbar ist.
    const float horizon = horizonYPx();

    g.setColour (juce::Colour (0xff05070c));
    g.fillRect (0.0f, 0.0f, (float) getWidth(), horizon);

    g.setColour (juce::Colour (0xff0b0b08));
    g.fillRect (0.0f, horizon, (float) getWidth(), (float) getHeight() - horizon);

    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawLine (0.0f, horizon, (float) getWidth(), horizon, 1.0f);

    drawPerspectiveGround (g);
    drawPerspectiveWalls (g);
    drawPerspectiveWavefronts (g);
    drawPerspectiveTrail (g);
    drawPerspectiveListener (g);
    drawPerspectiveSource (g);
}

void FieldComponent::drawPerspectiveGround (juce::Graphics& g) const
{
    const Vec3  cam     = cameraPosition();
    const float focal   = focalPixels();
    const float horizon = horizonYPx();

    // Exponentiell wachsende Tiefenstufen (1-2-5 je Dekade). Eine gleichmaessige
    // Teilung waere hier nutzlos: in der Perspektive fallen alle gleich weit
    // auseinanderliegenden Linien ab einer gewissen Entfernung auf denselben
    // Pixel. Mit der 1-2-5-Stufung bleibt der Abstand zwischen zwei Linien im
    // BILD ungefaehr gleich, und deshalb bleibt die Ferne lesbar.
    const double farthest = juce::jmax (20.0, 3.0 * fieldMetres);

    for (int decade = -1; decade <= 5; ++decade)
    {
        for (const double mantissa : { 1.0, 2.0, 5.0 })
        {
            const double depth = mantissa * std::pow (10.0, (double) decade);

            if (depth < nearPlaneMetres || depth > farthest)
                continue;

            // Waagerechte Linie: Bodenpunkte in dieser Tiefe. z = 0, also
            // ergibt sich die Bildhoehe direkt aus der Kamerahoehe.
            const float y = horizon + (float) ((double) focal * cam.z / depth);

            if (y < horizon || y > (float) getHeight())
                continue;

            const float alpha = 0.30f - 0.16f * (float) (std::log10 (depth) / 4.0);

            g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.06f, 0.30f, alpha)));
            g.drawLine (0.0f, y, (float) getWidth(), y, 1.0f);

            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.setFont (10.0f);
            g.drawText (formatMetres (depth), 2, (int) y - 12, 60, 12, juce::Justification::left);
        }
    }

    // Laengslinien, die zum Fluchtpunkt zusammenlaufen: das ist der Teil, der
    // die Tiefe ueberhaupt als Tiefe lesbar macht. Sie liegen bei festen
    // seitlichen Abstaenden zur Blickachse.
    const double lateral[] { 0.0, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0 };

    for (const double offset : lateral)
    {
        if (offset > farthest)
            break;

        for (const double sign : { -1.0, 1.0 })
        {
            if (offset == 0.0 && sign < 0.0)
                continue;   // die Mittellinie nur einmal

            std::vector<Vec3> line;

            // Ein Linienzug statt zweier Endpunkte: so greift die
            // Sichtbarkeitspruefung in strokeWorldPath() Stueck fuer Stueck.
            for (int i = 0; i <= 24; ++i)
            {
                const double u     = (double) i / 24.0;
                const double depth = nearPlaneMetres + u * u * (farthest - nearPlaneMetres);

                line.push_back ({ cam.x + sign * offset, cam.y + depth, 0.0 });
            }

            const float alpha = offset == 0.0 ? 0.22f : 0.12f;
            strokeWorldPath (g, line, juce::Colours::white.withAlpha (alpha), 1.0f);
        }
    }
}

void FieldComponent::drawPerspectiveWalls (juce::Graphics& g) const
{
    // Gezeichnet wird die Schnittlinie der Wand mit dem Boden plus ein paar
    // senkrechte Rippen. Eine unendliche Ebene laesst sich nicht ausmalen, aber
    // ihre Bodenlinie und ihre Aufrichtung genuegen, um zu sehen, wo sie steht.
    for (const auto& wall : snapshot.walls)
    {
        if (! wall.on)
            continue;

        const Vec3 dir { std::cos (wall.azimuthRad), std::sin (wall.azimuthRad), 0.0 };

        const double reach = juce::jmax (50.0, 3.0 * fieldMetres);

        std::vector<Vec3> ground;

        for (int i = 0; i <= 48; ++i)
        {
            const double u = -1.0 + 2.0 * (double) i / 48.0;
            ground.push_back (wall.anchor + dir * (u * reach));
        }

        const float upright = (float) std::abs (std::cos (wall.tiltRad));

        strokeWorldPath (g, ground,
                         juce::Colours::skyblue.withAlpha (0.20f + 0.30f * upright),
                         1.0f + 1.5f * upright);

        // Rippen: die Wand nach oben andeuten. Bei einer flach liegenden Wand
        // sind sie null lang, was genau richtig ist.
        const double ribHeight = (2.0 + 0.05 * fieldMetres) * (double) upright;

        for (int i = -8; i <= 8; ++i)
        {
            const Vec3 foot = wall.anchor + dir * ((double) i * reach / 8.0);

            const std::vector<Vec3> rib { foot, { foot.x, foot.y, foot.z + ribHeight } };

            strokeWorldPath (g, rib, juce::Colours::skyblue.withAlpha (0.14f), 1.0f);
        }
    }
}

void FieldComponent::drawPerspectiveWavefronts (juce::Graphics& g) const
{
    // Die Fronten sind Kugeln; gezeichnet wird ihr Schnitt mit der Bodenebene,
    // also ein Kreis um den Fusspunkt der Emissionsstelle. In der Perspektive
    // wird daraus eine Ellipse - deshalb nicht drawEllipse(), sondern ein
    // projizierter Linienzug.
    for (int i = 0; i < snapshot.wavefrontCount; ++i)
    {
        const double age = snapshot.now - snapshot.wavefrontEmitTimes[(size_t) i];

        if (age <= 0.0)
            continue;

        const Vec3   emit    = snapshot.wavefrontPositions[(size_t) i];
        const double sphereR = speedOfSound * age;

        // Liegt die Emissionsstelle ueber dem Boden, schneidet die Kugel den
        // Boden in einem kleineren Kreis - oder gar nicht.
        const double h = emit.z;

        if (sphereR <= std::abs (h))
            continue;

        const double radius = std::sqrt (sphereR * sphereR - h * h);

        std::vector<Vec3> circle;

        for (int k = 0; k <= 72; ++k)
        {
            const double a = 6.283185307179586 * (double) k / 72.0;
            circle.push_back ({ emit.x + radius * std::cos (a),
                                emit.y + radius * std::sin (a),
                                0.0 });
        }

        const float alpha = juce::jmap ((float) i, 0.0f,
                                        (float) juce::jmax (1, snapshot.wavefrontCount - 1),
                                        0.45f, 0.07f);

        strokeWorldPath (g, circle, juce::Colours::cyan.withAlpha (alpha), 1.2f);
    }
}

void FieldComponent::drawPerspectiveTrail (juce::Graphics& g) const
{
    if (snapshot.trailCount < 2)
        return;

    std::vector<Vec3> trail;

    for (int i = 0; i < snapshot.trailCount; ++i)
        trail.push_back (snapshot.trail[(size_t) i]);

    strokeWorldPath (g, trail, juce::Colours::orange.withAlpha (0.6f), 1.5f);

    // Der Schatten der Spur auf dem Boden. Ohne ihn ist bei einer fliegenden
    // Quelle nicht zu sehen, ob sie hoch und nah oder tief und fern ist - genau
    // die Frage, fuer die es diese Ansicht gibt.
    std::vector<Vec3> shadow;

    for (const auto& p : trail)
        shadow.push_back ({ p.x, p.y, 0.0 });

    strokeWorldPath (g, shadow, juce::Colours::orange.withAlpha (0.18f), 1.0f);
}

void FieldComponent::drawPerspectiveSource (juce::Graphics& g) const
{
    const Vec3 pos = snapshot.sourcePos;

    const auto pr = project (pos);

    if (! pr.visible)
    {
        // Hinter der Kamera: nur ein Hinweis am Bildrand, dass die Quelle
        // ueberhaupt existiert. Ohne den wirkt eine leere Ansicht wie ein
        // Fehler, obwohl sie richtig ist.
        g.setColour (juce::Colours::yellow.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f)
                           .withCentre ({ (float) getWidth() * 0.5f, (float) getHeight() - 6.0f }));
        return;
    }

    // Ausserhalb des Bildes, aber vor der Kamera: Marke am Rand in der
    // Richtung, in der die Quelle liegt. Auch das ist ein Hinweis und keine
    // Verlegenheitsloesung - bei einem Vorbeiflug in 90 m seitlichem Abstand und
    // 25 m Tiefe liegt sie schlicht ausserhalb des Blickfelds, und das soll man
    // sehen statt es zu raten.
    if (pr.px.x < 0.0f || pr.px.x > (float) getWidth()
        || pr.px.y < 0.0f || pr.px.y > (float) getHeight())
    {
        const juce::Point<float> edge {
            juce::jlimit (6.0f, (float) getWidth()  - 6.0f, pr.px.x),
            juce::jlimit (6.0f, (float) getHeight() - 6.0f, pr.px.y)
        };

        g.setColour (juce::Colours::yellow.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (edge));
        g.setColour (juce::Colours::yellow.withAlpha (0.30f));
        g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (edge), 1.2f);
        return;
    }

    // Lotlinie auf den Boden plus Fusspunkt: das ist die Stelle, an der die
    // Hoehe ablesbar wird.
    const Vec3 foot { pos.x, pos.y, 0.0 };
    const auto footPr = project (foot);

    if (footPr.visible)
    {
        g.setColour (juce::Colours::yellow.withAlpha (0.35f));
        g.drawLine (juce::Line<float> (pr.px, footPr.px), 1.0f);

        g.setColour (juce::Colours::yellow.withAlpha (0.30f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 3.0f).withCentre (footPr.px));
    }

    // Symbolgroesse mit der Entfernung, aber nach unten und oben begrenzt: eine
    // punktfoermige Quelle hat keine Groesse, und ein Punkt, der beim Vorbeiflug
    // das halbe Bild fuellt, sagt nichts mehr aus.
    const float r = juce::jlimit (2.5f, 22.0f, pr.scale * 0.4f);

    g.setColour (juce::Colours::yellow);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pr.px));

    g.setColour (juce::Colours::yellow.withAlpha (0.5f));

    for (int ring = 1; ring <= 3; ++ring)
    {
        const float rr = r + (float) ring * 4.0f;
        g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (pr.px), 1.2f);
    }
}

void FieldComponent::drawPerspectiveListener (juce::Graphics& g) const
{
    const Vec3 head = snapshot.listener.head;
    const auto pr   = project (head);

    if (! pr.visible)
        return;

    // Blickrichtung wie in der Draufsicht aus zwei projizierten Punkten, damit
    // die Perspektive auch den Nasenwinkel uebernimmt statt ihn zu erfinden.
    const auto nosePr = project (head + listenerNose (snapshot.listener));

    const float yaw = nosePr.visible ? std::atan2 (nosePr.px.y - pr.px.y, nosePr.px.x - pr.px.x)
                                     : -1.5707963267948966f;

    const float r = juce::jlimit (6.0f, 40.0f, pr.scale * 0.35f);

    HeadSymbol::Style style;
    style.headColour = juce::Colours::white;
    style.earColour  = juce::Colours::white;
    style.fillColour = juce::Colours::white.withAlpha (0.08f);

    HeadSymbol::draw (g, pr.px, r, yaw, style);

    // Lotlinie auch beim Hoerer: seine Ohrhoehe ist ein Regler, und man soll
    // sehen, dass er ueber dem Boden steht.
    const auto footPr = project (Vec3 { head.x, head.y, 0.0 });

    if (footPr.visible)
    {
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawLine (juce::Line<float> (pr.px, footPr.px), 1.0f);
    }
}

// ---- Maus / Drag -------------------------------------------------------------

FieldComponent::DragTarget FieldComponent::dragTargetAt (juce::Point<float> screenPx) const
{
    if (viewMode == ViewMode::Perspective)
    {
        // In der Perspektive gibt es nur ein Ziel: die Quelle. Den Hoerer dort
        // zu verschieben waere zweideutig (waagerechte Mausbewegung koennte
        // Seite ODER Tiefe heissen), und fuer seine Drehung fehlt der Bezug -
        // beides bleibt der Draufsicht vorbehalten.
        const auto pr = project (snapshot.sourcePos);

        if (pr.visible)
        {
            const float r = juce::jlimit (2.5f, 22.0f, pr.scale * 0.4f);

            if (screenPx.getDistanceFrom (pr.px) <= r + dragHitRadiusPx)
                return DragTarget::source;
        }

        return DragTarget::none;
    }

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
    if (viewMode == ViewMode::Perspective)
    {
        if (dragTarget != DragTarget::source)
            return;

        // Umkehrung von project() bei FESTGEHALTENER Tiefe: waagerecht wird die
        // Seitenlage, senkrecht die Hoehe gestellt. Die Tiefe bleibt, weil sie
        // aus einem einzelnen Bildpunkt nicht hervorgeht - eine Maus hat zwei
        // Achsen, der Raum drei.
        //
        // Damit ist diese Ansicht der einzige Weg, die Hoehe mit der Maus zu
        // setzen; in der Draufsicht gibt es dafuer keine Achse.
        const Vec3   cam   = cameraPosition();
        const double depth = snapshot.sourcePos.y - cam.y;

        if (depth < nearPlaneMetres)
            return;

        const double focal = (double) focalPixels();

        const double worldX = cam.x + ((double) screenPx.x - (double) getWidth() * 0.5) * depth / focal;
        const double worldZ = cam.z + ((double) horizonYPx() - (double) screenPx.y) * depth / focal;

        const double normX = juce::jlimit (0.0, 1.0, worldX / juce::jmax (1.0e-6, fieldMetres));

        if (onSourceDragged)
        {
            // Tiefe unveraendert weitermelden: der Rueckkanal ist normiert, und
            // die y-Normierung haengt am Seitenverhaeltnis der Flaeche.
            const double normY = juce::jlimit (0.0, 1.0,
                                               snapshot.sourcePos.y
                                               / juce::jmax (1.0e-6, fieldHeightMetres()));
            onSourceDragged (normX, normY);
        }

        if (onSourceHeightDragged)
            onSourceHeightDragged (juce::jmax (0.0, worldZ));

        return;
    }

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
    haveDragVelocity = false;

    if (dragTarget == DragTarget::source && onSourceGrabbed)
        onSourceGrabbed();

    if (dragTarget != DragTarget::none)
    {
        handleDragTo (e.position);

        if (viewMode == ViewMode::TopDown
            && (dragTarget == DragTarget::source || dragTarget == DragTarget::listenerHead))
        {
            lastDragWorldPos = screenToWorld (e.position);
            lastDragTimeMs   = juce::Time::getMillisecondCounterHiRes();
        }
    }
}

void FieldComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTarget == DragTarget::none)
        return;

    handleDragTo (e.position);

    // Geschwindigkeit nur fuer die Ziele schaetzen, die der Nachlauf ueberhaupt
    // unterstuetzt (siehe setCoastEnabled-Kommentar in FieldComponent.h) -
    // Perspektive und Kopfdrehung bleiben aussen vor.
    if (viewMode != ViewMode::TopDown
        || (dragTarget != DragTarget::source && dragTarget != DragTarget::listenerHead))
        return;

    const Vec3   pos = screenToWorld (e.position);
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt  = (now - lastDragTimeMs) * 0.001;

    if (dt > 1.0e-4)   // gegen Division durch (fast) null bei mehreren Events in derselben ms
    {
        const Vec3 instantVelocity = (pos - lastDragWorldPos) * (1.0 / dt);

        // Leicht geglaettet statt der rohen letzten Momentaufnahme - ein
        // einzelnes sehr kurzes/zittriges Mausereignis soll die
        // Nachlauf-Anfangsgeschwindigkeit nicht allein bestimmen.
        dragVelocityEstimate = haveDragVelocity
            ? dragVelocityEstimate + (instantVelocity - dragVelocityEstimate) * 0.6
            : instantVelocity;
        haveDragVelocity = true;
    }

    lastDragWorldPos = pos;
    lastDragTimeMs   = now;
}

void FieldComponent::mouseUp (const juce::MouseEvent&)
{
    const DragTarget released = dragTarget;
    dragTarget = DragTarget::none;

    if (coastEnabled && haveDragVelocity
        && (released == DragTarget::source || released == DragTarget::listenerHead)
        && dragVelocityEstimate.lengthSquared() > coastMinSpeedSquared)
    {
        // Integral von v0*exp(-t*ln(2)/halfLife) über t=0..unendlich =
        // v0*halfLife/ln(2) - der Gesamtweg des gedachten Abklingens in EINEM
        // Schritt statt ihn tickweise nachzubilden (siehe Klassenkommentar in
        // FieldComponent.h, warum das die vorherige Fassung mit "Slew
        // Limiter" kollidieren liess).
        const Vec3 projected = lastDragWorldPos
                              + dragVelocityEstimate * (coastHalfLifeSeconds / std::log (2.0));

        reportNormalisedDrag (projected, released == DragTarget::source);
    }

    haveDragVelocity = false;

    if (released == DragTarget::source && onSourceReleased)
        onSourceReleased();
}

void FieldComponent::setCoastEnabled (bool shouldCoast)
{
    coastEnabled = shouldCoast;
}
