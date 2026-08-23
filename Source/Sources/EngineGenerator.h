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

    // Wellenform der vier Teiltoene (@dpa 20260823: "mach die 4 Osc
    // umschaltbar auf Sines"). false = PolyBLEP-Saegezahn wie bisher, true =
    // reiner Sinus. Gilt fuer alle vier gemeinsam: sie sind ein Klang, keine
    // vier Einzelstimmen.
    void setSineMode (bool shouldUseSine);
    // Oktavlage der Unwucht, siehe Params::imbalanceOctave.
    void setImbalanceOctave (float octaves);

    // Betriebsart des Motors (@dpa 20260824: "in 'Motor' mehrere umschaltbar
    // machen"). kindIndex folgt exakt der Reihenfolge aus Params::engineKind
    // (createParameterLayout()): 0=Frei, 1=Duesenantrieb, 2=Raketenantrieb,
    // 3=Hubschrauber, 4=Propeller - siehe kindWeightTable in der .cpp.
    // Ueberschreibt keinen der uebrigen Setter hier, gewichtet nur, wie stark
    // ihre Ergebnisse beitragen (kein Regler wird dem Nutzer weggenommen).
    void setEngineKind (int kindIndex);

    // Nur in Betriebsart "Hubschrauber" hoerbar: Rotordrehzahl (Hz) und
    // Blattzahl, multipliziert ergeben sie die Blattschlag-Frequenz.
    void setHeliRotor (float rotorHz, float bladeCount);

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

    // Ueberblendung zwischen Saegezahn und Sinus. Ein harter Wechsel waere ein
    // Sprung im Signal: bei Phase 0,25 steht der Saegezahn auf -0,5 und der
    // Sinus auf +1. Beide Formen laufen deshalb weiter und werden ineinander
    // geblendet - die Phase selbst bleibt dabei unangetastet, es gibt also
    // weder Tonhoehen- noch Phasensprung.
    double sineBlend  = 0.0;   // 0 = Saegezahn, 1 = Sinus
    double sineTarget = 0.0;
    double sineBlendCoeff = 1.0;   // je Sample, aus sineBlendSeconds und der Rate

    static constexpr double sineBlendSeconds = 0.02;

    // Betriebsart des Motors (@dpa 20260824): fuenf Gewichte werden weich
    // Richtung Zieltabelle (kindWeightTable in der .cpp) nachgefuehrt, exakt
    // nach demselben Muster wie sineBlend oben - ein harter Umschalt waere
    // ein Sprung im Signal. "Frei" UND "Propeller" liegen beide auf den
    // Identitaets-Werten (harmonic=1, noise=1, alle Zusatzklaenge=0), darum
    // klingt der Default nach diesem Umbau bitgleich wie vorher.
    std::atomic<int> engineKind { 0 };

    double kindHarmonicGain = 1.0;   // Gewicht der vier Saegezahn/Sinus-Teiltoene
    double kindNoiseGain    = 1.0;   // Gewicht des vorhandenen RPM-Rauschbands
    double kindJetWhistle   = 0.0;   // Turbinen-Pfeifton (Duese)
    double kindRocketNoise  = 0.0;   // eigenes, tiefes Breitband-Rauschen (Rakete)
    double kindHeliRotor    = 0.0;   // Rotor-Blattschlag (Hubschrauber)
    double kindBlendCoeff = 1.0;     // wie sineBlendCoeff, aus kindBlendSeconds

    static constexpr double kindBlendSeconds = 0.05;

    // Turbinen-Pfeifton (Duesenantrieb): eigene Phase, ein Vielfaches der
    // Grundfrequenz - schmalbandig, weil ein reiner Sinus.
    double whistlePhase = 0.0;
    static constexpr double whistleRatio   = 12.0;
    static constexpr double whistleGainRef = 0.55;

    // Rakete: eigenes, von der Motor-RPM unabhaengiges Breitband-Rauschen -
    // eine Rakete hat keine rotierenden Teile, sie bruellt nur.
    juce::dsp::StateVariableTPTFilter<float> rocketNoiseFilter;
    juce::Random rocketNoiseRandom;
    static constexpr double rocketGainRef = 0.9;

    // Hubschrauber-Rotor: periodischer Blattschlag mit EIGENER Drehzahl
    // (@dpa: "Motor, und Rotoren mit Geschwindigkeit extra"), unabhaengig
    // vom Motorton. Frequenz = Rotordrehzahl * Blattzahl.
    std::atomic<float> heliRotorHz { 5.0f };
    std::atomic<float> heliBladeCount { 4.0f };
    double rotorPhase = 0.0;
    static constexpr double rotorSharpness = 24.0;   // je hoeher, desto kuerzer/schaerfer der Schlag
    static constexpr double heliGainRef = 0.85;

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
    std::atomic<float> imbalanceOctave { 0.0f };

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
