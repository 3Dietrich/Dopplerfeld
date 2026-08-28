#pragma once

#include "SoundSource.h"
#include "../Util/Crossfader.h"

// Quellstufe mit Überblendung (Plan 3.7, erste Instanziierung des
// DualPathCrossfader). Sitzt VOR dem Schreiben in den Signalpuffer: der
// gesamte Propagationsteil bleibt damit einfach vorhanden, nur die billige
// Quellstufe existiert kurzzeitig doppelt.
//
// Ist selbst eine SoundSource und steht damit überall dort, wo sonst eine
// rohe SoundSource* stünde (DopplerEngine::setSource, H13-Verdrahtung) - der
// Crossfade ist von außen nicht sichtbar.
//
// Kein Besitz: Generator und Sample leben im Processor, hier stehen nur
// Zeiger. Lebensdauer regelt der Aufrufer.
//
// Threading wie DopplerEngine::jumpSourceTo: setSource()/switchTo() gehören
// in den Audiothread (Plan 3.12: der Message-Thread schickt "Sample
// getauscht" über die lock-freie Kommandoqueue) oder vor den ersten Block.
class SoundSourceHolder : public SoundSource
{
public:
    // Bereitet den Doppelpfad vor und reicht prepare an die aktuell gesetzten
    // Quellen durch (jede genau einmal, auch wenn beide Slots denselben Zeiger
    // halten). Später gesetzte Quellen bereitet der Aufrufer selbst vor.
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void renderMono (float* out, int numSamples) override;

    // Grundfrequenz der ZIEL-Quelle: während eines Fades ist das die, die
    // gleich zu hören ist. Ein direkt folgender Wechsel bemisst seine Dauer
    // damit an dem Klang, von dem er tatsächlich weggeht.
    double dominantFrequencyHz() const override;

    // Harter Wechsel ohne Fade - für die Erstbelegung.
    void setSource (SoundSource* src);

    // Weicher Wechsel. Die Dauer kommt aus computeFadeSamples mit
    // FadeReason::SourceTimbre; die Grundfrequenz dafür liefert die NEUE
    // Quelle (Plan 3.7).
    void switchTo (SoundSource* newSource);

    // Zuletzt gewünschte Quelle, auch wenn der Fade dorthin noch läuft.
    SoundSource* getSource() const { return targetSource; }

    bool isFading() const { return xfade.isFading(); }

private:
    // Renderer-Seite des Doppelpfads: ein Zeiger, sonst nichts. Genau deshalb
    // ist dieser Fade billig - er verdoppelt die Quellstufe, nicht die Physik.
    struct Slot
    {
        SoundSource* src = nullptr;

        void renderInto (juce::AudioBuffer<float>& dest, int numSamples)
        {
            if (src != nullptr)
                src->renderMono (dest.getWritePointer (0), numSamples);
            else
                dest.clear (0, 0, numSamples);   // keine Quelle = Stille, kein Restrauschen
        }
    };

    int fadeSamplesFor (const SoundSource* newSource) const;

    DualPathCrossfader<Slot> xfade;

    // Die Warteschlange der Länge eins auf Aufruferseite (Protokoll siehe
    // DualPathCrossfader): der Crossfader merkt sich nur die Dauer, was
    // "Zielzustand" heißt, weiß nur diese Klasse.
    SoundSource* queuedSource = nullptr;
    SoundSource* targetSource = nullptr;

    double sr             = 48000.0;
    int    preparedBlock  = 0;
};
