#include "SampleSource.h"
#include "../Physics/Interpolation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    // Karenzzeit, bevor ein verdrängter Puffer wirklich freigegeben wird
    // (siehe SampleSource.h, Abschnitt "Thread-sicherer Pufferwechsel").
    // Ein einzelner renderMono()-Aufruf braucht Mikrosekunden bis wenige
    // Millisekunden; 500 ms Karenz liegen also um Größenordnungen darüber.
    constexpr int releaseGraceMs = 500;
    constexpr int releaseTimerIntervalMs = 250;

    // Mindestlänge einer Loop in Samples, damit loopStart/loopEnd nicht auf
    // denselben Punkt fallen können (sonst Division durch ~0 im Crossfade
    // und ein Endlos-Wrap in der while-Schleife von renderMono).
    constexpr double minLoopLenSamples = 8.0;
}

SampleSource::SampleSource()
{
    releaseTimer.startTimer (releaseTimerIntervalMs);
}

SampleSource::~SampleSource()
{
    releaseTimer.stopTimer();
    // Läuft (wie loadFile) auf dem Message-Thread: currentBuffer und alle
    // pendingRelease-Einträge werden hier ganz regulär freigegeben.
}

bool SampleSource::loadFile (const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const int numSourceChannels = (int) reader->numChannels;
    const int numSamples = (int) reader->lengthInSamples;
    if (numSourceChannels <= 0 || numSamples <= 0)
        return false;

    SampleBufferPtr newBuffer = new SampleBuffer();
    newBuffer->nativeSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    newBuffer->numSamples = numSamples;
    newBuffer->data.setSize (1, numSamples);

    if (numSourceChannels == 1)
    {
        reader->read (&newBuffer->data, 0, numSamples, 0, true, false);
    }
    else
    {
        // SoundSource verlangt Mono (Plan: "Mono ist Pflicht") - Downmix
        // durch gleichgewichtetes Aufaddieren aller Quellkanäle.
        juce::AudioBuffer<float> multi (numSourceChannels, numSamples);
        reader->read (&multi, 0, numSamples, 0, true, true);

        newBuffer->data.clear();
        const float mixGain = 1.0f / (float) numSourceChannels;
        for (int ch = 0; ch < numSourceChannels; ++ch)
            newBuffer->data.addFrom (0, 0, multi, ch, 0, numSamples, mixGain);
    }

    // Grobschätzung der dominanten Frequenz per Nulldurchgangszählung über
    // ein kurzes Startfenster (siehe Begründung unten bei estimatedDominantHz-
    // Verwendung / dominantFrequencyHz()).
    {
        const int windowLen = juce::jmin (numSamples, (int) (newBuffer->nativeSampleRate * 0.25)); // 250 ms
        const float* data = newBuffer->data.getReadPointer (0);
        int crossings = 0;
        for (int i = 1; i < windowLen; ++i)
            if ((data[i - 1] < 0.0f) != (data[i] < 0.0f))
                ++crossings;

        const double windowSeconds = windowLen / newBuffer->nativeSampleRate;
        if (crossings >= 4 && windowSeconds > 0.0)
            estimatedDominantHz.store ((crossings * 0.5) / windowSeconds);
        // sonst: vorheriger/Default-Wert (200 Hz) bleibt stehen - ein zu
        // stilles oder zu kurzes Fenster liefert keine verlässliche Schätzung.
    }

    // Zeigertausch unter Lock (siehe SampleSource.h für die Begründung,
    // warum das nicht ohne eigene Synchronisation geht).
    SampleBufferPtr displaced;
    {
        const juce::SpinLock::ScopedLockType sl (bufferLock);
        displaced = currentBuffer;
        currentBuffer = newBuffer;
    }

    if (displaced != nullptr)
        pendingRelease.push_back ({ displaced, juce::Time::currentTimeMillis() + releaseGraceMs });

    // Opportunistisch mitfegen, statt nur auf den Timer zu warten - falls
    // z.B. mehrere Dateien kurz hintereinander geladen werden.
    purgePendingRelease();

    return true;
}

void SampleSource::purgePendingRelease()
{
    // Nur Message-Thread (Timer-Callback bzw. Aufruf aus loadFile()). Die
    // tatsächliche Deallokation, falls ein Eintrag hier die letzte
    // Referenz hält, passiert also hier - genau das ist der Zweck dieser
    // Liste (siehe SampleSource.h).
    const auto now = juce::Time::currentTimeMillis();

    pendingRelease.erase (
        std::remove_if (pendingRelease.begin(), pendingRelease.end(),
                         [now] (const PendingRelease& p) { return now >= p.releaseAfterMs; }),
        pendingRelease.end());
}

void SampleSource::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    juce::dsp::ProcessSpec spec { newSampleRate, (juce::uint32) maxBlockSize, 1 };
    eqLow.prepare (spec);
    eqMid.prepare (spec);
    eqHigh.prepare (spec);

    // NaN erzwingt beim nächsten updateEqCoefficientsIfNeeded() eine
    // Neuberechnung, weil jeder Vergleich mit NaN als "!=" auswertet -
    // einfacher als ein separates "dirty"-Flag mitzuführen.
    lastEqLowGainDb = lastEqMidGainDb = lastEqMidFreqHz = lastEqHighGainDb = std::numeric_limits<float>::quiet_NaN();

    reset();
}

void SampleSource::reset()
{
    readPos = 0.0;

    eqLow.reset();
    eqMid.reset();
    eqHigh.reset();
}

float SampleSource::readSample (const SampleBuffer& buf, double pos)
{
    if (buf.numSamples <= 0)
        return 0.0f;

    pos = juce::jlimit (0.0, (double) (buf.numSamples - 1), pos);

    const int i1 = (int) std::floor (pos);
    const double frac = pos - (double) i1;
    const float* data = buf.data.getReadPointer (0);

    auto at = [&] (int idx)
    {
        idx = juce::jlimit (0, buf.numSamples - 1, idx);
        return data[idx];
    };

    // Lagrange 4. Ordnung, geteilte Funktion mit dem Rest des Projekts
    // (Physics/Interpolation.h) statt einer eigenen Kopie derselben Mathematik.
    return lagrange4 (at (i1 - 1), at (i1), at (i1 + 1), at (i1 + 2), frac);
}

void SampleSource::updateEqCoefficientsIfNeeded()
{
    const float lowDb = eqLowGainDb.load();
    const float midDb = eqMidGainDb.load();
    const float midHz = eqMidFreqHz.load();
    const float highDb = eqHighGainDb.load();

    // Bewusst exakter Vergleich (kein Toleranzband): es geht nicht um
    // numerische Nähe, sondern nur darum, ob sich seit dem letzten Aufruf
    // überhaupt etwas geändert hat - inklusive des NaN-Tricks aus prepare()
    // (NaN != NaN ist wahr, das erzwingt das erste Update). -Wfloat-equal
    // lokal aus, weil genau diese Identitätsprüfung gewollt ist.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
    const bool changed = lowDb != lastEqLowGainDb
                          || midDb != lastEqMidGainDb
                          || midHz != lastEqMidFreqHz
                          || highDb != lastEqHighGainDb;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    if (! changed)
        return;

    // Low-/High-Shelf-Eckfrequenzen sind bewusst fest (kein eigener Regler
    // dafür in Params.cpp, siehe Plan 3.11) - 200 Hz/8 kHz sind für einen
    // Motor-/Umgebungssample-Klang brauchbare Bass-/Höhen-Kanten. Die Mitte
    // (Peak) ist über eqMidFreq frei einstellbar.
    constexpr float lowShelfFreqHz = 200.0f;
    constexpr float highShelfFreqHz = 8000.0f;
    constexpr float shelfQ = 0.707f;   // Butterworth-Kante, kein Überschwinger
    constexpr float peakQ = 1.0f;

    const float nyquistGuard = (float) (sampleRate * 0.45);
    const float clampedMidHz = juce::jlimit (20.0f, juce::jmax (20.0f, nyquistGuard), midHz);

    // Neuberechnung alloziert (JUCE-übliches Coefficients-Objekt), passiert
    // aber nur bei tatsächlicher Wertänderung - also an der Reglerrate,
    // nicht an jedem Block/Sample.
    eqLow.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sampleRate, lowShelfFreqHz, shelfQ, juce::Decibels::decibelsToGain (lowDb));
    eqMid.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sampleRate, clampedMidHz, peakQ, juce::Decibels::decibelsToGain (midDb));
    eqHigh.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, highShelfFreqHz, shelfQ, juce::Decibels::decibelsToGain (highDb));

    lastEqLowGainDb = lowDb;
    lastEqMidGainDb = midDb;
    lastEqMidFreqHz = midHz;
    lastEqHighGainDb = highDb;
}

void SampleSource::renderMono (float* out, int numSamples)
{
    // Lokale Kopie unter Lock: hält den Puffer für die Dauer dieses Aufrufs
    // am Leben, unabhängig davon, ob loadFile() zwischenzeitlich tauscht
    // (siehe SampleSource.h für die vollständige Begründung des Musters).
    SampleBufferPtr local;
    {
        const juce::SpinLock::ScopedLockType sl (bufferLock);
        local = currentBuffer;
    }

    if (local == nullptr || local->numSamples <= 0)
    {
        juce::FloatVectorOperations::clear (out, numSamples);
        return;
    }

    const SampleBuffer& buf = *local;

    // Loop-Punkte: normiert (0..1 der Dateilänge, Params.cpp) in native
    // Sample-Indizes umrechnen, mit Mindestlänge gegen Degeneration.
    double loopStartIdx = (double) juce::jlimit (0.0f, 1.0f, loopStartNorm.load()) * (buf.numSamples - 1);
    double loopEndIdx = (double) juce::jlimit (0.0f, 1.0f, loopEndNorm.load()) * (buf.numSamples - 1);
    if (loopEndIdx < loopStartIdx + minLoopLenSamples)
        loopEndIdx = juce::jmin ((double) (buf.numSamples - 1), loopStartIdx + minLoopLenSamples);
    const double loopLen = loopEndIdx - loopStartIdx;

    // Neuer Puffer seit dem letzten Aufruf (erstes Laden oder Nachladen):
    // Leseposition sauber an den Loop-Start setzen, statt eine Position aus
    // einem u.U. ganz anders langen alten Puffer weiterzuschleppen.
    if (local.get() != lastRenderedBuffer)
    {
        readPos = loopStartIdx;
        lastRenderedBuffer = local.get();
    }

    double xfadeSamples = (double) juce::jlimit (0.0f, 20.0f, loopXfadeMsValue.load()) * 0.001 * buf.nativeSampleRate;
    xfadeSamples = juce::jmin (xfadeSamples, loopLen * 0.5);   // nie mehr als die halbe Loop überlappen

    const double semitones = (double) pitchSemitones.load();
    // Verhältnis aus Datei- zu Host-Samplerate UND Pitch-Faktor in einem
    // Increment - so klingt samplePitch=0 immer in der Originaltonhöhe,
    // egal ob die Datei nativ mit einer anderen Rate vorliegt (Plan 3.10:
    // "Pitch als Resampling-Verhältnis").
    const double increment = (buf.nativeSampleRate / sampleRate) * std::pow (2.0, semitones / 12.0);

    updateEqCoefficientsIfNeeded();

    for (int i = 0; i < numSamples; ++i)
    {
        const float mainSample = readSample (buf, readPos);
        float outSample = mainSample;

        if (xfadeSamples > 0.0 && readPos >= loopEndIdx - xfadeSamples)
        {
            // Naht-Crossfade (Plan 3.10, 2-20 ms): kurz vor loopEnd wird
            // parallel schon die entsprechende Stelle ab loopStart gelesen
            // und mit Equal-Power eingeblendet.
            const double t = juce::jlimit (0.0, 1.0, (readPos - (loopEndIdx - xfadeSamples)) / xfadeSamples);
            const double overlapPos = loopStartIdx + (readPos - (loopEndIdx - xfadeSamples));
            const float overlapSample = readSample (buf, overlapPos);

            const float gOut = (float) std::cos (t * juce::MathConstants<double>::halfPi);
            const float gIn = (float) std::sin (t * juce::MathConstants<double>::halfPi);
            outSample = mainSample * gOut + overlapSample * gIn;
        }

        out[i] = outSample;

        readPos += increment;

        // Beim Wrap NICHT die volle loopLen abziehen: die letzten
        // xfadeSamples wurden oben schon als Vorschau auf [loopStart,
        // loopStart+xfadeSamples) eingeblendet (gIn->1 kurz vor loopEnd).
        // Der Wrap muss deshalb genau dort weiterlaufen lassen, sonst
        // wird dieses Stück doppelt gehört UND es entsteht ein Phasensprung
        // an der Naht (erst per Selftest so gefunden: Sample-zu-Sample-
        // Sprung von ~0,31 bei einer 0,5-Amplitude-Sinusquelle). Effektive
        // Loop-Länge für den Wrap ist also loopLen - xfadeSamples.
        const double wrapLen = loopLen - xfadeSamples;
        while (wrapLen > 0.0 && readPos >= loopEndIdx)
            readPos -= wrapLen;
    }

    // EQ in Serie: Low-Shelf, Peak, High-Shelf (Plan 3.10).
    float* channels[] = { out };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);
    eqLow.process (context);
    eqMid.process (context);
    eqHigh.process (context);

    juce::FloatVectorOperations::multiply (out, juce::Decibels::decibelsToGain (gainDb.load()), numSamples);
}

double SampleSource::dominantFrequencyHz() const
{
    // Bei einer Sample-Quelle gibt es (anders als beim RPM-getriebenen
    // EngineGenerator) keine explizite Grundfrequenz. Statt hier teuer
    // online zu analysieren, wird einmalig beim Laden per Nulldurchgangs-
    // zählung über ein kurzes Startfenster geschätzt (siehe loadFile()).
    // Das reicht für den einzigen Verwendungszweck laut SoundSource-
    // Interface - die SourceTimbre-Fadedauer (Plan 3.7) und später den
    // Nahfeldterm (Plan 2.7) - beide brauchen keine exakte Tonhöhe,
    // sondern nur eine plausible Größenordnung. Ohne geladene Datei bzw.
    // bei zu wenig Nulldurchgängen bleibt der Default von 200 Hz stehen.
    return estimatedDominantHz.load();
}
