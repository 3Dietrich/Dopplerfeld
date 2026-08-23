#include "EngineGenerator.h"
#include <cmath>

namespace
{
    // Fuenf Gewichte pro Betriebsart, exakt in der Reihenfolge von
    // Params::engineKind (Params.cpp createParameterLayout()): 0=Frei,
    // 1=Duesenantrieb, 2=Raketenantrieb, 3=Hubschrauber, 4=Propeller.
    // "Frei" und "Propeller" liegen beide auf den Identitaets-Werten
    // (harmonic=1, noise=1, alle Zusatzklaenge=0) - Bestandspresets klingen
    // dadurch bitgleich wie vor diesem Umbau (@dpa-Vorgabe).
    struct KindWeights
    {
        double harmonic;     // Gewicht der vier Teiltoene
        double noise;        // Gewicht des vorhandenen RPM-Rauschbands
        double jetWhistle;    // Turbinen-Pfeifton (nur Duese)
        double rocketNoise;   // eigenes Breitband-Rauschen (nur Rakete)
        double heliRotor;     // Rotor-Blattschlag (nur Hubschrauber)
    };

    constexpr std::array<KindWeights, 5> kindWeightTable
    {{
        { 1.0,  1.0, 0.0, 0.0, 0.0 },   // Frei
        { 0.35, 1.8, 1.0, 0.0, 0.0 },   // Duesenantrieb: Rauschband dominiert, Teiltoene treten zurueck
        { 0.05, 2.2, 0.0, 1.0, 0.0 },   // Raketenantrieb: fast nur tiefes Breitbandrauschen, keine Tonhoehe
        { 1.0,  1.0, 0.0, 0.0, 1.0 },   // Hubschrauber: Motorton bleibt, Rotor kommt dazu
        { 1.0,  1.0, 0.0, 0.0, 0.0 },   // Propeller: vorerst wie Frei (Geometrie folgt in Source/Physics)
    }};
}

EngineGenerator::EngineGenerator()
{
    // Default-Verhältnisse bewusst leicht schief (Plan 3.10): exakt
    // ganzzahlige Teiltöne klingen elektronisch, nicht mechanisch.
    harmonics[0].ratio = 1.000f;
    harmonics[0].levelDb = 0.0f;

    harmonics[1].ratio = 2.017f;
    harmonics[1].levelDb = -6.0f;

    harmonics[2].ratio = 2.981f;
    harmonics[2].levelDb = -12.0f;

    harmonics[3].ratio = 4.043f;
    harmonics[3].levelDb = -18.0f;
}

void EngineGenerator::prepare (double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate;

    // Zeitkonstante der Wellenform-Ueberblendung, siehe sineBlendSeconds.
    sineBlendCoeff = 1.0 - std::exp (-1.0 / std::max (1.0, sineBlendSeconds * sampleRate));

    // Zeitkonstante der Betriebsart-Ueberblendung, siehe kindBlendSeconds.
    kindBlendCoeff = 1.0 - std::exp (-1.0 / std::max (1.0, kindBlendSeconds * sampleRate));

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    noiseFilter.prepare (spec);
    noiseFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

    jitterFilter.prepare (spec);
    jitterFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    rocketNoiseFilter.prepare (spec);
    rocketNoiseFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    reset();
}

void EngineGenerator::reset()
{
    for (auto& h : harmonics)
        h.phase = 0.0;

    halfPhase = 0.0;
    whistlePhase = 0.0;
    rotorPhase = 0.0;

    noiseFilter.reset();
    jitterFilter.reset();
    rocketNoiseFilter.reset();

    // Feste Startwerte statt der Uhrzeit-Aussaat, die juce::Random von sich
    // aus mitbringt. Zwei Läufe mit denselben Einstellungen liefern damit
    // dasselbe Ergebnis - musikalisch macht das keinen Unterschied (Rauschen
    // bleibt Rauschen), aber es ist die Voraussetzung dafür, dass sich zwei
    // Renderings überhaupt vergleichen lassen. Ohne das schwankt schon der
    // Spitzenpegel zweier identischer Durchläufe um ein paar Promille, und
    // jede Aussage der Form "diese Änderung verändert den Ausgang nicht"
    // wäre nicht prüfbar.
    //
    // Zwei verschiedene Zahlen, damit Rauschen und Jitter nicht dieselbe Folge
    // durchlaufen und sich dadurch korrelieren. Der Raketen-Rauschzweig
    // bekommt aus demselben Grund eine dritte, eigene Saat.
    noiseRandom.setSeed (0x5eed1234);
    jitterRandom.setSeed (0x5eed4321);
    rocketNoiseRandom.setSeed (0x5eed9999);
}

void EngineGenerator::setSineMode (bool shouldUseSine)
{
    sineTarget = shouldUseSine ? 1.0 : 0.0;
}

double EngineGenerator::polyBlep (double phase, double phaseInc)
{
    // Standardform: an der steigenden Flanke (phase nahe 0) und an der
    // fallenden Flanke (phase nahe 1) je ein quadratisches Korrekturstück,
    // das die Sprungstelle des rohen Sägezahns bandbegrenzt.
    if (phase < phaseInc)
    {
        const double t = phase / phaseInc;
        return t + t - t * t - 1.0;
    }

    if (phase > 1.0 - phaseInc)
    {
        const double t = (phase - 1.0) / phaseInc;
        return t * t + t + t + 1.0;
    }

    return 0.0;
}

void EngineGenerator::renderMono (float* out, int numSamples)
{
    // --- Block-Snapshot der Reglerwerte (Plan/Codebase-Konvention: wie
    // apvts-Parameter in granular/PluginProcessor.cpp einmal pro Block lesen,
    // nicht pro Sample - RPM-Änderungen per Automation sind langsam genug). ---
    const double rpmTarget = (double) rpm.load();
    const double u = juce::jlimit (0.0, 1.0, rpmTarget / rpmMaxForNormalisation);

    const double fBaseNominal = rpmTarget / 60.0;
    lastDominantFrequency.store (fBaseNominal);

    // Rauschband: Mittenfrequenz und Pegel wandern mit u (Plan 3.10).
    const double fcLo = juce::jmax (1.0, (double) noiseFcLo.load());
    const double fcHi = juce::jmax (fcLo, (double) noiseFcHi.load());
    const double fcNoise = juce::jlimit (20.0, currentSampleRate * 0.49, fcLo * std::pow (fcHi / fcLo, u));
    const double gLoDb = (double) noiseGainLoDb.load();
    const double gHiDb = (double) noiseGainHiDb.load();
    const double gNoiseDb = gLoDb + (gHiDb - gLoDb) * std::pow (u, 1.5);
    const double noiseGain = juce::Decibels::decibelsToGain (gNoiseDb);

    noiseFilter.setCutoffFrequency ((float) fcNoise);
    noiseFilter.setResonance (juce::jmax (0.05f, noiseQ.load()));

    // Betriebsart: Zieltabelle nachschlagen (Index kommt aus setEngineKind(),
    // dort schon auf die Tabellengroesse geklemmt). Die eigentliche
    // Ueberblendung passiert weiter unten sample-genau, siehe kindBlendCoeff.
    const auto& kindTarget = kindWeightTable[(size_t) engineKind.load()];

    // Rakete: eigenes Breitbandrauschen, Eckfrequenz oeffnet sich leicht mit
    // u ("Gas geben"), bleibt aber immer tief-breitbandig - keine rotierenden
    // Teile, die einen Ton geben koennten.
    const double rocketFc = juce::jlimit (20.0, currentSampleRate * 0.49, 80.0 + 400.0 * u);
    rocketNoiseFilter.setCutoffFrequency ((float) rocketFc);
    rocketNoiseFilter.setResonance (1.0f / (float) std::sqrt (2.0));

    // Hubschrauber-Rotor: Blattschlagfrequenz = Rotordrehzahl * Blattzahl,
    // beides eigene Regler, unabhaengig von der Motor-RPM (@dpa: "Motor, und
    // Rotoren mit Geschwindigkeit extra").
    const double rotorBpf = juce::jmax (0.01, (double) heliRotorHz.load() * (double) heliBladeCount.load());
    const double rotorPhaseInc = juce::jlimit (0.0, 0.45, rotorBpf / currentSampleRate);

    // Jitter: Tiefpass 3-15 Hz aus jitterRateHz, Tiefe j = j0 * u.
    const double jitterCutoff = juce::jlimit (0.5, currentSampleRate * 0.49, (double) jitterRateHz.load());
    jitterFilter.setCutoffFrequency ((float) jitterCutoff);
    jitterFilter.setResonance (1.0f / (float) std::sqrt (2.0));   // Standard-12dB/Okt-Tiefpass, keine Resonanzüberhöhung nötig

    // Tiefpass-gefiltertes weißes Rauschen verliert einen Großteil seiner
    // Energie (Bandbreite fc gegenüber sampleRate/2), sonst würde "j" kaum
    // hörbar wirken. Kompensation über die Wurzel des Bandbreitenverhältnisses,
    // danach hart auf ±1 geklemmt, damit der Hub wirklich ±j Prozent bleibt.
    const double jitterGainCompensation = std::sqrt (juce::jmax (1.0, (currentSampleRate * 0.5) / jitterCutoff));
    const double jitterDepthPercent = (double) jitterAmountPercent.load() * u;

    const double imbalance = (double) imbalanceAmount.load();
    // Unwucht-Frequenz: von Haus aus die halbe Grundfrequenz (Zuendtakt), per
    // Oktavregler nach oben oder unten verschiebbar (@dpa 20260820: "Imbalance:
    // zusaetzlicher Octave Regler [-2 .. 6]"). Bei 0 bleibt es beim Zuendtakt,
    // jede Stufe verdoppelt bzw. halbiert.
    const double imbalanceOctaves = (double) imbalanceOctave.load();
    const double halfPhaseInc = (fBaseNominal * 0.5 * std::pow (2.0, imbalanceOctaves))
                                / currentSampleRate;

    struct HarmSnapshot
    {
        double coeff;         // r_i * (RPM_ref/60) * 2^(d_i/1200), konstant für den Block
        double trackAmount;   // t_i
        double levelGain;
    };

    std::array<HarmSnapshot, numHarmonics> snap;

    for (int i = 0; i < numHarmonics; ++i)
    {
        auto& h = harmonics[(size_t) i];
        const double ratio = (double) h.ratio.load();
        const double detuneCents = (double) h.detuneCents.load();
        const double trackAmount = (double) h.trackAmount.load();
        const double levelDb = (double) h.levelDb.load();

        snap[(size_t) i].coeff = ratio * (rpmRef / 60.0) * std::pow (2.0, detuneCents / 1200.0);
        snap[(size_t) i].trackAmount = trackAmount;
        snap[(size_t) i].levelGain = juce::Decibels::decibelsToGain (levelDb);
    }

    for (int n = 0; n < numSamples; ++n)
    {
        // Wellenform-Ueberblendung je Sample fortschreiben (siehe sineBlend im
        // Header).
        sineBlend += (sineTarget - sineBlend) * sineBlendCoeff;

        // Betriebsart-Gewichte ebenso sample-genau nachfuehren, statt hart
        // umzuschalten - sonst waere ein Wechsel der Betriebsart ein Sprung
        // im Signal, genau wie beim Sinus/Saegezahn-Umschalter oben.
        kindHarmonicGain += (kindTarget.harmonic    - kindHarmonicGain) * kindBlendCoeff;
        kindNoiseGain    += (kindTarget.noise       - kindNoiseGain)    * kindBlendCoeff;
        kindJetWhistle   += (kindTarget.jetWhistle  - kindJetWhistle)   * kindBlendCoeff;
        kindRocketNoise  += (kindTarget.rocketNoise - kindRocketNoise)  * kindBlendCoeff;
        kindHeliRotor    += (kindTarget.heliRotor   - kindHeliRotor)    * kindBlendCoeff;

        // Jitter-Quelle: eigenes Rauschen durch den Tiefpass, normiert und geklemmt.
        const double jitterNoiseSample = (double) jitterRandom.nextFloat() * 2.0 - 1.0;
        const double jitterFiltered = (double) jitterFilter.processSample (0, (float) jitterNoiseSample);
        const double jitterNorm = juce::jlimit (-1.0, 1.0, jitterFiltered * jitterGainCompensation);

        // f_base wird um ±j Prozent moduliert, indem die RPM selbst dafür
        // instantan verschoben wird - das zieht die Track-Formel aller
        // Teiltöne konsistent mit, statt nur die Grundfrequenz zu wackeln.
        double rpmJit = rpmTarget * (1.0 + (jitterDepthPercent / 100.0) * jitterNorm);
        rpmJit = juce::jmax (0.0, rpmJit);

        double harmonicSum = 0.0;

        for (int i = 0; i < numHarmonics; ++i)
        {
            auto& h = harmonics[(size_t) i];
            const auto& s = snap[(size_t) i];

            const double freq = s.coeff * std::pow (rpmJit / rpmRef, s.trackAmount);
            const double phaseInc = juce::jlimit (0.0, 0.45, freq / currentSampleRate);

            double saw = 0.0;

            if (phaseInc > 0.0)
                saw = 2.0 * h.phase - 1.0 - polyBlep (h.phase, phaseInc);

            // Sinus aus derselben Phase - kein zweiter Oszillator, damit beim
            // Umschalten nichts auseinanderlaeuft (siehe sineBlend im Header).
            const double sine = std::sin (juce::MathConstants<double>::twoPi * h.phase);

            harmonicSum += (saw + (sine - saw) * sineBlend) * s.levelGain;

            h.phase += phaseInc;

            while (h.phase >= 1.0)
                h.phase -= 1.0;   // Wrap per Subtraktion, nicht fmod/Clamp (Plan-Vorgabe)
        }

        // Betriebsart gewichtet die vier Teiltoene, statt sie hart ein-/
        // auszuschalten (kindHarmonicGain ist 1.0 in "Frei"/"Propeller" -
        // bitgleich zum bisherigen Verhalten).
        harmonicSum *= kindHarmonicGain;

        // Motorband-Rauschen durch das RPM-abhängige Bandpassfilter.
        const double noiseSample = (double) noiseRandom.nextFloat() * 2.0 - 1.0;
        const double noiseFiltered = (double) noiseFilter.processSample (0, (float) noiseSample);

        // Duesenantrieb: hoher, mit der (gejitterten) Grundfrequenz mitlaufender
        // Turbinen-Pfeifton - schmalbandig, weil reiner Sinus. Phase laeuft
        // immer durch, kindJetWhistle blendet den Beitrag nur ein/aus, sonst
        // gaebe es beim Umschalten einen Phasensprung.
        const double whistleFreq = (rpmJit / 60.0) * whistleRatio;
        const double whistlePhaseInc = juce::jlimit (0.0, 0.45, whistleFreq / currentSampleRate);
        const double whistleSample = std::sin (juce::MathConstants<double>::twoPi * whistlePhase)
                                    * whistleGainRef * kindJetWhistle;
        whistlePhase += whistlePhaseInc;

        while (whistlePhase >= 1.0)
            whistlePhase -= 1.0;

        // Raketenantrieb: eigenes, tiefes Breitbandrauschen statt rotierender
        // Teile - "sie bruellt nur".
        const double rocketNoiseSample = (double) rocketNoiseRandom.nextFloat() * 2.0 - 1.0;
        const double rocketFiltered = (double) rocketNoiseFilter.processSample (0, (float) rocketNoiseSample);

        // Hubschrauber: periodischer Rotor-Blattschlag, eigene Phase mit
        // eigener Drehzahl (rotorBpf, s.o.). Kosinus-Potenz ergibt einen
        // schmalen, scharfen Schlag statt einer glatten Welle - je hoeher
        // rotorSharpness, desto kuerzer/knackiger.
        const double rotorRaised = std::pow (0.5 + 0.5 * std::cos (juce::MathConstants<double>::twoPi * rotorPhase), rotorSharpness);
        const double heliSample = rotorRaised * heliGainRef * kindHeliRotor;
        rotorPhase += rotorPhaseInc;

        while (rotorPhase >= 1.0)
            rotorPhase -= 1.0;

        // Unwucht: Amplitudenmodulation mit einer POSITIVEN Welle, 0 bis 1
        // (@dpa 20260820: "es kam mir so vor wie eine positive (sinus?)welle
        // (0..1) als amplitudenmodulation und davon die freq einfach
        // vervielfachen - dann bleibt der Modulationseffekt am wirksamsten").
        //
        // Der Unterschied zu einer Modulation um 1 herum: dort schwingt der
        // Faktor symmetrisch nach oben und unten, das Signal wird also
        // abwechselnd lauter und leiser und behaelt im Mittel seinen Pegel. Hier
        // geht er bei vollem Regler bis auf null herunter und nur bis eins
        // hinauf - das Signal wird wirklich zerhackt, und genau das sind die
        // Flanken, um die es geht. Die Staerke regelt weiter der Unwucht-Regler:
        // bei 0 bleibt der Faktor konstant 1.
        const double wave = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * halfPhase);
        const double imbalanceFactor = 1.0 - imbalance * (1.0 - wave);

        halfPhase += halfPhaseInc;

        while (halfPhase >= 1.0)
            halfPhase -= 1.0;

        out[n] = (float) ((harmonicSum
                          + noiseFiltered * noiseGain * kindNoiseGain
                          + whistleSample
                          + rocketFiltered * rocketGainRef * kindRocketNoise
                          + heliSample) * imbalanceFactor);
    }
}

double EngineGenerator::dominantFrequencyHz() const
{
    return lastDominantFrequency.load();
}

void EngineGenerator::setRpm (float rpmValue)
{
    rpm.store (rpmValue);
}

void EngineGenerator::setHarmonic (int index, float ratio, float detuneCents, float trackAmount, float levelDb)
{
    jassert (index >= 0 && index < numHarmonics);

    if (index < 0 || index >= numHarmonics)
        return;

    auto& h = harmonics[(size_t) index];
    h.ratio.store (ratio);
    h.detuneCents.store (detuneCents);
    h.trackAmount.store (trackAmount);
    h.levelDb.store (levelDb);
}

void EngineGenerator::setNoiseParams (float fcLoHz, float fcHiHz, float gainLoDb, float gainHiDb, float q)
{
    noiseFcLo.store (fcLoHz);
    noiseFcHi.store (fcHiHz);
    noiseGainLoDb.store (gainLoDb);
    noiseGainHiDb.store (gainHiDb);
    noiseQ.store (q);
}

void EngineGenerator::setJitter (float amountPercent, float rateHz)
{
    jitterAmountPercent.store (amountPercent);
    jitterRateHz.store (rateHz);
}

void EngineGenerator::setImbalance (float amount)
{
    imbalanceAmount.store (amount);
}

void EngineGenerator::setImbalanceOctave (float octaves)
{
    imbalanceOctave.store (octaves);
}

void EngineGenerator::setEngineKind (int kindIndex)
{
    // Auf die Tabellengroesse klemmen statt zu jassert()en - eine falsche ID
    // waere ein Params.cpp/EngineGenerator.cpp-Mismatch, kein Nutzerfehler,
    // und soll hier nicht abstuerzen (renderMono() indiziert ungeprueft).
    engineKind.store (juce::jlimit (0, 4, kindIndex));
}

void EngineGenerator::setHeliRotor (float rotorHz, float bladeCount)
{
    heliRotorHz.store (rotorHz);
    heliBladeCount.store (bladeCount);
}
