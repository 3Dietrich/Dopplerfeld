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

// --- Klangvorlagen der beiden Rausch-Betriebsarten ---
//
// Reihenfolge bindend, sie ist die von Params::jetVoice bzw.
// Params::rocketVoice. Wer dort umsortiert, muss hier mitziehen.
//
// Die Zahlen sind keine Messwerte, sondern Klangbilder: gesucht war je
// Eintrag ein Triebwerk, das man wiedererkennt, nicht eine Kennlinie. Was
// sie unterscheidet, ist die VERTEILUNG der Energie auf die drei Bänder -
// genau das, was ein einzelner Hochpass nicht konnte.
const std::array<EngineVoicePreset, 5> jetVoiceTable
{{
    // Turbofan: der moderne Verkehrsjet. Der Bypass-Mantelstrom macht den
    // Klang, und der ist tief und breit - das satte Rauschen beim Start,
    // nicht das Kreischen.
    { 220.0,  700.0, 0.70, 3000.0,   1.00, 0.85, 0.35,      0.0, 1.0, 0.00 },

    // Turbojet: die ältere, schmalere Bauart. Weniger Mantelstrom, mehr
    // Schärfe, und über allem der singende Verdichter - das Kreischen, das
    // man von startenden Militärmaschinen kennt.
    { 150.0, 1400.0, 0.90, 4500.0,   0.50, 1.00, 0.75,   2800.0, 9.0, 0.20 },

    // Nachbrenner: rohe Verbrennung hinter der Turbine. Alles wandert nach
    // unten, der Klang wird ein Brüllen statt eines Zischens.
    {  90.0,  400.0, 0.55, 2200.0,   1.45, 0.90, 0.45,      0.0, 1.0, 0.00 },

    // Ferne: dasselbe Triebwerk, nur weit weg. Die Höhen hat die Luft
    // unterwegs geschluckt, übrig bleibt das Grundrauschen.
    { 160.0,  500.0, 0.60, 1800.0,   1.10, 0.50, 0.06,      0.0, 1.0, 0.00 },

    // Breit: keine Kennzeichnung, alle drei Bänder gleich laut - der
    // neutrale Ausgangspunkt zum Selberdrehen.
    { 250.0, 1000.0, 0.55, 2500.0,   0.85, 0.85, 0.85,      0.0, 1.0, 0.00 }
}};

const std::array<EngineVoicePreset, 5> rocketVoiceTable
{{
    // Vollschub: ein Flüssigkeitstriebwerk unter Last. Fast alles sitzt
    // unten, das ist das Wummern, das man im Bauch spürt.
    {  70.0,  260.0, 0.50, 1200.0,   1.55, 0.80, 0.28,      0.0, 1.0, 0.00 },

    // Feststoff: rauer und körniger als flüssig, mit deutlich mehr Mitten -
    // ein Feststoffbooster prasselt, er wummert nicht nur.
    { 110.0,  600.0, 0.85, 2200.0,   1.00, 1.25, 0.55,      0.0, 1.0, 0.00 },

    // Zündung: der Augenblick, in dem der Strahl aufreißt. Breiter als der
    // eingeschwungene Vollschub, mit deutlich mehr Obenrum.
    {  90.0,  450.0, 0.65, 3200.0,   1.20, 1.00, 0.80,      0.0, 1.0, 0.00 },

    // Ferne: nur noch Grollen, die Luft hat den Rest geschluckt.
    {  60.0,  200.0, 0.50,  900.0,   1.60, 0.45, 0.04,      0.0, 1.0, 0.00 },

    // Breit: neutraler Ausgangspunkt.
    { 120.0,  500.0, 0.55, 2000.0,   1.00, 1.00, 1.00,      0.0, 1.0, 0.00 }
}};

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

void EngineGenerator::BandVoicing::prepare (const juce::dsp::ProcessSpec& spec)
{
    low.prepare (spec);
    low.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    mid.prepare (spec);
    mid.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

    // Das Hochband ist ein breiter BANDPASS, kein Hochpass. Ein Hochpass
    // laesst alles bis zur Nyquistgrenze durch, und weisses Rauschen hinter
    // einem Hochpass ist immer noch weisses Rauschen - genau das, was an der
    // Duese zu hoeren war (@dpa: "hat einfach nur weises Rauschen?"). Erst
    // ein oben begrenztes Band macht aus der Zerlegung eine Klangfarbe.
    high.prepare (spec);
    high.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

    narrow.prepare (spec);
    narrow.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
}

void EngineGenerator::BandVoicing::reset()
{
    low.reset();
    mid.reset();
    high.reset();
    narrow.reset();
}

EngineGenerator::VoiceGains EngineGenerator::applyVoicing (BandVoicing& v,
                                                            const EngineVoicePreset& preset,
                                                            double tone01, double u)
{
    // Klangfarbe als Kippung um die Mitte: -1 ganz dunkel, 0 die Vorlage
    // unveraendert, +1 ganz hell.
    const double tilt = juce::jlimit (-1.0, 1.0, (tone01 - 0.5) * 2.0);

    // Der Regler zieht BEIDES mit, Baenderpegel und Eckfrequenzen. Nur die
    // Pegel zu kippen klaenge nach einer Hoehenblende; erst die wandernden
    // Frequenzen machen daraus einen anderen Klang statt eines lauteren
    // Bandes. Eine gute Oktave Weg nach oben wie nach unten.
    const double fScale = std::pow (2.0, tilt * 1.1);

    // Gas hebt die Baender ebenfalls an - ein Triebwerk unter Last klingt
    // nicht nur lauter, sondern hoeher.
    const double uScale = 1.0 + 0.8 * u;

    const double nyquist = currentSampleRate * 0.49;
    const double halfRate = currentSampleRate * 0.5;

    // Ausgleich der Bandbreite.
    //
    // Ohne ihn bedeuten die Pegel in der Vorlage nicht das, was sie sagen: ein
    // Tiefpass bei 220 Hz laesst aus weissem Rauschen ein Zweihundertstel der
    // Energie durch, ein breites Band bei 3 kHz ein Viertel. Das Tiefband
    // waere gegen das Hochband chancenlos, egal wie die Pegel dastehen - der
    // Turbofan klaenge dann so hell wie der Turbojet.
    //
    // Weisses Rauschen durch ein Filter behaelt die Wurzel des Verhaeltnisses
    // der Bandbreiten. Also wird jedes Band mit dem Kehrwert davon
    // hochgezogen, und erst dann heisst "lowGain 1,0" wirklich "dieses Band
    // in voller Lautstaerke".
    auto place = [&] (juce::dsp::StateVariableTPTFilter<float>& f, double fc, double q, double bandwidth)
    {
        const double placed = juce::jlimit (20.0, nyquist, fc * fScale * uScale);

        f.setCutoffFrequency ((float) placed);
        f.setResonance ((float) juce::jmax (0.05, q));

        // bandwidth ist die wirksame Rauschbandbreite als Vielfaches der
        // Eckfrequenz (Tiefpass) bzw. der Bandbreite fc/Q (Bandpass).
        const double effective = juce::jmax (1.0, placed * bandwidth);

        return std::sqrt (halfRate / effective);
    };

    // Tiefpass zweiter Ordnung: wirksame Bandbreite rund 1,1 x Eckfrequenz.
    const double lowComp  = place (v.low,  preset.lowFc,  0.6, 1.11);

    // Bandpaesse: die -3-dB-Breite ist fc/Q, die wirksame Rauschbandbreite
    // etwa pi/2 davon.
    const double midComp  = place (v.mid,  preset.midFc,  preset.midQ, 1.571 / juce::jmax (0.05, preset.midQ));
    const double highComp = place (v.high, preset.highFc, 0.7,         1.571 / 0.7);

    // Der singende Ton bleibt schmal und folgt nur dem Gas, nicht der
    // Klangfarbe: er ist eine Eigenschaft der Maschine (Schaufelzahl mal
    // Drehzahl), nicht der Filterung.
    if (preset.narrowGain > 0.0)
    {
        v.narrow.setCutoffFrequency ((float) juce::jlimit (20.0, nyquist, preset.narrowFc * uScale));
        v.narrow.setResonance ((float) juce::jmax (0.05, preset.narrowQ));
    }

    // Pegel gegenlaeufig kippen: hell nimmt unten weg und gibt oben dazu.
    const double gainTilt = std::pow (2.0, tilt * 1.4);

    return { preset.lowGain  / gainTilt * lowComp,
             preset.midGain            * midComp,
             preset.highGain * gainTilt * highComp,
             preset.narrowGain };
}

double EngineGenerator::voiceSample (BandVoicing& v, const VoiceGains& g, double in, double narrowIn)
{
    // Alle drei Baender bekommen DASSELBE Rauschsample - sie sind eine
    // Zerlegung einer Quelle, keine drei Rauschgeneratoren. Drei getrennte
    // Quellen klaengen breiter und leerer zugleich, weil sich nichts mehr
    // ueberlagert.
    double outSample = (double) v.low.processSample  (0, (float) in) * g.low
                     + (double) v.mid.processSample  (0, (float) in) * g.mid
                     + (double) v.high.processSample (0, (float) in) * g.high;

    // Der singende Ton kommt aus EIGENEM Rauschen: er soll neben dem Strahl
    // stehen, nicht aus ihm herausgefiltert sein - sonst waere er nur ein
    // schmaler Ausschnitt dessen, was ohnehin schon da ist.
    if (g.narrow > 0.0)
        outSample += (double) v.narrow.processSample (0, (float) narrowIn) * g.narrow;

    return outSample;
}

double EngineGenerator::nWaveShape (double t, double duration, double rise)
{
    // Dieselbe Form wie PropagationPath::nWaveAt(), nur ohne die Groessen der
    // Ausbreitung: senkrecht auf +1, lineare Gerade durch null, senkrecht von
    // -1 zurueck. Zwei Stossfronten mit einer Geraden dazwischen - das ist
    // eine Stosswelle, und nicht dasselbe wie ein abklingender Rauschstoss.
    if (t < 0.0 || t > duration || duration <= 0.0)
        return 0.0;

    const double r = std::max (1.0e-9, std::min (rise, 0.4 * duration));

    double shape = 1.0 - 2.0 * t / duration;

    // Vordere Front: aus der Ruhe auf +1 hochziehen.
    if (t < r)
        shape *= t / r;

    // Hintere Front: von -1 zurueck auf Ruhe.
    if (t > duration - r)
        shape *= (duration - t) / r;

    return shape;
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

    // Fahrtwind: Hochpass, denn Wind an Kanten ist ein Zischen, kein Wummern.
    windFilter.prepare (spec);
    windFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);

    // Strahlrauschen der Düse und Brüllen der Rakete: beide bekommen dieselbe
    // Dreiband-Formung, aber je eine eigene Vorlagenliste (siehe
    // jetVoiceTable/rocketVoiceTable oben).
    //
    // Die Druckstöße der Rakete brauchen KEIN Filter mehr: sie sind seit
    // @dpa 20260824 echte N-Wellen, und deren Form IST ihr Klang - ein
    // Bandpass darüber würde genau die senkrechten Fronten abrunden, um die
    // es geht.
    jetVoicing.prepare (spec);
    rocketVoicing.prepare (spec);

    // Rotor: das Schwirren sitzt in den Mitten, der Blattknall darüber.
    rotorFilter.prepare (spec);
    rotorFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

    slapFilter.prepare (spec);
    slapFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

    // Verzoegerungsleitungen der Blaetter. Bemessen nach der groessten
    // moeglichen Laufzeitschwankung (2r/c beim groessten Radius), plus zwei
    // Samples Reserve fuer die Interpolation. Hier ist der einzige Ort, an
    // dem sie Speicher bekommen - im Renderpfad wird nichts allokiert.
    {
        const int ringLength = 4 + (int) std::ceil (2.0 * maxRotorRadiusM / 300.0 * sampleRate);

        for (size_t i = 0; i < blades.size(); ++i)
        {
            auto& blade = blades[i];

            blade.ring.assign ((size_t) ringLength, 0.0f);
            blade.writePos   = 0;
            blade.slapEnv    = 0.0;
            blade.prevPhase  = 0.0;
            blade.shockPhase = -1.0;

            // Deutlich gestreute Startwerte, nicht nur ein anderes letztes
            // Byte: die Blaetter sollen wirklich unabhaengig rauschen.
            blade.random.setSeed ((juce::int64) (0x9e3779b97f4a7c15ull * (std::uint64_t) (i + 1)) | 1);
        }
    }

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

    shockCountdown = 0.0;
    shocks.fill ({});
    slapEnv        = 0.0;

    activeKind = engineKind.load();
    kindFade   = 1.0;

    for (auto& h : harmonics)
        h.sineBlend = (double) h.sineTarget.load();

    noiseFilter.reset();
    jitterFilter.reset();
    windFilter.reset();
    jetVoicing.reset();
    rocketVoicing.reset();
    rotorFilter.reset();
    slapFilter.reset();

    rotorRevPhase = 0.0;

    for (auto& blade : blades)
    {
        std::fill (blade.ring.begin(), blade.ring.end(), 0.0f);
        blade.writePos   = 0;
        blade.slapEnv    = 0.0;
        blade.prevPhase  = 0.0;
        blade.shockPhase = -1.0;
    }

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
    jetNarrowRandom.setSeed (0x5eedc0de);
    rocketNarrowRandom.setSeed (0x5eeddead);
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

void EngineGenerator::setJetVoice (int voiceIndex, float tone01)
{
    jetVoiceIndex.store (juce::jlimit (0, (int) jetVoiceTable.size() - 1, voiceIndex));
    jetTone.store (juce::jlimit (0.0f, 1.0f, tone01));
}

void EngineGenerator::setRocketVoice (int voiceIndex, float tone01)
{
    rocketVoiceIndex.store (juce::jlimit (0, (int) rocketVoiceTable.size() - 1, voiceIndex));
    rocketTone.store (juce::jlimit (0.0f, 1.0f, tone01));
}

void EngineGenerator::setRocketShockShape (float sizeMetres, float rateHz)
{
    rocketShockSizeM.store (juce::jmax (0.001f, sizeMetres));
    rocketShockRateHz.store (juce::jmax (0.01f, rateHz));
}

void EngineGenerator::setRotorDoppler (bool shouldUseDoppler)
{
    rotorDoppler.store (shouldUseDoppler);
}

void EngineGenerator::setRotorRadius (float metres)
{
    rotorRadiusM.store (juce::jlimit (0.1f, (float) maxRotorRadiusM, metres));
}

void EngineGenerator::setRotorInPlane (float factor01)
{
    rotorInPlane.store (juce::jlimit (0.0f, 1.0f, factor01));
}

void EngineGenerator::setRotorFlightSpeed (float metresPerSecond)
{
    rotorFlightSpeed.store (metresPerSecond);
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

    // Duese und Rakete: gewaehlte Klangvorlage, verbogen um den
    // Klangfarbe-Regler und mitgezogen vom Gas. Einmal je Block gesetzt, wie
    // alle uebrigen Filter hier auch.
    const auto jetGains = applyVoicing (jetVoicing,
                                        jetVoiceTable[(size_t) jetVoiceIndex.load()],
                                        (double) jetTone.load(), u);

    const auto rocketGains = applyVoicing (rocketVoicing,
                                           rocketVoiceTable[(size_t) rocketVoiceIndex.load()],
                                           (double) rocketTone.load(), u);

    // Druckstoesse im Raketenstrahl: Dauer aus der Ausdehnung der Stosszelle
    // (wie Params::nWaveSize: Hin- und Rueckweg des Schalls durch die Zelle),
    // Folge aus dem Rate-Regler.
    const double shockAmount = (double) rocketShock.load();

    const double shockDurationSeconds = 2.0 * (double) rocketShockSizeM.load() / shockSpeedOfSound;

    // Anstiegszeit der beiden Fronten: ein fester Bruchteil der Wellendauer,
    // nach unten aber nie kuerzer als zwei Samples - darunter waere die Front
    // nicht mehr darstellbar und faltete als Aliasing zurueck.
    const double shockRise = std::max (shockDurationSeconds * shockRiseFraction,
                                       2.0 / currentSampleRate);

    const double shockMeanSamples = currentSampleRate
                                  / std::max (0.01, (double) rocketShockRateHz.load());

    // Zeitschritt je Sample, fuer die Phase der laufenden Stosswellen.
    const double sampleSeconds = 1.0 / currentSampleRate;

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

    // Rotor-Doppler (siehe setRotorDoppler): Umdrehungsphase statt Blattfolge,
    // und die groesste Laufzeit, die ein Blatt auf seinem Kreis erreichen kann.
    const bool   dopplerRotor  = rotorDoppler.load();
    const int    bladeSlots    = juce::jlimit (1, maxRotorBlades, (int) std::lround (bladeCount));
    const double revPhaseInc   = juce::jlimit (0.0, 0.45, rotorRps / currentSampleRate);

    // Schallgeschwindigkeit als Modellkonstante: der Generator sitzt VOR der
    // Ausbreitung und kennt weder Temperatur noch Hoehe. Der Unterschied
    // zwischen 331 und 350 m/s verschiebt die Laufzeitschwankung um wenige
    // Prozent - hoerbar ist daran nichts, und eine zweite Zahl durch alle
    // Schichten zu reichen waere der Preis dafuer.
    constexpr double rotorSoundSpeed = 343.0;

    const double rotorInPlaneNow = (double) rotorInPlane.load();
    const double rotorRadiusNow  = (double) rotorRadiusM.load();

    const double maxBladeDelaySamples = rotorRadiusNow * 2.0
                                      / rotorSoundSpeed * currentSampleRate
                                      * rotorInPlaneNow;

    // Konvektionsverstaerkung der Blattspitze. Die Laufzeit allein macht das
    // Knattern noch nicht: sie verschiebt die Schlaege im Takt, aendert aber
    // kaum ihre Lautstaerke, und bei je eigenem Rauschen je Blatt entsteht
    // auch keine Interferenz. Was man wirklich hoert, ist die Richtwirkung -
    // eine Quelle, die sich auf einen zubewegt, strahlt um 1/(1-M_r)^2
    // staerker ab.
    //
    // Und die ist am Rotor gewaltig: bei 6 m Blatt und 5 Umdrehungen/s laeuft
    // die Spitze mit 188 m/s, also M = 0,55. Das vorlaufende Blatt kommt
    // dadurch rund 25-mal lauter an als das ruecklaufende - genau das ist das
    // WOP-WOP, und genau das verschwindet, wenn der Hubschrauber senkrecht
    // ueber einem steht (rotorInPlane geht auf 0, mit ihm M_r).
    const double tipMach = juce::MathConstants<double>::twoPi * rotorRadiusNow
                               * rotorRps / rotorSoundSpeed;

    // Und dazu faehrt der ganze Rotor (siehe setRotorFlightSpeed). Auf der
    // vorlaufenden Seite addieren sich beide Geschwindigkeiten - das ist der
    // Grund, warum ein Hubschrauber im Reiseflug knallt und im Schwebeflug
    // nur schwirrt.
    const double flightMach = (double) rotorFlightSpeed.load() / rotorSoundSpeed;

    // Wie stark der Schlag die Richtwirkung ueberhaupt ausspielt. "Knattern"
    // regelt nicht mehr nur den Pegel des Schlages, sondern auch, wie hart er
    // ausfaellt (@dpa 20260824: "der Control 'Knattern' reicht einfach
    // nicht"). Bei 1 steht die Physik pur da, darueber wird sie ueberzeichnet.
    // Untergrenze 1: dort steht die Richtwirkung unveraendert. Ein Exponent
    // darunter wuerde sie wegbuegeln, und "kein Knattern" soll heissen "kein
    // Schlag", nicht "kein Rotor".
    const double slapSharpness = juce::jlimit (1.0, 4.0, slapAmount);

    auto machRadialAt = [tipMach, flightMach, rotorInPlaneNow] (double angle)
    {
        // Radiale Machzahl der Blattspitze: ihr Umlauf, projiziert auf die
        // Sichtlinie, plus die Fahrt des ganzen Rotors darauf.
        return juce::jlimit (-0.99, 0.995,
                             tipMach * rotorInPlaneNow * std::sin (angle) + flightMach);
    };

    auto convectiveGain = [machRadialAt] (double angle)
    {
        const double denom = std::max (0.05, 1.0 - machRadialAt (angle));

        return 1.0 / (denom * denom);
    };

    // Ab der Delokalisierungsgrenze loesen sich die Verdichtungsstoesse von
    // der Blattspitze und laufen als eigene Wellen davon (siehe
    // bladeDelocalisationMach im Header). Das ist der harte Knall, den @dpa
    // beim Ueberflug hoert, obwohl nichts Ueberschall fliegt. Er waechst mit
    // dem Abstand zur Grenze und ist unterhalb davon exakt null - kein
    // weicher Uebergang, denn das Ereignis selbst hat keinen.
    const double delocalisation = std::max (0.0, machRadialAt (0.25 * juce::MathConstants<double>::twoPi)
                                                     - bladeDelocalisationMach)
                                / (1.0 - bladeDelocalisationMach);

    // Auf gleichen Gesamtpegel normieren, sonst wuerde der Rotor mit
    // steigender Drehzahl einfach lauter statt knackiger. Der Effektivwert
    // ueber einen Umlauf, einmal je Block aus 64 Stuetzstellen - im
    // Samplepfad steht davon nur noch eine Division.
    //
    // Gerechnet OHNE die Fahrt. Mit ihr waere die Normierung ein Nullsummen-
    // spiel: schneller unterwegs stiege die Ueberhoehung, und derselbe
    // Nenner zoege sie sofort wieder ab - genau der Effekt, um den es hier
    // geht, verschwaende in der Division. Die Drehzahl soll normiert werden,
    // die Fahrt nicht.
    double rotorGainNorm = 1.0;

    {
        constexpr int steps = 64;
        double sumSq = 0.0;

        for (int i = 0; i < steps; ++i)
        {
            const double angle = juce::MathConstants<double>::twoPi
                                     * (double) i / (double) steps;

            const double m     = juce::jlimit (-0.99, 0.995,
                                               tipMach * rotorInPlaneNow * std::sin (angle));
            const double denom = std::max (0.05, 1.0 - m);
            const double g     = 1.0 / (denom * denom);

            sumSq += g * g;
        }

        rotorGainNorm = std::max (1.0e-6, std::sqrt (sumSq / (double) steps));
    }

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

                // Das Strahlrauschen durch die gewaehlte Dreiband-Formung
                // (@dpa 20260824: "Duesenantrieb hat einfach nur weises
                // Rauschen?"). Vorher war es genau das: weisses Rauschen
                // hinter einem einzelnen Hochpass.
                const double jetNoise = voiceSample (jetVoicing, jetGains,
                                                     whiteNoise (jetRandom),
                                                     whiteNoise (jetNarrowRandom));

                kindSample = tone * jetToneLevel * u + jetNoise * jetNoiseLevel * (0.25 + 0.75 * u);
                break;
            }

            case KindRocket:
            {
                // Kein Ton. Nur Brüllen - und die Druckstöße aus den Stoßzellen
                // des Strahls, die es auch dann gibt, wenn die Rakete selbst
                // noch langsamer als der Schall fliegt: überschallschnell ist
                // hier der Abgasstrahl, nicht die Rakete.
                //
                // Die Stöße sind echte N-Wellen (@dpa 20260824: "Die
                // Druckstöße sind Überschall, also donnernde N-Waves"), keine
                // Rauschstöße mit Hüllkurve. Der Unterschied ist nicht
                // kosmetisch: eine Stoßwelle hat zwei senkrechte Fronten mit
                // einer Geraden dazwischen, und diese Fronten sind der Knall.
                // Ein abklingender Rauschburst hat sie nicht, er hat nur einen
                // Anfang.
                const double roar = voiceSample (rocketVoicing, rocketGains,
                                                 whiteNoise (rocketNoiseRandom),
                                                 whiteNoise (rocketNarrowRandom));

                shockCountdown -= 1.0;

                if (shockCountdown <= 0.0 && shockAmount > 0.0)
                {
                    // Unregelmäßiger Abstand, nicht im Takt: Stoßzellen sind
                    // keine Maschine mit fester Drehzahl. Der nächste Stoß
                    // liegt zwischen dem halben und dem anderthalbfachen
                    // mittleren Abstand.
                    shockCountdown = shockMeanSamples * (0.5 + (double) shockRandom.nextFloat());

                    // Auf einen FREIEN Platz legen. Einen noch laufenden
                    // Stoß zu überschreiben hiesse, ihn mitten in seiner
                    // Flanke abzuschneiden - ein Sprung im Signal, also ein
                    // Knacken. Ist gerade keiner frei (sehr lange Wellen bei
                    // sehr hoher Folge), fällt dieser eine Stoß aus. Das ist
                    // an dieser Stelle nicht zu hören: es laufen dann bereits
                    // zweiunddreissig übereinander.
                    Shock* free = nullptr;

                    for (auto& candidate : shocks)
                    {
                        if (candidate.duration <= 0.0 || candidate.phase > candidate.duration)
                        {
                            free = &candidate;
                            break;
                        }
                    }

                    if (free != nullptr)
                    {
                        // Streuung der Dauer: Stoßzellen sind nicht alle
                        // gleich groß, und lauter identische N-Wellen klängen
                        // nach einem Maschinengewehr statt nach einem Strahl.
                        free->duration = shockDurationSeconds * (0.6 + 0.8 * (double) shockRandom.nextFloat());
                        free->rise     = shockRise;

                        // Amplitude streut, das Vorzeichen NICHT: eine
                        // Stoßwelle beginnt immer mit Überdruck. Ein
                        // zufälliges Vorzeichen machte aus der N-Welle wieder
                        // ein Rauschen.
                        free->amp   = 0.6 + 0.4 * (double) shockRandom.nextFloat();
                        free->phase = 0.0;
                    }
                }

                // Alle laufenden Stoßwellen aufsummieren. Bei hoher Folge
                // überlappen sie sich - dieses Übereinander ist das Knattern
                // ("crackle"), das eine Rakete von einem Rauschgenerator
                // unterscheidet.
                double shockSum = 0.0;

                for (auto& sh : shocks)
                {
                    if (sh.duration <= 0.0 || sh.phase > sh.duration)
                        continue;

                    shockSum += sh.amp * nWaveShape (sh.phase, sh.duration, sh.rise);
                    sh.phase += sampleSeconds;
                }

                kindSample = roar * rocketNoiseLevel * (0.3 + 0.7 * u)
                           + shockSum * shockAmount * rocketShockLevel;
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
                // Am Propeller ist der Schlag deutlich weicher als am Rotor
                // eines Hubschraubers - dort schlagen die Blattspitzen in die
                // eigene Wirbelschleppe.
                const double slapScale = (activeKind == KindHeli) ? 1.0 : 0.45;

                double swish     = 0.0;
                double slapMixed = 0.0;

                if (dopplerRotor)
                {
                    // Jedes Blatt ist eine eigene Quelle auf der Kreisbahn
                    // (siehe setRotorDoppler). Sein Rauschen - Schwirren plus
                    // sein eigener Schlag - geht in seine eigene Leitung und
                    // wird mit der Laufzeit gelesen, die zu seiner Stellung
                    // auf dem Kreis gehoert. Die Modulation entsteht dadurch,
                    // sie wird nicht aufgepraegt.
                    //
                    // Verzoegerung: cos ist +1, wenn das Blatt am naechsten
                    // steht, -1 am fernsten. (1 - cos)/2 macht daraus 0..1,
                    // also eine Leitung, die nie negativ wird.
                    const double twoPi = juce::MathConstants<double>::twoPi;

                    double sum      = 0.0;
                    double shockSum = 0.0;

                    // Laenge der abgeloesten Stosswelle in Samples, siehe
                    // bladeShockSeconds.
                    const double shockLen = std::max (2.0, bladeShockSeconds * currentSampleRate);

                    for (int k = 0; k < bladeSlots; ++k)
                    {
                        auto& blade = blades[(size_t) k];

                        const double bladePhase = rotorRevPhase + (double) k / (double) bladeSlots;
                        const double angle      = twoPi * bladePhase;

                        // Eigener Schlag je Blatt, und zwar genau dort, wo das
                        // Blatt am schnellsten auf den Hoerer zulaeuft
                        // (Viertelumlauf, sin = 1). Das ist keine Feinheit,
                        // sondern der ganze Effekt: am Umlaufpunkt selbst ist
                        // die Radialgeschwindigkeit null, dort bekaeme der
                        // Schlag gar keine Richtwirkung ab und das Knattern
                        // bliebe in jeder Lage gleich. Zusammen ergeben die N
                        // Blaetter dieselbe Schlagrate wie zuvor.
                        const double slapPhase   = bladePhase - 0.25;
                        const double slapWrapped = slapPhase - std::floor (slapPhase);

                        if (slapWrapped < blade.prevPhase)
                        {
                            blade.slapEnv = 1.0;

                            // Nur wenn die Blattspitze die Grenze reisst,
                            // loest sich ueberhaupt ein Stoss ab.
                            if (delocalisation > 0.0)
                                blade.shockPhase = 0.0;
                        }

                        blade.prevPhase = slapWrapped;

                        const double bladeNoise = whiteNoise (blade.random);

                        // Richtwirkung dieses Blattes in seiner aktuellen
                        // Stellung (siehe convectiveGain oben). "Knattern"
                        // zieht sie zusaetzlich hoch: bei 1 steht die Physik
                        // pur da, darueber wird sie ueberzeichnet.
                        const double raw = convectiveGain (angle) / rotorGainNorm;

                        // Gedeckelt, damit der Regler auf 4 aus einer schon
                        // hohen Ueberhoehung keine Zahl macht, die nur noch
                        // den Begrenzer beschaeftigt. 36 dB ist reichlich -
                        // mehr Unterschied als zwischen Fluestern und Rufen.
                        const double directivity = std::min (maxBladeDirectivity,
                                                             std::pow (raw, slapSharpness));

                        // Die Richtwirkung trifft den SCHLAG, nicht das
                        // Schwirren. Das ist keine Bequemlichkeit: das
                        // Schwirren entsteht ueber die ganze Blattspanne, wo
                        // die oertliche Machzahl von null an der Nabe bis zum
                        // Vollen an der Spitze reicht - im Mittel also viel
                        // weniger. Der Schlag dagegen sitzt genau an der
                        // Spitze und bekommt die volle Ueberhoehung ab.
                        //
                        // Hoerbar ist der Unterschied der ganze Punkt: das
                        // Schwirren bleibt ein gleichmaessiger Teppich, aus
                        // dem der Schlag heraussticht, statt dass beides
                        // zusammen atmet.
                        const float input = (float) (bladeNoise * rotorSwishLevel
                                                     + directivity * bladeNoise * blade.slapEnv
                                                           * slapAmount * rotorSlapLevel * slapScale);

                        // Der abgeloeste Stoss laeuft NEBEN der Leitung, nicht
                        // in ihr: er darf nicht durch den Bandpass des
                        // Schwirrens, sonst wird aus dem Knall ein Blubbern.
                        // Seine Laufzeit darf er trotzdem behalten - er feuert
                        // bei jedem Blatt am selben Azimut, dort ist die
                        // Verzoegerung fuer alle gleich und damit eine reine,
                        // unhoerbare Zeitverschiebung.
                        if (blade.shockPhase >= 0.0)
                        {
                            const double u = blade.shockPhase / shockLen;

                            if (u >= 1.0)
                            {
                                blade.shockPhase = -1.0;
                            }
                            else
                            {
                                // Dieselbe N-Form wie in der Ausbreitung:
                                // Sprung auf +1, Gerade durch null, Ruecksprung
                                // von -1. Nur viel kuerzer.
                                double shape = 1.0 - 2.0 * u;

                                if (u < bladeShockRise)
                                    shape *= u / bladeShockRise;
                                else if (u > 1.0 - bladeShockRise)
                                    shape *= (1.0 - u) / bladeShockRise;

                                shockSum += shape * delocalisation * bladeShockLevel
                                                * std::min (1.0, slapAmount) * slapScale;

                                blade.shockPhase += 1.0;
                            }
                        }

                        blade.slapEnv *= slapDecay;

                        const int ringLength = (int) blade.ring.size();

                        blade.ring[(size_t) blade.writePos] = input;

                        const double delaySamples =
                            juce::jlimit (0.0, (double) (ringLength - 3),
                                          maxBladeDelaySamples * 0.5 * (1.0 - std::cos (angle)));

                        const double readPos = (double) blade.writePos - delaySamples;
                        const int    i0      = (int) std::floor (readPos);
                        const double frac    = readPos - (double) i0;

                        const int a = ((i0 % ringLength) + ringLength) % ringLength;
                        const int b = (a + 1) % ringLength;

                        sum += (double) blade.ring[(size_t) a] * (1.0 - frac)
                             + (double) blade.ring[(size_t) b] * frac;

                        blade.writePos = (blade.writePos + 1) % ringLength;
                    }

                    // Auf gleiche Lautstaerke wie der gefakte Weg bringen: N
                    // unkorrelierte Quellen summieren sich mit sqrt(N).
                    const double norm = std::sqrt ((double) bladeSlots);

                    sum /= norm;

                    // Gefiltert wird die Summe, nicht jedes Blatt einzeln -
                    // ein Bandpass je Blatt klaenge gleich und kostete das
                    // Achtfache. Die Stoesse kommen erst DANACH dazu.
                    swish = (double) rotorFilter.processSample (0, (float) sum)
                          + shockSum / norm;

                    rotorRevPhase += revPhaseInc;

                    if (rotorRevPhase >= 1.0)
                        rotorRevPhase -= 1.0;
                }
                else
                {
                    const double bladeWave = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::twoPi * rotorPhase);
                    const double swishGain = 1.0 - rotorSwishDepth * (1.0 - bladeWave);

                    swish = (double) rotorFilter.processSample (0, (float) whiteNoise (rotorRandom))
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

                    slapMixed = slapNoise * slapEnv * slapAmount * rotorSlapLevel * slapScale;

                    slapEnv *= slapDecay;
                }

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

                kindSample = swish + slapMixed + tone;

                // Der Verbrennermotor des Hubschraubers sind die vier Teiltöne
                // (oben schon gerechnet) plus sein Rauschband.
                if (activeKind == KindHeli)
                    kindSample += harmonicSum + noiseFiltered * noiseGain;

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
