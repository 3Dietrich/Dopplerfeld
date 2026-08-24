#include "EngineGenerator.h"
#include <cmath>

namespace
{
    // Reihenfolge der Betriebsarten, exakt wie in Params::engineKind
    // (Params.cpp createParameterLayout()). Wer dort umsortiert, muss hier
    // mitziehen.
    enum Kind
    {
        KindFree = 0,
        KindJet,
        KindRocket,
        KindHeli,
        KindProp
    };

    // Ein Rauschsample in [-1, 1].
    inline double whiteNoise (juce::Random& r)
    {
        return (double) r.nextFloat() * 2.0 - 1.0;
    }
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

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    noiseFilter.prepare (spec);
    noiseFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

    jitterFilter.prepare (spec);
    jitterFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    rocketNoiseFilter.prepare (spec);
    rocketNoiseFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    // Fahrtwind: Hochpass, denn Wind an Kanten ist ein Zischen, kein Wummern.
    windFilter.prepare (spec);
    windFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);

    // Strahlrauschen der Düse: breites Band, Schwerpunkt in den oberen Mitten.
    jetNoiseFilter.prepare (spec);
    jetNoiseFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);

    // Druckstöße der Rakete: Bandpass, damit sie knallen und nicht nur pumpen.
    shockFilter.prepare (spec);
    shockFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

    // Rotor: das Schwirren sitzt in den Mitten, der Blattknall darüber.
    rotorFilter.prepare (spec);
    rotorFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

    slapFilter.prepare (spec);
    slapFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

    // Zeitkonstante der Betriebsart-Blende: kindFade legt in kindFadeSeconds
    // die volle Strecke zurück, deshalb ein Schritt je Sample statt eines
    // Ein-Pols - eine Blende soll wirklich ankommen, nicht asymptotisch
    // kriechen.
    kindFadeStep = 1.0 / std::max (1.0, kindFadeSeconds * sampleRate);

    reset();
}

void EngineGenerator::reset()
{
    for (auto& h : harmonics)
        h.phase = 0.0;

    halfPhase = 0.0;
    jetTonePhase = 0.0;
    propTonePhase = 0.0;
    rotorPhase = 0.0;

    shockEnv       = 0.0;
    shockCountdown = 0.0;
    slapEnv        = 0.0;

    activeKind = engineKind.load();
    kindFade   = 1.0;

    for (auto& h : harmonics)
        h.sineBlend = (double) h.sineTarget.load();

    noiseFilter.reset();
    jitterFilter.reset();
    rocketNoiseFilter.reset();
    windFilter.reset();
    jetNoiseFilter.reset();
    shockFilter.reset();
    rotorFilter.reset();
    slapFilter.reset();

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
    windRandom.setSeed (0x5eed2468);
    jetRandom.setSeed (0x5eed1357);
    shockRandom.setSeed (0x5eedabcd);
    rotorRandom.setSeed (0x5eedbeef);
    slapRandom.setSeed (0x5eedf00d);
}

void EngineGenerator::setSineMode (int index, bool shouldUseSine)
{
    if (index >= 0 && index < numHarmonics)
        harmonics[(size_t) index].sineTarget = shouldUseSine ? 1.0f : 0.0f;
}

void EngineGenerator::setKindLevelDb (float levelDb)
{
    kindLevelDb = levelDb;
}

void EngineGenerator::setAirspeed (float metresPerSecond)
{
    airspeedMps = std::max (0.0f, metresPerSecond);
}

void EngineGenerator::setRocketShock (float amount01)
{
    rocketShock = juce::jlimit (0.0f, 1.0f, amount01);
}

void EngineGenerator::setRotorSlap (float amount01)
{
    rotorSlap = juce::jlimit (0.0f, 1.0f, amount01);
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

    // --- Betriebsart ---
    //
    // Gewechselt wird nur an einer Nullstelle der Blende, siehe kindFade im
    // Header. Der gewuenschte Index steht in engineKind, gerechnet wird
    // activeKind.
    const int wantedKind = engineKind.load();
    const double kindGain = (activeKind == KindFree)
                          ? 1.0
                          : juce::Decibels::decibelsToGain ((double) kindLevelDb.load());

    // Fahrtwind, in jeder Betriebsart: der Pegel waechst mit der
    // Geschwindigkeit, oberhalb der Bezugsgeschwindigkeit nur noch mit der
    // Wurzel - sonst deckte er bei Ueberschall alles andere zu.
    const double airspeed = (double) airspeedMps.load();
    const double windNorm = airspeed / airspeedRefMps;
    const double windAmount = windNorm <= 1.0 ? windNorm : std::sqrt (windNorm);

    // Schneller Fahrtwind zischt hoeher: die Eckfrequenz wandert mit.
    const double windFc = juce::jlimit (20.0, currentSampleRate * 0.49, 300.0 + 8.0 * airspeed);
    windFilter.setCutoffFrequency ((float) windFc);
    windFilter.setResonance (1.0f / (float) std::sqrt (2.0));

    // Duese: Strahlrauschen als Hochpass, Eckfrequenz mit dem Gas steigend.
    const double jetFc = juce::jlimit (20.0, currentSampleRate * 0.49, 250.0 + 2500.0 * u);
    jetNoiseFilter.setCutoffFrequency ((float) jetFc);
    jetNoiseFilter.setResonance (0.9f);

    // Rakete: eigenes Breitbandrauschen, Eckfrequenz oeffnet sich leicht mit
    // u ("Gas geben"), bleibt aber immer tief-breitbandig - keine rotierenden
    // Teile, die einen Ton geben koennten.
    const double rocketFc = juce::jlimit (20.0, currentSampleRate * 0.49, 120.0 + 700.0 * u);
    rocketNoiseFilter.setCutoffFrequency ((float) rocketFc);
    rocketNoiseFilter.setResonance (1.2f);

    // Druckstoesse im Raketenstrahl: Bandpass in den unteren Mitten, damit sie
    // schlagen statt zu pumpen.
    const double shockAmount = (double) rocketShock.load();
    shockFilter.setCutoffFrequency ((float) juce::jlimit (20.0, currentSampleRate * 0.49, 180.0 + 300.0 * u));
    shockFilter.setResonance (1.6f);

    const double shockMeanSamples = currentSampleRate / std::max (0.1, shockRateHz);
    const double shockDecay = std::exp (-1.0 / std::max (1.0, shockDecayMs * 0.001 * currentSampleRate));

    // Rotor: Blattfolgefrequenz = Rotordrehzahl * Blattzahl, beides eigene
    // Regler, unabhaengig von der Motor-RPM (@dpa: "Motor, und Rotoren mit
    // Geschwindigkeit extra").
    const double rotorRps = juce::jmax (0.01, (double) heliRotorHz.load());
    const double bladeCount = juce::jmax (1.0, (double) heliBladeCount.load());
    const double rotorBpf = rotorRps * bladeCount;
    const double rotorPhaseInc = juce::jlimit (0.0, 0.45, rotorBpf / currentSampleRate);

    // Das Schwirren sitzt dort, wo die Blattspitze die Luft schneidet: mit der
    // Umfangsgeschwindigkeit steigend, also mit der Rotordrehzahl.
    rotorFilter.setCutoffFrequency ((float) juce::jlimit (20.0, currentSampleRate * 0.49, 350.0 + 60.0 * rotorRps));
    rotorFilter.setResonance (0.8f);

    // Der Blattknall liegt darueber und ist schmaler - das ist das Knattern.
    slapFilter.setCutoffFrequency ((float) juce::jlimit (20.0, currentSampleRate * 0.49, 900.0 + 120.0 * rotorRps));
    slapFilter.setResonance (2.2f);

    const double slapAmount = (double) rotorSlap.load();
    const double slapDecay  = std::exp (-1.0 / std::max (1.0, slapDecayMs * 0.001 * currentSampleRate));

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
        // --- Betriebsart blenden ---
        //
        // Erst herunterfahren, dann umschalten, dann wieder hoch. Umgeschaltet
        // wird ausschliesslich bei kindFade = 0, dort ist der Ausgang still
        // und der Wechsel folglich unhoerbar.
        if (activeKind != wantedKind)
        {
            kindFade -= kindFadeStep;

            if (kindFade <= 0.0)
            {
                kindFade   = 0.0;
                activeKind = wantedKind;
            }
        }
        else if (kindFade < 1.0)
        {
            kindFade = std::min (1.0, kindFade + kindFadeStep);
        }

        // Jitter-Quelle: eigenes Rauschen durch den Tiefpass, normiert und geklemmt.
        const double jitterNoiseSample = whiteNoise (jitterRandom);
        const double jitterFiltered = (double) jitterFilter.processSample (0, (float) jitterNoiseSample);
        const double jitterNorm = juce::jlimit (-1.0, 1.0, jitterFiltered * jitterGainCompensation);

        // f_base wird um ±j Prozent moduliert, indem die RPM selbst dafür
        // instantan verschoben wird - das zieht die Track-Formel aller
        // Teiltöne konsistent mit, statt nur die Grundfrequenz zu wackeln.
        double rpmJit = rpmTarget * (1.0 + (jitterDepthPercent / 100.0) * jitterNorm);
        rpmJit = juce::jmax (0.0, rpmJit);

        // --- Bausteine, die mehrere Betriebsarten brauchen ---

        // Die vier Teiltöne. Gerechnet werden sie nur, wo sie auch gehört
        // werden: Düse und Rakete haben keine vier Teiltöne, und was nicht
        // gebraucht wird, soll auch nichts kosten.
        double harmonicSum = 0.0;

        if (activeKind == KindFree || activeKind == KindHeli)
        {
            for (int i = 0; i < numHarmonics; ++i)
            {
                auto& h = harmonics[(size_t) i];
                const auto& sn = snap[(size_t) i];

                const double freq = sn.coeff * std::pow (rpmJit / rpmRef, sn.trackAmount);
                const double phaseInc = juce::jlimit (0.0, 0.45, freq / currentSampleRate);

                // Wellenform dieses Teiltons nachführen (je Oszillator eigener
                // Schalter, siehe Harmonic::sineTarget).
                h.sineBlend += ((double) h.sineTarget.load() - h.sineBlend) * sineBlendCoeff;

                double saw = 0.0;

                if (phaseInc > 0.0)
                    saw = 2.0 * h.phase - 1.0 - polyBlep (h.phase, phaseInc);

                // Sinus aus DERSELBEN Phase - kein zweiter Oszillator, damit
                // beim Umschalten nichts auseinanderläuft.
                const double sine = std::sin (juce::MathConstants<double>::twoPi * h.phase);

                harmonicSum += (saw + (sine - saw) * h.sineBlend) * sn.levelGain;

                h.phase += phaseInc;

                while (h.phase >= 1.0)
                    h.phase -= 1.0;   // Wrap per Subtraktion, nicht fmod/Clamp (Plan-Vorgabe)
            }
        }

        // Fahrtwind - in jeder Betriebsart AUSSER "Frei". Dort bleibt alles so,
        // wie es war: "Frei" ist die Betriebsart, in der die alten Snapshots
        // liegen (@dpa 20260824: "Alle 'alten' Snapshots bleiben einfach in
        // 'frei'"), und ein zusätzliches Rauschen darin würde sie hörbar
        // verändern. Wer den Fahrtwind will, wählt eine Betriebsart.
        const double windSample = (activeKind == KindFree)
                                ? 0.0
                                : (double) windFilter.processSample (0, (float) whiteNoise (windRandom)) * windAmount;

        // Motorband-Rauschen durch das RPM-abhängige Bandpassfilter.
        const double noiseFiltered = (double) noiseFilter.processSample (0, (float) whiteNoise (noiseRandom));

        // --- Der eigentliche Klang, je Betriebsart ---
        double kindSample = 0.0;

        switch (activeKind)
        {
            case KindJet:
            {
                // Ein Verdichterton, hoch und leise, plus das Strahlrauschen,
                // das den Klang trägt. Bewusst KEIN einzelner Sinus obendrauf
                // (@dpa: "Bei Düsenantrieb höre ich nur einen 5. Osc, sine, was
                // hast Du dir dabei gedacht?") - der Ton ist hier eine Beigabe
                // zum Rauschen, nicht umgekehrt.
                const double toneFreq = (rpmJit / 60.0) * jetToneRatio;
                const double tonePhaseInc = juce::jlimit (0.0, 0.45, toneFreq / currentSampleRate);

                double tone = 0.0;

                if (tonePhaseInc > 0.0)
                    tone = 2.0 * jetTonePhase - 1.0 - polyBlep (jetTonePhase, tonePhaseInc);

                jetTonePhase += tonePhaseInc;

                while (jetTonePhase >= 1.0)
                    jetTonePhase -= 1.0;

                const double jetNoise = (double) jetNoiseFilter.processSample (0, (float) whiteNoise (jetRandom));

                kindSample = tone * jetToneLevel * u + jetNoise * jetNoiseLevel * (0.25 + 0.75 * u);
                break;
            }

            case KindRocket:
            {
                // Kein Ton. Nur Brüllen - und die Druckstöße aus den Stoßzellen
                // des Strahls, die es auch dann gibt, wenn die Rakete selbst
                // noch langsamer als der Schall fliegt: überschallschnell ist
                // hier der Abgasstrahl, nicht die Rakete.
                const double roar = (double) rocketNoiseFilter.processSample (0, (float) whiteNoise (rocketNoiseRandom));

                shockCountdown -= 1.0;

                if (shockCountdown <= 0.0)
                {
                    // Unregelmäßiger Abstand, nicht im Takt: Stoßzellen sind
                    // keine Maschine mit fester Drehzahl. Der nächste Stoß
                    // liegt zwischen dem halben und dem anderthalbfachen
                    // mittleren Abstand.
                    shockCountdown = shockMeanSamples * (0.5 + (double) shockRandom.nextFloat());
                    shockEnv       = 1.0;
                }

                const double shockNoise = (double) shockFilter.processSample (0, (float) whiteNoise (shockRandom));

                kindSample = roar * rocketNoiseLevel * (0.3 + 0.7 * u)
                           + shockNoise * shockEnv * shockAmount * rocketShockLevel;

                shockEnv *= shockDecay;
                break;
            }

            case KindHeli:
            case KindProp:
            {
                // Rotor bzw. Propeller: ein Schwirren, das mit jedem Blatt
                // an- und abschwillt, und ein kurzer harter Schlag je Blatt.
                //
                // Das Schwirren ist gefiltertes Rauschen, dessen Pegel mit der
                // Blattfolge atmet - genau das, was aus der Entfernung wie ein
                // im Kreis laufendes Rauschen klingt. Der Schlag darüber ist
                // das Knattern.
                const double bladeWave = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::twoPi * rotorPhase);
                const double swishGain = 1.0 - rotorSwishDepth * (1.0 - bladeWave);

                const double swish = (double) rotorFilter.processSample (0, (float) whiteNoise (rotorRandom))
                                   * swishGain * rotorSwishLevel;

                const double prevPhase = rotorPhase;
                rotorPhase += rotorPhaseInc;

                // Ein Blatt ist vorbei: harter Rauschstoß. Ausgelöst am
                // Phasenumlauf, nicht an einer Schwelle im Wert - so kommt
                // genau ein Schlag je Blatt, unabhängig von der Drehzahl.
                if (rotorPhase >= 1.0)
                {
                    rotorPhase -= 1.0;
                    slapEnv = 1.0;
                }
                else if (prevPhase == 0.0)
                {
                    slapEnv = 1.0;
                }

                const double slapNoise = (double) slapFilter.processSample (0, (float) whiteNoise (slapRandom));

                // Am Propeller ist der Schlag deutlich weicher als am Rotor
                // eines Hubschraubers - dort schlagen die Blattspitzen in die
                // eigene Wirbelschleppe.
                const double slapScale = (activeKind == KindHeli) ? 1.0 : 0.45;

                double tone = 0.0;

                if (activeKind == KindProp)
                {
                    // Ein einzelner, leiser Ton (@dpa: "Ein Propellerflugzeug
                    // hat keine zusätzlichen 4 Oscillatoren, sondern höchstens
                    // einen (leiseren)").
                    const double toneFreq = rpmJit / 60.0;
                    const double tonePhaseInc = juce::jlimit (0.0, 0.45, toneFreq / currentSampleRate);

                    if (tonePhaseInc > 0.0)
                        tone = 2.0 * propTonePhase - 1.0 - polyBlep (propTonePhase, tonePhaseInc);

                    propTonePhase += tonePhaseInc;

                    while (propTonePhase >= 1.0)
                        propTonePhase -= 1.0;

                    tone *= propToneLevel;
                }

                kindSample = swish
                           + slapNoise * slapEnv * slapAmount * rotorSlapLevel * slapScale
                           + tone;

                // Der Verbrennermotor des Hubschraubers sind die vier Teiltöne
                // (oben schon gerechnet) plus sein Rauschband.
                if (activeKind == KindHeli)
                    kindSample += harmonicSum + noiseFiltered * noiseGain;

                slapEnv *= slapDecay;
                break;
            }

            case KindFree:
            default:
                kindSample = harmonicSum + noiseFiltered * noiseGain;
                break;
        }

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

        // Der Fahrtwind haengt am Fliegen, nicht am Motor - er laeuft deshalb
        // an der Unwucht vorbei und wird auch nicht vom Betriebsart-Pegel
        // skaliert, sondern nur von der Blende.
        out[n] = (float) (((kindSample * kindGain) * imbalanceFactor + windSample * windLevel) * kindFade);
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
