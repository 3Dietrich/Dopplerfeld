#pragma once

#include "ReverbParts.h"
#include "ReverbUnit.h"

// Die billigste Bauart: eine Kette von Allpaessen, ohne Rueckkopplung ueber die
// Kette hinweg.
//
// Sie hat keinen Nachhallschwanz und soll auch keinen haben. Was sie macht, ist
// ein einzelnes Echo in eine kurze Flaeche zu verschmieren - genau das, was
// eine reale Wand mit einer Reflexion tut, die eben nicht spiegelglatt ist.
// Damit ist sie die richtige Wahl fuer einen Abgriffpunkt dicht an einer
// Flaeche, wo ein voller Raumhall falsch klaenge.
//
// setDecaySeconds steuert hier die Streuungsstaerke statt einer Abklingzeit:
// laenger heisst dichter verschmiert, nicht laenger nachklingend. Der
// Wertebereich ist derselbe, damit die Oberflaeche fuer alle Bauarten
// dieselben Regler zeigen kann.
class AllpassDiffuser : public ReverbUnit
{
public:
    static constexpr int stages = 4;

    void prepare (double sampleRate, int /*maxBlock*/) override
    {
        sr = sampleRate;

        // Die Stufen sind kurz: der Diffusor streut, er speichert nicht. Ein
        // Zehntel der Raumlaufzeit reicht dafuer und haelt den Puffer klein.
        const int maxLen = (int) (reverbparts::maxRoomMetres / reverbparts::soundSpeed * sr * 0.12) + 2;

        for (int i = 0; i < stages; ++i)
        {
            left[i].prepare (maxLen);
            right[i].prepare (maxLen);
        }

        updateLengths();
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < stages; ++i)
        {
            left[i].reset();
            right[i].reset();
        }
    }

    void process (const float* in, float* outL, float* outR, int numSamples) override
    {
        for (int n = 0; n < numSamples; ++n)
        {
            float l = in[n];
            float r = in[n];

            for (int i = 0; i < stages; ++i)
            {
                l = left[i].process (l);
                r = right[i].process (r);
            }

            outL[n] = l;
            outR[n] = r;
        }
    }

    void setRoomSize (double metres) override
    {
        roomMetres = std::clamp (metres, 0.5, reverbparts::maxRoomMetres);
        updateLengths();
    }

    void setDecaySeconds (double seconds) override
    {
        // Umgedeutet als Streuungsstaerke, siehe Klassenkommentar. Ueber 0.9
        // faengt eine Allpasskette an, hoerbar zu klingeln.
        const double a = std::clamp (seconds / 4.0, 0.0, 1.0);
        gain = (float) (0.35 + 0.55 * a);

        updateLengths();
    }

    void setDamping (double /*amount01*/) override
    {
        // Ein Allpass ohne Rueckkopplung hat keinen Umlauf, in dem sich eine
        // Daempfung aufsummieren koennte. Ein Tiefpass hier waere ein
        // gewoehnlicher Klangregler und gehoert nicht in den Hall.
    }

    // Bezugswert der Kostenskala: gemessen 0,09 % Echtzeit bei 48 kHz
    // (reverb_probe, Apple Silicon). Vier Allpaesse je Seite, sonst nichts.
    double      relativeCost() const override { return 1.0; }
    const char* name()         const override { return "Diffusor"; }

private:
    void updateLengths()
    {
        const double baseSamples = roomMetres / reverbparts::soundSpeed * sr;

        for (int i = 0; i < stages; ++i)
        {
            const double factor = baseSamples / 1400.0;

            // Links und rechts verschieden lang: daher kommt die Breite. Der
            // Versatz ist klein genug, dass beide Seiten denselben Raum
            // beschreiben, und gross genug, dass sie nicht zusammenfallen.
            left [i].setLength (reverbparts::scaledPrime (primesL[i], factor, 4, 1 << 20));
            right[i].setLength (reverbparts::scaledPrime (primesR[i], factor, 4, 1 << 20));

            left [i].setGain (gain);
            right[i].setGain (gain);
        }
    }

    // Primzahlen, absteigend gestaffelt: die laengste Stufe zuerst streut grob,
    // die kurzen danach fuellen die Luecken. Umgekehrt bliebe die grobe
    // Struktur der ersten Stufe hoerbar.
    static constexpr int primesL[stages] { 149, 89, 53, 31 };
    static constexpr int primesR[stages] { 157, 97, 59, 37 };

    reverbparts::Allpass left[stages];
    reverbparts::Allpass right[stages];

    double sr         = 48000.0;
    double roomMetres = 10.0;
    float  gain       = 0.62f;
};
