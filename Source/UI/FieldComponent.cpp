#include "FieldComponent.h"
#include "Labels.h"
#include "Tooltips.h"
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

    // Dauerhafter Merker fuer den Nachlauf-Schalter (@dpa-Beschwerde: "immer
    // an", weil coastEnabled bewusst KEIN APVTS-Parameter ist, siehe
    // PluginProcessor.h - der Zustand wurde bisher also nie gespeichert und
    // stand nach jedem Laden wieder auf dem Default true). Eigene
    // ApplicationProperties, dieselbe Datei wie WelcomeOverlays
    // "welcomeSeen"-Merker (gleiche Options: Ordner/Name "Dopplerfeld"),
    // nur ein anderer Schluessel - gilt damit fuer Plugin UND Standalone
    // gleichermassen, unabhaengig vom Host-Preset/-Projekt.
    // Function-lokales static: legt die Storage-Parameter genau einmal fest,
    // bevor irgendein Zugriff stattfindet (ApplicationProperties liefert erst
    // nach setStorageParameters() eine echte PropertiesFile zurueck).
    juce::ApplicationProperties& coastProperties()
    {
        static juce::ApplicationProperties properties;
        static bool initialised = false;

        if (! initialised)
        {
            juce::PropertiesFile::Options options;
            options.applicationName     = "Dopplerfeld";
            options.filenameSuffix      = ".settings";
            options.folderName          = "Dopplerfeld";
            options.osxLibrarySubFolder = "Application Support";

            properties.setStorageParameters (options);
            initialised = true;
        }

        return properties;
    }

    bool loadPersistedCoastEnabled()
    {
        return coastProperties().getUserSettings()->getBoolValue ("coastEnabled", true);
    }

    void savePersistedCoastEnabled (bool shouldCoast)
    {
        auto* settings = coastProperties().getUserSettings();
        settings->setValue ("coastEnabled", shouldCoast);
        settings->saveIfNeeded();
    }
}

FieldComponent::FieldComponent()
{
    setSize (700, 400); // Plan 3.13: exakte Groesse
    setWantsKeyboardFocus (true); // fuer Tastatur-Kurzbefehle, s. keyPressed()

    // Letzten Nachlauf-Zustand holen statt immer beim Default true zu starten
    // (siehe coastProperties() oben) - der Editor fragt isCoastEnabled() kurz
    // nach diesem Konstruktor ab, um coastButton entsprechend zu setzen.
    coastEnabled = loadPersistedCoastEnabled();
}

void FieldComponent::setTooltip (const juce::String& newTooltip)
{
    // Haengt den Text zur Perspektiv-Bedienung an das an, was PluginEditor.cpp
    // hier setzt (Basistext: Ziehen an M/Kopf/Nase) - siehe Header-Kommentar.
    juce::SettableTooltipClient::setTooltip (
        newTooltip + Tooltips::text (Tooltips::Key::FieldPerspectiveHelp));
}

void FieldComponent::setSnapshot (const FieldSnapshot& snapshotIn)
{
    snapshot = snapshotIn; // Wertkopie, FieldSnapshot ist klein und allokationsfrei

    // Tiefpass fuer die jitterfreie Ankerposition, s. Kommentar bei
    // sourceAnchorWorld (FieldComponent.h). snapshot.now ist die Engine-Zeit
    // in Sekunden (dieselbe Basis wie die Wellenfront-Emissionszeiten) - damit
    // laeuft die Glaettung an der TATSAECHLICHEN Zeit zwischen zwei Snapshots
    // statt an einer angenommenen festen Taktrate.
    const double dt = snapshot.now - lastAnchorSnapshotTime;

    if (! haveSourceAnchor || dt <= 0.0 || dt > 1.0)
    {
        // Erster Aufruf oder Zeitsprung (z.B. Loop-Neustart, Engine-Reset) -
        // kein Nachziehen ueber eine Distanz, die so gar nicht durchlaufen wurde.
        sourceAnchorWorld = snapshot.sourcePos;
    }
    else
    {
        const double alpha = 1.0 - std::exp (-dt / sourceAnchorSmoothTauSeconds);
        sourceAnchorWorld = sourceAnchorWorld + (snapshot.sourcePos - sourceAnchorWorld) * alpha;
    }

    lastAnchorSnapshotTime = snapshot.now;
    haveSourceAnchor = true;

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

double FieldComponent::convertSpeed (double sourceSpeedMps, double speedOfSoundMps, SpeedUnit unit)
{
    switch (unit)
    {
        case SpeedUnit::KmH:  return sourceSpeedMps * 3.6;
        case SpeedUnit::Ms:   return sourceSpeedMps;
        case SpeedUnit::Mach: return sourceSpeedMps / juce::jmax (1.0, speedOfSoundMps);
    }

    return sourceSpeedMps;
}

juce::String FieldComponent::formatSpeed (double sourceSpeedMps, double speedOfSoundMps, SpeedUnit unit)
{
    const double value = convertSpeed (sourceSpeedMps, speedOfSoundMps, unit);
    const char* label = unit == SpeedUnit::KmH ? "km/h" : unit == SpeedUnit::Ms ? "m/s" : "Mach";

    // Feste Breite (@dpa-Feedback "Langsamkeit der Anzeigewahrnehmung"): egal
    // wie viele Stellen die Zahl gerade hat, Zahl und Einheit duerfen im
    // laufenden Betrieb nicht seitlich wandern - nur die Ziffern selbst
    // duerfen sich aendern. 9 Stellen vor dem Komma decken auch km/h beim
    // groessten einstellbaren Tempo ab (100000 m/s = 360000 km/h), ohne dass
    // etwas abgeschnitten wird. Alle Abnehmer (Anzeige im Feld, Statuszeile,
    // Tempo-Regler) bekommen die Zeichenkette damit schon hier auf fester
    // Breite, statt selbst nachtraeglich padden zu muessen.
    return juce::String::formatted ("%9.1f %-4s", value, label);
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

void FieldComponent::setPerspectiveFromListener (bool shouldUseListenerView)
{
    if (shouldUseListenerView == perspectiveFromListener)
        return;

    perspectiveFromListener = shouldUseListenerView;
    repaint();
}

void FieldComponent::paint (juce::Graphics& g)
{
    if (viewMode == ViewMode::Perspective)
    {
        drawPerspective (g);
        drawSpeedReadout (g);
        drawDistanceReadout (g);
        return;
    }

    g.fillAll (juce::Colours::black);
    drawGrid (g);
    drawWalls (g);
    drawWavefronts (g);
    drawReflectionWavefronts (g);
    drawTrail (g);
    drawFlyByPreview (g);
    drawSource (g);
    drawListener (g);
    drawSpeedReadout (g);
    drawDistanceReadout (g);
}

void FieldComponent::drawSpeedReadout (juce::Graphics& g) const
{
    // Cockpit-Tempoanzeige (@dpa-Feedback: "wie im Cockpit, aber nicht
    // uebertrieben"). Alle drei Einheiten stehen gleichzeitig nebeneinander
    // (Mach, m/s, km/h), die Einheit klein unter der jeweiligen Zahl - @dpa
    // kann mit m/s schlecht rechnen und will nicht erst umschalten muessen.
    // Alpha-Gelb #ffff0055, oben rechts. Ohne Rahmen (@dpa 20260819) - nur ein
    // schwacher Flaechenton dahinter, das soll ins Bild einsinken, nicht
    // draufkleben.
    //
    // Pixelfest (@dpa-Regel "Zahlenanzeige pixelfest", wichtigste Vorgabe
    // hier): jede Spalte bekommt eine FESTE Zeichenzahl (printf-Padding),
    // gezeichnet in Monospace (gleiche Glyphenbreite fuer jede Ziffer) - nur
    // die Ziffern selbst duerfen wechseln, nie Spaltenbreite oder Position.
    // Die Breite je Spalte ist auf den vollen Reglerbereich bemessen
    // (globalMaxSpeed geht bis 100000 m/s = 360000 km/h, s. Params.cpp), nicht
    // auf den ueblichen Fall: sonst schneidet eine zu schmale Box den Text ab,
    // und JUCE tut das ohne Auslassungszeichen, also unbemerkt.
    struct Column { double value; int intDigits; int decimals; const char* unit; };

    const Column columns[3] =
    {
        // Mach bis ca. 8 im Normalfall, aber ohne verstecktes Limit bis zum
        // theoretischen Maximum von globalMaxSpeed/langsamster Schallgeschw.
        // (~300) - 3 Vorkommastellen lassen dafuer Luft, ohne die Spalte
        // unnoetig breit zu machen.
        { convertSpeed (displaySpeedMps, displaySpeedOfSoundMps, SpeedUnit::Mach), 3, 2, "Mach" },
        { convertSpeed (displaySpeedMps, displaySpeedOfSoundMps, SpeedUnit::Ms),   6, 1, "m/s"  },
        { convertSpeed (displaySpeedMps, displaySpeedOfSoundMps, SpeedUnit::KmH),  6, 1, "km/h" },
    };

    // @dpa-Feedback (20260819): Zahlen doppelt so gross, dafuer Rahmen weg und
    // Aussen-/Spaltenabstand deutlich enger - die Anzeige soll dadurch nicht
    // mehr Platz beanspruchen als vorher. Die Einheit waechst im Verhaeltnis
    // mit, bleibt aber bewusst klein.
    const juce::Font numberFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 30.0f, juce::Font::bold));
    const juce::Font unitFont   (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));

    // Spaltenbreiten aus der festen Zeichenzahl je Spalte, NICHT aus dem
    // aktuellen Zahlenwert - die Box selbst haengt damit an keiner Stelle vom
    // gerade angezeigten Tempo ab und kann folglich nicht "zappeln". Punkt
    // zaehlt als eigenes Zeichen mit (Vorkommastellen + Punkt + Nachkommastellen).
    const float digitWidth = juce::GlyphArrangement::getStringWidth (numberFont, "0");
    constexpr int columnGap = 5;
    constexpr int sidePad   = 3;
    constexpr int topPad    = 2;
    constexpr int numberRowHeight = 38;
    constexpr int unitRowHeight   = 17;

    int columnWidths[3] {};
    int contentWidth = 0;

    for (int i = 0; i < 3; ++i)
    {
        const int chars = columns[i].intDigits + 1 + columns[i].decimals; // +1 fuer den Dezimalpunkt
        columnWidths[i] = (int) std::ceil (digitWidth * (float) chars);
        contentWidth += columnWidths[i];
    }

    contentWidth += columnGap * 2; // zwei Luecken zwischen drei Spalten

    const int w = sidePad * 2 + contentWidth;
    const int h = topPad * 2 + numberRowHeight + unitRowHeight;
    const juce::Rectangle<int> box (getWidth() - w - 10, 10, w, h);

    const juce::Colour hudYellow (0xffffff00);

    // Kein Rahmen mehr (@dpa 20260819) - nur der schwache Flaechenton bleibt,
    // der schafft genug Kontrast zum Feld ohne eine Kontur zu ziehen.
    g.setColour (hudYellow.withAlpha (0.06f));
    g.fillRoundedRectangle (box.toFloat(), 4.0f);

    int x = box.getX() + sidePad;
    const int numberY = box.getY() + topPad;
    const int unitY   = numberY + numberRowHeight;

    for (int i = 0; i < 3; ++i)
    {
        const auto& col = columns[i];

        // Dynamisches printf-Format aus fester Vor-/Nachkommastellenzahl -
        // dieselbe Feldbreite bei jedem Aufruf, egal wie gross der Wert ist.
        const juce::String fmt = "%" + juce::String (col.intDigits + 1 + col.decimals)
                                      + "." + juce::String (col.decimals) + "f";
        const juce::String numberText = juce::String::formatted (fmt.toRawUTF8(), col.value);

        g.setColour (hudYellow.withAlpha (0x55 / 255.0f));
        g.setFont (numberFont);
        g.drawText (numberText, x, numberY, columnWidths[i], numberRowHeight,
                    juce::Justification::centred, false);

        g.setColour (hudYellow.withAlpha (0x40 / 255.0f));
        g.setFont (unitFont);
        g.drawText (col.unit, x, unitY, columnWidths[i], unitRowHeight,
                    juce::Justification::centred, false);

        x += columnWidths[i] + columnGap;
    }
}

void FieldComponent::drawDistanceReadout (juce::Graphics& g) const
{
    // Derselbe Aufbau wie drawSpeedReadout, nur eine Spalte und links statt
    // rechts. Bewusst dieselbe Farbe und dieselbe Schrift: Tempo und
    // Entfernung gehoeren zusammen, das eine ist die Ableitung des anderen.
    //
    // Pixelfest wie dort: feste Zeichenzahl in Monospace, damit sich beim
    // Zaehlen nichts verschiebt. Sechs Vorkommastellen decken die Diagonale
    // des groessten Feldes (10000 m Kantenlaenge) mit Luft ab.
    const double distance = (snapshot.sourcePos - snapshot.listener.head).length();

    constexpr int intDigits = 6;
    constexpr int decimals  = 1;

    const juce::Font numberFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 30.0f, juce::Font::bold));
    const juce::Font unitFont   (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));

    const float digitWidth = juce::GlyphArrangement::getStringWidth (numberFont, "0");

    constexpr int sidePad = 3;
    constexpr int topPad  = 2;
    constexpr int numberRowHeight = 38;
    constexpr int unitRowHeight   = 17;

    // Die Beschriftung ist laenger als die Zahl - die Box richtet sich nach
    // dem breiteren von beiden, sonst schnitte JUCE den Text ab, und zwar
    // ohne Auslassungszeichen, also unbemerkt.
    const juce::String label = Labels::text ("Entfernung");

    const int numberWidth = (int) std::ceil (digitWidth * (float) (intDigits + 1 + decimals + 2));
    const int labelWidth  = (int) std::ceil (juce::GlyphArrangement::getStringWidth (unitFont, label)) + 6;

    const int contentWidth = juce::jmax (numberWidth, labelWidth);

    const int w = sidePad * 2 + contentWidth;
    const int h = topPad * 2 + numberRowHeight + unitRowHeight;
    const juce::Rectangle<int> box (10, 10, w, h);

    const juce::Colour hudYellow (0xffffff00);

    g.setColour (hudYellow.withAlpha (0.06f));
    g.fillRoundedRectangle (box.toFloat(), 4.0f);

    const juce::String fmt = "%" + juce::String (intDigits + 1 + decimals)
                                 + "." + juce::String (decimals) + "f m";

    g.setColour (hudYellow.withAlpha (0x55 / 255.0f));
    g.setFont (numberFont);
    g.drawText (juce::String::formatted (fmt.toRawUTF8(), distance),
                box.getX() + sidePad, box.getY() + topPad, contentWidth, numberRowHeight,
                juce::Justification::centred, false);

    g.setColour (hudYellow.withAlpha (0x40 / 255.0f));
    g.setFont (unitFont);
    g.drawText (label,
                box.getX() + sidePad, box.getY() + topPad + numberRowHeight,
                contentWidth, unitRowHeight,
                juce::Justification::centred, false);
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

float FieldComponent::wavefrontBrightnessFactor() const
{
    // DopplerEngine::publishSnapshot() klemmt den Emissions-Zeitabstand nach
    // unten auf 0.02s (spacing = clamp(fieldMetres/(c*maxWavefronts), 0.02,
    // 0.5)). Bei kleinen Feldern greift dieser Boden, und es liegen relativ
    // zur Feldflaeche deutlich mehr, dichter gepackte Ringe im Bild als bei
    // dem Feld, gegen das die Grundhelligkeit unten getuned ist - ohne
    // Gegenskalierung wirkt das Feld dort ueberfuellt/zu hell (@dpa 20260818,
    // Screenshot "voller heller Schallkreise"). Nach unten grosszuegig
    // gedeckelt, damit auch bei sehr kleinen Feldern noch etwas zu sehen
    // bleibt - "alle hoerbaren sollen sichtbar bleiben".
    constexpr double referenceFieldMetres = 6000.0;
    return (float) juce::jlimit (0.30, 1.0, fieldMetres / referenceFieldMetres);
}

void FieldComponent::drawWavefronts (juce::Graphics& g) const
{
    // Jede Front sitzt an der Quellposition zum EIGENEN Emissionszeitpunkt
    // M(t_k) (FieldSnapshot::wavefrontPositions), nicht an der aktuellen -
    // erst dieser Versatz erzeugt die vorne gestauchten Fronten und bei
    // Ueberschall die Einhuellende, die den Mach-Kegel bildet (Plan 3.12).
    const float pxPerM = pixelsPerMetre();
    const float brightness = wavefrontBrightnessFactor();

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
        const float alpha = brightness * juce::jmap ((float) i, 0.0f,
                                         (float) juce::jmax (1, snapshot.wavefrontCount - 1),
                                         0.55f, 0.08f);
        g.setColour (juce::Colours::cyan.withAlpha (alpha));
        g.drawEllipse (juce::Rectangle<float> (radiusPx * 2.0f, radiusPx * 2.0f).withCentre (centrePx), 1.2f);
    }
}

void FieldComponent::drawReflectionWavefronts (juce::Graphics& g) const
{
    // Bildquellen-Kreise fuer Wandreflexionen (@dpa: "kannst Du die
    // Reflektionen ... darstellen, aehnlich den Cyan Kreisen") - dieselbe
    // Konstruktion wie drawWavefronts(), nur um die gespiegelten Positionen
    // aus dem Snapshot statt um die echte Quellposition, und in eigener
    // Farbe, damit man Direktschall und Reflexion auseinanderhaelt.
    const float pxPerM = pixelsPerMetre();
    const float brightness = wavefrontBrightnessFactor();

    auto drawSet = [&] (const FieldSnapshot::ImageWavefronts& wf, juce::Colour colour, float thickness)
    {
        // wf.gain: stetiges Wand-Seiten-Mass (DopplerEngine::wallSideGain),
        // 0 sobald Quelle/Hoerer nicht mehr auf der Seite stehen, von der aus
        // die Wand ueberhaupt zurueckwerfen kann - dieselbe Groesse wie im
        // Audiothread, damit hier nicht sichtbar ist, was dort schon still
        // ist (@dpa: "hinter den Waenden soll eigentlich nichts reflektieren").
        if (! wf.active || wf.gain <= 0.001f)
            return;

        for (int i = 0; i < snapshot.wavefrontCount; ++i)
        {
            const double age = snapshot.now - snapshot.wavefrontEmitTimes[(size_t) i];
            if (age <= 0.0)
                continue;

            const double radiusM  = speedOfSound * age;
            const float  radiusPx = (float) (radiusM * pxPerM);
            if (radiusPx <= 0.5f || radiusPx > 4000.0f)
                continue;

            const auto centrePx = worldToScreen (wf.positions[(size_t) i]);

            // Deutlich kraeftiger als der erste Wurf (@dpa: "muessen
            // sichtbarer sein, 'leise' vom Gemuet her, aber sichtbar") -
            // jetzt in der Naehe der Direktschall-Kreise (0.55/0.08), nicht
            // mehr darunter.
            const float alpha = brightness * wf.gain
                               * juce::jmap ((float) i, 0.0f,
                                            (float) juce::jmax (1, snapshot.wavefrontCount - 1),
                                            0.65f, 0.12f);
            g.setColour (colour.withAlpha (alpha));
            g.drawEllipse (juce::Rectangle<float> (radiusPx * 2.0f, radiusPx * 2.0f).withCentre (centrePx), thickness);
        }
    };

    for (const auto& wf : snapshot.wallWavefronts)
        drawSet (wf, juce::Colours::violet, 1.6f);

    // Mehrfachreflexion etwas duenner (eigene Farbe) - sonst wird das Feld
    // bei zwei aktiven Waenden schnell unruhig.
    for (const auto& wf : snapshot.wallPairWavefronts)
        drawSet (wf, juce::Colours::hotpink, 1.3f);
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

FieldComponent::SourceMarker FieldComponent::topDownSourceMarker() const
{
    // Dieselbe Position wie drawSource() beim normalen M (dragTarget==source
    // -> zuletzt gemeldete Zielposition, sonst snapshot.sourcePos) - keine
    // Anker-Glaettung (sourceAnchorWorld), die ist nur fuers Fangen von M
    // IM Feld gedacht (s. dragTargetAt()). EIN Ort fuer Zeichnen UND
    // Hit-Test der Randmarke, analog perspectiveSourceMarker() oben.
    const Vec3 pos = dragTarget == DragTarget::source ? sourceDragWorldOverride : snapshot.sourcePos;
    const auto px  = worldToScreen (pos);

    // 6 px Randabstand wie in der Perspektive (perspectiveSourceMarker()) -
    // dieselbe Randmarken-Konvention in beiden Ansichten. Nach oben mehr
    // Abstand (topDownMarkerTopMarginPx), damit die Marke nicht unter der
    // Tempo-/Entfernungsanzeige verschwindet, die oben ueber ihr liegt (s.
    // dort). Liegt M ohnehin im Bild, aendert die Klemmung nichts an px.
    const juce::Point<float> edge {
        juce::jlimit (6.0f, (float) getWidth()  - 6.0f, px.x),
        juce::jlimit (topDownMarkerTopMarginPx, (float) getHeight() - 6.0f, px.y)
    };
    return { edge, 4.5f };
}

void FieldComponent::drawSource (juce::Graphics& g) const
{
    // Der Schwarm zuerst, damit die Quelle selbst obenauf liegt. Klein und
    // blass: die Klone sind ein Umfeld, kein zweites M - man soll sehen, wie
    // weit sie streuen und dass sie einzeln wackeln, ohne dass die eigentliche
    // Quelle darin untergeht.
    if (showClones)
    {
        // Deutlich genug, um sie zwischen Bewegungsspur und Wellenringen zu
        // finden: ein gefuellter Punkt mit Rand. Zu klein und zu blass sind sie
        // im Feld schlicht nicht auffindbar, und dann wirkt ein funktionierender
        // Schwarm wie ein kaputter.
        const float r = sourceRadiusPx * 0.7f;
        const auto  view = getLocalBounds().toFloat().reduced (r + 1.0f);

        for (int i = 0; i < snapshot.clonePositionCount; ++i)
        {
            const auto p = worldToScreen (snapshot.clonePositions[(size_t) i]);

            // Eine Streuung groesser als das Feld setzt Klone ausserhalb des
            // sichtbaren Ausschnitts ab. Sie verschwinden dann spurlos, und ein
            // Schwarm, der laeuft, sieht aus wie keiner. Deshalb werden sie an
            // den Rand geklemmt und dort hohl gezeichnet: der Punkt sagt "hier
            // entlang, aber weiter draussen", statt gar nichts zu sagen.
            const bool outside = ! view.contains (p);
            const auto clamped = juce::Point<float> (juce::jlimit (view.getX(), view.getRight(),  p.x),
                                                     juce::jlimit (view.getY(), view.getBottom(), p.y));

            const auto box = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (clamped);

            if (! outside)
            {
                g.setColour (juce::Colours::yellow.withAlpha (0.45f));
                g.fillEllipse (box);
            }

            g.setColour (juce::Colours::yellow.withAlpha (outside ? 0.55f : 0.85f));
            g.drawEllipse (box, outside ? 1.5f : 1.0f);
        }
    }

    // Waehrend M gezogen wird: die zuletzt gemeldete Zielposition statt der
    // (moeglicherweise gejitterten) Snapshot-Position, s. sourceDragWorldOverride.
    const Vec3 sourcePos = dragTarget == DragTarget::source ? sourceDragWorldOverride : snapshot.sourcePos;
    const auto centrePx  = worldToScreen (sourcePos);

    if (! getLocalBounds().toFloat().contains (centrePx))
    {
        // M steht ausserhalb des sichtbaren Feldes (@dpa 20260825: "manchmal
        // fliegt M weiter als das Feld gross, oder ist irgendwie draussen...
        // dann will man ihn irgendwann wieder haben. In der Perspektivansicht
        // geht das - so aehnlich soll es auch in der Draufsicht sein"). Statt
        // des normalen Punkts nur noch die an den Rand geklemmte Randmarke -
        // Ring statt gefuellter Punkt, damit auf einen Blick klar ist "M ist
        // eigentlich weiter draussen" (gleicher Stil wie die Perspektiv-
        // Randmarke, s. drawPerspectiveSource()). dragTargetAt() greift auf
        // dieselbe Stelle zu (topDownSourceMarker()); ein Klick/Zug darauf
        // holt M ueber den normalen Drag-Pfad zurueck.
        const auto marker = topDownSourceMarker();

        g.setColour (juce::Colours::yellow.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (marker.radiusPx * 2.0f, marker.radiusPx * 2.0f)
                           .withCentre (marker.px));
        g.setColour (juce::Colours::yellow.withAlpha (0.30f));
        g.drawEllipse (juce::Rectangle<float> (marker.radiusPx * 2.0f + 7.0f, marker.radiusPx * 2.0f + 7.0f)
                           .withCentre (marker.px), 1.2f);
        return;
    }

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
// Die Kamera steht im Standardfall hinter und ueber dem Hoerer, damit er
// selbst im Bild ist; beide Abstaende wachsen mit der Feldgroesse, sonst
// waere die Ansicht bei n = 10000 m ein Blick auf die eigene Schuhspitze und
// bei n = 5 m ein Blick aus dem Weltall. Das sind Gestaltungswerte, keine
// gemessenen Groessen.
//
// "Aus L Sicht" (perspectiveFromListener, s. setPerspectiveFromListener()):
// die Kamera steht stattdessen direkt an der Hoererposition und blickt
// entlang seiner Nase statt fest in Richtung Welt-+y - cameraForward()/
// cameraRight() tragen diese Drehung, project() unten kennt beide Faelle
// nicht mehr einzeln.

Vec3 FieldComponent::cameraPosition() const
{
    const Vec3 head = snapshot.listener.head;

    if (perspectiveFromListener)
        return head; // genau die Stelle, aus der der Hoerer selbst hoert

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

Vec3 FieldComponent::cameraForward() const
{
    return perspectiveFromListener ? listenerNose (snapshot.listener) : Vec3 { 0.0, 1.0, 0.0 };
}

Vec3 FieldComponent::cameraRight() const
{
    return perspectiveFromListener ? listenerRight (snapshot.listener) : Vec3 { 1.0, 0.0, 0.0 };
}

float FieldComponent::focalPixels() const
{
    // Brennweite in Pixeln. 0,7 * Breite entspricht einem halben
    // Oeffnungswinkel von rund 35 Grad bei perspectiveZoom = 1 - weit genug,
    // dass ein Vorbeiflug seitlich noch ins Bild passt, und eng genug, dass
    // die Tiefe nicht flach aussieht. perspectiveZoom skaliert das
    // (@dpa-Feedback "Man braucht aber auch ein Zoom regler"), Bedienung:
    // Mausrad in der Perspektive, s. mouseWheelMove().
    return 0.7f * (float) juce::jmax (1, getWidth()) * perspectiveZoom;
}

float FieldComponent::horizonYPx() const
{
    // Horizont als Anteil der Bildhoehe von oben, standardmaessig bei 0.40 -
    // unterhalb spielt sich der Boden ab, und der braucht mehr Platz als der
    // leere Himmel darueber. perspectiveHorizonFraction ist einstellbar
    // (@dpa-Feedback "ob mit Boden mehr oben oder mittig"), Bedienung:
    // Umschalt+Mausrad in der Perspektive, s. mouseWheelMove().
    return perspectiveHorizonFraction * (float) juce::jmax (1, getHeight());
}

FieldComponent::Projected FieldComponent::project (Vec3 worldMetres) const
{
    const Vec3   cam   = cameraPosition();
    const Vec3   rel   = worldMetres - cam;
    const double depth = dot (rel, cameraForward());

    Projected out;

    if (depth < nearPlaneMetres)
        return out;   // hinter der Kamera oder zu dicht davor

    const double lateral = dot (rel, cameraRight());
    const float  focal   = focalPixels();
    const float  scale   = (float) ((double) focal / depth);

    out.px = { (float) getWidth() * 0.5f + scale * (float) lateral,
               horizonYPx()               - scale * (float) rel.z };
    out.visible = true;
    out.scale   = scale;

    return out;
}

FieldComponent::SourceMarker FieldComponent::perspectiveSourceMarker() const
{
    // Waehrend M gezogen wird: die zuletzt gemeldete Zielposition statt der
    // (moeglicherweise gejitterten) Snapshot-Position, s. sourceDragWorldOverride -
    // haelt die "EINE Stelle fuer Zeichnen UND Hit-Test" auch mitten im Drag ein.
    const auto pr = project (dragTarget == DragTarget::source ? sourceDragWorldOverride
                                                               : snapshot.sourcePos);

    if (! pr.visible)
    {
        // Hinter der Kamera: fixer Hinweis-Punkt unten mittig, keine echte
        // Bildposition (siehe drawPerspectiveSource()).
        return { { (float) getWidth() * 0.5f, (float) getHeight() - 6.0f }, 4.0f };
    }

    if (pr.px.x < 0.0f || pr.px.x > (float) getWidth()
        || pr.px.y < 0.0f || pr.px.y > (float) getHeight())
    {
        // Vor der Kamera, aber ausserhalb des Bildes: an den Rand geklemmte
        // Randmarke.
        const juce::Point<float> edge {
            juce::jlimit (6.0f, (float) getWidth()  - 6.0f, pr.px.x),
            juce::jlimit (6.0f, (float) getHeight() - 6.0f, pr.px.y)
        };
        return { edge, 4.5f };
    }

    return { pr.px, juce::jlimit (2.5f, 22.0f, pr.scale * 0.4f) };
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

    // Reflektiert der Boden, ist er eine echte Flaeche: sanft, aber in einer
    // anderen Farbe als der blosse Rasterboden - man soll auf einen Blick sehen,
    // ob unten etwas zurueckwirft oder ob dort nur ein Massstab liegt
    // (@dpa: "den Boden mit sanfter aber anderer Farbe, so dass man einen
    // Unterschied sieht").
    g.setColour (snapshot.groundReflectionOn ? juce::Colour (0xff0a1010)
                                             : juce::Colour (0xff0b0b08));
    g.fillRect (0.0f, horizon, (float) getWidth(), (float) getHeight() - horizon);

    g.setColour (juce::Colours::white.withAlpha (snapshot.groundReflectionOn ? 0.25f : 0.16f));
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

            // Ohne Reflexion ist der Boden nur ein Massstab zum Abschaetzen der
            // Weite und darf sich zurueckhalten; mit Reflexion ist er eine
            // Flaeche, die etwas tut, und darf deutlicher stehen.
            const float reach = snapshot.groundReflectionOn ? 1.0f : 0.6f;
            const float alpha = reach * (0.30f - 0.16f * (float) (std::log10 (depth) / 4.0));

            g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.04f, 0.30f, alpha)));
            g.drawLine (0.0f, y, (float) getWidth(), y, 1.0f);

            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.setFont (10.0f);
            g.drawText (formatMetres (depth), 2, (int) y - 12, 60, 12, juce::Justification::left);
        }
    }

    // Laengslinien, die zum Fluchtpunkt zusammenlaufen: das ist der Teil, der
    // die Tiefe ueberhaupt als Tiefe lesbar macht. Sie liegen bei festen
    // seitlichen Abstaenden zur Blickachse - "seitlich" heisst hier
    // cameraRight(), nicht zwingend Welt-x (s. "aus L Sicht").
    const Vec3 fwd   = cameraForward();
    const Vec3 right = cameraRight();

    // Bis 5 km statt bis 500 m: weit herausgezoomt (perspectiveZoomMin, s.
    // Header) steht ein Feld von mehreren Kilometern im Bild, und mit nur
    // zehn Linien um die Blickachse herum waere der Rest davon leer. Die
    // Schleife bricht ohnehin ab, sobald ein Abstand groesser ist als die
    // gezeichnete Tiefe - auf einem kleinen Feld aendert sich damit nichts.
    const double lateral[] { 0.0, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0,
                             500.0, 1000.0, 2000.0, 5000.0 };

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

                Vec3 groundPoint = cam + fwd * depth + right * (sign * offset);
                groundPoint.z = 0.0; // Bodenebene, unabhaengig von der Kamerahoehe
                line.push_back (groundPoint);
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
    // Der Schwarm zuerst, damit die Quelle obenauf bleibt - gleiche Regel wie in
    // der Draufsicht. In der Perspektive haben die Klone eine echte Tiefe: sie
    // stehen wirklich vor und hinter der Quelle, nicht nur neben ihr.
    if (showClones)
    {
        g.setColour (juce::Colours::yellow.withAlpha (0.35f));

        for (int i = 0; i < snapshot.clonePositionCount; ++i)
        {
            const auto cp = project (snapshot.clonePositions[(size_t) i]);

            if (! cp.visible)
                continue;

            // Groesse folgt der Tiefe wie bei der Quelle, nur kleiner - und mit
            // einer Untergrenze, damit ein weit entfernter Klon nicht ganz
            // verschwindet.
            const float r = juce::jlimit (2.0f, sourceRadiusPx, cp.scale * sourceRadiusPx * 0.7f);
            const auto box = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (cp.px);

            g.setColour (juce::Colours::yellow.withAlpha (0.45f));
            g.fillEllipse (box);

            g.setColour (juce::Colours::yellow.withAlpha (0.85f));
            g.drawEllipse (box, 1.0f);
        }
    }

    const Vec3 pos = snapshot.sourcePos;

    const auto pr     = project (pos);
    const auto marker = perspectiveSourceMarker(); // dieselbe Stelle wie der Hit-Test in dragTargetAt()

    if (! pr.visible)
    {
        // Hinter der Kamera: nur ein Hinweis am Bildrand, dass die Quelle
        // ueberhaupt existiert - und anklickbar (@dpa: ein Klick darauf holt
        // M vor die Kamera, s. handleDragTo()). Ohne den Hinweis wirkt eine
        // leere Ansicht wie ein Fehler, obwohl sie richtig ist.
        g.setColour (juce::Colours::yellow.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (marker.radiusPx * 2.0f, marker.radiusPx * 2.0f)
                           .withCentre (marker.px));
        return;
    }

    // Ausserhalb des Bildes, aber vor der Kamera: Marke am Rand in der
    // Richtung, in der die Quelle liegt. Auch das ist ein Hinweis und keine
    // Verlegenheitsloesung - bei einem Vorbeiflug in 90 m seitlichem Abstand und
    // 25 m Tiefe liegt sie schlicht ausserhalb des Blickfelds, und das soll man
    // sehen statt es zu raten. Ebenfalls anklickbar (s.o.).
    if (pr.px.x < 0.0f || pr.px.x > (float) getWidth()
        || pr.px.y < 0.0f || pr.px.y > (float) getHeight())
    {
        g.setColour (juce::Colours::yellow.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (marker.radiusPx * 2.0f, marker.radiusPx * 2.0f)
                           .withCentre (marker.px));
        g.setColour (juce::Colours::yellow.withAlpha (0.30f));
        g.drawEllipse (juce::Rectangle<float> (marker.radiusPx * 2.0f + 7.0f, marker.radiusPx * 2.0f + 7.0f)
                           .withCentre (marker.px), 1.2f);
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
    // das halbe Bild fuellt, sagt nichts mehr aus. Derselbe Wert wie
    // marker.radiusPx (perspectiveSourceMarker()), hier nur unter dem Namen r,
    // mit dem der Rest der Funktion schon rechnet.
    const float r = marker.radiusPx;

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

    // Lotlinie und Fusspunkt wie bei der Quelle (@dpa 20260826: "senkrechten
    // strich zu z=0 (wie bei m)") - erst der Boden, dann der Kopf darueber,
    // damit die Linie unter ihm endet statt ueber ihn zu laufen.
    const auto footPr = project (Vec3 { head.x, head.y, 0.0 });

    if (footPr.visible)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawLine (juce::Line<float> (pr.px, footPr.px), 1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 3.0f).withCentre (footPr.px));
    }

    // Dieselbe Zeichnung wie in der Draufsicht, nur perspektivisch verzerrt:
    // das Symbol liegt flach in der Ebene z = Ohrhoehe, jeder seiner Punkte
    // geht einzeln durch project() (@dpa 20260826: "ruhig als die gleiche
    // 2D-Darstellung, aber perspektivisch verzerrt.. quasi auf einer
    // xy-flaeche in der z-Hoehe"). Aus dem Kopfkreis wird dadurch von selbst
    // die flach liegende Ellipse, und die Nase zeigt in die Richtung, in die
    // der Hoerer im RAUM schaut - das bildschirmparallel aufgestellte Symbol
    // von vorher konnte beides nicht zeigen.
    const float rPx = juce::jlimit (6.0f, 40.0f, pr.scale * 0.35f);

    // Die Groesse bleibt eine Bildgroesse und wird nur zurueck in Meter
    // gerechnet, damit die Verzerrung eine Ebene hat, in der sie stattfinden
    // kann: ein massstaeblicher Kopf (gut 0,1 m) waere bei den ueblichen
    // Feldgroessen ein unsichtbarer Punkt.
    const double rMetres = (double) rPx / (double) juce::jmax (1.0e-6f, pr.scale);

    const Vec3 nose  = listenerNose  (snapshot.listener);
    const Vec3 right = listenerRight (snapshot.listener);

    HeadSymbol::Style style;
    style.headColour = juce::Colours::white;
    style.earColour  = juce::Colours::white;
    style.fillColour = juce::Colours::white.withAlpha (0.08f);

    HeadSymbol::drawMapped (g,
                            [&] (float lx, float ly)
                            {
                                const Vec3 world = head
                                                   + nose  * (rMetres * (double) lx)
                                                   + right * (rMetres * (double) ly);
                                const auto p = project (world);

                                // Ein Punkt der Kopfebene kann dicht vor der
                                // Kamera aus dem Bild fallen, waehrend die
                                // Kopfmitte noch steht; dann faellt er auf
                                // sie zurueck, statt die Zeichnung an den
                                // Bildursprung zu reissen.
                                return p.visible ? p.px : pr.px;
                            },
                            style);
}

// ---- Maus / Drag -------------------------------------------------------------

FieldComponent::GrabAnchor FieldComponent::grabAnchorPx() const
{
    if (dragTarget == DragTarget::source)
    {
        if (viewMode == ViewMode::Perspective)
        {
            const auto drawn  = project (snapshot.sourcePos);   // dagegen hat dragTargetAt() geprueft
            const auto anchor = project (sourceAnchorWorld);    // davon aus wird gerechnet

            // Nur der frei im Bild stehende Punkt ist anfassbar. Hinter der
            // Kamera oder ausserhalb des Bildes zeigt die Perspektive
            // stattdessen eine Randmarke, und ein Klick darauf soll M
            // ausdruecklich dorthin holen (s. drawPerspectiveSource()).
            const bool onScreen = drawn.visible
                                  && drawn.px.x >= 0.0f && drawn.px.x <= (float) getWidth()
                                  && drawn.px.y >= 0.0f && drawn.px.y <= (float) getHeight();

            return { drawn.px,
                     anchor.visible ? anchor.px : drawn.px,
                     perspectiveSourceMarker().radiusPx + sourceDragHitRadiusPx,
                     onScreen && anchor.visible };
        }

        // In der Draufsicht prueft dragTargetAt() ohnehin schon am ruhenden
        // Anker (s. sourceAnchorWorld), beide Punkte fallen also zusammen.
        // Steht M weit ausserhalb des Feldes, liegt der Klick auf der
        // Randmarke - und damit weit genug vom Anker weg, dass die
        // Abstandspruefung in mouseDown() ihn von selbst als Sprung behandelt.
        const auto px = worldToScreen (sourceAnchorWorld);

        return { px, px, sourceRadiusPx + sourceDragHitRadiusPx, true };
    }

    if (dragTarget == DragTarget::listenerHead)
    {
        const auto px = worldToScreen (snapshot.listener.head);

        return { px, px, headRadiusPx + dragHitRadiusPx * 0.6f, true };
    }

    // Die Nase dreht den Kopf, sie verschiebt nichts - ein Versatz waere dort
    // sinnlos, der Winkel zaehlt ab dem ersten Ereignis.
    return { {}, {}, 0.0f, false };
}

FieldComponent::DragTarget FieldComponent::dragTargetAt (juce::Point<float> screenPx) const
{
    if (viewMode == ViewMode::Perspective)
    {
        // In der Perspektive gibt es nur ein Ziel: die Quelle - und zwar
        // dort, wo ihr gelber Marker tatsaechlich zu sehen ist
        // (perspectiveSourceMarker()), auch wenn das der an den Rand
        // geklemmte oder der fixe Hinter-der-Kamera-Punkt ist (@dpa: Klick
        // auf den Marker holt M dorthin, s. handleDragTo()). Den Hoerer dort
        // zu verschieben waere zweideutig (waagerechte Mausbewegung koennte
        // Seite ODER Tiefe heissen), und fuer seine Drehung fehlt der Bezug -
        // beides bleibt der Draufsicht vorbehalten.
        const auto marker = perspectiveSourceMarker();

        if (screenPx.getDistanceFrom (marker.px) <= marker.radiusPx + sourceDragHitRadiusPx)
            return DragTarget::source;

        return DragTarget::none;
    }

    const auto headPx = worldToScreen (snapshot.listener.head);
    const float yaw = listenerScreenYaw();
    const auto nosePx = HeadSymbol::noseTip (headPx, headRadiusPx, yaw);

    // Am ruhenden Anker pruefen, nicht am gezeichneten (gejitterten) Punkt -
    // s. sourceAnchorWorld: M wird dort gefangen, wo es "eigentlich" ist,
    // auch wenn es gerade zappelnd irgendwo daneben gezeichnet wird (@dpa:
    // "ich habe Schwierigkeiten, M zu bewegen, wenn Jitter ueber seine
    // Darstellung hinausgeht - man kann es nicht mehr fangen").
    const auto sourcePx = worldToScreen (sourceAnchorWorld);

    // Quelle M zuerst pruefen (@dpa-Feedback 20260819: "M muss maus-trigger
    // Layer-technisch ueber L liegen") - liegen M und der Hoererkopf/seine
    // Nase uebereinander oder dicht beieinander, soll ein Klick die Quelle
    // greifen, nicht den Hoerer. Der Fangradius der Quelle bleibt dabei ihr
    // eigener (sourceRadiusPx + sourceDragHitRadiusPx, grosszuegiger als der
    // des Hoerers), er wird durch den Vorrang nicht weiter vergroessert -
    // ausserhalb davon bleibt der Hoerer weiterhin ganz normal greifbar.
    if (screenPx.getDistanceFrom (sourcePx) <= sourceRadiusPx + sourceDragHitRadiusPx)
        return DragTarget::source;

    // M steht ausserhalb des sichtbaren Feldes: dort zeichnet drawSource()
    // statt des normalen Punkts nur noch die an den Rand geklemmte Randmarke
    // (topDownSourceMarker()) - deshalb muss auch der Hit-Test dorthin
    // wandern, sonst waere die einzige sichtbare Anfassstelle fuer M gar
    // nicht klickbar (@dpa: Klick/Zug auf die Randmarke holt M zurueck, s.
    // handleDragTo()). Liegt M ohnehin im Feld, faellt dieser Treffer mit dem
    // obigen Anker-Test praktisch zusammen und aendert nichts.
    const auto marker = topDownSourceMarker();

    if (screenPx.getDistanceFrom (marker.px) <= marker.radiusPx + sourceDragHitRadiusPx)
        return DragTarget::source;

    // Nase vor Kopf: sie liegt oft innerhalb des grosszuegigen
    // Kopf-Fangradius, soll aber Vorrang vor "Kopf verschieben" haben.
    if (screenPx.getDistanceFrom (nosePx) <= dragHitRadiusPx)
        return DragTarget::listenerNose;

    if (screenPx.getDistanceFrom (headPx) <= headRadiusPx + dragHitRadiusPx * 0.6f)
        return DragTarget::listenerHead;

    return DragTarget::none;
}

// Umkehrung von project() bei FESTGEHALTENER Tiefe (entlang cameraForward()):
// quer zur Blickrichtung (cameraRight()) wird die Seitenlage, senkrecht die
// Hoehe gestellt. Die Tiefe bleibt, weil sie aus einem einzelnen Bildpunkt
// nicht hervorgeht - eine Maus hat zwei Achsen, der Raum drei. Die Tiefe wird
// an M selbst (snapshot.sourcePos) festgemacht, nicht an einem gerade
// laufenden Drag - so liefert die Funktion auch fuer die Nachlauf-
// Geschwindigkeitsschaetzung (dragScreenToWorld(), mouseDrag()) dieselbe
// Ebene wie fuers eigentliche Ziehen (handleDragTo()), ohne die Rechnung
// zweimal hinzuschreiben.
Vec3 FieldComponent::perspectiveScreenToWorld (juce::Point<float> screenPx) const
{
    const Vec3 cam   = cameraPosition();
    const Vec3 fwd   = cameraForward();
    const Vec3 right = cameraRight();

    // Grosszuegig nach vorn geklemmt statt bei ungueltiger Tiefe
    // abzubrechen (@dpa: ein Klick auf den gelben Marker soll M IMMER an
    // die geklickte Stelle holen - auch wenn M gerade hinter der Kamera
    // steht und es dort keine echte Tiefe zum Festhalten gibt).
    const double rawDepth = dot (snapshot.sourcePos - cam, fwd);
    const double depth    = juce::jmax (rawDepth, nearPlaneMetres + 1.0);

    const double focal = (double) focalPixels();

    const double lateralOffset = ((double) screenPx.x - (double) getWidth() * 0.5) * depth / focal;
    const double heightOffset  = ((double) horizonYPx() - (double) screenPx.y) * depth / focal;

    // Absolute Weltposition aus Tiefe (entlang fwd) + Seitenlage (entlang
    // right) - bei der festen Standardkamera (fwd = +y, right = +x)
    // deckt sich das exakt mit den bisherigen einzelnen x/y-Formeln; aus
    // Hoerer-Sicht (gedrehte Kamera, s. setPerspectiveFromListener())
    // verteilt sich die Seitenlage stattdessen auf Welt-x UND Welt-y.
    Vec3 worldPos = cam + fwd * depth + right * lateralOffset;
    worldPos.z    = cam.z + heightOffset;

    // Unter den Boden nur, solange er nichts zurueckwirft. Reflektiert er,
    // ist er eine Flaeche, und eine Quelle darunter waere ein Zustand, den
    // die Rechnung nicht abbildet: ihr Spiegelbild laege dann ueber ihr
    // (@dpa: "wenn Bodenreflexion an ist, dann nur z>=0"). Dieselbe
    // Klemmung gilt auch fuers Zeichnen (sourceDragWorldOverride) und die
    // Nachlauf-Geschwindigkeit - sonst zeigte der gezogene bzw. nachlaufende
    // Punkt eine Position, die so nie gemeldet wird.
    if (snapshot.groundReflectionOn)
        worldPos.z = juce::jmax (0.0, worldPos.z);

    return worldPos;
}

// Weltposition unter der Maus, wie sie die jeweils aktive Ansicht versteht -
// siehe Kommentar bei der Deklaration (FieldComponent.h).
Vec3 FieldComponent::dragScreenToWorld (juce::Point<float> screenPx) const
{
    return viewMode == ViewMode::Perspective ? perspectiveScreenToWorld (screenPx)
                                              : screenToWorld (screenPx);
}

void FieldComponent::handleDragTo (juce::Point<float> screenPx)
{
    if (viewMode == ViewMode::Perspective)
    {
        if (dragTarget != DragTarget::source)
            return;

        // Damit ist diese Ansicht der einzige Weg, die Hoehe mit der Maus zu
        // setzen; in der Draufsicht gibt es dafuer keine Achse.
        const Vec3 worldPos = perspectiveScreenToWorld (screenPx);

        // Zum Zeichnen benutzt, solange M gezogen wird (s. drawSource(),
        // perspectiveSourceMarker()) - folgt damit der Maus 1:1, ohne den
        // Wackel-Versatz der naechsten (moeglicherweise gejitterten)
        // Snapshot-Position (@dpa: "waehrend des Ziehens folgt M der Maus
        // ohne Wackel-Versatz").
        sourceDragWorldOverride = worldPos;

        const double normX = juce::jlimit (0.0, 1.0, worldPos.x / juce::jmax (1.0e-6, fieldMetres));

        if (onSourceDragged)
        {
            const double normY = juce::jlimit (0.0, 1.0,
                                               worldPos.y / juce::jmax (1.0e-6, fieldHeightMetres()));
            onSourceDragged (normX, normY);
        }

        if (onSourceHeightDragged)
            onSourceHeightDragged (worldPos.z);

        return;
    }

    switch (dragTarget)
    {
        case DragTarget::source:
        {
            const Vec3 worldPos = screenToWorld (screenPx);

            // s. Kommentar bei sourceDragWorldOverride (FieldComponent.h) -
            // gilt hier genauso fuer die Draufsicht.
            sourceDragWorldOverride = worldPos;
            reportNormalisedDrag (worldPos, true);
            break;
        }

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
    grabKeyboardFocus(); // Tastatur-Kurzbefehle (z.B. 'L', s. keyPressed()) brauchen den Fokus

    dragTarget = dragTargetAt (e.position);
    haveDragVelocity = false;
    grabOffsetPx     = {};

    if (dragTarget == DragTarget::source && onSourceGrabbed)
        onSourceGrabbed();

    if (dragTarget != DragTarget::none)
    {
        // Anfassen bewegt nichts: getroffen wurde das Symbol irgendwo in
        // seinem Fangradius, und genau dieser Abstand wird gemerkt statt
        // eingeebnet (s. grabOffsetPx). Erst das Ziehen zaehlt, und zwar als
        // Mausversatz ab der Klickstelle.
        const auto anchor = grabAnchorPx();

        if (anchor.valid && e.position.getDistanceFrom (anchor.hitPx) <= anchor.radiusPx)
        {
            grabOffsetPx = anchor.anchorPx - e.position;

            // Der gezeichnete Punkt setzt beim Anfassen auf demselben Anker
            // auf wie die Meldung. Optisch ist das der einzige Sprung, den es
            // noch gibt - vom gewackelten Punkt auf seine Ruhelage (@dpa:
            // "Das kann er optisch gerne tun") - gehoert bleibt keiner.
            if (dragTarget == DragTarget::source)
                sourceDragWorldOverride = sourceAnchorWorld;
        }
        else
        {
            // Randmarke oder Hinweispunkt: hier ist der Sprung der Zweck -
            // ein Klick darauf holt M an die geklickte Stelle zurueck.
            handleDragTo (e.position);
        }

        // Startpunkt der Nachlauf-Geschwindigkeitsschaetzung (s. mouseDrag()).
        // listenerHead ist in der Perspektive ohnehin nie das Ziel (s.
        // dragTargetAt()), dragScreenToWorld() waehlt die richtige Umrechnung
        // trotzdem selbst je nach viewMode.
        if (dragTarget == DragTarget::source || dragTarget == DragTarget::listenerHead)
        {
            lastDragWorldPos = dragScreenToWorld (e.position + grabOffsetPx);
            lastDragTimeMs   = juce::Time::getMillisecondCounterHiRes();
        }
    }
}

void FieldComponent::setMouseFrameSmoothing (bool shouldBeEnabled)
{
    mouseFrameSmoothing = shouldBeEnabled;

    if (! shouldBeEnabled)
    {
        // Ausgeschaltet gibt es nichts nachzufuehren: was noch aussteht, geht
        // sofort raus, damit die Quelle nicht auf halbem Weg stehen bleibt.
        if (havePendingDrag)
            handleDragTo (pendingDragScreen);

        havePendingDrag = false;
        stopTimer();
    }
}

void FieldComponent::timerCallback()
{
    if (! havePendingDrag || dragTarget == DragTarget::none)
        return;

    // Ein Schritt pro Bild in Richtung der zuletzt gesehenen Mausposition. Der
    // Anteil ist so gewaehlt, dass der Rest nach rund zwei Bildern erledigt ist
    // - genug, um den unregelmaessigen Ereignistakt zu verteilen, zu wenig, um
    // sich als Traegheit bemerkbar zu machen.
    constexpr float catchUpPerFrame = 0.5f;

    smoothedDragScreen += (pendingDragScreen - smoothedDragScreen) * catchUpPerFrame;

    handleDragTo (smoothedDragScreen);
}

void FieldComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTarget == DragTarget::none)
        return;

    // Ab hier zaehlt nicht die Mausposition, sondern die Stelle, an der das
    // gegriffene Symbol unter ihr liegt (s. grabOffsetPx) - eine reine
    // Verschiebung, alles Weitere rechnet unveraendert damit weiter.
    const auto dragPx = e.position + grabOffsetPx;

    if (mouseFrameSmoothing)
    {
        // Nur merken: gemeldet wird auf dem Bildtakt (timerCallback). Sonst
        // steckt der unregelmaessige Ereignistakt der Maus in der Bewegung und
        // damit im Doppler.
        if (! havePendingDrag)
        {
            smoothedDragScreen = dragPx;
            havePendingDrag    = true;
            startTimerHz (mouseFrameHz);
        }

        pendingDragScreen = dragPx;
    }
    else
    {
        handleDragTo (dragPx);
    }

    // Geschwindigkeit nur fuer die Ziele schaetzen, die der Nachlauf ueberhaupt
    // unterstuetzt (siehe setCoastEnabled-Kommentar in FieldComponent.h) - die
    // Kopfdrehung bleibt aussen vor. listenerHead kommt hier ohnehin nur in
    // der Draufsicht an (s. dragTargetAt()); dragScreenToWorld() liefert fuer
    // die Quelle in JEDER Ansicht die zur aktiven Perspektive passende
    // Weltposition, statt sie flach wie in der Draufsicht zu behandeln -
    // sonst bliebe der Nachlauf auf die Draufsicht beschraenkt.
    if (dragTarget != DragTarget::source && dragTarget != DragTarget::listenerHead)
        return;

    const Vec3   pos = dragScreenToWorld (dragPx);
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

    // Der letzte Stand der Maus muss noch ankommen, sonst bliebe die Quelle ein
    // Stueck vor dem Punkt stehen, an dem losgelassen wurde.
    if (havePendingDrag)
    {
        handleDragTo (pendingDragScreen);
        havePendingDrag = false;
    }

    stopTimer();

    if (coastEnabled && haveDragVelocity && released == DragTarget::source
        && dragVelocityEstimate.lengthSquared() > coastMinSpeedSquared
        && onSourceCoast != nullptr)
    {
        // Die Quelle laeuft in der Bewegungskette aus, nicht hier: uebergeben
        // wird die Geschwindigkeit, das Abklingen macht der Processor
        // (startSourceCoast). Ein hier gesetzter Zielpunkt waere wieder eine
        // Strecke, die der jeweils aktive Glaetter auf seine Weise abfaehrt.
        onSourceCoast (dragVelocityEstimate);
    }
    else if (coastEnabled && haveDragVelocity && released == DragTarget::listenerHead
             && dragVelocityEstimate.lengthSquared() > coastMinSpeedSquared)
    {
        // Der Hoerer bekommt weiterhin einen Zielpunkt: das Abklingen in der
        // Bewegungskette gilt der Quelle, deren Bewegung man hoert. Integral
        // von v0*exp(-t*ln(2)/halfLife) ueber t=0..unendlich = v0*halfLife/ln(2)
        // - der Gesamtweg des gedachten Abklingens in EINEM Schritt statt ihn
        // hier tickweise nachzubilden (siehe Klassenkommentar in
        // FieldComponent.h, warum ein eigener Timer hier mit dem "Slew
        // Limiter" kollidierte).
        const Vec3 projected = lastDragWorldPos
                              + dragVelocityEstimate * (coastHalfLifeSeconds / std::log (2.0));

        reportNormalisedDrag (projected, false);
    }

    haveDragVelocity = false;

    if (released == DragTarget::source && onSourceReleased)
        onSourceReleased();
}

void FieldComponent::setCoastEnabled (bool shouldCoast)
{
    coastEnabled = shouldCoast;

    // Persistiert ausserhalb des Host-Zustands (siehe coastProperties() oben)
    // - das ist der Grund, warum der Schalter jetzt ueber ein Laden hinweg
    // seinen zuletzt gewaehlten Stand behaelt statt immer wieder auf true.
    savePersistedCoastEnabled (shouldCoast);
}

void FieldComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    // "Aus L Sicht" (@dpa-Feedback) per Doppelklick erreichbar, ohne den
    // Editor anfassen zu muessen, s. setPerspectiveFromListener(). Nur in der
    // Perspektive - in der Draufsicht bedeutet ein Doppelklick nichts
    // Vergleichbares.
    if (viewMode == ViewMode::Perspective)
        setPerspectiveFromListener (! perspectiveFromListener);
}

bool FieldComponent::keyPressed (const juce::KeyPress& key)
{
    // 'L' als Tastatur-Zugang zur Hoerer-Sicht (@dpa-Feedback) - ein
    // Umschalter im Editor waere eine Panel-Aenderung, s.
    // setPerspectiveFromListener().
    if (key.getKeyCode() == 'L')
    {
        setPerspectiveFromListener (! perspectiveFromListener);
        return true;
    }

    // '0' setzt Zoom und Horizontlage der Perspektive zurueck. Der
    // Doppelklick ist bereits mit dem Kamera-Wechsel oben belegt, deshalb
    // hier ein eigener Tastendruck statt eines zweiten Doppelklick-Effekts.
    if (key.getKeyCode() == '0' && viewMode == ViewMode::Perspective)
    {
        perspectiveZoom = perspectiveZoomDefault;
        perspectiveHorizonFraction = perspectiveHorizonFractionDefault;
        repaint();
        return true;
    }

    return false;
}

void FieldComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Nur in der Perspektive - die Draufsicht hat mit fieldMetres
    // (Params.cpp) schon ihre eigene, per Regler gesetzte Skalierung; ein
    // zweites, mausgesteuertes Zoomen waere dort nur verwirrend.
    if (viewMode != ViewMode::Perspective)
        return;

    if (wheel.deltaX == 0.0f && wheel.deltaY == 0.0f)
        return;

    if (e.mods.isShiftDown())
    {
        // Umschalt+Mausrad: Zusatzweg fuer reine Mausbenutzer (kein deltaX
        // verfuegbar), nicht mehr der Hauptweg (der ist jetzt die
        // Zwei-Finger-Geste unten). Hoch scrollen schiebt den Horizont nach
        // oben (mehr Boden im Bild) - dieselbe "hoch = mehr/naeher"-Richtung
        // wie beim Zoom unten. Laeuft bewusst am Achsen-Lock vorbei, der ist
        // nur fuer die echte Zwei-Finger-Touchpad-Geste gedacht.
        if (wheel.deltaY == 0.0f)
            return;

        perspectiveHorizonFraction = juce::jlimit (perspectiveHorizonFractionMin,
                                                    perspectiveHorizonFractionMax,
                                                    perspectiveHorizonFraction
                                                        - wheel.deltaY * (float) perspectiveHorizonWheelSensitivity);
        repaint();
        return;
    }

    // Achsen-Lock fuer die Zwei-Finger-Geste, exakt wie
    // ScopeComponent::mouseWheelMove() (s. dort): die erste Bewegung einer
    // Geste entscheidet per groesserem Delta, ob waagerecht oder senkrecht
    // gilt, und bleibt dabei, bis wheelGestureGapMs lang kein Wheel-Event
    // mehr kam (Finger abgehoben).
    const juce::int64 now = juce::Time::currentTimeMillis();

    if (now - lastWheelEventMs > wheelGestureGapMs)
        wheelGestureAxis = WheelGestureAxis::none;

    lastWheelEventMs = now;

    if (wheelGestureAxis == WheelGestureAxis::none)
        wheelGestureAxis = std::abs (wheel.deltaX) > std::abs (wheel.deltaY)
                          ? WheelGestureAxis::horizontal : WheelGestureAxis::vertical;

    if (wheelGestureAxis == WheelGestureAxis::horizontal)
    {
        // 2 Finger waagerecht -> Horizontlage verschieben. In der
        // Perspektive gibt es keine Zeitachse zum Pannen (anders als im
        // Scope mit seiner Sample-Historie) - der Horizont (mehr oder
        // weniger Boden im Bild) ist die einzige zweite verstellbare Groesse
        // hier, deshalb liegt sie auf der waagerechten Achse statt eines
        // Pans ins Leere. Richtung (Finger nach rechts = mehr Boden) ist
        // eine Setzung ohne Vorbild - falls sich das beim Ausprobieren
        // verkehrt anfuehlt, hier das Vorzeichen drehen.
        perspectiveHorizonFraction = juce::jlimit (perspectiveHorizonFractionMin,
                                                    perspectiveHorizonFractionMax,
                                                    perspectiveHorizonFraction
                                                        - wheel.deltaX * (float) perspectiveHorizonWheelSensitivity);
    }
    else
    {
        // 2 Finger senkrecht -> Zoom. Stetige Exponentialkurve statt fixem
        // Sprung pro Event - wie im Vorbild ScopeComponent::mouseWheelMove(),
        // damit sich Trackpad-Gesten mit vielen kleinen deltaY nicht
        // ruckelig anfuehlen.
        const float factor = std::exp (wheel.deltaY * (float) perspectiveZoomWheelSensitivity);
        perspectiveZoom = juce::jlimit (perspectiveZoomMin, perspectiveZoomMax, perspectiveZoom * factor);
    }

    repaint();
}

void FieldComponent::mouseMagnify (const juce::MouseEvent&, float scaleFactor)
{
    // Nur in der Perspektive, s. mouseWheelMove(). Pinch ist auf dem
    // Touchpad die natuerlichste Zoomgeste und lief bisher ganz ins Leere.
    if (viewMode != ViewMode::Perspective || scaleFactor <= 0.0f)
        return;

    // Direkte Anwendung wie ScopeComponent::mouseMagnify() - dort wird
    // 1/scaleFactor gebraucht, weil dort ein GROESSERER Wert (displaySamples)
    // RAUSzoomt. Hier ist es umgekehrt: ein GROESSERER perspectiveZoom
    // zoomt REIN (s. focalPixels()), deshalb direkt ohne Umkehrung -
    // Finger spreizen (scaleFactor > 1) vergroessert die Brennweite direkt.
    perspectiveZoom = juce::jlimit (perspectiveZoomMin, perspectiveZoomMax, perspectiveZoom * scaleFactor);
    repaint();
}
