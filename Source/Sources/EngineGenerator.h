#pragma once

#include "SoundSource.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// Motorgenerator (Plan 3.10): vier PolyBLEP-Sägezahn-Teiltöne mit RPM-Tracking,
// ein RPM-abhängiges Rauschband und langsamer Jitter auf der Grundfrequenz.
//
// Seit @dpa 20260824 ("jeder Betriebsmode ist ein eigener Generator!") ist das
// nur noch EINE der Betriebsarten, nämlich "Frei". Die anderen vier sind keine
// Gewichtung derselben Bausteine, sondern eigene Klangerzeuger mit eigenem
// Aufbau und eigenen Pegeln:
//
//   Düsenantrieb   - EIN Verdichterton, darüber ein lautes Strahlrauschen.
//   Raketenantrieb - kein Ton, nur Brüllen, dazu Druckstöße aus der Düse.
//   Hubschrauber   - Verbrennermotor (die vier Teiltöne) UND ein Rotor, der
//                    aus der Nähe schwirrt und dessen Blätter knallen.
//   Propeller      - ein leiser Ton plus Blattschlag.
//
// Was allen gemeinsam ist: das Fahrtwindrauschen. Je schneller die Quelle
// fliegt, desto lauter (@dpa: "Die Luftwiderstandsgeräusche haben alle,
// vielleicht unterschiedlich, aber je schneller um so lauter"). Die
// Geschwindigkeit kommt von außen, siehe setAirspeed().
//
// Beim Wechsel der Betriebsart wird kurz aus- und wieder eingeblendet (siehe
// kindFade). Zwei Betriebsarten gleichzeitig zu rechnen und ineinander zu
// blenden wäre der aufwendigere Weg für einen Vorgang, den niemand im Takt
// bedient - eine Betriebsart wählt man einmal.
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

    // Wellenform JE Teilton (@dpa 20260824: "der sinus soll (zumindest bei
    // Hubschrauber und Propeller) für jeden osc setzbar sein"). false =
    // PolyBLEP-Sägezahn, true = reiner Sinus. index 0..3.
    void setSineMode (int index, bool shouldUseSine);
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

    // Gesamtpegel der Betriebsart in dB. Gilt NICHT für "Frei" - dort machen
    // die vier Teilton-Pegel den Pegel, und daran darf sich nichts ändern,
    // sonst klängen alte Snapshots anders.
    //
    // Die anderen Betriebsarten brauchen ihn, weil ihre Pegel aus der Sache
    // kommen und nicht aus vier Reglern: ein Hubschrauber in drei Metern
    // Abstand ist ohrenbetäubend, und genau das war er vorher nicht (@dpa:
    // "die Lautstärken sind noch irgendwie völlig unrealistisch: Hubschrauber
    // in 3m abstand ist flüsterleise..!").
    void setKindLevelDb (float levelDb);

    // Fahrtwind: Betrag der Quellgeschwindigkeit in m/s. Treibt in jeder
    // Betriebsart das Rauschen mit, das allein vom Fliegen kommt.
    void setAirspeed (float metresPerSecond);

    // Stärke der Druckstöße aus der Raketendüse, 0..1 (@dpa: "aus einem
    // Raketenantrieb kann Überschall druck rauskommen. das mus einerseits
    // einstellbar sein, andererseits müssen diese noisy N-Waves zu hören sein
    // (auch wenn die Rakete noch um subsonic ist)").
    //
    // Das sind NICHT die N-Wellen der Ausbreitung (die entstehen in
    // PropagationPath und hängen an M_r, der auf den Hörer bezogenen
    // Mach-Zahl). Das hier sitzt im Quellsignal selbst: der Abgasstrahl einer
    // Rakete ist selbst überschallschnell, seine Stoßzellen knallen, ganz
    // unabhängig davon, wie schnell die Rakete durch die Luft fliegt.
    void setRocketShock (float amount01);

    // Stärke des Blattknallens am Rotor, 0..1. Die Blattspitzen laufen
    // schneller als der Rumpf und schlagen bei jedem Umlauf in die eigene
    // Wirbelschleppe - das ist das laute Knattern (@dpa: "beim fliegen
    // überschlagen sie sich ... lautes Knattern").
    void setRotorSlap (float amount01);

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

        // Wellenform dieses einen Teiltons, siehe setSineMode(). Geblendet
        // statt geschaltet: bei Phase 0,25 steht der Sägezahn auf -0,5 und der
        // Sinus auf +1, ein harter Wechsel wäre ein Sprung im Signal.
        std::atomic<float> sineTarget { 0.0f };
        double sineBlend = 0.0;
    };

    // Standard-PolyBLEP-Korrektur an den Sägezahn-Flanken, damit die
    // Unstetigkeit nicht als Alias-Rauschen ins Spektrum faltet.
    static double polyBlep (double phase, double phaseInc);

    static constexpr int numHarmonics = 4;

    // Zeitkonstante der Wellenform-Überblendung je Teilton (siehe Harmonic).
    double sineBlendCoeff = 1.0;
    static constexpr double sineBlendSeconds = 0.02;

    // --- Betriebsart ---
    //
    // engineKind ist der Wunsch von außen, activeKind der gerade gerechnete.
    // Unterscheiden sie sich, fährt kindFade auf null, DANN wird umgeschaltet,
    // dann fährt kindFade wieder hoch. So gibt es beim Wechsel eine kurze
    // Lücke statt eines Sprungs - und zwei komplette Betriebsarten gleichzeitig
    // zu rechnen erspart es auch.
    std::atomic<int> engineKind { 0 };
    int    activeKind = 0;
    double kindFade   = 1.0;
    double kindFadeStep = 1.0;   // je Sample, aus kindFadeSeconds

    static constexpr double kindFadeSeconds = 0.03;

    // Gesamtpegel der Betriebsart, linear. Wirkt auf alles außer "Frei".
    std::atomic<float> kindLevelDb { 0.0f };

    // Fahrtwind, siehe setAirspeed(). Der Bezugswert ist die Geschwindigkeit,
    // bei der das Fahrtwindrauschen seinen vollen Pegel erreicht - darüber
    // wächst es weiter, aber flacher (Wurzel), sonst überdeckte es bei
    // Überschall alles andere.
    std::atomic<float> airspeedMps { 0.0f };
    static constexpr double airspeedRefMps = 120.0;

    // Pegel des Fahrtwindrauschens bei der Bezugsgeschwindigkeit.
    static constexpr double windLevel = 0.35;

    juce::dsp::StateVariableTPTFilter<float> windFilter;
    juce::Random windRandom;

    // --- Düsenantrieb ---
    //
    // Ein Verdichterton (nicht vier: @dpa "braucht es höchstens 1
    // Oscillator") und darüber das Strahlrauschen, das den Klang trägt. Der
    // Ton ist die Blattfolgefrequenz des Fans, also ein Vielfaches der
    // Wellendrehzahl - er sitzt hoch und ist deutlich leiser als der Strahl.
    double jetTonePhase = 0.0;
    juce::dsp::StateVariableTPTFilter<float> jetNoiseFilter;
    juce::Random jetRandom;

    static constexpr double jetToneRatio = 8.0;    // Fanblätter je Umdrehung
    static constexpr double jetToneLevel = 0.10;   // gegen das Strahlrauschen
    static constexpr double jetNoiseLevel = 0.85;

    // --- Raketenantrieb ---
    //
    // Kein Ton, nur Brüllen: tiefes Breitbandrauschen. Dazu die Druckstöße
    // der Stoßzellen im Strahl, siehe setRocketShock() - kurze, harte
    // Rauschstöße in unregelmäßigem Abstand, nicht ein sauberer Puls.
    juce::dsp::StateVariableTPTFilter<float> rocketNoiseFilter;
    juce::Random rocketNoiseRandom;

    juce::dsp::StateVariableTPTFilter<float> shockFilter;
    juce::Random shockRandom;
    double shockEnv       = 0.0;   // Hüllkurve des laufenden Stoßes
    double shockCountdown = 0.0;   // Samples bis zum nächsten Stoß
    std::atomic<float> rocketShock { 0.0f };

    static constexpr double rocketNoiseLevel = 1.10;
    static constexpr double rocketShockLevel = 2.20;
    static constexpr double shockRateHz      = 18.0;   // mittlerer Abstand der Stöße
    static constexpr double shockDecayMs     = 12.0;

    // --- Rotor (Hubschrauber und Propeller) ---
    //
    // Der Rotor ist KEIN Klick. Aus der Nähe schwirrt er - ein Rauschen, das
    // mit jedem vorbeilaufenden Blatt an- und abschwillt - und beim Fliegen
    // knallen die Blätter obendrein (@dpa: "das ist also aus entfernung ein
    // sich im Kreis drehene Noise mit 'dm großen Knallen'").
    //
    // Umgesetzt als bandpassgefiltertes Rauschen, dessen Pegel mit der
    // Blattfolge moduliert wird, plus ein kurzer, harter Rauschstoß je Blatt.
    std::atomic<float> heliRotorHz { 5.0f };
    std::atomic<float> heliBladeCount { 4.0f };
    std::atomic<float> rotorSlap { 0.5f };

    double rotorPhase = 0.0;
    double slapEnv    = 0.0;

    juce::dsp::StateVariableTPTFilter<float> rotorFilter;
    juce::Random rotorRandom;
    juce::dsp::StateVariableTPTFilter<float> slapFilter;
    juce::Random slapRandom;

    static constexpr double rotorSwishLevel = 0.55;
    static constexpr double rotorSlapLevel  = 1.60;
    static constexpr double slapDecayMs     = 9.0;

    // Wie stark das Schwirren mit der Blattfolge atmet: 0 wäre gleichmäßiges
    // Rauschen, 1 zerhackt es ganz. Dazwischen bleibt der Rest, der zwischen
    // den Blättern stehen bleibt.
    static constexpr double rotorSwishDepth = 0.85;

    // --- Propeller ---
    //
    // Ein leiser Ton (@dpa: "höchstens einen (leiseren)") und derselbe
    // Blattschlag wie am Rotor, nur weicher.
    double propTonePhase = 0.0;
    static constexpr double propToneLevel = 0.22;

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
