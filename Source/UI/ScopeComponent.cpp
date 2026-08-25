#include "ScopeComponent.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

ScopeComponent::ScopeComponent()
{
    setTooltip (Tooltips::text (Tooltips::Key::Scope));

    shownLeft.resize ((size_t) displaySamples, 0.0f);
    shownRight.resize ((size_t) displaySamples, 0.0f);
}

int ScopeComponent::nearestRisingZero (const float* left, int searchLo, int searchHi, int target)
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

void ScopeComponent::buildSyncLowpass (const float* left, int length) const
{
    syncScratch.resize ((size_t) length);

    if (length <= 0)
        return;

    // Grenzfrequenz aus dem Signal selbst. Die Nulldurchgangsrate ist eine
    // grobe Frequenzschaetzung, die von den Obertoenen nach oben gezogen wird:
    // ein reiner Sinus hat 2 Durchgaenge je Periode, ein obertonreicher Klang
    // ein Vielfaches davon. Ein Viertel dieser Schaetzung liegt deshalb
    // zuverlaessig unter der Grundwelle - und genau die soll uebrig bleiben.
    //
    // Ohne diese Kopplung braeuchte es eine feste Zahl, und die waere fuer
    // jedes zweite Signal falsch: 250 Hz loeschen einen Klang mit 1 kHz
    // Grundton vollstaendig aus, 2 kHz lassen bei einem Bass alle Obertoene
    // stehen.
    int crossings = 0;

    for (int n = 1; n < length; ++n)
        if ((left[n - 1] <= 0.0f) != (left[n] <= 0.0f))
            ++crossings;

    const double sr       = sampleRateHint > 0.0 ? sampleRateHint : 48000.0;
    const double zeroRate = 0.5 * (double) crossings * sr / (double) length;
    const double cutoff   = juce::jlimit (10.0, 0.125 * sr, 0.25 * zeroRate);

    const double coeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoff / sr);

    // Vorwaerts ...
    double y = (double) left[0];

    for (int n = 0; n < length; ++n)
    {
        y += coeff * ((double) left[n] - y);
        syncScratch[(size_t) n] = (float) y;
    }

    // ... und rueckwaerts, damit keine Phase uebrig bleibt (siehe Header).
    y = (double) syncScratch[(size_t) (length - 1)];

    for (int n = length - 1; n >= 0; --n)
    {
        y += coeff * ((double) syncScratch[(size_t) n] - y);
        syncScratch[(size_t) n] = (float) y;
    }
}

int ScopeComponent::findTriggerIndexNear (const float* left, int searchLo, int searchHi, int target) const
{
    if (searchHi <= searchLo)
        return -1;

    buildSyncLowpass (left, searchHi);

    // Stufe 1: die Grundwelle. Sie hat je Periode genau einen steigenden
    // Nulldurchgang, das Bild kann also nicht mehr zwischen Obertoenen
    // springen.
    // Die Periode faellt bei dieser Gelegenheit mit ab - sie steckt in
    // denselben Nulldurchgaengen (siehe measurePeriodSamples).
    lastPeriodSamples = measurePeriodSamples (searchLo, searchHi);

    const int coarse = nearestRisingZero (syncScratch.data(), searchLo, searchHi, target);

    if (coarse < 0)
    {
        // Nichts uebrig geblieben (Stille, oder ein Signal ganz ohne tiefen
        // Anteil): dann eben direkt im Rohsignal, wie zuvor.
        return nearestRisingZero (left, searchLo, searchHi, target);
    }

    // Stufe 2: die Flanke, die man wirklich sieht - die naechste des
    // Rohsignals. Findet sich dort keine, bleibt der grobe Fund stehen; er
    // ist immer noch besser als gar keine Ausrichtung.
    const int fine = nearestRisingZero (left, searchLo, searchHi, coarse);

    return fine >= 0 ? fine : coarse;
}

double ScopeComponent::measurePeriodSamples (int searchLo, int searchHi) const
{
    // Mittlerer Abstand aufeinanderfolgender steigender Nulldurchgaenge im
    // gefilterten Fenster. Der Mittelwert und nicht der erste Abstand: eine
    // einzelne Periode kann durch Rauschen daneben liegen, ueber ein ganzes
    // Fenster mittelt sich das heraus.
    const float* w = syncScratch.data();

    int    first = -1, last = -1, count = 0;

    for (int i = juce::jmax (1, searchLo); i < searchHi; ++i)
    {
        if (w[i - 1] <= 0.0f && w[i] > 0.0f)
        {
            if (first < 0)
                first = i;

            last = i;
            ++count;
        }
    }

    // Mindestens zwei Durchgaenge, sonst gibt es keinen Abstand zu messen.
    if (count < 2 || last <= first)
        return 0.0;

    return (double) (last - first) / (double) (count - 1);
}

int ScopeComponent::periodAlignedLength (int requested) const
{
    const double period = lastPeriodSamples;

    // Keine Grundwelle erkannt, oder sie passt gar nicht erst einmal ins
    // Bild: dann gibt es nichts zu rasten.
    if (period < 2.0 || period > (double) requested)
        return requested;

    const double cycles = std::floor ((double) requested / period + 0.5);

    if (cycles < 1.0)
        return requested;

    const int aligned = (int) std::lround (cycles * period);

    // Nur rasten, wenn es wirklich um weniger als eine halbe Periode geht -
    // sonst waere es kein Rasten mehr, sondern ein eigener Zoom. Und nie
    // ueber das hinaus, was tatsaechlich im Rohfenster steht.
    if (aligned < 2 || aligned > requested)
        return requested;

    return aligned;
}

void ScopeComponent::setEventTriggerEnabled (bool shouldTrigger)
{
    if (eventTriggerEnabled == shouldTrigger)
        return;

    eventTriggerEnabled = shouldTrigger;

    // Frisch scharf: keine Haltezeit, kein alter Fund. Sonst wartete das Bild
    // nach dem Einschalten noch den Rest einer Haltezeit ab, die zu einem
    // Ereignis von vorhin gehoerte.
    holdUntilMs      = 0.0;
    holding          = false;
    hasTriggeredOnce = false;

    repaint();
}

int ScopeComponent::findLevelRise (const float* left, const float* right,
                                   int searchLo, int searchHi) const
{
    const double sr = sampleRateHint > 0.0 ? sampleRateHint : 48000.0;

    // Einpolige Glaetter, Koeffizient aus der Zeitkonstante. Beide sehen
    // denselben Betrag (lauter der beiden Kanaele) - ein Knall, der nur auf
    // einem Ohr ankommt, ist trotzdem ein Knall.
    const double aFast = 1.0 - std::exp (-1.0 / (envFastSeconds * sr));
    const double aSlow = 1.0 - std::exp (-1.0 / (envSlowSeconds * sr));

    double envFast = 0.0;
    double envSlow = 0.0;

    for (int n = 0; n < searchHi; ++n)
    {
        const double mag = std::max (std::abs ((double) left[n]), std::abs ((double) right[n]));

        envFast += aFast * (mag - envFast);
        envSlow += aSlow * (mag - envSlow);

        // Der langsame Folger braucht Anlauf: erst ab searchLo zaehlt ein
        // Fund. Davor laeuft die Schleife nur, um beide einzuschwingen.
        if (n < searchLo)
            continue;

        if (envFast > (double) riseFloor && envFast > riseFactor * envSlow)
            return n;
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

void ScopeComponent::zoomAroundFraction (float factor, float anchorFraction)
{
    if (! historyMode)
    {
        // Rechter Rand ist immer "jetzt" und damit fix - kein Anker
        // moeglich, s. Klassenkommentar oben und Header.
        setDisplaySampleCount ((int) std::lround ((double) displaySamples * (double) factor));
        return;
    }

    anchorFraction = juce::jlimit (0.0f, 1.0f, anchorFraction);

    // Absoluter Sample-Index in der Historie unter dem Cursor - bleibt beim
    // Zoomen an derselben Bildschirmposition stehen, weil beide Distanzen
    // (Anker zu linkem/rechtem Rand) mit demselben factor skaliert werden -
    // genau wie im Vorbild (cursorVal - (cursorVal - xMin) * f).
    const double anchorAbsolute = (double) panOffset + (double) anchorFraction * (double) displaySamples;

    const int newCount = juce::jlimit (minDisplaySamples, juce::jmax (minDisplaySamples, maxDisplaySamples),
                                       (int) std::lround ((double) displaySamples * (double) factor));

    if (newCount == displaySamples)
        return;

    const int maxOffset = juce::jmax (0, frozenLength - newCount);
    const int newPanOffset = juce::jlimit (0, maxOffset,
                                           (int) std::lround (anchorAbsolute - (double) anchorFraction * (double) newCount));

    displaySamples = newCount;
    panOffset      = newPanOffset;

    shownLeft.assign ((size_t) displaySamples, 0.0f);
    shownRight.assign ((size_t) displaySamples, 0.0f);
    repaint();
}

void ScopeComponent::panBy (int deltaSamples)
{
    if (! historyMode)
        return;

    const int maxOffset = juce::jmax (0, frozenLength - displaySamples);
    panOffset = juce::jlimit (0, maxOffset, panOffset + deltaSamples);
    repaint();
}

void ScopeComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaX == 0.0f && wheel.deltaY == 0.0f)
        return;

    // Achsen-Lock (s. Header): eine laufende Geste behaelt ihre Achse, eine
    // schon wheelGestureGapMs alte gilt als beendet und wird neu entschieden.
    const juce::int64 now = juce::Time::currentTimeMillis();

    if (now - lastWheelEventMs > wheelGestureGapMs)
        wheelGestureAxis = WheelGestureAxis::none;

    lastWheelEventMs = now;

    if (wheelGestureAxis == WheelGestureAxis::none)
        wheelGestureAxis = std::abs (wheel.deltaX) > std::abs (wheel.deltaY)
                          ? WheelGestureAxis::horizontal : WheelGestureAxis::vertical;

    // Waagerecht = pannen. Wirkt nur im History-Modus (dort gibt es etwas
    // zum Verschieben) - im Live-Betrieb tut eine waagerechte Geste bewusst
    // nichts, statt ins Leere zu pannen (s. Klassenkommentar oben).
    if (wheelGestureAxis == WheelGestureAxis::horizontal)
    {
        if (historyMode && getWidth() > 0)
        {
            // JUCEs Wheel-Delta zurueck auf echte Bildschirm-Pixel gerechnet
            // und wie mouseDrag ueber die Breite normiert - genau die
            // dxData = deltaX/width*span-Formel aus dem Vorbild, nur mit
            // vorgeschalteter Skala-Rueckrechnung, s. Header.
            const double scrolledPixels = (double) wheel.deltaX / wheelPixelDeltaScale;
            const double deltaSamples   = -scrolledPixels / (double) getWidth() * (double) displaySamples;
            panBy ((int) std::lround (deltaSamples));
        }

        return;
    }

    // Senkrecht = zoomen, um den Mauszeiger herum. Stetige Exponentialkurve
    // proportional zum tatsaechlichen deltaY (wie im Vorbild:
    // Math.exp(deltaY * k)) statt fixem 0.8/1.25-Sprung pro Event - bei
    // Trackpad-Gesten kommen viele Events mit sehr kleinem deltaY, ein
    // fixer Sprung pro Event macht das sonst viel zu schnell/ruckelig.
    // Hoch scrollen (deltaY > 0) verkuerzt die Zeitbasis (reinzoomen), wie
    // bisher.
    const float anchorFraction = getWidth() > 0 ? (float) e.x / (float) getWidth() : 0.5f;
    const float factor = (float) std::exp (-(double) wheel.deltaY * zoomWheelSensitivity);
    zoomAroundFraction (factor, anchorFraction);
}

void ScopeComponent::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (scaleFactor <= 0.0f)
        return;

    const float anchorFraction = getWidth() > 0 ? (float) e.x / (float) getWidth() : 0.5f;
    zoomAroundFraction (1.0f / scaleFactor, anchorFraction);
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

void ScopeComponent::feed (const float* rawLeft, const float* rawRight, std::uint32_t windowEndSample)
{
    // Der manuelle Freeze hat Vorrang vor allem anderen.
    if (frozen)
        return;

    if (eventTriggerEnabled)
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();

        if (holding)
        {
            if (nowMs < holdUntilMs)
                return;                     // Bild steht, Haltezeit laeuft

            // Haltezeit um: wieder scharf. Das Bild bleibt stehen, bis der
            // naechste Einsatz kommt - ein mitlaufendes Bild waere zwischen
            // zwei Knallen nur Gewackel und liesse nicht erkennen, dass der
            // Trigger wartet.
            holding = false;
            repaint();
        }

        const int captureLen = captureWindowSampleCount();

        // Das Ereignis soll MITTIG stehen, es braucht hinter dem Fund also
        // noch ein halbes Anzeigefenster an Nachlauf. Und davor genauso viel,
        // damit der Vorlauf zu sehen ist.
        const int searchLo = displaySamples / 2;
        const int searchHi = captureLen - displaySamples / 2;

        const int trigger = findLevelRise (rawLeft, rawRight, searchLo, searchHi);

        if (trigger < 0)
            return;                          // nichts Neues, Bild stehen lassen

        // Ist das derselbe Einsatz wie beim letzten Mal? Die Rohfenster
        // ueberlappen bei 30 Hz Anzeigetakt stark, ein Knall steht deshalb in
        // mehreren hintereinander. Verglichen wird auf der absoluten
        // Zeitachse des Ringpuffers.
        const std::uint32_t triggerAbsolute =
            windowEndSample - (std::uint32_t) captureLen + (std::uint32_t) trigger;

        if (hasTriggeredOnce
            && (std::uint32_t) (triggerAbsolute - lastTriggerAbsolute) < (std::uint32_t) displaySamples)
            return;

        lastTriggerAbsolute = triggerAbsolute;
        hasTriggeredOnce    = true;

        const int start = trigger - displaySamples / 2;

        for (int n = 0; n < displaySamples; ++n)
        {
            shownLeft[(size_t) n]  = rawLeft[start + n];
            shownRight[(size_t) n] = rawRight[start + n];
        }

        lastFrameWasSynced = true;
        holding            = true;
        holdUntilMs        = nowMs + 1000.0 * holdSeconds;

        repaint();
        return;
    }

    // Ungesynct: juengste Haelfte des Rohfensters zeigen - rawLeft/rawRight
    // haben captureWindowSampleCount() == 2*displaySamples Samples, das
    // letzte Sample ist "jetzt" (s. ScopeRingBuffer::readLatest()), also
    // beginnt die juengste Haelfte bei Index displaySamples (NICHT
    // displaySamples/2 - das war ein Bug: zeigte die MITTE des Rohfensters,
    // liess das juengste Viertel komplett unangezeigt liegen und lag damit
    // permanent um displaySamples/2 Samples hinter "jetzt" zurueck, bei
    // grossem Zoom-Out mehrere hundert ms - @dpa: "zeigt den Inhalt immer
    // erst sehr spaet an, aber beim Freeze ist es ploetzlich weiter vorn",
    // weil enterHistoryMode() unten korrekt das aktuelle Ende zeigt).
    int start = displaySamples;
    lastFrameWasSynced = false;
    lastPeriodSamples  = 0.0;
    shownSampleCount   = displaySamples;

    if (syncEnabled)
    {
        const int captureLen = captureWindowSampleCount();
        const int trigger = findTriggerIndexNear (rawLeft, captureLen / 4, (captureLen * 3) / 4, captureLen / 2);

        if (trigger >= 0)
        {
            // Ganze Perioden ins Bild (siehe lastPeriodSamples im Header).
            // Gezeichnet wird ab dem Trigger nach RECHTS, nicht um ihn herum
            // zentriert: nur so faengt das Bild an der Flanke an und hoert
            // eine ganze Zahl von Perioden spaeter wieder dort auf.
            shownSampleCount = periodAlignedLength (displaySamples);

            start = shownSampleCount < displaySamples
                  ? trigger
                  : trigger - displaySamples / 2;

            // Innerhalb des Rohfensters bleiben.
            start = juce::jlimit (0, juce::jmax (0, captureLen - shownSampleCount), start);

            lastFrameWasSynced = true;
        }
    }

    for (int n = 0; n < shownSampleCount; ++n)
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
    // Vorherige Datei am selben Pfad (sollte es die geben) wegraeumen -
    // FileOutputStream haengt sonst an eine bestehende Datei an.
    file.deleteFile();

    auto* rawStream = file.createOutputStream().release();

    if (rawStream == nullptr)
        return false;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wavFormat.createWriterFor (rawStream, juce::jmax (1.0, sampleRateHint),
                                   2, 32, {}, 0));

    if (writer == nullptr)
    {
        delete rawStream;   // createWriterFor loescht den Stream nur bei Erfolg
        return false;
    }

    // AudioBuffer referenziert nur die vorhandenen Daten (kein Umkopieren) -
    // visibleLeft()/visibleRight() liefern schon zusammenhaengende Bereiche
    // (Live-Puffer bzw. History mit Offset). const_cast ist hier sicher:
    // writeFromAudioSampleBuffer liest nur, die referenzierte AudioBuffer
    // schreibt nirgends hinein.
    float* channels[2] { const_cast<float*> (visibleLeft()), const_cast<float*> (visibleRight()) };
    juce::AudioBuffer<float> buffer (channels, 2, displaySamples);

    return writer->writeFromAudioSampleBuffer (buffer, 0, displaySamples);
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
    else if ((syncEnabled || eventTriggerEnabled) && lastFrameWasSynced)
    {
        drawTriggerLine = true;
        triggerX = area.getCentreX();
    }

    if (drawTriggerLine)
    {
        g.setColour (juce::Colours::yellow.withAlpha (0.35f));
        g.drawLine (triggerX, area.getY(), triggerX, area.getBottom(), 1.0f);
    }

    // Zustand des Ereignis-Triggers. Ohne ihn waere nicht zu unterscheiden,
    // ob das Bild gerade auf den naechsten Einsatz wartet oder ihn schon
    // gefangen hat und haelt - beides sieht als stehendes Bild gleich aus.
    if (eventTriggerEnabled && ! historyMode && ! frozen)
    {
        g.setColour (holding ? juce::Colours::orangered.withAlpha (0.85f)
                             : juce::Colours::white.withAlpha (0.45f));
        g.setFont (12.0f);
        g.drawText (holding ? "gehalten" : "scharf",
                    area.reduced (6.0f).removeFromTop (16.0f),
                    juce::Justification::topRight, false);
    }

    // Im Live-Sync koennen weniger Samples gezeichnet werden als der Zoom
    // vorgibt - sie werden dann auf die volle Breite gestreckt, damit das Bild
    // gefuellt bleibt (siehe shownSampleCount im Header). Im History-Modus
    // gilt immer die volle Fensterbreite.
    const int traceCount = (historyMode || shownSampleCount <= 0)
                         ? displaySamples
                         : shownSampleCount;

    auto drawTrace = [&] (const float* samples, juce::Colour colour)
    {
        juce::Path path;
        const float xStep = area.getWidth() / (float) juce::jmax (1, traceCount - 1);

        for (int n = 0; n < traceCount; ++n)
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
