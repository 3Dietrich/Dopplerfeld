#pragma once

#include "ReverbParts.h"

#include <algorithm>
#include <cmath>

// Die ersten kraeftigen Einzelechos, bevor der Hall diffus wird.
//
// Sie sind der Grund, warum ein Knall in einem Tal wuchtig klingt und in einem
// Hallgeraet duenn. Ein Nachhallnetz verteilt die Energie eines Impulses auf
// tausende winzige Echos - energetisch bleibt alles erhalten, aber jedes
// einzelne Echo ist unhoerbar leise, und was ankommt, ist ein Teppich. Eine
// reale Talflanke wirft dagegen EIN Echo zurueck, das fast so laut ist wie das
// Original, und davon gibt es eine Handvoll.
//
// Gemessen an einer Dopplerfeld-Aufnahme mit lauter N-Welle: der trockene Knall
// faellt 50 ms nach seiner Spitze um 1,7 dB, hinter einem reinen Nachhallnetz
// um 11 dB. Diese Luecke fuellt diese Stufe.
//
// Technisch eine getappte Verzoegerungsleitung: eine Leitung, viele Lesekoepfe.
// Das kostet fast nichts - ein Lesekopf ist eine Addition, kein zweiter Puffer.
class EarlyReflections
{
public:
    static constexpr int taps = 16;

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;

        // Der spaeteste Tap liegt bei der doppelten Raumlaufzeit.
        const int maxLen = (int) (reverbparts::maxRoomMetres / reverbparts::soundSpeed * sr * 2.2) + 2;

        line.prepare (maxLen);
        update();
        reset();
    }

    void reset() { line.reset(); }

    void setRoomSize (double metres)
    {
        roomMetres = std::clamp (metres, 0.5, reverbparts::maxRoomMetres);
        update();
    }

    // Wie stark die Echos gegenueber dem Original sind. 1 = das erste Echo ist
    // so laut wie das Original, was einer harten, nahen Flanke entspricht.
    void setAmount (double amount01) { amount = (float) std::clamp (amount01, 0.0, 4.0); }

    // Schreibt die Echos nach outL/outR und gibt gleichzeitig ihre Summe
    // zurueck, damit der spaete Hall sie als Eingang nehmen kann. Genau so
    // waechst der Nachhall AUS den frueh Reflexionen heraus, statt daneben zu
    // stehen - der Uebergang ist dann keine Naht.
    void process (const float* in, float* outL, float* outR, float* toLate, int numSamples)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            line.write (in[n]);

            float l = 0.0f;
            float r = 0.0f;

            for (int t = 0; t < taps; ++t)
            {
                const float v = line.readAt (delaySamples[t]) * gain[t] * amount;

                l += v * panL[t];
                r += v * panR[t];
            }

            outL[n]   = l;
            outR[n]   = r;
            toLate[n] = 0.5f * (l + r);
        }
    }

private:
    void update()
    {
        const double baseSamples = roomMetres / reverbparts::soundSpeed * sr;

        for (int t = 0; t < taps; ++t)
        {
            // Die Echos ruecken mit der Zeit zusammen: der Abstand zwischen
            // zwei Reflexionen wird kleiner, je mehr Umwege der Schall schon
            // genommen hat. Ein gleichmaessiges Raster waere ein Kammfilter und
            // klaenge nach Metallrohr.
            const double frac = (double) (t + 1) / (double) taps;
            const double when = baseSamples * (0.08 + 1.9 * frac * frac);

            delaySamples[t] = std::max (1, (int) std::lround (when));

            // Pegel faellt mit dem Weg, wie jeder Schall. Nicht schneller: eine
            // steilere Kurve waere schon nach drei Echos wieder der duenne
            // Teppich, den diese Stufe gerade vermeiden soll.
            const float distance = (float) (delaySamples[0] > 0
                                              ? (double) delaySamples[t] / (double) delaySamples[0]
                                              : 1.0);

            // Vorzeichen wechselt nach einem festen, unregelmaessigen Muster.
            // Gleiche Vorzeichen wuerden sich in den tiefen Frequenzen
            // aufaddieren und einen Bassschlag erzeugen, den es im Original
            // nicht gibt.
            const float sign = ((t * 5 + (t / 3)) % 2 == 0) ? 1.0f : -1.0f;

            gain[t] = sign / std::max (1.0f, distance);

            // Abwechselnd nach links und rechts, mit wachsender Streuung: die
            // ersten Echos kommen aus einer Richtung, die spaeteren von
            // ueberall.
            const float spread = 0.5f + 0.5f * (float) frac;
            const float side   = (t % 2 == 0) ? spread : -spread;

            panL[t] = 0.5f * (1.0f + side);
            panR[t] = 0.5f * (1.0f - side);
        }
    }

    reverbparts::DelayLine line;

    int   delaySamples[taps] {};
    float gain[taps] {};
    float panL[taps] {};
    float panR[taps] {};

    double sr         = 48000.0;
    double roomMetres = 30.0;
    float  amount     = 0.0f;
};
