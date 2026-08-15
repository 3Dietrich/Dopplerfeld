#include "HeadSymbol.h"

namespace
{
    // Ohr als gebogener Strich (Vorlage-Skizze: ein Bogen, der von der
    // Kopfkontur wegzeigt und leicht zurueckhoert wie ein Komma). side = +1
    // oder -1 waehlt, auf welcher Seite der Blickrichtung das Ohr sitzt.
    juce::Path buildEarPath (juce::Point<float> centre, float r, float noseAngle, float side)
    {
        // ~99 Grad seitlich der Nase, nicht exakt 90 - in der Vorlage sitzen
        // die Ohren leicht nach hinten versetzt, nicht auf der reinen Querachse.
        const float baseAngle = noseAngle + side * (juce::MathConstants<float>::pi * 0.55f);
        const juce::Point<float> baseDir { std::cos (baseAngle), std::sin (baseAngle) };
        const float outAngle = baseAngle + side * 0.9f;
        const juce::Point<float> outDir { std::cos (outAngle), std::sin (outAngle) };

        const auto start = centre + baseDir * (r * 0.85f);
        const auto tip   = centre + baseDir * (r * 1.55f) + outDir * (r * 0.35f);
        const auto hook  = centre + baseDir * (r * 1.15f) + outDir * (r * 0.85f);

        juce::Path p;
        p.startNewSubPath (start);
        p.quadraticTo (tip, hook);
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
