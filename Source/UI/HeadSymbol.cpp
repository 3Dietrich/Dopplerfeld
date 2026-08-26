#include "HeadSymbol.h"

namespace
{
    // Ohr als kleiner, nach vorne gekippter Haken direkt an der Kopfkontur.
    // side = +1 oder -1 waehlt, auf welcher Seite der Blickrichtung das Ohr
    // sitzt. Lokale Basis am Ansatzpunkt statt globaler Winkel: outward
    // zeigt radial von der Kopfmitte weg, forward tangential in
    // Blickrichtung (parallel zur Nase) - damit bleibt die Groesse des Ohrs
    // unabhaengig vom Kopfradius klein und kontrollierbar, statt sich ueber
    // einen grossen Kreisbogen bis zur Nase zu ziehen. Der Haken schwenkt
    // von "radial nach aussen" auf "tangential nach vorne", die Spitze
    // zeigt also zur Nase, nicht von ihr weg.
    //
    // Gerechnet wird in der Kopfebene (Kopfradius = 1, Nase entlang +lx,
    // siehe HeadSymbol::PointMapper); auf den Bildschirm kommt jeder Punkt
    // erst durch die uebergebene Abbildung.
    juce::Path buildEarPath (const HeadSymbol::PointMapper& map, float side)
    {
        const juce::Point<float> outward { 0.0f, side };
        const juce::Point<float> forward { 1.0f, 0.0f };

        const auto local = [&] (juce::Point<float> p) { return map (p.x, p.y); };

        const auto base = outward * 0.98f;
        const auto ctrl = base + outward * 0.30f + forward * 0.06f;
        const auto tip  = base + outward * 0.05f + forward * 0.30f;

        juce::Path p;
        p.startNewSubPath (local (base));
        p.quadraticTo (local (ctrl), local (tip));
        return p;
    }
}

namespace HeadSymbol
{
    juce::Point<float> noseTip (juce::Point<float> centre, float radiusPx, float angleRadians)
    {
        const juce::Point<float> noseDir { std::cos (angleRadians), std::sin (angleRadians) };
        return centre + noseDir * (radiusPx * 1.5f);
    }

    void drawMapped (juce::Graphics& g, const PointMapper& map, const Style& style)
    {
        // Kopf: Kreis der Kopfebene als Vieleck (siehe Kommentar im Header).
        // 48 Ecken sind bei den hier ueblichen Radien von wenigen bis einigen
        // Dutzend Pixeln nicht mehr von einem Kreis zu unterscheiden.
        constexpr int   segments = 48;
        constexpr float twoPi    = juce::MathConstants<float>::twoPi;

        juce::Path head;

        for (int i = 0; i < segments; ++i)
        {
            const float a  = twoPi * (float) i / (float) segments;
            const auto  px = map (std::cos (a), std::sin (a));

            if (i == 0)
                head.startNewSubPath (px);
            else
                head.lineTo (px);
        }

        head.closeSubPath();

        if (! style.fillColour.isTransparent())
        {
            g.setColour (style.fillColour);
            g.fillPath (head);
        }

        g.setColour (style.headColour);
        g.strokePath (head, juce::PathStrokeType (style.lineThickness));

        // Nase: Dreieck, Basis auf der Kreiskontur, Spitze nach aussen.
        constexpr float baseHalfAngle = 0.42f; // Radiant, Oeffnungswinkel der Nasenbasis

        juce::Path nose;
        nose.startNewSubPath (map (std::cos (-baseHalfAngle) * 0.95f,
                                   std::sin (-baseHalfAngle) * 0.95f));
        nose.lineTo (map (1.5f, 0.0f));
        nose.lineTo (map (std::cos (baseHalfAngle) * 0.95f,
                          std::sin (baseHalfAngle) * 0.95f));
        nose.closeSubPath();

        g.setColour (style.headColour);
        g.strokePath (nose, juce::PathStrokeType (style.lineThickness,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

        // Ohren: je ein gebogener Strich links und rechts der Blickrichtung.
        g.setColour (style.earColour);

        for (float side : { -1.0f, 1.0f })
        {
            auto earPath = buildEarPath (map, side);
            g.strokePath (earPath, juce::PathStrokeType (style.lineThickness,
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
        }
    }

    void draw (juce::Graphics& g, juce::Point<float> centre, float radiusPx,
               float angleRadians, const Style& style)
    {
        // Die flache Darstellung ist der Sonderfall "drehen und skalieren"
        // derselben Zeichnung - eine zweite Geometrie dafuer gaebe es nur,
        // damit beide auseinanderlaufen koennen.
        const juce::Point<float> forward { std::cos (angleRadians), std::sin (angleRadians) };
        const juce::Point<float> right   { -std::sin (angleRadians), std::cos (angleRadians) };

        drawMapped (g,
                    [&] (float lx, float ly)
                    {
                        return centre + (forward * lx + right * ly) * radiusPx;
                    },
                    style);
    }
}
