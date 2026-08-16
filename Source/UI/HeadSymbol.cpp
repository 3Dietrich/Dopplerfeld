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
    // zeigt also zur Nase, nicht von ihr weg - genau umgekehrt zur
    // fruaeheren, nach hinten weggeklappten Form.
    juce::Path buildEarPath (juce::Point<float> centre, float r, float noseAngle, float side)
    {
        const float sideAngle = noseAngle + side * (juce::MathConstants<float>::pi * 0.5f);
        const juce::Point<float> outward { std::cos (sideAngle), std::sin (sideAngle) };
        const juce::Point<float> forward { std::cos (noseAngle), std::sin (noseAngle) };

        const auto base = centre + outward * (r * 0.98f);
        const auto ctrl = base + outward * (r * 0.30f) + forward * (r * 0.06f);
        const auto tip  = base + outward * (r * 0.05f) + forward * (r * 0.30f);

        juce::Path p;
        p.startNewSubPath (base);
        p.quadraticTo (ctrl, tip);
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

    void draw (juce::Graphics& g, juce::Point<float> centre, float radiusPx,
               float angleRadians, const Style& style)
    {
        // Kopf: Kreiskontur, optional gefuellt.
        const auto headBounds = juce::Rectangle<float> (radiusPx * 2.0f, radiusPx * 2.0f)
                                     .withCentre (centre);
        if (! style.fillColour.isTransparent())
        {
            g.setColour (style.fillColour);
            g.fillEllipse (headBounds);
        }
        g.setColour (style.headColour);
        g.drawEllipse (headBounds, style.lineThickness);

        // Nase: Dreieck, Basis auf der Kreiskontur, Spitze nach aussen.
        constexpr float baseHalfAngle = 0.42f; // Radiant, Oeffnungswinkel der Nasenbasis
        const juce::Point<float> baseDirA { std::cos (angleRadians - baseHalfAngle),
                                             std::sin (angleRadians - baseHalfAngle) };
        const juce::Point<float> baseDirB { std::cos (angleRadians + baseHalfAngle),
                                             std::sin (angleRadians + baseHalfAngle) };
        juce::Path nose;
        nose.startNewSubPath (centre + baseDirA * (radiusPx * 0.95f));
        nose.lineTo (noseTip (centre, radiusPx, angleRadians));
        nose.lineTo (centre + baseDirB * (radiusPx * 0.95f));
        nose.closeSubPath();
        g.setColour (style.headColour);
        g.strokePath (nose, juce::PathStrokeType (style.lineThickness,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

        // Ohren: je ein gebogener Strich links und rechts der Blickrichtung.
        g.setColour (style.earColour);
        for (float side : { -1.0f, 1.0f })
        {
            auto earPath = buildEarPath (centre, radiusPx, angleRadians, side);
            g.strokePath (earPath, juce::PathStrokeType (style.lineThickness,
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
        }
    }
}
