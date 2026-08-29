#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Knopf, der statt eines Wortes ein gezeichnetes Zeichen traegt.
//
// Gedacht fuer die Faelle, in denen die Beschriftung ohnehin nur eine
// Abkuerzung waere: "Kop" und "Einf" sind auf einem 44 px breiten Knopf
// abgeschnittene Woerter, die man erst am Hinweistext versteht. Zwei Blaetter
// und ein Klemmbrett sagen dasselbe ohne Sprache und brauchen halb so viel
// Platz - auf dem Panel zaehlt jeder Pixel.
//
// Gezeichnet und nicht als Zeichensatz-Glyphe (Unicode/Emoji): welche Zeichen
// eine Schrift wirklich hat, ist je nach Rechner verschieden, und ein
// fehlendes Zeichen faellt als leeres Kaestchen aus.
class IconButton : public juce::Button
{
public:
    enum class Icon { copy, paste };

    IconButton (Icon which, const juce::String& name)
        : juce::Button (name), icon (which)
    {
    }

    void setTint (juce::Colour c) { tint = c; repaint(); }

private:
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat();

        // Flaeche wie bei den Textknoepfen daneben: gedaempfter Kopfzeilenton,
        // beim Ueberfahren etwas heller.
        auto fill = Theme::panelHeader;

        if (down)             fill = fill.brighter (0.35f);
        else if (highlighted) fill = fill.brighter (0.18f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, Theme::cornerRadius);

        const float alpha = isEnabled() ? 1.0f : 0.35f;
        g.setColour (tint.withAlpha (alpha));

        // Zeichenflaeche quadratisch und mittig, damit das Zeichen bei jeder
        // Knopfgroesse gleich aussieht.
        const float side = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 9.0f;
        const auto  box  = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());

        if (icon == Icon::copy)
            drawCopy (g, box, tint.withAlpha (alpha), fill);
        else
            drawPaste (g, box);
    }

    // Zwei versetzte Blaetter.
    static void drawCopy (juce::Graphics& g, juce::Rectangle<float> box,
                          juce::Colour ink, juce::Colour behind)
    {
        const float r  = 1.2f;
        const float in = box.getWidth() * 0.26f;

        const auto back  = box.withTrimmedRight (in).withTrimmedBottom (in);
        const auto front = box.withTrimmedLeft (in).withTrimmedTop (in);

        g.setColour (ink);
        g.drawRoundedRectangle (back, r, 1.1f);

        // Das vordere Blatt wird erst mit dem Knopfgrund gefuellt und dann
        // umrandet: sonst laufen die beiden Konturen im Ueberschneidungsfeld
        // ineinander, und bei dieser Groesse ist nicht mehr zu erkennen,
        // welches Blatt vorn liegt.
        g.setColour (behind);
        g.fillRoundedRectangle (front, r);

        g.setColour (ink);
        g.drawRoundedRectangle (front, r, 1.4f);
    }

    // Klemmbrett: Blatt mit Klemme oben.
    static void drawPaste (juce::Graphics& g, juce::Rectangle<float> box)
    {
        const float r = 1.2f;

        auto board = box.withTrimmedTop (box.getHeight() * 0.12f);

        g.drawRoundedRectangle (board, r, 1.4f);

        const float clipW = box.getWidth() * 0.46f;
        const float clipH = box.getHeight() * 0.24f;

        const auto clamp = juce::Rectangle<float> (clipW, clipH)
                               .withCentre ({ box.getCentreX(), box.getY() + clipH * 0.5f });

        g.fillRoundedRectangle (clamp, 1.0f);

        // Zwei Zeilen auf dem Blatt - ohne sie wirkt es leer und der
        // Unterschied zum Kopier-Zeichen ist nur die Klemme.
        const float lineX1 = board.getX() + board.getWidth() * 0.22f;
        const float lineX2 = board.getRight() - board.getWidth() * 0.22f;
        const float lineY  = board.getY() + board.getHeight() * 0.55f;

        g.drawLine (lineX1, lineY, lineX2, lineY, 1.0f);
        g.drawLine (lineX1, lineY + board.getHeight() * 0.22f,
                    lineX2, lineY + board.getHeight() * 0.22f, 1.0f);
    }

    Icon         icon;
    juce::Colour tint { Theme::muted };
};
