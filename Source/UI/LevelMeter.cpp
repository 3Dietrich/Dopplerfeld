#include "LevelMeter.h"

void LevelMeter::updateChannel (Channel& c, float peakLinear, double dtMs)
{
    const double peakDb = juce::jlimit ((double) minDb, 40.0, linearToDb (peakLinear));

    // Sofortiger Anstieg (die lautere Spitze zählt sofort), langsamer Fall -
    // klassisches Peak-Meter-Verhalten, sonst wäre die Anzeige bei kurzen
    // Spitzen (z.B. dem Überschall-Knall) kaum ablesbar.
    if (peakDb > c.displayDb)
        c.displayDb = (float) peakDb;
    else
        c.displayDb = (float) std::max ((double) minDb, (double) c.displayDb - releaseDbPerMs * dtMs);

    if (peakDb >= (double) clipThresholdDb)
        c.clipHoldMs = clipHoldDurationMs;
    else if (c.clipHoldMs > 0.0)
        c.clipHoldMs = std::max (0.0, c.clipHoldMs - dtMs);
}

void LevelMeter::pushLevels (float peakLLinear, float peakRLinear, double callIntervalMs)
{
    updateChannel (left,  peakLLinear, callIntervalMs);
    updateChannel (right, peakRLinear, callIntervalMs);
    repaint();
}

void LevelMeter::drawChannel (juce::Graphics& g, juce::Rectangle<float> area, const Channel& c) const
{
    constexpr float clipLedHeight = 6.0f;

    auto clipArea = area.removeFromTop (clipLedHeight);
    area.removeFromTop (2.0f);

    // Clip-LED: hell rot solange clipHoldMs > 0, sonst nur ein dunkler Rahmen -
    // bleibt sichtbar, dass an dieser Stelle überhaupt eine Anzeige ist.
    g.setColour (c.clipHoldMs > 0.0 ? juce::Colours::red : juce::Colours::darkred.darker (0.6f));
    g.fillRect (clipArea);

    // Bar-Hintergrund.
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (area);

    const float frac = juce::jlimit (0.0f, 1.0f, (c.displayDb - minDb) / (maxDb - minDb));
    auto filled = area.removeFromBottom (area.getHeight() * frac);

    // Grün bis knapp unter 0dB, darüber gelb/rot - grobe, aber sofort
    // ablesbare Ampel statt exakter dB-Zahl.
    const juce::Colour barColour = c.displayDb >= clipThresholdDb ? juce::Colours::red
                                  : c.displayDb >= markerDb       ? juce::Colours::yellow
                                                                   : juce::Colours::limegreen;
    g.setColour (barColour);
    g.fillRect (filled);

    // -6dB-Marke (@dpa-Vorgabe) als horizontaler Strich über die volle Breite.
    const float markerFrac = juce::jlimit (0.0f, 1.0f, (markerDb - minDb) / (maxDb - minDb));
    const float markerY    = area.getBottom() - area.getHeight() * markerFrac;
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawLine (area.getX(), markerY, area.getRight(), markerY, 1.0f);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    constexpr float gap = 3.0f;

    auto leftArea  = area.removeFromLeft ((area.getWidth() - gap) * 0.5f);
    area.removeFromLeft (gap);

    drawChannel (g, leftArea, left);
    drawChannel (g, area,     right);
}
