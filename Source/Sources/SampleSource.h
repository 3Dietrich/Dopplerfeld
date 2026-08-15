#pragma once

#include "SoundSource.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>   // juce::Timer, für die verzögerte Pufferfreigabe (s.u.)

#include <atomic>
#include <vector>

// SampleSource spielt eine geladene Audiodatei dauerhaft loopend ab
// (Plan 3.10). Wie EngineGenerator läuft sie ohne Note-on/-off durch -
// die Doppler-Physik entsteht ausschließlich aus der Position im Feld
// (DopplerEngine/PropagationPath), nicht aus einer Hüllkurve hier.
class SampleSource : public SoundSource
{
public:
    SampleSource();
    ~SampleSource() override;

    // MUSS vom Message-Thread gerufen werden: juce::AudioFormatManager und
    // Datei-IO sind im Audio-Thread verboten (Allokation, Systemcalls,
    // potenziell blockierend). Gibt bei Lese-/Formatfehlern false zurück;
    // der zuletzt geladene Stand (oder Stille, falls noch nie geladen)
    // bleibt dabei unverändert hörbar. Siehe currentBuffer weiter unten
    // für den threadsicheren Übergang zum Audio-Thread.
    bool loadFile (const juce::File& file);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void renderMono (float* out, int numSamples) override;
    double dominantFrequencyHz() const override;

    // Setter: Message-Thread (APVTS-Listener) schreibt, Audio-Thread liest.
    // Werte 1:1 in den Einheiten aus Params.cpp (dB, Halbtöne, 0..1, ms, Hz).
    void setGainDb (float db) { gainDb.store (db); }
    void setPitchSemitones (float semitones) { pitchSemitones.store (semitones); }
    void setLoopStart (float normalised01) { loopStartNorm.store (normalised01); }
    void setLoopEnd (float normalised01) { loopEndNorm.store (normalised01); }
    void setLoopXfadeMs (float ms) { loopXfadeMsValue.store (ms); }
    void setEqLowGainDb (float db) { eqLowGainDb.store (db); }
    void setEqMidGainDb (float db) { eqMidGainDb.store (db); }
    void setEqMidFreqHz (float hz) { eqMidFreqHz.store (hz); }
    void setEqHighGainDb (float db) { eqHighGainDb.store (db); }

private:
    //=====================================================================
    // Geladene Sample-Daten: immer 1 Kanal (Downmix beim Laden - Mono ist
    // laut SoundSource Pflicht), unveränderlich nach dem Bau. Weil ein
    // Objekt nach dem Konstruieren nie mehr verändert wird, dürfen Audio-
    // und Message-Thread gleichzeitig aus demselben Exemplar LESEN, ohne
    // sich gegenseitig zu blockieren - nur der ZEIGER darauf wechselt.
    struct SampleBuffer : public juce::ReferenceCountedObject
    {
        juce::AudioBuffer<float> data;   // 1 Kanal
        double nativeSampleRate = 44100.0;
        int numSamples = 0;
    };
    using SampleBufferPtr = juce::ReferenceCountedObjectPtr<SampleBuffer>;

    // --- Thread-sicherer Pufferwechsel (die heikelste Stelle hier) ---
    //
    // juce::ReferenceCountedObject zählt Referenzen zwar atomar mit (siehe
    // juce_ReferenceCountedObject.h), aber laut JUCE-Doku ausdrücklich NUR
    // das Erzeugen/Zerstören eines ReferenceCountedObjectPtr ist zwischen
    // Threads sicher - das Zuweisen/Tauschen DES ZEIGERS selbst nicht. Ein
    // roher Zeigertausch zwischen Message- (Schreiber) und Audio-Thread
    // (Leser) wäre also ein Data Race. Deshalb schützt eine SpinLock genau
    // diesen einen Zeiger: kein Allokieren, kein Syscall, im Audio-Thread
    // damit unbedenklich kurz zu halten.
    //
    // Die zweite Vorgabe - der ALTE Puffer darf nie im Audio-Thread
    // freigegeben werden - lässt sich mit Referenzzählung allein NICHT
    // garantieren: renderMono() muss sich pro Aufruf eine lokale Kopie
    // holen (sonst dürfte currentBuffer während eines laufenden Renders
    // nie getauscht werden), und wenn diese lokale Kopie beim Verlassen
    // der Funktion zufällig die letzte Referenz ist, deallokiert genau der
    // Audio-Thread. Lösung: loadFile() legt den verdrängten Puffer
    // zusätzlich in pendingRelease ab. Solange dort ein Eintrag liegt,
    // hält ihn diese Liste selbst am Leben (Refcount >= 1), unabhängig
    // davon, wann der Audio-Thread seine lokale Kopie fallen lässt. Ein
    // Timer im Message-Thread räumt pendingRelease erst nach einer
    // Karenzzeit ab - der Übergang von Refcount 1 auf 0 (und damit
    // `delete`) passiert dadurch praktisch immer im Message-Thread.
    // Restrisiko: eine absolute Garantie gäbe es nur mit Hazard-Pointern;
    // die Karenzzeit (mehrere 100 ms) gegen die Laufzeit einer einzelnen
    // renderMono()-Kopie (Mikrosekunden) macht das in der Praxis
    // vernachlässigbar - siehe purgePendingRelease().
    juce::SpinLock bufferLock;
    SampleBufferPtr currentBuffer;   // geschützt durch bufferLock

    struct PendingRelease
    {
        SampleBufferPtr buffer;
        juce::int64 releaseAfterMs = 0;   // juce::Time::currentTimeMillis()
    };
    std::vector<PendingRelease> pendingRelease;   // nur Message-Thread

    void purgePendingRelease();   // Message-Thread: Timer + opportunistisch aus loadFile()

    struct ReleaseTimer : public juce::Timer
    {
        explicit ReleaseTimer (SampleSource& o) : owner (o) {}
        void timerCallback() override { owner.purgePendingRelease(); }
        SampleSource& owner;
    };
    ReleaseTimer releaseTimer { *this };

    //=====================================================================
    // Wiedergabezustand, ausschließlich Audio-Thread.
    double sampleRate = 44100.0;
    double readPos = 0.0;   // fraktionaler Index in SampleBuffer::data (native Rate)

    // Erkennt einen Pufferwechsel zwischen zwei renderMono()-Aufrufen (nur
    // zum Vergleichen, NIE dereferenziert - der eigentliche Zugriff läuft
    // immer über die lokale SampleBufferPtr-Kopie in renderMono()).
    const SampleBuffer* lastRenderedBuffer = nullptr;

    static float readSample (const SampleBuffer&, double pos);

    //=====================================================================
    // Parameter: Message-Thread schreibt, Audio-Thread liest.
    std::atomic<float> gainDb { 0.0f };
    std::atomic<float> pitchSemitones { 0.0f };
    std::atomic<float> loopStartNorm { 0.0f };
    std::atomic<float> loopEndNorm { 1.0f };
    std::atomic<float> loopXfadeMsValue { 10.0f };
    std::atomic<float> eqLowGainDb { 0.0f };
    std::atomic<float> eqMidGainDb { 0.0f };
    std::atomic<float> eqMidFreqHz { 1000.0f };
    std::atomic<float> eqHighGainDb { 0.0f };

    // EQ: Low-Shelf, Peak (Mitte), High-Shelf in Serie (Plan 3.10).
    // Koeffizienten werden nur bei tatsächlicher Änderung neu berechnet
    // (billiger Vergleich pro Block statt teurer Neuberechnung pro Sample).
    juce::dsp::IIR::Filter<float> eqLow, eqMid, eqHigh;
    float lastEqLowGainDb = 0.0f;
    float lastEqMidGainDb = 0.0f;
    float lastEqMidFreqHz = 1000.0f;
    float lastEqHighGainDb = 0.0f;
    void updateEqCoefficientsIfNeeded();

    // Grobschätzung für dominantFrequencyHz(): siehe .cpp-Kommentar bei
    // estimateDominantFrequency() für die Begründung der Methode.
    std::atomic<double> estimatedDominantHz { 200.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleSource)
};
