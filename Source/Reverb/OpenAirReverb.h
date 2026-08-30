#pragma once

#include "ReverbParts.h"
#include "ReverbUnit.h"

#include <cmath>
#include <cstdint>

// Draussen: eine Flaeche, die man abtastet, und die sich untereinander
// verkoppeln kann.
//
// Ein Abgriffpunkt steht fuer eine Stelle im Gelaende - eine Bergflanke, eine
// Hauswand, einen Waldrand. Was von dort zurueckkommt, ist EIN Rueckwurf,
// verschmiert ueber die Ausdehnung der Flaeche: ihre Mitte antwortet zuerst,
// ihre Raender spaeter, weil sie weiter weg sind. Dass ein Tal mehrere
// Flaechen hat, bleibt Sache der acht Abgriffpunkte, von denen jeder seinen
// eigenen Ort, seine eigene Laufzeit und seine eigene Richtung mitbringt.
//
// Jede Verzoegerung hat hier eine EIGENE Leitung, nicht nur einen Lesekopf auf
// einer gemeinsamen. Das ist die Bedingung dafuer, dass sie sich gegenseitig
// speisen koennen - und genau das macht aus einzelnen Rueckwuerfen einen
// Teppich (@dpa 20260829: "geht es dass nicht jedes Delay auf sich selbst
// gefeedbackt wird, sondern mit jedem anderen, ausser sich selbst? Damit einen
// gleichmaessigen Teppich?").
//
// Die Verkopplung ist eine zyklisch verschobene Householder-Matrix:
//
//   feed[i] = v[(i + shift) % n] - (2/n) * Summe(v)
//
// Jede Leitung bekommt also den Inhalt einer ANDEREN plus einen kleinen Abzug
// von allen. Ihr eigener Anteil daran ist -2/n, bei vierundzwanzig Leitungen
// also ein Zwoelftel - praktisch kein Selbst-Feedback, genau wie gewuenscht.
//
// Warum nicht die naheliegende Form "Durchschnitt aller ausser mir selbst":
// die hat exakt null auf der Diagonale, ist aber nicht energieerhaltend. Ihre
// Eigenwerte sind 1 fuer den Gleichanteil und -1/(n-1) fuer alles andere; nach
// wenigen Umlaeufen traegt jede Leitung dasselbe, und aus dem Teppich wird ein
// einzelner Kammfilter. Die Verschiebung mal Householder ist dagegen das
// Produkt zweier orthogonaler Matrizen und damit selbst orthogonal - sie
// erhaelt die Energie und laesst alle Moden am Leben.
class OpenAirReverb : public ReverbUnit
{
public:
    // Obergrenze der Leitungen. Die tatsaechliche Zahl stellt der Echo-Regler
    // ein; alle liegen dauerhaft bereit, damit ein Drehen daran im Audiothread
    // nichts allokiert.
    static constexpr int maxLines = 48;

    void prepare (double sampleRate, int /*maxBlock*/, double capacityMetres) override
    {
        sr = sampleRate;

        capacity = std::clamp (capacityMetres, reverbparts::minRoomMetres, reverbparts::maxRoomMetres);

        const int longest = (int) (capacity / reverbparts::soundSpeed * sr * 1.1) + 2;

        for (auto& l : line)
            l.prepare (longest);

        update();
        reset();
    }

    void reset() override
    {
        for (auto& l : line)      l.reset();
        for (auto& d : damp)      d.reset();
        for (auto& r : rotator)   r.reset();
        for (auto& s : state)     s = 0.0f;
    }

    void processStereo (const float* inL, const float* inR,
                        float* outL, float* outR, int numSamples) override
    {
        const int n = lines;

        for (int k = 0; k < numSamples; ++k)
        {
            // Was von links kommt, trifft die linken Reflektoren staerker.
            // Ueber Mitte und Seite gerechnet, damit bei Mono (Seite = 0)
            // jede Leitung genau das bekommt, was sie immer bekommen hat.
            const float mid  = 0.5f * (inL[k] + inR[k]);
            const float side = 0.5f * (inL[k] - inR[k]);

            float l   = 0.0f;
            float r   = 0.0f;
            float sum = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                // Daempfung und Phasenverdreher sitzen IM Umlauf, nicht am
                // Ausgang: nur so verliert jede Runde erneut Hoehen und dreht
                // erneut die Phase, und der Teppich wird mit der Zeit dunkler
                // und dichter statt von Anfang an fertig.
                const float v = rotator[(size_t) i].process (
                                    damp[(size_t) i].process (line[(size_t) i].read()));

                state[(size_t) i] = v;
                sum += v;

                l += v * gain[(size_t) i] * panL[(size_t) i];
                r += v * gain[(size_t) i] * panR[(size_t) i];
            }

            const float share = (2.0f / (float) n) * sum;

            for (int i = 0; i < n; ++i)
            {
                const int from = (i + shift) % n;

                // Der Eingang geht in ALLE Leitungen. Beim allerersten
                // Durchlauf tasten sie damit die Flaeche ab - das ist der
                // Rueckwurf, den es auch ohne jede Rueckkopplung gibt.
                const float feed = mid + side * (panL[(size_t) i] - panR[(size_t) i]);

                line[(size_t) i].write (feed * inGain[(size_t) i]
                                        + (state[(size_t) from] - share) * feedback);
            }

            outL[k] = l;
            outR[k] = r;
        }
    }

    // Ausdehnung der Flaeche in Metern: ueber diese Zeit verschmiert ihr
    // Rueckwurf.
    void setRoomSize (double metres) override
    {
        extentMetres = std::clamp (metres, reverbparts::minRoomMetres, capacity);
        update();
    }

    // Oeffnet den Weg der Flaeche zurueck auf sich selbst. Bei null antwortet
    // sie einmal, wie eine freistehende Flanke; aufgedreht sieht sie sich
    // selbst wie in einem Talkessel, und es entstehen Echos der Echos.
    void setDecaySeconds (double seconds) override
    {
        decaySeconds = std::max (0.0, seconds);
        update();
    }

    void setDamping (double amount01) override
    {
        damping = std::clamp (amount01, 0.0, 1.0);
        update();
    }

    // Wie stark die Rueckwuerfe gegeneinander verdreht werden.
    //
    // Frueher hing das am Daempfungsregler mit dran, und damit waren zwei
    // verschiedene Dinge an einem Regler: Hoehen wegnehmen hoert man sofort,
    // die Verdrehung hoert man kaum, sie faerbt aber die Ausloeschungen
    // zwischen den Rueckwuerfen. Wer das eine wollte, bekam das andere dazu.
    void setPhaseAmount (double amount01)
    {
        phaseAmount = std::clamp (amount01, 0.0, 1.0);
        update();
    }

    // Wie viele Rueckwuerfe die Flaeche liefert. Weniger heisst einzeln
    // hoerbare Anschlaege, mehr heisst Flaeche.
    void setEchoCount (int count)
    {
        lines = std::clamp (count, 2, maxLines);
        update();
    }

    // Wuerfelbecher fuer die Verteilung der Rueckwuerfe. Dieselbe Zahl gibt
    // immer dieselbe Flaeche - ohne das klaenge jedes Laden anders und kein
    // Vergleich zweier Durchgaenge waere moeglich.
    void setSeed (int newSeed)
    {
        seed = newSeed;
        update();
    }

    double      relativeCost() const override { return 2.5; }
    const char* name()         const override { return "Draussen"; }

private:
    // Kleiner, schneller Zufall mit festem Anfang. Ein xorshift genuegt: die
    // Zahlen muessen nur ungleichmaessig aussehen, nicht statistisch sauber
    // sein.
    struct Rng
    {
        uint32_t s;

        explicit Rng (uint32_t seed) : s (seed * 2654435761u + 1u) {}

        uint32_t next()
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            return s;
        }

        // 0..1
        double uniform() { return (double) (next() >> 8) / 16777216.0; }
    };

    // Verschiebung, die teilerfremd zur Leitungszahl ist - sonst zerfiele der
    // Kreis in mehrere kleine, und ein Teil der Leitungen sprachen nie
    // miteinander.
    static int coprimeShift (int n)
    {
        auto gcd = [] (int a, int b) { while (b != 0) { const int t = a % b; a = b; b = t; } return a; };

        for (int s = n / 2; s >= 1; --s)
            if (gcd (s, n) == 1)
                return s;

        return 1;
    }

    void update()
    {
        const int    n       = lines;
        const double spanSec = extentMetres / reverbparts::soundSpeed;

        shift = coprimeShift (n);

        Rng rng ((uint32_t) (seed < 0 ? 0 : seed));

        double sumSquares = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double frac = (double) i / (double) std::max (1, n - 1);

            // Gewuerfelt statt gerastert (@dpa: "Derzeit sind die Delaytimes
            // glaub ich zu gleichmaessig, muss durcheinanderer sein"). Der
            // Wurf verschiebt jeden Rueckwurf um bis zu eine halbe Teilung
            // gegen sein Raster - das haelt die Reihenfolge grob erhalten,
            // damit die Flaeche vorn dicht und hinten duenn bleibt, macht die
            // Abstaende aber wirklich ungleich.
            const double jitter = (rng.uniform() - 0.5) * 1.6 / (double) n;
            const double rel    = std::clamp (frac + jitter, 0.0, 1.0);
            const double when   = spanSec * (0.02 + 0.98 * rel);

            const int len = std::max (1, (int) std::lround (when * sr));

            line[(size_t) i].setLength (len);

            if (i == n - 1)
                loopSamples = len;

            // Zum Rand hin leiser: diese Teile der Flaeche stehen schraeger
            // zum Schall und liegen weiter weg.
            const double shape = std::cos (frac * 1.5707963267948966);

            // Druck-Offset (@dpa: "Deswegen soll es ruhig ein bisschen
            // Druck-Offset kriegen. Ich glaube das tut dem Bass gut").
            //
            // Reine Vorzeichenwechsel loeschen den Gleichanteil aus - der Bass
            // verschwindet, und der Rueckweg hatte deshalb frueher fast nichts
            // zu tragen. Ein Uebergewicht von rund zwei Dritteln auf plus
            // laesst genug davon stehen, dass die Flaeche schiebt, ohne dass
            // aus dem Rueckwurf ein einzelner Bassschlag wird.
            const double sign = rng.uniform() < 0.68 ? 1.0 : -1.0;

            gain[(size_t) i]   = (float) (sign * shape);
            inGain[(size_t) i] = (float) shape;

            sumSquares += shape * shape;

            damp[(size_t) i].setCoefficient (reverbparts::dampingCoefficient (damping, sr));

            // Die Eckfrequenz des Verdrehers ist je Leitung VERSCHIEDEN -
            // darauf kommt es an. Gleich gedreht waere gar nicht gedreht, weil
            // sich am Verhaeltnis der Rueckwuerfe zueinander nichts aenderte.
            // Ganz oben (12 kHz) betrifft die Drehung fast nichts von dem, was
            // man hoert; nach unten hin wandert sie durch das ganze Spektrum.
            // Bei voll aufgedrehtem Regler liegt sie unter dem Bass, dann ist
            // ALLES gedreht - das ist die Stellung, in der sich die Rueckwuerfe
            // hoerbar gegenseitig ausloeschen.
            const double baseHz = 12000.0 * std::pow (30.0 / 12000.0, phaseAmount);
            const double spread = 3.2 * (frac - 0.5) * (0.4 + 0.6 * rng.uniform());

            rotator[(size_t) i].setFrequency (baseHz * std::pow (2.0, spread), sr);

            // Streuung im Stereobild, gewuerfelt und zum Rand hin weiter.
            const double side = (rng.uniform() * 2.0 - 1.0) * (0.3 + 0.7 * frac);

            panL[(size_t) i] = (float) (0.5 * (1.0 + side));
            panR[(size_t) i] = (float) (0.5 * (1.0 - side));
        }

        // Auf gleiche Leistung normieren, unabhaengig von der Zahl der
        // Rueckwuerfe. Ohne das waere der Echo-Regler in erster Linie ein
        // Lautstaerkeregler, und man koennte die Klangaenderung nicht
        // beurteilen.
        //
        // Der Faktor 1,5 hebt die Bauart auf den Pegel der anderen drei. Mit
        // der frueheren Normierung lag sie 14 dB darunter und ging neben den
        // fruehen Reflexionen schlicht unter - gemessen an einer Aufnahme mit
        // lauter N-Welle unterschieden sich Abkling 0 und 3 s nur noch um
        // 16 dB unter dem Signal, also gar nicht hoerbar.
        const float norm = (float) (1.5 / std::sqrt (std::max (1.0e-9, sumSquares)));

        for (int i = 0; i < n; ++i)
        {
            gain[(size_t) i]   *= norm;
            inGain[(size_t) i] *= norm;
        }

        // Rueckkopplung aus der Umlaufzeit. Die Matrix ist orthogonal, die
        // Schleifenverstaerkung ist damit der Faktor selbst - der Deckel bei
        // 0,93 haelt sie sicher unter eins.
        const double loopSec = std::max (1.0e-4, (double) loopSamples / sr);

        feedback = (float) std::min (0.93,
                       reverbparts::feedbackForDecay (loopSec, decaySeconds));
    }

    reverbparts::DelayLine     line[maxLines];
    reverbparts::DampingFilter damp[maxLines];
    reverbparts::PhaseRotator  rotator[maxLines];

    float state[maxLines]  {};
    float gain[maxLines]   {};
    float inGain[maxLines] {};
    float panL[maxLines]   {};
    float panR[maxLines]   {};

    double sr           = 48000.0;
    double extentMetres = 60.0;

    // Groesster Raum, den die Puffer tragen. In prepare() bemessen; darueber
    // hinaus wird der Raumregler geklemmt, bis die Kapazitaet ausserhalb des
    // Audiothreads nachgezogen ist (siehe TapBus::roomShortfall).
    double capacity = reverbparts::baseCapacityMetres;

    // Staerke der Phasenverdreher, siehe setPhaseAmount.
    double phaseAmount = 0.35;

    double decaySeconds = 0.0;
    double damping      = 0.35;

    int    lines       = 24;
    int    seed        = 137;
    int    shift       = 7;
    int    loopSamples = 1;
    float  feedback    = 0.0f;
};
