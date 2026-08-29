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
// Zwei Dinge kommen dazu, die eine reale Flaeche eben auch tut.
//
// Sie DREHT die Phase, und zwar frequenzabhaengig. Ein Tiefpass allein war
// hier zu wenig (@dpa 20260829: "das was Du beschreibst ist aufgrund der
// fehlenden Feedbacks ja nur Lopass. Das war kaum zu hoeren und passt auch
// nicht") - ohne Rueckkopplung summiert sich eine Daempfung ja nirgends auf.
// Ein Phasenverdreher je Abtastpunkt aendert dagegen, wie sich die
// vierundzwanzig Rueckwuerfe gegenseitig ausloeschen und verstaerken, und das
// ist deutlich hoerbar (siehe reverbparts::PhaseRotator).
//
// Und sie kann in sich zuruecksehen. Ein Talkessel, eine Felsnische, ein Hof
// zwischen zwei Waenden - dort trifft der Rueckwurf wieder auf die Flaeche
// selbst. Der Abkling-Regler oeffnet genau diesen Weg (@dpa: "kann man die
// einzel Delays nicht miteinander hallmaessig verknuepfen, so dass noch mehr
// Echos der Echos entstehen"). Bei null bleibt es bei der einen Antwort.

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

        for (auto& r : rotator)
            r.reset();

        loopDamp.reset();
        loopRotator.reset();

        loopState = 0.0f;
    }

    void process (const float* in, float* outL, float* outR, int numSamples) override
    {
        for (int n = 0; n < numSamples; ++n)
        {
            // Was von der letzten Runde zurueckkommt, geht zusammen mit dem
            // Eingang wieder auf die Flaeche. Der Rueckweg laeuft ueber die
            // MONO-Summe: eine Flaeche, die in sich zurueckwirft, hoert sich
            // selbst ja nicht in Stereo.
            line.write (in[n] + loopState * feedback);

            float l = 0.0f;
            float r = 0.0f;

            for (int t = 0; t < taps; ++t)
            {
                float v = damp[t].process (line.readAt (delaySamples[t]));

                // Phasenverdreher NACH der Daempfung: er soll die schon
                // gefilterte Antwort drehen, nicht das rohe Signal.
                v = rotator[t].process (v) * gain[t];

                l += v * panL[t];
                r += v * panR[t];
            }

            // Der Rueckweg hat einen EIGENEN Abgriff und nimmt nicht die Summe
            // der Abtastpunkte. Deren Vorzeichen wechseln, damit sich die
            // Rueckwuerfe nicht zu einem Bassschlag addieren - ihre Summe ist
            // deshalb fast null, und eine Rueckkopplung darueber waere
            // wirkungslos (gemessen: 41 dB zu leise).
            //
            // Stattdessen der Weg ueber die ganze Flaeche, einmal hin und
            // zurueck. Das ist auch die Zeit, mit der die Abklingzeit
            // gerechnet wird, und deshalb stimmt sie.
            loopState = loopRotator.process (loopDamp.process (line.readAt (loopDelay)));

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

    // Jetzt wieder eine echte Abklingzeit: sie oeffnet den Weg der Flaeche
    // zurueck auf sich selbst.
    //
    // Bei null bleibt es bei der einen Antwort - eine freistehende Flanke, die
    // ins Offene wirft. Aufgedreht sieht die Flaeche sich selbst, wie in einem
    // Talkessel oder einer Nische, und es entstehen Echos der Echos. Der
    // Rueckkopplungsfaktor wird dabei aus der mittleren Umlaufzeit gerechnet,
    // damit die eingestellte Zeit ungefaehr auch die gemessene ist.
    void setDecaySeconds (double seconds) override
    {
        decaySeconds = std::max (0.0, seconds);
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

    // Gemessen 0,62 % Echtzeit bei 48 kHz, also gut das Doppelte des
    // Diffusors. Das meiste davon sind die vierundzwanzig Phasenverdreher mit
    // je vier Stufen - sechsundneunzig Filter je Sample, und sie sind den
    // Klang wert.
    double      relativeCost() const override { return 2.3; }
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
            const double when   = spanSec * (0.02 + 0.98
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

            // Die Eckfrequenz des Verdrehers wandert mit dem Regler von ganz
            // oben (kaum Wirkung im Hoerbereich) bis in den Grundtonbereich,
            // und sie ist je Abtastpunkt VERSCHIEDEN - darauf kommt es an.
            // Gleiche Drehung fuer alle waere keine Drehung, weil sich am
            // Verhaeltnis der Rueckwuerfe zueinander nichts aenderte.
            const double spreadOct = 3.2 * (0.35 + 0.65 * frac);
            const double baseHz    = 12000.0 * std::pow (60.0 / 12000.0, damping);

            rotator[t].setFrequency (baseHz * std::pow (2.0, spreadOct * (frac - 0.5)), sr);

            // Die Flaeche hat eine Breite, also kommt ihre Antwort nicht aus
            // einem Punkt. Die Streuung waechst zum Rand hin - dort ist der
            // Winkel zum Hoerer am groessten.
            const float side = (float) (std::sin (2.4 * (double) t + 0.7) * frac);

            panL[t] = 0.5f * (1.0f + side);
            panR[t] = 0.5f * (1.0f - side);
        }

        // Der Umlauf geht ueber die ganze Flaeche. Sein Abgriff sitzt beim
        // spaetesten Abtastpunkt, seine Laenge ist damit dieselbe Zeit, mit der
        // die Abklingzeit gerechnet wird.
        loopDelay = delaySamples[taps - 1];

        const double loopSec = std::max (1.0e-4, (double) loopDelay / sr);

        // Der Deckel bei 0,93 haelt die Schleife sicher unter eins. Anders als
        // beim vorigen Weg ueber die Tap-Summe hat dieser Abgriff Verstaerkung
        // eins, der Faktor ist also die Schleifenverstaerkung selbst - bei 0,93
        // reicht das fuer rund sechzehn Sekunden Ausklang.
        feedback = (float) std::min (0.93,
                       reverbparts::feedbackForDecay (loopSec, decaySeconds));

        // Im Umlauf dieselbe Daempfung wie an den Abtastpunkten, sonst wuerde
        // die Flaeche beim zweiten Durchgang heller antworten als beim ersten.
        loopDamp.setCoefficient (reverbparts::dampingCoefficient (damping, sr));
        loopRotator.setFrequency (12000.0 * std::pow (60.0 / 12000.0, damping), sr);
    }

    reverbparts::DelayLine     line;
    reverbparts::DampingFilter damp[taps];
    reverbparts::PhaseRotator  rotator[taps];

    reverbparts::DampingFilter loopDamp;
    reverbparts::PhaseRotator  loopRotator;

    int   delaySamples[taps] {};
    float gain[taps] {};
    float panL[taps] {};
    float panR[taps] {};

    double sr           = 48000.0;
    double extentMetres  = 60.0;
    double decaySeconds  = 0.0;
    double damping       = 0.35;
    int    loopDelay     = 1;
    float  feedback      = 0.0f;
    float  loopState     = 0.0f;
};
