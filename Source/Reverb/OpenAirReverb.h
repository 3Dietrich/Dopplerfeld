#pragma once

#include "ReverbParts.h"
#include "ReverbUnit.h"

#include <cmath>

// Draussen: einzelne, klar getrennte Rueckwuerfe statt eines Nachhalls.
//
// Der Unterschied zu den drei anderen Bauarten steckt nicht im Klangregler,
// sondern im Aufbau, und er hat einen physikalischen Grund. In einem Raum
// treffen die Wellen immer wieder auf Waende, die Zahl der Wege verdoppelt
// sich mit jeder Reflexion, und die Echodichte WAECHST - nach ein paar hundert
// Millisekunden ist nichts mehr einzeln hoerbar, es ist Hall.
//
// Draussen gibt es diese Vervielfachung nicht. Es gibt eine Handvoll weit
// entfernter Flaechen - eine Bergflanke, eine Hauswand, ein Waldrand - und
// jede wirft genau einmal zurueck. Die Dichte bleibt konstant und niedrig, und
// man hoert bis zuletzt einzelne Antworten statt eines Teppichs.
//
// Deshalb hier: keine Rueckkopplung, keine Diffusion, keine Allpaesse. Nur
// eine getappte Leitung mit wenigen, weit gestreuten Lesekoepfen.
//
// Was stattdessen dazukommt, ist die Luftdaempfung. Ueber hunderte Meter
// schluckt Luft die Hoehen messbar - bei einem Kilometer liegen 10 kHz rund
// vierzig Dezibel unter dem Tiefton. Drinnen ist der Effekt zu klein, um ihn
// zu bauen; draussen ist er der Grund, warum ein fernes Echo dumpf
// zurueckkommt und ein nahes hell.
class OpenAirReverb : public ReverbUnit
{
public:
    static constexpr int taps = 12;

    void prepare (double sampleRate, int /*maxBlock*/) override
    {
        sr = sampleRate;

        // Bis zu sechzig Sekunden Streuung: draussen sind die Wege lang, und
        // ein Echo nach zehn Sekunden ist in den Bergen nichts Besonderes.
        line.prepare ((int) (maxSpreadSeconds * sr) + 2);

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

    // Entfernung der naechsten reflektierenden Flaeche. Sie bestimmt, wann die
    // erste Antwort kommt.
    void setRoomSize (double metres) override
    {
        roomMetres = std::clamp (metres, 0.5, reverbparts::maxRoomMetres);
        update();
    }

    // Wie weit die Antworten zeitlich reichen. Anders als bei den
    // rueckgekoppelten Bauarten ist das keine Abklingzeit, sondern eine
    // Spreizung: das letzte Echo liegt dort, danach ist nichts mehr. Ein
    // Ausklang, der sich totlaeuft, waere wieder ein Saal.
    void setDecaySeconds (double seconds) override
    {
        spreadSeconds = std::clamp (seconds, 0.05, maxSpreadSeconds);
        update();
    }

    void setDamping (double amount01) override
    {
        damping = std::clamp (amount01, 0.0, 1.0);
        update();
    }

    double      relativeCost() const override { return 1.5; }
    const char* name()         const override { return "Draussen"; }

private:
    static constexpr double maxSpreadSeconds = 60.0;

    void update()
    {
        const double firstSec = roomMetres * 2.0 / reverbparts::soundSpeed;

        for (int t = 0; t < taps; ++t)
        {
            const double frac = (double) t / (double) (taps - 1);

            // GLEICHMAESSIG gestreut, nicht verdichtend - das ist der
            // Unterschied zu EarlyReflections, wo die Echos mit der Zeit
            // zusammenruecken. Die Unregelmaessigkeit kommt aus einem festen
            // Versatz je Tap, damit die Antworten nicht als Takt hoerbar
            // werden; gewuerfelt wird nichts, sonst klaenge jedes Laden anders.
            const double jitter = 0.35 * std::sin (7.7 * (double) t + 1.3);
            const double when   = firstSec + (spreadSeconds - firstSec)
                                             * std::clamp (frac + jitter / (double) taps, 0.0, 1.0);

            delaySamples[t] = std::max (1, (int) std::lround (when * sr));

            // Pegel nach dem Weg, also 1/r. Der Bezug ist die erste Antwort:
            // sie ist die lauteste, alles Weitere kommt von weiter her.
            const double relative = std::max (1.0, when / std::max (1.0e-6, firstSec));

            const float sign = ((t * 3 + t / 2) % 2 == 0) ? 1.0f : -1.0f;

            gain[t] = (float) (sign / relative);

            // Luftdaempfung: sie waechst mit dem Weg, nicht mit der Zahl der
            // Reflexionen. Der Regler stellt ein, wie stark die Luft nimmt -
            // bei 0 ist sie trocken und kalt, bei 1 dunstig.
            //
            // Der Weg steckt in "when": ein Echo nach zwei Sekunden ist rund
            // 680 m gelaufen. Der Bezugswert 3 s ist so gewaehlt, dass ein
            // ferner Rueckwurf bei voller Reglerstellung deutlich dumpf, aber
            // nicht tonlos ankommt.
            const double perTap = std::clamp (damping * (when / 3.0), 0.0, 1.0);

            damp[t].setCoefficient (reverbparts::dampingCoefficient (perTap, sr));

            // Jede Antwort kommt aus ihrer eigenen Richtung. Draussen liegen
            // die Flaechen weit auseinander, das Stereobild ist deshalb weit
            // und nicht diffus - eine Flanke ist links, die andere rechts.
            const float side = (float) std::sin (2.4 * (double) t + 0.7);

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

    double sr            = 48000.0;
    double roomMetres    = 60.0;
    double spreadSeconds = 3.0;
    double damping       = 0.35;
};
