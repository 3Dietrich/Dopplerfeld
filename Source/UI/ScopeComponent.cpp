#include "ScopeComponent.h"

#include <cmath>

ScopeComponent::ScopeComponent()
{
    setTooltip ("Oszilloskop des Ausgangs (nach Gain/Limiter). Mausrad senkrecht zoomt, Pinch "
               "zoomt ebenfalls. Freeze haelt das Bild an UND schaltet auf die komplette "
               "Historie um - darin waagerecht scrollen oder ziehen, um frei zu suchen. "
               "Speichern legt den sichtbaren Ausschnitt als CSV in Downloads ab.");

    shownLeft.resize ((size_t) displaySamples, 0.0f);
    shownRight.resize ((size_t) displaySamples, 0.0f);
}

int ScopeComponent::findTriggerIndexNear (const float* left, int searchLo, int searchHi, int target)
{
    const int maxRadius = juce::jmax (searchHi - target, target - searchLo);

    for (int radius = 0; radius <= maxRadius; ++radius)
    {
        const int right = target + radius;
        const int leftIdx = target - radius;

        if (right < searchHi && right > searchLo && right - 1 >= 0
            && left[right - 1] <= 0.0f && left[right] > 0.0f)
            return right;

        if (radius > 0 && leftIdx >= searchLo && leftIdx < searchHi && leftIdx - 1 >= 0
            && left[leftIdx - 1] <= 0.0f && left[leftIdx] > 0.0f)
            return leftIdx;
    }

    return -1;
}

void ScopeComponent::setDisplaySampleCount (int newCount)
{
    newCount = juce::jlimit (minDisplaySamples, juce::jmax (minDisplaySamples, maxDisplaySamples), newCount);

    if (newCount == displaySamples)
        return;

    if (historyMode)
    {
        // Bildmitte (absolute Position in der Historie) bleibt beim Zoomen
        // stehen, statt dass der sichtbare Ausschnitt hin- und herspringt.
        const int centreAbsolute = panOffset + displaySamples / 2;
        displaySamples = newCount;
        const int maxOffset = juce::jmax (0, frozenLength - displaySamples);
        panOffset = juce::jlimit (0, maxOffset, centreAbsolute - displaySamples / 2);
    }
    else
    {
        displaySamples = newCount;
    }

    shownLeft.assign ((size_t) displaySamples, 0.0f);
    shownRight.assign ((size_t) displaySamples, 0.0f);
    repaint();
}

void ScopeComponent::setMaxDisplaySampleCount (int maxSamples)
{
    maxDisplaySamples = juce::jmax (minDisplaySamples, maxSamples);

    if (displaySamples > maxDisplaySamples)
        setDisplaySampleCount (maxDisplaySamples);
}

void ScopeComponent::zoomStep (float factor)
{
    setDisplaySampleCount ((int) std::lround ((float) displaySamples * factor));
}

void ScopeComponent::panBy (int deltaSamples)
{
    if (! historyMode)
        return;

    const int maxOffset = juce::jmax (0, frozenLength - displaySamples);
    panOffset = juce::jlimit (0, maxOffset, panOffset + deltaSamples);
    repaint();
}

void ScopeComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaX == 0.0f && wheel.deltaY == 0.0f)
        return;

    // Achsen-Unterscheidung wie im Vorbild (@dpa: ~/hass/sensor-archive/mac/
    // index.html, gesturePlugin()): waagerecht = pannen, senkrecht = zoomen.
    // Waagerecht wirkt nur im History-Modus (dort gibt es etwas zum
    // Verschieben) - im Live-Betrieb tut eine waagerechte Geste bewusst
    // nichts, statt ins Leere zu pannen.
    if (std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        if (historyMode)
            panBy ((int) std::lround (-(double) wheel.deltaX * displaySamples * 0.15));

        return;
    }

    // Senkrecht = zoomen. Hoch scrollen verkuerzt die Zeitbasis
    // (reinzoomen), runter verlaengert sie - wie in jedem DAW-Editor.
    zoomStep (wheel.deltaY > 0.0f ? 0.8f : 1.25f);
}

void ScopeComponent::mouseMagnify (const juce::MouseEvent&, float scaleFactor)
{
    if (scaleFactor > 0.0f)
        zoomStep (1.0f / scaleFactor);
}

void ScopeComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStartX = e.x;
    dragStartPanOffset = panOffset;
}

void ScopeComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! historyMode || getWidth() <= 0)
        return;

    // Absolut vom Drag-Start aus berechnet statt akkumuliert - so driftet
    // nichts durch aufsummierte Rundungsfehler bei einem langen Zug.
    const int deltaPixels  = e.x - dragStartX;
    const int deltaSamples = (int) std::lround (-(double) deltaPixels / (double) getWidth() * displaySamples);

    const int maxOffset = juce::jmax (0, frozenLength - displaySamples);
    panOffset = juce::jlimit (0, maxOffset, dragStartPanOffset + deltaSamples);
    repaint();
}

void ScopeComponent::feed (const float* rawLeft, const float* rawRight)
{
    if (frozen)
        return;

    int start = displaySamples / 2;   // ungesynct: juengste Haelfte des Rohfensters
    lastFrameWasSynced = false;

    if (syncEnabled)
    {
        const int captureLen = captureWindowSampleCount();
        const int trigger = findTriggerIndexNear (rawLeft, captureLen / 4, (captureLen * 3) / 4, captureLen / 2);

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

void ScopeComponent::enterHistoryMode (const float* fullLeft, const float* fullRight, int fullLength)
{
    frozenLeft.assign (fullLeft, fullLeft + fullLength);
    frozenRight.assign (fullRight, fullRight + fullLength);
    frozenLength = fullLength;

    const int maxOffset = juce::jmax (0, frozenLength - displaySamples);
    triggerAbsoluteIndex = -1;

    if (syncEnabled && frozenLength > displaySamples)
    {
        // Wie im Live-Betrieb: Trigger moeglichst nahe am "jetzt" (Ende der
        // Historie) suchen, im letzten captureWindowSampleCount()-Abschnitt.
        const int searchHi = frozenLength;
        const int searchLo = juce::jmax (0, frozenLength - captureWindowSampleCount());
        const int target   = frozenLength - displaySamples / 2;

        const int trigger = findTriggerIndexNear (fullLeft, searchLo, searchHi, target);

        if (trigger >= 0)
        {
            triggerAbsoluteIndex = trigger;
            panOffset = juce::jlimit (0, maxOffset, trigger - displaySamples / 2);
        }
    }

    if (triggerAbsoluteIndex < 0)
        panOffset = maxOffset;   // juengster Ausschnitt, wie ohne Sync

    historyMode = true;
    frozen      = true;
    repaint();
}

void ScopeComponent::exitHistoryMode()
{
    historyMode = false;
    frozen      = false;
    triggerAbsoluteIndex = -1;

    frozenLeft.clear();
    frozenRight.clear();
    frozenLeft.shrink_to_fit();
    frozenRight.shrink_to_fit();
    frozenLength = 0;
    panOffset    = 0;

    repaint();
}

const float* ScopeComponent::visibleLeft() const
{
    return historyMode ? frozenLeft.data() + panOffset : shownLeft.data();
}

const float* ScopeComponent::visibleRight() const
{
    return historyMode ? frozenRight.data() + panOffset : shownRight.data();
}

bool ScopeComponent::exportVisibleWindow (const juce::File& file) const
{
    const float* left  = visibleLeft();
    const float* right = visibleRight();

    juce::String csv;
    csv << "# dopplerfeld scope export\n";
    csv << "# samples=" << displaySamples << " sampleRateHint=" << sampleRateHint << "\n";
    csv << "# mode=" << (historyMode ? "history" : "live")
        << " frozen=" << (frozen ? 1 : 0)
        << " sync=" << (syncEnabled ? 1 : 0) << "\n";
    csv << "sample,time_ms,left,right\n";

    for (int n = 0; n < displaySamples; ++n)
    {
        const double timeMs = 1000.0 * (double) n / juce::jmax (1.0, sampleRateHint);
        csv << n << ',' << juce::String (timeMs, 3) << ','
            << juce::String (left[n], 6) << ',' << juce::String (right[n], 6) << '\n';
    }

    return file.replaceWithText (csv);
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

    // Trigger-Linie: im Live-Modus nur wenn Sync gerade wirklich ausgerichtet
    // hat (sonst zeigte sie eine Mitte an, die keine ist), im History-Modus
    // nur wenn der markierte Trigger gerade im sichtbaren Ausschnitt liegt
    // (man kann ja wegpannen).
    bool drawTriggerLine = false;
    float triggerX = 0.0f;

    if (historyMode)
    {
        if (triggerAbsoluteIndex >= panOffset && triggerAbsoluteIndex < panOffset + displaySamples)
        {
            drawTriggerLine = true;
            triggerX = area.getX() + (float) (triggerAbsoluteIndex - panOffset)
                                    / (float) displaySamples * area.getWidth();
        }
    }
    else if (syncEnabled && lastFrameWasSynced)
    {
        drawTriggerLine = true;
        triggerX = area.getCentreX();
    }

    if (drawTriggerLine)
    {
        g.setColour (juce::Colours::yellow.withAlpha (0.35f));
        g.drawLine (triggerX, area.getY(), triggerX, area.getBottom(), 1.0f);
    }

    auto drawTrace = [&] (const float* samples, juce::Colour colour)
    {
        juce::Path path;
        const float xStep = area.getWidth() / (float) juce::jmax (1, displaySamples - 1);

        for (int n = 0; n < displaySamples; ++n)
        {
            const float v = juce::jlimit (-amplitudeRange, amplitudeRange, samples[n]);
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

    drawTrace (visibleLeft(),  juce::Colours::limegreen.withAlpha (0.85f));
    drawTrace (visibleRight(), juce::Colours::orange.withAlpha (0.7f));

    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.drawRect (area, 1.0f);

    // Zeitbasis-Beschriftung (@dpa-Feedback: "zoombar") - reine Anzeige aus
    // sampleRateHint, damit man sieht, wie weit man gerade reingezoomt ist.
    const double windowMs = 1000.0 * (double) displaySamples / juce::jmax (1.0, sampleRateHint);
    juce::String label = windowMs >= 1000.0
                        ? juce::String (windowMs / 1000.0, 2) + " s"
                        : juce::String (windowMs, 1) + " ms";

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (12.0f);
    g.drawText (label, area.reduced (6.0f).removeFromTop (16.0f),
               juce::Justification::topLeft);

    if (frozen)
    {
        g.setColour (juce::Colours::orangered.withAlpha (0.8f));
        g.setFont (13.0f);
        g.drawText ("FREEZE", area.reduced (6.0f), juce::Justification::topRight);
    }

    // Pan-Position (@dpa-Feedback: "frei herumsuchen") - wie weit der Anfang
    // des sichtbaren Fensters hinter dem Ende der aufgezeichneten Historie
    // zurueckliegt, damit man sich in der Historie orientieren kann.
    if (historyMode)
    {
        const double behindSeconds = (double) (frozenLength - (panOffset + displaySamples))
                                    / juce::jmax (1.0, sampleRateHint);
        juce::String posLabel = behindSeconds < 0.01 ? "aktuellstes Ende"
                                                      : "vor " + juce::String (behindSeconds, 2) + " s";

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (12.0f);
        g.drawText (posLabel, area.reduced (6.0f).removeFromBottom (16.0f),
                   juce::Justification::bottomLeft);
    }
}
