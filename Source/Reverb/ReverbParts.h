#pragma once

#include <algorithm>
#include <cmath>
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

// Groesster gedachter Raum, in Metern. Das ist eine Puffergrenze, kein
// Klangregler: die Verzoegerungsleitungen werden in prepare() auf diese Laenge
// bemessen und danach nie wieder allokiert.
//
// Der Preis steht in RAM, und er ist der einzige Punkt am ganzen Hall, an dem
// RAM ueberhaupt zaehlt. Ein Abgriffpunkt haelt alle drei Bauarten gleichzeitig
// bereit, damit ein Typwechsel im Audiothread nichts allokiert; das sind bei
// 200 m und 48 kHz rund 4,4 MB, bei acht Abgriffpunkten also gut 35 MB.
// Bei 500 m waeren es 90 MB, und dafuer ist der Gewinn zu klein: 200 m
// Raumgroesse sind bereits eine Grundlaufzeit von 583 ms.
//
// Die ENTFERNUNG eines Abgriffpunkts ist davon nicht betroffen. Sie steckt im
// Vorlauf des TapBus, und der ist eine einzelne Monoleitung - die darf
// kilometerweit reichen, ohne dass es auffaellt.
inline constexpr double maxRoomMetres = 200.0;

// Ringpuffer mit ganzzahliger Verzoegerung.
//
// Ganzzahlig genuegt, weil im Hall keine Tonhoehe an der Verzoegerung haengt -
// anders als beim Doppler-Pfad, wo Bruchteile eines Samples den Ton verstimmen.
// Eine Interpolation waere hier reiner Aufwand und wuerde obendrein Hoehen
// verschlucken.
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
    }

    // Laenge in Samples, auf die Kapazitaet begrenzt. Der Puffer wird dabei
    // NICHT geleert: eine Laengenaenderung im Betrieb soll den Nachhall
    // umfaerben, nicht abschneiden.
    void setLength (int samples)
    {
        length = std::clamp (samples, 1, capacity);
    }

    int getLength() const { return length; }

    float read() const
    {
        int p = writePos - length;
        if (p < 0)
            p += capacity;

        return buffer[(size_t) p];
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
// Die Eckfrequenz laeuft geometrisch von 20 kHz (offen) auf 300 Hz (zu). Ein
// linearer Verlauf waere in der oberen Haelfte des Reglers fast wirkungslos,
// weil das Gehoer Frequenzen logarithmisch nimmt.
inline double dampingCoefficient (double amount01, double sampleRate)
{
    const double a  = std::clamp (amount01, 0.0, 1.0);
    const double fc = 20000.0 * std::pow (300.0 / 20000.0, a);
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
