#pragma once

#include "SoundSource.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// Motorgenerator (Plan 3.10): vier PolyBLEP-Sägezahn-Teiltöne mit RPM-Tracking,
// ein RPM-abhängiges Rauschband und langsamer Jitter auf der Grundfrequenz.
//
// Liest nichts selbst aus einer APVTS - das verdrahtet erst H13 im Processor
// per Listener. Stattdessen setzt der Message-Thread hier über einfache
// Setter je einen atomaren Wert pro Parameter; der Audio-Thread liest sie
// lock-frei. Die Werte hängen nicht zusammen (kein gemeinsam-atomarer
// Snapshot nötig), darum reichen einzelne std::atomic<float>.
class EngineGenerator : public SoundSource
{
public:
    EngineGenerator();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void renderMono (float* out, int numSamples) override;
    double dominantFrequencyHz() const override;

    // --- Setter: Message-Thread schreibt, Audio-Thread liest (Plan 3.10) ---

    void setRpm (float rpmValue);

    // index 0..3 für die vier Teiltöne. ratio = r_i, detuneCents = d_i (Cent),
    // trackAmount = t_i in [0,1], levelDb = Pegel des Teiltons in dB.
    void setHarmonic (int index, float ratio, float detuneCents, float trackAmount, float levelDb);

    void setNoiseParams (float fcLoHz, float fcHiHz, float gainLoDb, float gainHiDb, float q);
    void setJitter (float amountPercent, float rateHz);
    void setImbalance (float amount);

private:
    // Ein Sägezahn-Teilton: Reglerwerte atomar, Phase ist reiner Audio-Thread-
    // Zustand (kein Fremdzugriff, daher kein Atomic nötig).
    struct Harmonic
    {
        std::atomic<float> ratio { 1.0f };
        std::atomic<float> detuneCents { 0.0f };
        std::atomic<float> trackAmount { 1.0f };
        std::atomic<float> levelDb { 0.0f };

        double phase = 0.0;   // wrappt per Subtraktion von 1.0, nie fmod/Clamp (Plan-Vorgabe)
    };

    // Standard-PolyBLEP-Korrektur an den Sägezahn-Flanken, damit die
    // Unstetigkeit nicht als Alias-Rauschen ins Spektrum faltet.
    static double polyBlep (double phase, double phaseInc);

    static constexpr int numHarmonics = 4;

    // Bezugsdrehzahl der Track-Formel aus Plan 3.10 (f_i-Formel), keine
    // Automation, daher als Konstante statt Parameter.
    static constexpr double rpmRef = 1000.0;

    // Obergrenze des rpm-Parameters aus Params.cpp - dient hier nur der
    // Normierung auf u = RPM/RPM_max in [0,1] für Rauschband und Jitter.
    static constexpr double rpmMaxForNormalisation = 12000.0;

    std::array<Harmonic, numHarmonics> harmonics;

    std::atomic<float> rpm { 1000.0f };

    std::atomic<float> noiseFcLo { 400.0f };
    std::atomic<float> noiseFcHi { 3000.0f };
    std::atomic<float> noiseGainLoDb { -24.0f };
    std::atomic<float> noiseGainHiDb { -6.0f };
    std::atomic<float> noiseQ { 1.2f };

    std::atomic<float> jitterAmountPercent { 1.5f };
    std::atomic<float> jitterRateHz { 8.0f };

    // "Unwucht": periodische Amplitudenkomponente bei f_base/2 für den
    // Zündtakt eines Viertakters (Plan 3.10, optional, Default 0 = aus).
    std::atomic<float> imbalanceAmount { 0.0f };

    // TPT-Struktur ist explizit für Modulation pro Sample ausgelegt (siehe
    // JUCE-Doku), darum werden Cutoff/Resonanz hier ohne Zögern block- bzw.
    // sample-genau nachgeführt statt vorsichtig geglättet.
    juce::dsp::StateVariableTPTFilter<float> noiseFilter;
    juce::dsp::StateVariableTPTFilter<float> jitterFilter;

    // Getrennte Rauschquellen für Motorband und Jitter, sonst wären beide
    // Modulationen korreliert und der Jitter würde im Rauschband durchhören.
    juce::Random noiseRandom;
    juce::Random jitterRandom;

    double halfPhase = 0.0;   // Unwucht-AM-Phase bei f_base/2, wrappt wie die Sägezähne

    double currentSampleRate = 48000.0;

    // Für dominantFrequencyHz(): f_base der aktuellen RPM (unjittert, siehe
    // Plan: "gibt f_base zurück"), block-genau aktualisiert. Wird vermutlich
    // von einem anderen Thread als dem Audio-Thread gelesen (Fade-Policy),
    // daher atomar statt Plain-Member.
    std::atomic<double> lastDominantFrequency { 1000.0 / 60.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineGenerator)
};
