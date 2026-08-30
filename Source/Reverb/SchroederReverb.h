#pragma once

#include "ReverbParts.h"
#include "ReverbUnit.h"

// Der klassische Aufbau: mehrere rueckgekoppelte Kammfilter parallel, dahinter
// eine Allpasskette.
//
// Die Kaemme erzeugen den Nachhall und seine Dauer, die Allpaesse verwischen
// das Kammraster zu einer Flaeche. Beides zusammen kostet wenig, weil pro
// Sample nur je eine Addition und Multiplikation pro Leitung anfaellt und
// keine Matrix im Spiel ist.
//
// Der Preis dafuer steht im Klang: die Kaemme sind untereinander nicht
// verkoppelt, ihre Echodichte waechst also nicht mit der Zeit an. Bei langen
// Abklingzeiten hoert man das als leichtes Flattern. Wer das nicht will, nimmt
// FdnReverb - und bezahlt dafuer.
class SchroederReverb : public ReverbUnit
{
public:
    static constexpr int combs    = 8;
    static constexpr int allpasses = 4;

    void prepare (double sampleRate, int /*maxBlock*/, double capacityMetres) override
    {
        sr = sampleRate;

        capacity = std::clamp (capacityMetres, reverbparts::minRoomMetres, reverbparts::maxRoomMetres);

        const double maxBase = capacity / reverbparts::soundSpeed * sr;
        const int    maxComb = (int) (maxBase * (2100.0 / 1400.0)) + 2;
        const int    maxAp   = (int) (maxBase * (0.12)) + 2;

        for (int i = 0; i < combs; ++i)
        {
            combL[i].prepare (maxComb);
            combR[i].prepare (maxComb);
        }

        for (int i = 0; i < allpasses; ++i)
        {
            apL[i].prepare (maxAp);
            apR[i].prepare (maxAp);
        }

        update();
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < combs; ++i)
        {
            combL[i].reset();
            combR[i].reset();
            dampL[i].reset();
            dampR[i].reset();
        }

        for (int i = 0; i < allpasses; ++i)
        {
            apL[i].reset();
            apR[i].reset();
        }
    }

    void processStereo (const float* inL, const float* inR,
                        float* outL, float* outR, int numSamples) override
    {
        // Der Eingang wird auf die Zahl der Kaemme normiert, damit die Summe
        // unabhaengig von combs denselben Pegel hat.
        const float inScale = 1.0f / (float) combs;

        for (int n = 0; n < numSamples; ++n)
        {
            // Kaemme und Allpaesse liegen ohnehin doppelt vor, einmal je
            // Seite - sie bekommen jetzt je ihre.
            const float xL = inL[n] * inScale;
            const float xR = inR[n] * inScale;

            float l = 0.0f;
            float r = 0.0f;

            for (int i = 0; i < combs; ++i)
            {
                const float dl = combL[i].read();
                const float dr = combR[i].read();

                l += dl;
                r += dr;

                // Daempfung IM Rueckkopplungsweg, nicht am Ausgang: nur so
                // verliert jede Wiederholung erneut Hoehen, und der Nachhall
                // wird mit der Zeit dunkler statt von Anfang an dumpf.
                combL[i].write (xL + dampL[i].process (dl) * fb);
                combR[i].write (xR + dampR[i].process (dr) * fb);
            }

            for (int i = 0; i < allpasses; ++i)
            {
                l = apL[i].process (l);
                r = apR[i].process (r);
            }

            outL[n] = l;
            outR[n] = r;
        }
    }

    void setRoomSize (double metres) override
    {
        roomMetres = std::clamp (metres, reverbparts::minRoomMetres, capacity);
        update();
    }

    void setDecaySeconds (double seconds) override
    {
        decaySeconds = std::max (0.0, seconds);
        update();
    }

    void setDamping (double amount01) override
    {
        const double c = reverbparts::dampingCoefficient (amount01, sr);

        for (int i = 0; i < combs; ++i)
        {
            dampL[i].setCoefficient (c);
            dampR[i].setCoefficient (c);
        }
    }

    // Die teuerste der drei, gemessen rund das Siebenfache des Diffusors
    // (0,67 % Echtzeit bei 48 kHz). Das ueberrascht, weil die Bauart die
    // einfachste ist - der Grund ist, dass sie fuer Stereo alles doppelt
    // rechnet: sechzehn Leitungen mit sechzehn Filtern statt der acht des
    // FDN. Wer beides gehoert hat, nimmt in aller Regel das FDN, es klingt
    // dichter UND kostet weniger.
    double      relativeCost() const override { return 7.4; }
    const char* name()         const override { return "Schroeder"; }

private:
    void update()
    {
        const double baseSamples = roomMetres / reverbparts::soundSpeed * sr;
        const double factor      = baseSamples / 1400.0;

        for (int i = 0; i < combs; ++i)
        {
            combL[i].setLength (reverbparts::scaledPrime (combPrimes[i],      factor, 8, 1 << 22));
            combR[i].setLength (reverbparts::scaledPrime (combPrimes[i] + 23, factor, 8, 1 << 22));
        }

        for (int i = 0; i < allpasses; ++i)
        {
            apL[i].setLength (reverbparts::scaledPrime (apPrimes[i],     factor, 4, 1 << 20));
            apR[i].setLength (reverbparts::scaledPrime (apPrimes[i] + 7, factor, 4, 1 << 20));
            apL[i].setGain (0.5f);
            apR[i].setGain (0.5f);
        }

        // Ein gemeinsamer Faktor fuer alle Kaemme, bemessen am Mittel der
        // Leitungslaengen (1379 ist der Durchschnitt von combPrimes). Ihn je
        // Kamm einzeln zu rechnen waere genauer, wuerde aber die kurzen
        // Leitungen so weit hochziehen, dass sie den Klang dominieren.
        //
        // Die gemessene Abklingzeit faellt trotzdem etwas kuerzer aus als die
        // eingestellte: die kurzen Kaemme sind vor den langen still, und die
        // T30-Messung sieht diesen frueh steileren Abfall. Das ist der Preis
        // der Bauart und der Grund, warum FdnReverb hier genauer trifft.
        const double meanDelay = (baseSamples * (1379.0 / 1400.0)) / sr;
        fb = (float) reverbparts::feedbackForDecay (meanDelay, decaySeconds);
    }

    // Freeverb-Staffelung, auf Primzahlen gerueckt.
    static constexpr int combPrimes[combs]     { 1117, 1187, 1277, 1361, 1423, 1493, 1559, 1619 };
    static constexpr int apPrimes[allpasses]   { 557, 441, 341, 225 };

    reverbparts::DelayLine     combL[combs], combR[combs];
    reverbparts::DampingFilter dampL[combs], dampR[combs];
    reverbparts::Allpass       apL[allpasses], apR[allpasses];

    double sr           = 48000.0;
    double roomMetres   = 10.0;

    // Groesster Raum, den die Puffer tragen. In prepare() bemessen; darueber
    // hinaus wird der Raumregler geklemmt, bis die Kapazitaet ausserhalb des
    // Audiothreads nachgezogen ist (siehe TapBus::roomShortfall).
    double capacity = reverbparts::baseCapacityMetres;

    double decaySeconds = 1.5;
    float  fb           = 0.8f;
};
