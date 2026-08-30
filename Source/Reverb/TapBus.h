#pragma once

#include "AllpassDiffuser.h"
#include "EarlyReflections.h"
#include "FdnReverb.h"
#include "OpenAirReverb.h"
#include "ReverbParts.h"
#include "SchroederReverb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

// Was an einem Abgriffpunkt haengt: Vorlauf, Hall, Breite, Pegel.
//
// Der Abgriffpunkt selbst ist Physik und liegt in DopplerEngine - ein
// PropagationPath, dessen Empfaenger nicht ein Ohr ist, sondern ein fester
// Punkt im Feld. Was hier ankommt, ist also bereits das Signal, wie es an
// dieser Stelle eintrifft, mit Laufzeit, Abstandsverlust und Doppler. Der Hall
// danach laeuft NICHT noch einmal durch die Physik, und genau das macht ihn
// bezahlbar.
//
// Alle vier Bauarten liegen gleichzeitig bereit. Das kostet RAM, erlaubt aber
// einen Typwechsel im Audiothread ohne Allokation - und ohne den koennte man
// den Typ nicht automatisieren.
//
// Wie viel RAM, entscheidet die Kapazitaet: die Leitungen sind nicht auf den
// groessten einstellbaren Raum bemessen, sondern auf den, der wirklich
// gebraucht wird (reverbparts::capacityFor). Verlangt der Regler mehr, wird
// der Raum bis auf Weiteres geklemmt und der Mehrbedarf in roomShortfall
// gemeldet; das Nachbemessen selbst allokiert und gehoert deshalb in den
// Nachrichtenthread (DopplerfeldProcessor::growTapCapacityIfNeeded).
class TapBus
{
public:
    enum class Type { diffuser = 0, schroeder = 1, fdn = 2, openAir = 3 };

    // Groesste Entfernung, die der Vorlauf abbilden kann. Grosszuegig, weil er
    // nur eine einzelne Monoleitung ist: 5 km kosten bei 96 kHz 5,6 MB, und
    // damit ist jede Feldgroesse abgedeckt, die das Plugin kennt.
    static constexpr double maxPredelayMetres = 5000.0;

    void prepare (double sampleRate, int maxBlock, double capacityMetres)
    {
        sr = sampleRate;

        capacity = reverbparts::capacityFor (capacityMetres);
        shortfall.store (0.0);

        early.prepare (sampleRate, maxBlock, capacity);
        diffuser.prepare (sampleRate, maxBlock, capacity);
        schroeder.prepare (sampleRate, maxBlock, capacity);
        fdn.prepare (sampleRate, maxBlock, capacity);
        openAir.prepare (sampleRate, maxBlock, capacity);

        const int maxDelay = (int) (maxPredelayMetres / reverbparts::soundSpeed * sr) + 2;

        preA.prepare (maxDelay);
        preB.prepare (maxDelay);

        wetL.assign ((size_t) std::max (1, maxBlock), 0.0f);
        wetR.assign ((size_t) std::max (1, maxBlock), 0.0f);
        dry.assign  ((size_t) std::max (1, maxBlock), 0.0f);
        erL.assign  ((size_t) std::max (1, maxBlock), 0.0f);
        erR.assign  ((size_t) std::max (1, maxBlock), 0.0f);
        erSum.assign((size_t) std::max (1, maxBlock), 0.0f);
        chainL.assign ((size_t) std::max (1, maxBlock), 0.0f);
        chainR.assign ((size_t) std::max (1, maxBlock), 0.0f);

        reset();
    }

    void reset()
    {
        early.reset();
        diffuser.reset();
        schroeder.reset();
        fdn.reset();
        openAir.reset();

        preA.reset();
        preB.reset();

        // Die Ueberblendung faellt weg, der Vorlauf steht danach aber auf
        // seiner eingestellten Laenge. Ohne das Nachziehen bliebe die Leitung
        // auf dem Wert stehen, den DelayLine::prepare() gesetzt hat - das ist
        // die volle Kapazitaet, also mehrere Sekunden, und der Hall kaeme nach
        // jedem Zuruecksetzen viel zu spaet.
        preA.setLength (targetLength);
        preB.setLength (targetLength);

        fadePos     = -1;
        currentGain = targetGain;
    }

    void setType (Type t) { type = t; }

    void setRoomSize (double metres)
    {
        // Ueber der Kapazitaet wird geklemmt und der Bedarf gemeldet. Die
        // Bauarten klemmen selbst noch einmal - hier steht es, damit der
        // Mehrbedarf ueberhaupt jemand erfaehrt.
        if (metres > capacity)
            shortfall.store (metres);

        early.setRoomSize (metres);

        for (auto* u : units())
            u->setRoomSize (metres);
    }

    // Groesster Raum, den die Puffer derzeit tragen.
    double roomCapacity() const { return capacity; }

    // Groesster Raum, der seit dem letzten Bemessen verlangt wurde - 0, wenn
    // die Kapazitaet reicht. Wird vom Nachrichtenthread gelesen.
    double roomShortfall() const { return shortfall.load(); }

    // Staerke der fruehen Einzelechos. 0 = reiner Nachhall wie vorher.
    void setEarlyAmount (double amount)
    {
        earlyAmount = (float) std::max (0.0, amount);
        early.setAmount (amount);
    }

    // Nur die Bauart Draussen kennt diese beiden: wie viele Rueckwuerfe die
    // Flaeche liefert und mit welchem Wuerfelbecher sie verteilt sind. Die
    // Raumbauarten haben ihre Leitungszahl fest, dort waere ein Regler dafuer
    // eine Einladung, das Netz zu zerlegen.
    void setEchoCount (int count) { openAir.setEchoCount (count); }
    void setSeed (int seed)       { openAir.setSeed (seed); }

    void setDecaySeconds (double seconds)
    {
        for (auto* u : units())
            u->setDecaySeconds (seconds);
    }

    void setDamping (double amount01)
    {
        early.setDamping (amount01);

        for (auto* u : units())
            u->setDamping (amount01);
    }

    // Staerke der Phasenverdreher im Umlauf. Nur die Bauart Draussen hat
    // welche - die Raumbauarten faerben ueber ihre Leitungslaengen, dort waere
    // ein zusaetzlicher Verdreher nur Matsch.
    void setPhaseAmount (double amount01) { openAir.setPhaseAmount (amount01); }

    // Der Rueckweg vom Abgriffpunkt zum Hoerer, als Laufzeit.
    //
    // Ohne ihn kaeme der Hall gleichzeitig mit dem Direktschall, egal wie weit
    // der Punkt entfernt ist - eine Talwand in 300 m Entfernung wuerde
    // klingen, als klebte sie am Ohr. Mit ihm antwortet sie knapp eine Sekunde
    // spaeter, und genau daran erkennt das Gehoer die Entfernung.
    //
    // Die Laenge wird NICHT gleitend nachgefuehrt. Eine gleitende
    // Verzoegerung ist ein Doppler, und der gehoert hier ausdruecklich nicht
    // hin: der Weg vom Abgriffpunkt zum Hoerer ist eine Abkuerzung, keine
    // zweite Ausbreitungsrechnung. Stattdessen laufen zwei Lesekoepfe, und
    // eine Aenderung wird ueberblendet - kein Klick, keine Tonhoehenaenderung.
    void setPredelayMetres (double metres)
    {
        const double m = std::clamp (metres, 0.0, maxPredelayMetres);
        const int    n = std::max (1, (int) std::lround (m / reverbparts::soundSpeed * sr));

        if (n == targetLength)
            return;

        // Kleine Aenderungen sind nicht der Rede wert und wuerden nur
        // dauernd Ueberblendungen ausloesen, solange ein Regler wandert.
        if (fadePos < 0 && std::abs (n - preA.getLength()) < minStepSamples)
            return;

        targetLength = n;

        if (fadePos < 0)
        {
            preB.setLength (n);
            fadePos = 0;
        }
        else
        {
            // Waehrend einer laufenden Ueberblendung wird nur das Ziel
            // nachgezogen; ein zweiter Fade obendrauf gaebe einen Sprung.
            preB.setLength (n);
        }
    }

    void setGain (double linear) { targetGain = (float) linear; }

    // 0 = mono in der Mitte, 1 = wie der Hall sie liefert, darueber breiter.
    void setWidth (double w) { width = (float) std::max (0.0, w); }

    // Wo der Hall im Stereobild sitzt: -1 ganz links, 0 mittig, +1 ganz rechts.
    //
    // Der Wert kommt aus dem ORT des Abgriffpunkts, nicht aus einem Regler
    // (siehe DopplerEngine::tapPanorama). Ein Punkt links vom Hoerer soll auch
    // von links klingen - das ist die halbe Ortsinformation, und sie kostet
    // nichts. Die andere Haelfte waere die Laufzeit, und die bliebe eine
    // zweite Ausbreitungsrechnung; sie steckt naeherungsweise im Vorlauf.
    //
    // Breite und Panorama wirken beide: das Panorama verschiebt, die Breite
    // spreizt um die verschobene Mitte.
    void setPanorama (double p) { panorama = (float) std::clamp (p, -1.0, 1.0); }

    // Mono rein, ADDIERT auf outL/outR. Der Abgriffpunkt ist eine zusaetzliche
    // Signalquelle fuer den Hoerer, kein Ersatz fuer irgendetwas.
    void processAdd (const float* in, float* outL, float* outR, int numSamples)
    {
        processAdd (in, nullptr, outL, outR, numSamples);
    }

    // Stereo rein - das ist der Fall der Kette, wo eine Hallbauart in die
    // naechste geht (siehe DopplerEngine::setTapChain). Zwei Unterschiede zum
    // Mono-Fall, beide aus der Sache heraus:
    //
    //   - Kein Vorlauf. Er bildet den Weg vom Abgriffpunkt zurueck zum Hoerer
    //     ab; dieser Punkt hat keinen eigenen Ort mehr, sein Weg steckt schon
    //     im Vorgaenger.
    //   - Die fruehen Echos hoeren die Mono-Summe. Sie sind Rueckwuerfe einer
    //     Flaeche, also selbst kein Stereovorgang; der Regler "Energie" bleibt
    //     damit wirksam.
    //
    // Die Bauart selbst bekommt beide Seiten. Genau darum geht es: die zwei
    // Seiten einer Hallbauart sind absichtlich unkorreliert, und eine
    // Mono-Summe loescht sie stellenweise aus.
    void processAdd (const float* inL, const float* inR,
                     float* outL, float* outR, int numSamples)
    {
        const int n = std::min (numSamples, (int) dry.size());

        if (n <= 0)
            return;

        const bool stereoIn = (inR != nullptr);
        const float* in = inL;

        // 1) Vorlauf. Zwei Lesekoepfe, damit eine Laengenaenderung ueberblendet
        //    statt zu springen. In der Kette entfaellt er (s.o.); dort steht
        //    in dry die Mono-Summe der beiden Eingaenge.
        for (int i = 0; stereoIn && i < n; ++i)
            dry[(size_t) i] = 0.5f * (inL[i] + inR[i]);

        for (int i = 0; ! stereoIn && i < n; ++i)
        {
            const float a = preA.read();
            const float b = preB.read();

            preA.write (in[i]);
            preB.write (in[i]);

            if (fadePos < 0)
            {
                dry[(size_t) i] = a;
            }
            else
            {
                const float p = (float) fadePos / (float) fadeLength;

                dry[(size_t) i] = a * (1.0f - p) + b * p;

                if (++fadePos >= fadeLength)
                {
                    // Der neue Kopf wird zum alten; der andere ist ab jetzt
                    // frei fuer die naechste Aenderung.
                    preA.setLength (targetLength);
                    fadePos = -1;
                }
            }
        }

        // 2) Fruehe Einzelechos - ausser bei Draussen, das sie selbst macht.
        //
        //    Die beiden taten dasselbe zur selben Zeit: verstreute Rueckwuerfe
        //    einer Flaeche in den ersten paar hundert Millisekunden. Die
        //    fruehen Echos waren dabei rund zwoelf Dezibel lauter, und die
        //    Bauart verschwand darunter - gemessen unterschieden sich Abkling
        //    0 und 3 Sekunden nur noch um 16 dB unter dem Signal, also gar
        //    nicht mehr hoerbar (@dpa 20260829: "die 4 Beispiele klingen alle
        //    gleich!").
        //
        //    Bei Draussen macht deshalb die Bauart die Arbeit, und der
        //    Energie-Regler hebt stattdessen ihren Pegel. Er tut damit fuer
        //    das Ohr dasselbe wie bei den Raumbauarten - mehr Wucht -, nur
        //    ohne die Doppelung.
        const bool ownsEarly = (type == Type::openAir);

        if (! ownsEarly)
        {
            early.process (dry.data(), erL.data(), erR.data(), erSum.data(), n);

            // Der spaete Hall wird aus dem Direktsignal UND den fruehen Echos
            // gespeist, damit er aus ihnen herauswaechst statt daneben zu
            // stehen.
            for (int i = 0; i < n; ++i)
                dry[(size_t) i] += erSum[(size_t) i];
        }

        // 3) Die Bauart. In der Kette mit beiden Seiten, sonst wie bisher mit
        //    dem einen Signal. Die fruehen Echos stecken in dry und gehen
        //    deshalb auf beide Seiten - sie sind ohnehin dieselben.
        if (stereoIn)
        {
            for (int i = 0; i < n; ++i)
            {
                const float extra = dry[(size_t) i] - 0.5f * (inL[i] + inR[i]);

                chainL[(size_t) i] = inL[i] + extra;
                chainR[(size_t) i] = inR[i] + extra;
            }

            activeUnit()->processStereo (chainL.data(), chainR.data(),
                                         wetL.data(), wetR.data(), n);
        }
        else
        {
            activeUnit()->process (dry.data(), wetL.data(), wetR.data(), n);
        }

        if (ownsEarly)
        {
            // Kein Nullpunkt bei abgedrehtem Regler: die Bauart IST hier der
            // Hall, sie darf nicht verschwinden. Bei 1 steht sie auf ihrem
            // vollen Pegel, darueber schiebt sie.
            const float lift = 0.4f + 0.6f * earlyAmount;

            for (int i = 0; i < n; ++i)
            {
                wetL[(size_t) i] *= lift;
                wetR[(size_t) i] *= lift;
                erL[(size_t) i]   = 0.0f;
                erR[(size_t) i]   = 0.0f;
            }
        }

        // 4) Breite ueber Mitte/Seite und Pegel, in einem Durchgang. Der Pegel
        //    laeuft ueber den Block auf sein Ziel zu, damit ein gezogener
        //    Regler nicht knackst.
        const float step = (targetGain - currentGain) / (float) n;

        for (int i = 0; i < n; ++i)
        {
            currentGain += step;

            const float l = wetL[(size_t) i] + erL[(size_t) i];
            const float r = wetR[(size_t) i] + erR[(size_t) i];

            const float mid  = 0.5f * (l + r);
            const float side = 0.5f * (l - r) * width;

            // Gleiche Leistung links wie rechts: gL^2 + gR^2 bleibt konstant,
            // der Hall wird beim Wandern also nicht lauter oder leiser. Eine
            // lineare Verteilung haette in der Mitte ein hoerbares Loch.
            const float t  = 0.5f * (1.0f + panorama);
            const float gL = std::sqrt (1.0f - t);
            const float gR = std::sqrt (t);

            outL[i] += (mid + side) * gL * 1.41421356f * currentGain;
            outR[i] += (mid - side) * gR * 1.41421356f * currentGain;
        }

        currentGain = targetGain;
    }

    const char* typeName() const { return activeUnit()->name(); }

private:
    ReverbUnit* activeUnit()
    {
        switch (type)
        {
            case Type::schroeder: return &schroeder;
            case Type::fdn:       return &fdn;
            case Type::openAir:   return &openAir;
            case Type::diffuser:
            default:              return &diffuser;
        }
    }

    const ReverbUnit* activeUnit() const { return const_cast<TapBus*> (this)->activeUnit(); }

    // Die Stellwerte gehen an ALLE Bauarten, nicht nur an die aktive: sonst
    // stuende die eben eingeschaltete auf den Werten von vorhin und der
    // Typwechsel klaenge nach Sprung statt nach anderer Bauart.
    std::array<ReverbUnit*, 4> units() { return { &diffuser, &schroeder, &fdn, &openAir }; }

    static constexpr int fadeLength     = 1024;   // rund 21 ms bei 48 kHz
    static constexpr int minStepSamples = 32;

    EarlyReflections early;
    AllpassDiffuser diffuser;
    SchroederReverb schroeder;
    FdnReverb       fdn;
    OpenAirReverb   openAir;

    reverbparts::DelayLine preA, preB;

    std::vector<float> wetL, wetR, dry, erL, erR, erSum;

    // Nur fuer den Stereo-Eingang der Kette: die beiden Seiten samt der
    // fruehen Echos, so wie die Bauart sie bekommt.
    std::vector<float> chainL, chainR;

    Type   type         = Type::fdn;
    double sr           = 48000.0;
    double capacity     = reverbparts::baseCapacityMetres;

    // Audiothread schreibt, Nachrichtenthread liest.
    std::atomic<double> shortfall { 0.0 };
    int    targetLength = 1;
    int    fadePos      = -1;
    float  width        = 1.0f;
    float  earlyAmount  = 1.0f;
    float  panorama     = 0.0f;
    float  currentGain  = 0.0f;
    float  targetGain   = 0.0f;
};
