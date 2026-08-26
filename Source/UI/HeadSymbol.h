#pragma once

#include <juce_graphics/juce_graphics.h>
#include <functional>

// Freie Zeichenfunktion(en) fuer das Hoerer-Symbol (Plan 3.13), nach der
// Vorlage "Kopf von oben mit Nase und Ohren.jpg": Kreis fuer den Kopf, ein
// Dreieck als Nase in Blickrichtung, zwei kleine Boegen seitlich als Ohren.
//
// Reine Geometrie in Pixel-Koordinaten, kein eigener State. Der Aufrufer
// (FieldComponent) uebergibt Mittelpunkt, Radius und die Blickrichtung
// bereits als Bildschirm-Winkel - der Welt<->Bildschirm-Vorzeichenwechsel
// aus Plan 2.1 bleibt dadurch ausschliesslich in
// FieldComponent::worldToScreen/screenToWorld, wie im Plan gefordert.
//
// Winkelkonvention: 0 Radiant zeigt die Nase entlang +x (Bildschirm-rechts),
// positive Winkel drehen im Uhrzeigersinn - Standard-atan2/cos/sin in
// JUCE-Pixelkoordinaten (y zeigt nach unten).
namespace HeadSymbol
{
    struct Style
    {
        juce::Colour headColour = juce::Colours::white;   // Kontur Kopf + Nase
        juce::Colour earColour  = juce::Colours::white;    // Kontur Ohren
        juce::Colour fillColour = juce::Colours::transparentBlack; // Kopf-Fuellung, transparent = nur Kontur
        float lineThickness = 1.6f;
    };

    // centre/radius in Pixeln. angleRadians = Blickrichtung als Bildschirm-Winkel
    // (siehe Konvention oben, vom Aufrufer aus lisYaw + worldToScreen gebildet).
    void draw (juce::Graphics& g, juce::Point<float> centre, float radiusPx,
               float angleRadians, const Style& style = {});

    // Abbildung eines Punktes der KOPFEBENE auf den Bildschirm: lx/ly in
    // Vielfachen des Kopfradius, Ursprung = Kopfmitte, +lx = Blickrichtung,
    // +ly ihre rechte Senkrechte (dieselbe Drehrichtung wie oben, also im
    // Uhrzeigersinn in Pixelkoordinaten).
    using PointMapper = std::function<juce::Point<float> (float lx, float ly)>;

    // Dasselbe Symbol, aber Punkt fuer Punkt durch eine beliebige Abbildung
    // gelegt statt starr in die Bildebene gezeichnet. Damit kann der Aufrufer
    // den Kopf flach in eine Ebene des Raumes legen und perspektivisch
    // verzerren lassen (siehe FieldComponent::drawPerspectiveListener) - die
    // Zeichnung bleibt dieselbe, nur die Abbildung ist eine andere. draw()
    // oben ist genau dieser Fall mit Drehung und Skalierung als Abbildung.
    //
    // Der Kopfkreis wird deshalb als Vieleck gezeichnet und nicht als
    // Ellipse: unter einer perspektivischen Abbildung ist das Bild eines
    // Kreises keine achsenparallele Ellipse mehr, und juce::Graphics kennt
    // nur die.
    void drawMapped (juce::Graphics& g, const PointMapper& map, const Style& style = {});

    // Position der Nasenspitze in Pixeln - fuer den Hit-Test beim Ziehen
    // (Plan 3.13: "Ziehen an der Nase dreht ihn"), damit FieldComponent die
    // Zeichen-Geometrie nicht duplizieren muss.
    juce::Point<float> noseTip (juce::Point<float> centre, float radiusPx, float angleRadians);
}
