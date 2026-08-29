#pragma once

#include "ReverbParts.h"
#include "ReverbUnit.h"

#include <cmath>

// Draussen: die Antwort EINER Flaeche, nicht die eines ganzen Tals.
//
// Ein Abgriffpunkt steht fuer eine Stelle im Gelaende - eine Bergflanke, eine
// Hauswand, einen Waldrand. Was von dort zurueckkommt, ist EIN Rueckwurf. Dass
// es in einem Tal mehrere gibt, ist die Sache der acht Abgriffpunkte, von denen
// jeder an seinem eigenen Ort sitzt und seine eigene Laufzeit und Richtung
// mitbringt.
//
// Deshalb hat diese Bauart ausdruecklich KEINE Nachechos (@dpa 20260829: "Es
// hat seine eigenen Nachechos, was ungünstig ist, weil das die 8 Reverbs das ja
// eigentlich in 'richtig' machen sollten"). Ein Punkt, der sich selbst weitere
// Rueckwuerfe ausdenkt, macht die Arbeit der anderen sieben noch einmal - nur
// ohne deren Ort, Richtung und Laufzeit, also falsch.
//
// Was bleibt, ist die Streuung der einen Flaeche. Eine Bergflanke ist kein
// Spiegel: sie ist Fels, Geroell, Bewuchs, und sie ist gross. Die Mitte
// antwortet zuerst, die Raender kommen spaeter, weil sie weiter weg sind - eine
// Flanke von hundert Metern Ausdehnung verschmiert den Rueckwurf ueber knapp
// dreihundert Millisekunden. Das ist kein Nachhall, das ist eine einzige
// Antwort mit Tiefe.
//
// Keine Rueckkopplung, keine Allpaesse, keine Wiederholung. Nur eine getappte
// Leitung, deren Lesekoepfe die Flaeche abtasten, und ein Tiefpass fuer das,
// was Luft und Bewuchs von den Hoehen uebrig lassen.

class OpenAirReverb : public ReverbUnit
{
public:
    // Genug Abtastpunkte, dass die Flaeche als Flaeche klingt und nicht als
    // Reihe einzelner Echos. Vierundzwanzig sind der Punkt, ab dem man bei
    // grossen Flaechen keine einzelnen Anschlaege mehr heraushoert - darunter
    // klingt es nach Kamm, darueber wird es nicht mehr besser, nur teurer.
    static constexpr int taps = 24;

    void prepare (double sampleRate, int /*maxBlock*/) override
    {
        sr = sampleRate;

        // Die groesste Flaeche verschmiert ueber ihre eigene Ausdehnung.
        line.prepare ((int) (reverbparts::maxRoomMetres / reverbparts::soundSpeed * sr * 1.2) + 2);

        update();
        reset();
    }

    void reset() override
    {
        line.reset();

        for (auto& d : damp)
            d.reset();
    }

    void process (const float* in, float* outL, float* outR, int numSamples) override
    {
        for (int n = 0; n < numSamples; ++n)
        {
            line.write (in[n]);

            float l = 0.0f;
            float r = 0.0f;

            for (int t = 0; t < taps; ++t)
            {
                const float v = damp[t].process (line.readAt (delaySamples[t])) * gain[t];

                l += v * panL[t];
                r += v * panR[t];
            }

            outL[n] = l;
            outR[n] = r;
        }
    }

    // Ausdehnung der Flaeche in Metern. Sie bestimmt, ueber welche Zeit der
    // Rueckwurf verschmiert: die Raender einer hundert Meter breiten Flanke
    // liegen knapp dreihundert Millisekunden hinter ihrer Mitte.
    void setRoomSize (double metres) override
    {
        extentMetres = std::clamp (metres, 0.5, reverbparts::maxRoomMetres);
        update();
    }

    // Rauigkeit der Flaeche. Bei kleinen Werten antwortet sie fast wie ein
    // Spiegel - ein harter Fels, ein Betonwall -, bei grossen wie Geroell oder
    // dichter Bewuchs, der den Rueckwurf ueber seine ganze Ausdehnung
    // ausschmiert.
    //
    // Es ist ausdruecklich KEINE Abklingzeit. Diese Bauart klingt nicht aus,
    // sie antwortet einmal; der Regler heisst nur so, weil alle Bauarten
    // dieselben vier Regler zeigen.
    void setDecaySeconds (double seconds) override
    {
        roughness = std::clamp (seconds / 6.0, 0.0, 1.0);
        update();
    }

    // Was Luft und Bewuchs von den Hoehen uebrig lassen. Anders als bei den
    // Raumbauarten wirkt es auf ALLE Abtastpunkte gleich stark: der Weg zur
    // Flaeche und zurueck ist fuer sie derselbe, nur die Raender liegen etwas
    // weiter.
    void setDamping (double amount01) override
    {
        damping = std::clamp (amount01, 0.0, 1.0);
        update();
    }

    double      relativeCost() const override { return 2.0; }
    const char* name()         const override { return "Draussen"; }

private:
    void update()
    {
        // Ausdehnung als Laufzeit: der Rand einer Flaeche liegt um ihre
        // halbe Ausdehnung weiter weg, hin und zurueck also um die ganze.
        const double spanSec = extentMetres / reverbparts::soundSpeed;

        for (int t = 0; t < taps; ++t)
        {
            const double frac = (double) t / (double) (taps - 1);

            // Eine glatte Flaeche antwortet fast auf einen Schlag, eine raue
            // ueber ihre ganze Ausdehnung. Die Rauigkeit stellt also ein, wie
            // weit die Abtastpunkte auseinanderruecken.
            //
            // Die Unregelmaessigkeit kommt aus einem festen Versatz je Punkt.
            // Ein gleichmaessiges Raster waere ein Kammfilter und klaenge nach
            // Metallrohr; gewuerfelt wird trotzdem nichts, sonst klaenge jedes
            // Laden anders.
            const double jitter = 0.4 * std::sin (12.9898 * (double) t + 0.7);
            const double when   = spanSec * (0.02 + (0.05 + 0.95 * roughness)
                                                    * std::clamp (frac + jitter / (double) taps, 0.0, 1.0));

            delaySamples[t] = std::max (1, (int) std::lround (when * sr));

            // Zum Rand hin leiser: diese Teile der Flaeche stehen schraeger
            // zum Schall und liegen weiter weg. Ohne dieses Gefaelle klaenge
            // die Flaeche wie ein Rechteckfenster - mit hoerbarem Ende.
            const float shape = (float) std::cos (frac * 1.5707963);

            const float sign = ((t * 7 + t / 3) % 2 == 0) ? 1.0f : -1.0f;

            // Auf die Zahl der Punkte normiert, damit die Rauigkeit den Pegel
            // nicht mitzieht: eine raue Flaeche wirft nicht weniger zurueck als
            // eine glatte, sie verteilt es nur anders.
            gain[t] = sign * shape * (1.6f / (float) taps);

            damp[t].setCoefficient (reverbparts::dampingCoefficient (damping, sr));

            // Die Flaeche hat eine Breite, also kommt ihre Antwort nicht aus
            // einem Punkt. Die Streuung waechst zum Rand hin - dort ist der
            // Winkel zum Hoerer am groessten.
            const float side = (float) (std::sin (2.4 * (double) t + 0.7) * frac);

            panL[t] = 0.5f * (1.0f + side);
            panR[t] = 0.5f * (1.0f - side);
        }
    }

    reverbparts::DelayLine     line;
    reverbparts::DampingFilter damp[taps];

    int   delaySamples[taps] {};
    float gain[taps] {};
    float panL[taps] {};
    float panR[taps] {};

    double sr           = 48000.0;
    double extentMetres = 60.0;
    double roughness    = 0.5;
    double damping      = 0.35;
};
