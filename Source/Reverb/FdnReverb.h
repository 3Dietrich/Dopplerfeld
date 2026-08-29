#pragma once

#include "ReverbParts.h"
#include "ReverbUnit.h"

// Feedback Delay Network: acht Verzoegerungsleitungen, deren Ausgaenge ueber
// eine Hadamard-Matrix wieder auf ALLE Eingaenge verteilt werden.
//
// Genau diese Verkopplung ist der Unterschied zu SchroederReverb. Dort laeuft
// jeder Kamm fuer sich, die Echodichte bleibt konstant und man hoert das
// Raster. Hier speist jede Leitung jede andere, die Zahl der Wege waechst mit
// jedem Umlauf um den Faktor acht - nach wenigen hundert Millisekunden ist der
// Hall dicht, ohne dass eine einzelne Laufzeit heraussticht.
//
// Warum Hadamard und nicht irgendeine Matrix: sie ist orthogonal, also
// energieerhaltend. Damit haengt die Abklingzeit ausschliesslich an den
// Rueckkopplungsfaktoren der Leitungen und ist berechenbar. Eine beliebige
// Matrix koennte Energie erzeugen und das Netz aufschwingen lassen.
//
// Und sie ist billig: die schnelle Hadamard-Transformation braucht bei acht
// Leitungen 24 Additionen statt 64 Multiplikationen, weil alle Eintraege
// bis aufs Vorzeichen gleich sind. Das Netz kostet damit kaum mehr als die
// acht Leitungen selbst.
class FdnReverb : public ReverbUnit
{
public:
    static constexpr int lines = 8;

    void prepare (double sampleRate, int /*maxBlock*/, double capacityMetres) override
    {
        sr = sampleRate;

        capacity = std::clamp (capacityMetres, reverbparts::minRoomMetres, reverbparts::maxRoomMetres);

        const double maxBase = capacity / reverbparts::soundSpeed * sr;
        const int    maxLen  = (int) (maxBase * (2500.0 / 1400.0)) + 2;

        for (int i = 0; i < lines; ++i)
            delay[i].prepare (maxLen);

        for (int i = 0; i < diffusers; ++i)
        {
            inDiffL[i].prepare ((int) (maxBase * 0.1) + 2);
            inDiffR[i].prepare ((int) (maxBase * 0.1) + 2);
        }

        update();
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < lines; ++i)
        {
            delay[i].reset();
            damp[i].reset();
            state[i] = 0.0f;
        }

        for (int i = 0; i < diffusers; ++i)
        {
            inDiffL[i].reset();
            inDiffR[i].reset();
        }
    }

    void process (const float* in, float* outL, float* outR, int numSamples) override
    {
        // Energetisch normiert, nicht arithmetisch: die acht Leitungen sind
        // untereinander unkorreliert, ihre Summe waechst deshalb mit der
        // WURZEL aus der Zahl der Leitungen und nicht mit ihr selbst. Ein
        // Faktor 1/8 waere die richtige Antwort auf acht gleiche Kopien
        // desselben Signals; hier verschenkt er rund 9 dB, und der Hall klingt
        // kraftlos, obwohl das Netz selbst nichts verliert.
        constexpr float inScale = 0.35355339059327373f;   // 1/sqrt(8)

        for (int n = 0; n < numSamples; ++n)
        {
            // Eingangsstreuung vor dem Netz. Ohne sie waeren die ersten
            // Millisekunden acht einzelne Echos statt eines Einsatzes - das
            // Netz braucht ein paar Umlaeufe, bis es von selbst dicht ist.
            float d = in[n];
            for (int i = 0; i < diffusers; ++i)
                d = inDiffL[i].process (d);

            const float x = d * inScale;

            for (int i = 0; i < lines; ++i)
                state[i] = delay[i].read();

            // L und R greifen ALLE acht Leitungen ab, nur mit verschiedenem
            // Vorzeichenmuster. Die billigere Fassung nimmt je vier Leitungen
            // je Seite - sie ist genauso dekorreliert, laesst aber die Haelfte
            // der Energie im Netz liegen, weil jede Seite nur die Haelfte der
            // Leitungen hoert. Das Vorzeichenmuster trennt die Seiten ohne
            // diesen Verlust: beide hoeren alles, nur anders gemischt.
            //
            // Die Muster stehen senkrecht aufeinander (ihr Skalarprodukt ist
            // null). Genau das macht die Seiten unkorreliert, ohne dass sie
            // sich in der Summe ausloeschen.
            const float sumA = state[0] + state[2] + state[5] + state[7];
            const float sumB = state[1] + state[3] + state[4] + state[6];

            outL[n] = (sumA + sumB) * outScale;
            outR[n] = (sumA - sumB) * outScale;

            hadamard8 (state);

            for (int i = 0; i < lines; ++i)
                delay[i].write (x + damp[i].process (state[i]) * fb[i]);
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

        for (int i = 0; i < lines; ++i)
            damp[i].setCoefficient (c);
    }

    // Gemessen rund das Doppelte des Diffusors (0,18 % Echtzeit bei 48 kHz)
    // und damit deutlich billiger als SchroederReverb, obwohl es die
    // aufwendigere Bauart ist: das Netz laeuft einmal und wird nur
    // verschieden abgegriffen, waehrend die Kammfilter-Bauart fuer Stereo
    // alles zweimal rechnet.
    double      relativeCost() const override { return 2.0; }
    const char* name()         const override { return "FDN"; }

private:
    // Schnelle Hadamard-Transformation, an Ort und Stelle. Normiert mit
    // 1/sqrt(8), damit die Matrix orthogonal bleibt und das Netz weder Energie
    // gewinnt noch verliert.
    static void hadamard8 (float* v)
    {
        for (int step = 1; step < lines; step <<= 1)
        {
            for (int i = 0; i < lines; i += step << 1)
            {
                for (int j = i; j < i + step; ++j)
                {
                    const float a = v[j];
                    const float b = v[j + step];

                    v[j]        = a + b;
                    v[j + step] = a - b;
                }
            }
        }

        constexpr float norm = 0.35355339059327373f;   // 1/sqrt(8)

        for (int i = 0; i < lines; ++i)
            v[i] *= norm;
    }

    void update()
    {
        const double baseSamples = roomMetres / reverbparts::soundSpeed * sr;
        const double factor      = baseSamples / 1400.0;

        for (int i = 0; i < lines; ++i)
        {
            const int len = reverbparts::scaledPrime (linePrimes[i], factor, 8, 1 << 22);

            delay[i].setLength (len);

            // Rueckkopplung je Leitung EINZELN aus ihrer eigenen Laenge. Anders
            // als beim Kammfilter-Hall geht das hier auf: weil die Matrix alle
            // Leitungen mischt, klingt keine einzelne fuer sich durch, und nur
            // so klingen kurze und lange Leitungen gleich schnell ab. Mit einem
            // gemeinsamen Faktor blieben die langen Leitungen stehen, waehrend
            // die kurzen schon weg sind - der Hall wuerde zum Schluss hin
            // duenner und tonaler.
            fb[i] = (float) reverbparts::feedbackForDecay ((double) len / sr, decaySeconds);
        }

        for (int i = 0; i < diffusers; ++i)
        {
            inDiffL[i].setLength (reverbparts::scaledPrime (diffPrimes[i], factor, 4, 1 << 20));
            inDiffR[i].setLength (reverbparts::scaledPrime (diffPrimes[i] + 5, factor, 4, 1 << 20));
            inDiffL[i].setGain (0.62f);
            inDiffR[i].setGain (0.62f);
        }
    }

    // Der Ausgang summiert acht unkorrelierte Leitungen, waechst also um den
    // Faktor sqrt(8); die Mitte/Seite-Bildung darueber halbiert nicht, sondern
    // laesst die Energie stehen. Ohne diesen Faktor waere das Netz nach der
    // Umstellung zwar kraeftig, aber ueber der Aussteuerung.
    static constexpr float outScale = 0.35355339059327373f;   // 1/sqrt(8)

    static constexpr int diffusers = 3;

    // Breit gestaffelte Primzahlen: das Verhaeltnis von laengster zu kuerzester
    // Leitung bestimmt, wie schnell das Netz dicht wird. Zu eng beieinander
    // heisst spuerbare Anlaufzeit, zu weit heisst hoerbare Einzelechos am
    // Anfang.
    static constexpr int linePrimes[lines]     { 1117, 1327, 1523, 1741, 1949, 2129, 2311, 2503 };
    static constexpr int diffPrimes[diffusers] { 233, 149, 89 };

    reverbparts::DelayLine     delay[lines];
    reverbparts::DampingFilter damp[lines];
    reverbparts::Allpass       inDiffL[diffusers], inDiffR[diffusers];

    float  state[lines] {};
    float  fb[lines]    {};
    double sr           = 48000.0;
    double roomMetres   = 30.0;

    // Groesster Raum, den die Puffer tragen. In prepare() bemessen; darueber
    // hinaus wird der Raumregler geklemmt, bis die Kapazitaet ausserhalb des
    // Audiothreads nachgezogen ist (siehe TapBus::roomShortfall).
    double capacity = reverbparts::baseCapacityMetres;

    double decaySeconds = 2.0;
};
