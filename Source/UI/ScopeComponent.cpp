#include "ScopeComponent.h"

ScopeComponent::ScopeComponent()
{
    setTooltip ("Oszilloskop des Ausgangs (nach Gain/Limiter). Freeze haelt das Bild an, "
               "Sync richtet einen steigenden Nulldurchgang von L in der Mitte des Scopes aus.");
}

int ScopeComponent::findTriggerIndex (const float* rawLeft)
{
    constexpr int centre = captureWindowSamples / 2;
    constexpr int lo     = captureWindowSamples / 4;
    constexpr int hi     = (captureWindowSamples * 3) / 4;

    // Von der Mitte aus in beide Richtungen wachsend suchen, damit bei
    // mehreren Treffern automatisch der naechste zur Mitte gewinnt - kein
    // nachtraeglicher Vergleich noetig.
    for (int radius = 0; radius < (hi - lo); ++radius)
    {
        const int right = centre + radius;
        const int left  = centre - radius;

        if (right < hi && right > lo
            && rawLeft[right - 1] <= 0.0f && rawLeft[right] > 0.0f)
            return right;

        if (radius > 0 && left >= lo && left < hi
            && rawLeft[left - 1] <= 0.0f && rawLeft[left] > 0.0f)
            return left;
    }

    return -1;
}

void ScopeComponent::feed (const float* rawLeft, const float* rawRight)
{
    if (frozen)
        return;

    int start = displaySamples / 2;   // ungesynct: juengste Haelfte des Rohfensters
    lastFrameWasSynced = false;

    if (syncEnabled)
    {
        const int trigger = findTriggerIndex (rawLeft);

        if (trigger >= 0)
        {
            start = trigger - displaySamples / 2;
            lastFrameWasSynced = true;
        }
    }

    for (int n = 0; n < displaySamples; ++n)
    {
        shownLeft[(size_t) n]  = rawLeft[start + n];
        shownRight[(size_t) n] = rawRight[start + n];
    }

    repaint();
}

void ScopeComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setColour (juce::Colour (0xff0c0c0c));
    g.fillRect (area);

    // Nullinie.
    const float midY = area.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawLine (area.getX(), midY, area.getRight(), midY, 1.0f);

    // Trigger-Linie exakt in der Mitte, nur wenn Sync gerade wirklich
    // ausgerichtet hat - sonst zeigte sie eine Mitte an, die keine ist.
    if (syncEnabled && lastFrameWasSynced)
    {
        const float centreX = area.getCentreX();
        g.setColour (juce::Colours::yellow.withAlpha (0.35f));
        g.drawLine (centreX, area.getY(), centreX, area.getBottom(), 1.0f);
    }

    auto drawTrace = [&] (const std::array<float, displaySamples>& samples, juce::Colour colour)
    {
        juce::Path path;
        const float xStep = area.getWidth() / (float) (displaySamples - 1);

        for (int n = 0; n < displaySamples; ++n)
        {
            const float v = juce::jlimit (-amplitudeRange, amplitudeRange, samples[(size_t) n]);
            const float x = area.getX() + (float) n * xStep;
            const float y = midY - (v / amplitudeRange) * (area.getHeight() * 0.5f);

            if (n == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.0f));
    };

    drawTrace (shownLeft,  juce::Colours::limegreen.withAlpha (0.85f));
    drawTrace (shownRight, juce::Colours::orange.withAlpha (0.7f));

    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.drawRect (area, 1.0f);

    if (frozen)
    {
        g.setColour (juce::Colours::orangered.withAlpha (0.8f));
        g.setFont (13.0f);
        g.drawText ("FREEZE", area.reduced (6.0f), juce::Justification::topRight);
    }
}
