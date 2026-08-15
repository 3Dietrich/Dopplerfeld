#pragma once

#include <juce_graphics/juce_graphics.h>

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

    // Position der Nasenspitze in Pixeln - fuer den Hit-Test beim Ziehen
    // (Plan 3.13: "Ziehen an der Nase dreht ihn"), damit FieldComponent die
    // Zeichen-Geometrie nicht duplizieren muss.
    juce::Point<float> noseTip (juce::Point<float> centre, float radiusPx, float angleRadians);
}
