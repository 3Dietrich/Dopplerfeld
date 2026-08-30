#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <vector>

// Bausteine, die sich alle Hallbauarten teilen: Verzoegerungsleitung,
// Daempfungsfilter, Allpass. Bewusst getrennt von den Bauarten selbst, damit
// ein neuer Typ nur noch die Verschaltung beschreibt und nicht wieder einen
// eigenen Ringpuffer mitbringt.
//
// Alles JUCE-frei und ohne Allokation im Betrieb: die Puffer werden in
// prepare() auf die groesste je gebrauchte Laenge gestellt, danach aendert sich
// nur noch der Lesezeiger.
namespace reverbparts
{

// Schallgeschwindigkeit fuer die Umrechnung Raumgroesse -> Laufzeit. Bewusst
// ein fester Wert und nicht der aus Medium.h: die Laufzeiten im Hallnetz
// beschreiben eine gedachte Raumform, keinen gemessenen Weg. Sie mit der
// Temperatur mitwandern zu lassen wuerde die Delay-Puffer bei jedem
// Wetterregler neu bemessen, ohne dass man es hoert.
inline constexpr double soundSpeed = 343.0;

// Groesster einstellbarer Raum, in Metern. Reiner Deckel des Reglers, keine
// Pufferbemessung mehr: 2000 m sind knapp sechs Sekunden Grundlaufzeit, und
// damit ist jede Talflanke abgedeckt, die das Feld hergibt.
inline constexpr double maxRoomMetres = 2000.0;

// Kleinster Raum. Darunter waeren die Leitungen kuerzer als ein Sample.
inline constexpr double minRoomMetres = 0.5;

// Womit ein Abgriffpunkt anfaengt, solange niemand mehr verlangt hat.
inline constexpr double baseCapacityMetres = 25.0;

// Wie gross die Verzoegerungsleitungen bemessen werden, um einen Raum von
// `metres` zu tragen.
//
// Der Speicher waechst linear mit der Raumgroesse, und er waechst pro
// Abgriffpunkt: bei Draussen liegen bis zu 48 eigene Leitungen nebeneinander.
// Gemessen bei 48 kHz, acht Punkte, alle Bauarten bereitliegend:
//
//     25 m       6,8 MB je Punkt      55 MB
//    200 m      16,4 MB je Punkt     131 MB
//   2000 m     104,6 MB je Punkt     837 MB
//
// Fest auf den groessten Raum bemessen zahlte also jede Einstellung den
// Extremfall - fuer Leitungen, die dabei fast leer bleiben. (Bei 96 kHz noch
// einmal die Haelfte mehr; im 25-m-Fall sind fast sechs der 6,8 MB der
// Vorlauf, der bis 5 km reicht und nicht an der Raumgroesse haengt.)
//
// Deshalb: bemessen wird nach dem, was wirklich eingestellt ist, und zwar in
// Verdopplungsschritten. Die Treppe ist der Punkt an der Sache - eine Leitung,
// die bei jedem Reglerpixel exakt neu bemessen wuerde, muesste beim Ziehen
// hunderte Male neu angelegt werden; so passiert es beim Weg von 25 auf 2000 m
// genau sieben Mal.
//
// Neu bemessen wird ausserhalb des Audiothreads, siehe TapBus::roomShortfall
// und DopplerfeldProcessor::growTapCapacityIfNeeded.
inline double capacityFor (double metres)
{
    double c = baseCapacityMetres;

    while (c < metres && c < maxRoomMetres)
        c *= 2.0;

    return std::min (c, maxRoomMetres);
}

// Ringpuffer mit ganzzahliger Verzoegerung.
//
// Ganzzahlig genuegt, weil im Hall keine Tonhoehe an der Verzoegerung haengt -
// anders als beim Doppler-Pfad, wo Bruchteile eines Samples den Ton verstimmen.
// Eine Interpolation waere hier reiner Aufwand und wuerde obendrein Hoehen
// verschlucken.
// Ausschlag des Wanderns in Samples, aus dem Regler und der Laenge der Leitung
// (siehe DelayLine::setMotion).
//
// Anteilig zur Laenge, nicht als feste Strecke: eine lange Leitung darf weiter
// wandern, ohne dass es nach Verstimmung klingt, waehrend dieselbe Strecke eine
// kurze voellig verbiegen wuerde. Zwei Prozent bei vollem Regler sind rund ein
// Drittel Halbton Verstimmung, wenn die Bewegung am schnellsten ist - deutlich
// zu hoeren, aber noch keine Seekrankheit. Nach unten mindestens ein halbes
// Sample, sonst bewegte sich bei den kuerzesten Leitungen gar nichts.
inline double motionDepthFor (double amount01, int lengthSamples)
{
    const double a = std::clamp (amount01, 0.0, 1.0);

    if (a <= 0.0)
        return 0.0;

    return std::max (0.5, a * 0.02 * (double) lengthSamples);
}

class DelayLine
{
public:
    void prepare (int maxLengthSamples)
    {
        capacity = std::max (1, maxLengthSamples);
        buffer.assign ((size_t) capacity, 0.0f);
        writePos = 0;
        setLength (capacity);
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;

        motionA = 0.0;
        motionB = 0.0;
    }

    // Die Leitung wandert: der Lesekopf bewegt sich langsam um seine Ruhelage,
    // gefuehrt von einem zweipolig tiefpassgefilterten Rauschen. Das bricht die
    // stehenden Wellen auf, die ein Netz aus festen Leitungen unweigerlich
    // hat - der Hall wird lebendig statt metallisch, und bei groesserem
    // Ausschlag hoert man das Wandern als leises Verstimmen.
    //
    // amount01 ist der Reglerwert (0 = still), rateHz die Geschwindigkeit des
    // Rauschens, seed macht jede Leitung unabhaengig - laufen sie im
    // Gleichtakt, wandert nicht der Raum, sondern die Tonhoehe.
    //
    // Uebergeben wird der Regler und nicht der fertige Ausschlag: die Laenge
    // kennt die Leitung selbst und aendert sie im Betrieb, wenn jemand am
    // Raum dreht. So bleibt der Ausschlag von allein passend.
    void setMotion (double amount01, double rateHz, double sampleRate, int seed)
    {
        motionAmount = (float) std::clamp (amount01, 0.0, 1.0);

        // Zweimal derselbe Ein-Pol hintereinander: ein einzelner liesse noch
        // hoerbares Zappeln durch, zwei machen daraus eine weiche Fahrt.
        const double f = std::clamp (rateHz, 0.01, 50.0);
        const double c = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * f
                                         / std::max (1.0, sampleRate));

        motionCoeff = c;

        // Ein Tiefpass, der aus 48000 Werten je Sekunde eine halbe Schwingung
        // macht, laesst vom Rauschen fast nichts uebrig: hinter zwei solchen
        // Polen bleibt der Bruchteil c/(2-c), bei einer Fuenftel-Hertz also
        // ein Hunderttausendstel. Ohne diesen Ausgleich stuende der Regler auf
        // voll und nichts bewegte sich.
        //
        // Gerechnet auf einen Effektivwert von rund einem Drittel: dann liegen
        // die Spitzen bei eins, und die Begrenzung darunter greift selten
        // genug, um die Fahrt nicht abzuflachen.
        const double rms = 0.5773502691896257 * (c / (2.0 - c));

        motionGain = rms > 1.0e-12 ? 0.35 / rms : 0.0;

        if (motionSeed != seed)
        {
            motionSeed  = seed;
            motionState = (std::uint32_t) (seed * 2654435761u + 1u);
        }
    }

    // Laenge in Samples, auf die Kapazitaet begrenzt. Der Puffer wird dabei
    // NICHT geleert: eine Laengenaenderung im Betrieb soll den Nachhall
    // umfaerben, nicht abschneiden.
    void setLength (int samples)
    {
        length = std::clamp (samples, 1, capacity);
    }

    int getLength() const { return length; }

    // Lesen an der eingestellten Laenge. Wandert die Leitung (siehe
    // setMotion), sitzt der Kopf zwischen zwei Samples und wird linear
    // interpoliert - ohne das waere die Bewegung eine Treppe aus ganzen
    // Samples und knisterte.
    float read()
    {
        if (motionAmount <= 0.0f)
        {
            int p = writePos - length;
            if (p < 0)
                p += capacity;

            return buffer[(size_t) p];
        }

        // Weisses Rauschen durch zwei Ein-Pole. Der Ausgang eines solchen
        // Paares bleibt weit unter eins, deshalb der Ausgleich am Ende - so
        // steht im Regler wirklich der Ausschlag, den er verspricht.
        motionState = motionState * 1664525u + 1013904223u;

        const double white = (double) motionState * (2.0 / 4294967296.0) - 1.0;

        // Die Zustaende in doppelter Genauigkeit: bei einer Zeitkonstante von
        // Sekunden ist der Schritt je Sample so klein, dass er in einfacher
        // Genauigkeit in der Rundung verschwaende.
        motionA += (white   - motionA) * motionCoeff;
        motionB += (motionA - motionB) * motionCoeff;

        const double depth  = motionDepthFor ((double) motionAmount, length);
        const float  wander = (float) (std::clamp (motionB * motionGain, -1.0, 1.0) * depth);

        // Nie ueber die Kapazitaet und nie unter ein Sample: der Kopf muss
        // zwischen zwei gueltigen Stellen liegen.
        const float target = std::clamp ((float) length + wander,
                                         1.0f, (float) (capacity - 1));

        const int   whole = (int) target;
        const float frac  = target - (float) whole;

        int p0 = writePos - whole;
        if (p0 < 0)
            p0 += capacity;

        int p1 = p0 - 1;
        if (p1 < 0)
            p1 += capacity;

        return buffer[(size_t) p0] * (1.0f - frac) + buffer[(size_t) p1] * frac;
    }

    // Lesen an einer beliebigen Stelle, unabhaengig von der eingestellten
    // Laenge. Dafuer sind die getappten Leitungen da: eine Leitung, viele
    // Lesekoepfe, statt einer Leitung je Echo.
    float readAt (int samples) const
    {
        int p = writePos - std::clamp (samples, 1, capacity);
        if (p < 0)
            p += capacity;

        return buffer[(size_t) p];
    }

    void write (float x)
    {
        buffer[(size_t) writePos] = x;

        if (++writePos >= capacity)
            writePos = 0;
    }

private:
    std::vector<float> buffer;
    int                capacity = 1;
    int                length   = 1;
    int                writePos = 0;

    // Wandern des Lesekopfes, siehe setMotion.
    float         motionAmount = 0.0f;
    double        motionCoeff  = 0.0;
    double        motionGain   = 0.0;
    double        motionA      = 0.0;
    double        motionB      = 0.0;
    std::uint32_t motionState  = 1u;
    int           motionSeed   = 0;
};

// Einpol-Tiefpass im Rueckkopplungsweg. Gleichstromverstaerkung 1, damit die
// Daempfung nur Hoehen nimmt und nicht den Pegel - genau wie die
// Flaechendaempfung der Spiegelpfade.
class DampingFilter
{
public:
    void setCoefficient (double c) { a = (float) std::clamp (c, 0.0, 0.999); }
    void reset() { z = 0.0f; }

    float process (float x)
    {
        z = x * (1.0f - a) + z * a;
        return z;
    }

private:
    float a = 0.0f;
    float z = 0.0f;
};

// Daempfungsstaerke 0..1 in einen Filterkoeffizienten uebersetzen.
//
// Die Eckfrequenz laeuft geometrisch von 20 kHz (offen) auf 80 Hz (ganz zu).
// Ein linearer Verlauf waere in der oberen Haelfte des Reglers fast
// wirkungslos, weil das Gehoer Frequenzen logarithmisch nimmt.
//
// 80 Hz und nicht die urspruenglichen 300 (@dpa 20260829: "max Damp bitte noch
// tiefer noch erdiger"). Bei voller Stellung bleibt damit wirklich nur noch
// Grollen stehen - ein Rueckwurf von weichem Waldboden oder aus grosser Ferne,
// bei dem alles Sprechende weg ist. Der obere Teil des Reglerwegs aendert sich
// dadurch kaum, weil der Verlauf geometrisch ist: die neue Strecke haengt
// unten dran, sie staucht nicht die alte.
inline double dampingCoefficient (double amount01, double sampleRate)
{
    const double a  = std::clamp (amount01, 0.0, 1.0);
    const double fc = 20000.0 * std::pow (80.0 / 20000.0, a);
    const double x  = std::exp (-2.0 * 3.14159265358979323846 * fc / std::max (1.0, sampleRate));

    return std::clamp (x, 0.0, 0.999);
}

// Rueckkopplungsfaktor, der eine Leitung der Laenge delaySeconds nach
// decaySeconds auf -60 dB bringt: g = 10^(-3 delay / decay).
inline double feedbackForDecay (double delaySeconds, double decaySeconds)
{
    if (decaySeconds <= 0.0)
        return 0.0;

    return std::clamp (std::pow (10.0, -3.0 * delaySeconds / decaySeconds), 0.0, 0.999);
}

// Einpol-Allpass, gebaut als Hochpass minus Tiefpass.
//
// Ein Einpol-Tiefpass und der zugehoerige Hochpass (x - lp) ergeben addiert
// wieder x. Zieht man sie stattdessen VONEINANDER ab, bleibt der Betragsgang
// bei eins, aber die Phase dreht sich: unterhalb der Eckfrequenz laeuft das
// Signal durch, oberhalb kommt es verpolt heraus, und genau an der Eckfrequenz
// steht es in der Mitte, um neunzig Grad gedreht.
//
//   ap = hp - lp = (x - lp) - lp = x - 2 lp
//
// Fuer sich allein hoert man das nicht - der Betragsgang aendert sich ja
// nicht. Hoerbar wird es, sobald mehrere so behandelte Signale ZUSAMMEN-
// kommen: dann loeschen und verstaerken sich ihre Anteile je nach Frequenz
// verschieden, und aus einer Phasendrehung wird ein Klang. Deshalb sitzt der
// Verdreher in OpenAirReverb je Abtastpunkt und nicht einmal am Eingang -
// vierundzwanzig gleich gedrehte Signale klaengen wie eines.
//
// Mehrere Stufen hintereinander drehen weiter, jede um bis zu hundertachtzig
// Grad, und machen das Muster dichter.
class PhaseRotator
{
public:
    void setFrequency (double hz, double sampleRate)
    {
        // TPT-Form: sie bleibt bis dicht unter die Nyquistfrequenz stabil und
        // trifft die Eckfrequenz genau, waehrend die naive Form dort
        // wegdriftet.
        const double f = std::clamp (hz, 10.0, sampleRate * 0.45);
        const double g = std::tan (3.14159265358979323846 * f / sampleRate);

        coeff = (float) (g / (1.0 + g));
    }

    void reset()
    {
        for (auto& s : state)
            s = 0.0f;
    }

    float process (float x)
    {
        float v = x;

        for (auto& z : state)
        {
            const float lp = z + coeff * (v - z);

            // Zustand des TPT-Integrators fortschreiben.
            z = lp + coeff * (v - z);

            // hp - lp, ausgeschrieben: (v - lp) - lp
            v = v - 2.0f * lp;
        }

        return v;
    }

    // Vier Stufen: genug, dass sich das Muster ueber den ganzen Hoerbereich
    // zieht, und wenig genug, dass vierundzwanzig davon noch bezahlbar sind.
    static constexpr int stages = 4;

private:
    float state[stages] {};
    float coeff = 0.5f;
};

// Schroeder-Allpass: streut die Phase, ohne den Betragsgang zu aendern. Er
// macht aus einzelnen Echos eine Flaeche und ist der Grund, warum ein Hall
// nicht nach Flatterecho klingt.
class Allpass
{
public:
    void prepare (int maxLengthSamples) { line.prepare (maxLengthSamples); }
    void reset()                        { line.reset(); }
    void setLength (int samples)        { line.setLength (samples); }
    void setGain (float g)              { gain = std::clamp (g, -0.95f, 0.95f); }

    void setMotion (double depthSamples, double rateHz, double sampleRate, int seed)
    {
        line.setMotion (depthSamples, rateHz, sampleRate, seed);
    }

    int getLength() const { return line.getLength(); }

    float process (float x)
    {
        const float delayed = line.read();
        const float v       = x + gain * delayed;

        line.write (v);

        return delayed - gain * v;
    }

private:
    DelayLine line;
    float     gain = 0.5f;
};

// Wandergeschwindigkeit je Leitung, in Hertz. Bewusst nicht ueberall gleich:
// liefen alle im selben Takt, wanderte nicht der Raum, sondern die Tonhoehe.
inline double motionRateFor (int index)
{
    return 0.19 + 0.061 * (double) (index % 7);
}

// Zueinander teilerfremde Laengen, damit sich die Umlaeufe nicht auf gemeinsame
// Vielfache legen. Genau daher kommt der metallische Ton eines schlecht
// bemessenen Halls: fallen mehrere Leitungen periodisch zusammen, entsteht eine
// Tonhoehe statt einer Flaeche.
//
// Die Werte sind Primzahlen; skaliert wird ueber einen Faktor, das Ergebnis
// bleibt praktisch teilerfremd.
inline int scaledPrime (int prime, double factor, int minSamples, int maxSamples)
{
    const int n = (int) std::lround ((double) prime * factor);
    return std::clamp (n, minSamples, maxSamples);
}

} // namespace reverbparts
