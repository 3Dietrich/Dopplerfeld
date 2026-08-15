#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

// Crossfade-Engine (Plan 3.7). Ein eigenständiges Bauteil, das an mehreren
// Stellen instanziiert wird: Quellwechsel (SoundSourceHolder), Positions-
// sprung und Feldgrößenwechsel (DopplerEngine).
//
// Ausdrücklich NICHT hierüber läuft der Solver-Regimewechsel (Plan 3.7,
// letzter Absatz): ein globaler Crossfade würde den Knall wegmitteln, den man
// gerade hören will. Der sitzt als kurzer Envelope pro Wurzelzweig in
// PropagationPath und hat mit dieser Datei nichts zu tun.

enum class FadeReason
{
    SourcePosition,   // Quelle springt an eine neue Stelle
    SourceTimbre,     // Generator <-> Sample
    FieldSize,        // Feldmaßstab geändert
    MediumChange,     // Phase 2: Temperatur/Schallgeschwindigkeit
    Manual            // Aufrufer gibt die Dauer selbst vor
};

struct FadeContext
{
    FadeReason reason = FadeReason::Manual;
    double     sampleRate = 48000.0;
    double     positionDeltaMetres = 0.0;   // für SourcePosition
    double     baseFrequencyHz     = 0.0;   // für SourceTimbre
    double     smootherTauSeconds  = 0.0;
    double     manualSeconds       = 0.05;
    bool       useManual           = false;
};

// Natürliche Fadedauer in Samples, freie Funktion, keine Allokation.
// Formeln und Grenzen stehen in der .cpp, jeweils mit ihrer Begründung.
int computeFadeSamples (const FadeContext& ctx);

//======================================================================
// Doppelpfad mit Equal-Power-Überblendung.
//
// Ablauf: der Aufrufer konfiguriert pending() fertig und ruft beginSwitch().
// Ab dann rendern beide Renderer in getrennte Zwischenpuffer und werden
// gemischt. Nach Abschluss tauschen active und pending; der neue pending
// wird stillgelegt und rendert nicht mehr, kostet also keine CPU.
//
// Anforderungen an Renderer:
//   - default-konstruierbar
//   - void renderInto (juce::AudioBuffer<float>& dest, int numSamples)
//     schreibt genau numSamples ÜBERSCHREIBEND (nicht addierend) in alle
//     Kanäle von dest. dest darf länger sein als numSamples und hat
//     mindestens die in prepare() angegebene Kanalzahl.
// Alles Weitere (prepare/reset des Renderers, seine Parameter) regelt der
// Besitzer direkt über active()/pending() - der Crossfader kennt nur das
// Rendern, sonst würde er zur zweiten Kopie jeder Renderer-Schnittstelle.
//
// Retrigger während eines laufenden Fades startet NICHT neu. Sonst
// kaskadieren schnelle Reglerbewegungen zu einem Dauerfade, und man hört nie
// einen Zielzustand fertig (Plan 3.7 nennt das ausdrücklich als Bugquelle).
// Stattdessen liegt genau ein Wechsel in einer Warteschlange der Länge eins,
// der letzte gewinnt.
//
// Protokoll für diesen Fall (der Crossfader kann pending() nicht selbst
// konfigurieren - was "Zielzustand" heißt, weiß nur der Aufrufer):
//   1. beginSwitch() während eines Fades legt den Wunsch nur ab.
//   2. Ist der laufende Fade durch, meldet queuedSwitchDue() true.
//   3. Der Aufrufer konfiguriert pending() neu und ruft startQueuedSwitch().
// Punkt 2/3 gehören an den Blockanfang, der Nachfolge-Fade startet also
// höchstens einen Block später. Das ist der Preis dafür, dass hier kein
// Callback und kein dritter Renderer-Slot nötig ist - und er fällt genau in
// dem Fall an, den die Regel ohnehin entschleunigen soll.
template <typename Renderer>
class DualPathCrossfader
{
public:
    // numChannels ist gegenüber Plan 3.7 ergänzt: die Zwischenpuffer müssen
    // vorab stehen (im Audiothread wird nichts allokiert), und die Kanalzahl
    // ist die einzige Angabe, die dafür fehlt. Default 2 = der Stereofall.
    void prepare (double sampleRate, int maxBlock, int numChannels = 2)
    {
        sr        = sampleRate;
        blockSize = std::max (1, maxBlock);

        const int ch = std::max (1, numChannels);

        scratchA.setSize (ch, blockSize, false, true, true);
        scratchB.setSize (ch, blockSize, false, true, true);

        gainA.assign ((size_t) blockSize, 0.0f);
        gainB.assign ((size_t) blockSize, 0.0f);

        reset();
    }

    void reset()
    {
        activeIndex = 0;
        fading      = false;
        fadePos     = 0;
        fadeLength  = 1;
        queued      = false;
        queuedDue   = false;
        queuedLength = 1;

        scratchA.clear();
        scratchB.clear();
    }

    Renderer&       active()        { return slots[activeIndex]; }
    const Renderer& active() const  { return slots[activeIndex]; }
    Renderer&       pending()       { return slots[1 - activeIndex]; }
    const Renderer& pending() const { return slots[1 - activeIndex]; }

    bool isFading() const { return fading; }

    // Ein Wechsel steht noch aus: entweder wartet er auf das Ende des
    // laufenden Fades oder auf startQueuedSwitch(). Der Aufrufer kann daran
    // erkennen, dass die aktuell hörbare Konfiguration nicht die zuletzt
    // gewünschte ist (DopplerEngine friert daran die Zielverfolgung ein).
    bool hasQueuedSwitch() const { return queued || queuedDue; }

    // Der angemeldete Wechsel ist an der Reihe: pending() neu konfigurieren,
    // dann startQueuedSwitch().
    bool queuedSwitchDue() const { return queuedDue; }

    void startQueuedSwitch()
    {
        if (! queuedDue)
            return;

        queuedDue = false;
        beginSwitch (queuedLength);
    }

    // pending() ist vorher zu konfigurieren.
    void beginSwitch (int fadeSamples)
    {
        const int n = std::max (1, fadeSamples);

        if (fading || queuedDue)
        {
            // Warteschlange der Länge eins, der letzte Aufruf gewinnt.
            queued       = true;
            queuedLength = n;
            return;
        }

        fadeLength = n;
        fadePos    = 0;
        fading     = true;
    }

    void process (juce::AudioBuffer<float>& out)
    {
        const int numSamples = std::min (out.getNumSamples(), blockSize);

        if (numSamples <= 0 || out.getNumChannels() <= 0)
            return;

        if (! fading)
        {
            // Kein Zwischenpuffer, kein Kopieren: der stillgelegte Renderer
            // läuft gar nicht erst mit.
            slots[activeIndex].renderInto (out, numSamples);
            return;
        }

        slots[activeIndex].renderInto (scratchA, numSamples);
        slots[1 - activeIndex].renderInto (scratchB, numSamples);

        // Equal Power: gA = cos(θ), gB = sin(θ) mit θ = (π/2)·p. gA² + gB² = 1,
        // die Summenleistung bleibt also über den ganzen Fade konstant - genau
        // richtig für zwei unkorrelierte Signale, wie es zwei verschiedene
        // Quellen oder zwei verschiedene Laufzeiten immer sind.
        constexpr double halfPi = 1.57079632679489661923;

        for (int i = 0; i < numSamples; ++i)
        {
            // Nach dem Ende des Fades bleibt p auf 1, der Rest des Blocks ist
            // damit reines B - der Fade endet sample-genau, nicht am Blockende.
            const double p     = std::min (1.0, (double) (fadePos + i) / (double) fadeLength);
            const double theta = halfPi * p;

            gainA[(size_t) i] = (float) std::cos (theta);
            gainB[(size_t) i] = (float) std::sin (theta);
        }

        const int mixCh = std::min (out.getNumChannels(), scratchA.getNumChannels());

        for (int ch = 0; ch < mixCh; ++ch)
        {
            float*       dst = out.getWritePointer (ch);
            const float* a   = scratchA.getReadPointer (ch);
            const float* b   = scratchB.getReadPointer (ch);

            for (int i = 0; i < numSamples; ++i)
                dst[i] = a[i] * gainA[(size_t) i] + b[i] * gainB[(size_t) i];
        }

        // Kanäle, für die kein Zwischenpuffer existiert, bleiben sonst mit
        // altem Hostinhalt stehen.
        for (int ch = mixCh; ch < out.getNumChannels(); ++ch)
            out.clear (ch, 0, numSamples);

        fadePos += numSamples;

        if (fadePos >= fadeLength)
        {
            activeIndex = 1 - activeIndex;
            fading      = false;
            fadePos     = 0;

            if (queued)
            {
                queued    = false;
                queuedDue = true;
            }
        }
    }

    double getSampleRate() const { return sr; }
    int    maxBlockSize() const { return blockSize; }

private:
    Renderer slots[2];

    juce::AudioBuffer<float> scratchA, scratchB;

    // Verlaufsgewichte vorab gerechnet statt cos/sin pro Kanal und Sample -
    // bei mehreren Kanälen sonst dieselbe Transzendente mehrfach.
    std::vector<float> gainA, gainB;

    double sr        = 0.0;
    int    blockSize = 0;

    int  activeIndex = 0;
    bool fading      = false;
    int  fadePos     = 0;
    int  fadeLength  = 1;

    bool queued       = false;
    bool queuedDue    = false;
    int  queuedLength = 1;
};
