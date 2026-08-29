#pragma once

#include "AllpassDiffuser.h"
#include "EarlyReflections.h"
#include "FdnReverb.h"
#include "ReverbParts.h"
#include "SchroederReverb.h"

#include <algorithm>
#include <array>
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
// Alle drei Bauarten liegen gleichzeitig bereit. Das kostet RAM (siehe
// maxRoomMetres), erlaubt aber einen Typwechsel im Audiothread ohne
// Allokation - und ohne den koennte man den Typ nicht automatisieren.
class TapBus
{
public:
    enum class Type { diffuser = 0, schroeder = 1, fdn = 2 };

    // Groesste Entfernung, die der Vorlauf abbilden kann. Grosszuegig, weil er
    // nur eine einzelne Monoleitung ist: 5 km kosten bei 96 kHz 5,6 MB, und
    // damit ist jede Feldgroesse abgedeckt, die das Plugin kennt.
    static constexpr double maxPredelayMetres = 5000.0;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;

        early.prepare (sampleRate, maxBlock);
        diffuser.prepare (sampleRate, maxBlock);
        schroeder.prepare (sampleRate, maxBlock);
        fdn.prepare (sampleRate, maxBlock);

        const int maxDelay = (int) (maxPredelayMetres / reverbparts::soundSpeed * sr) + 2;

        preA.prepare (maxDelay);
        preB.prepare (maxDelay);

        wetL.assign ((size_t) std::max (1, maxBlock), 0.0f);
        wetR.assign ((size_t) std::max (1, maxBlock), 0.0f);
        dry.assign  ((size_t) std::max (1, maxBlock), 0.0f);
        erL.assign  ((size_t) std::max (1, maxBlock), 0.0f);
        erR.assign  ((size_t) std::max (1, maxBlock), 0.0f);
        erSum.assign((size_t) std::max (1, maxBlock), 0.0f);

        reset();
    }

    void reset()
    {
        early.reset();
        diffuser.reset();
        schroeder.reset();
        fdn.reset();

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
        early.setRoomSize (metres);

        for (auto* u : units())
            u->setRoomSize (metres);
    }

    // Staerke der fruehen Einzelechos. 0 = reiner Nachhall wie vorher.
    void setEarlyAmount (double amount) { early.setAmount (amount); }

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

    // Mono rein, ADDIERT auf outL/outR. Der Abgriffpunkt ist eine zusaetzliche
    // Signalquelle fuer den Hoerer, kein Ersatz fuer irgendetwas.
    void processAdd (const float* in, float* outL, float* outR, int numSamples)
    {
        const int n = std::min (numSamples, (int) dry.size());

        if (n <= 0)
            return;

        // 1) Vorlauf. Zwei Lesekoepfe, damit eine Laengenaenderung ueberblendet
        //    statt zu springen.
        for (int i = 0; i < n; ++i)
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

        // 2) Fruehe Einzelechos. Sie gehen sowohl direkt in den Ausgang als
        //    auch in den spaeten Hall - so waechst der Nachhall aus ihnen
        //    heraus, statt als zweite Schicht daneben zu stehen.
        early.process (dry.data(), erL.data(), erR.data(), erSum.data(), n);

        // 3) Spaeter Hall, gespeist aus dem Direktsignal UND den fruehen
        //    Echos. Ohne den Direktanteil verschwaende ein abgedrehter
        //    Frueh-Regler auch den Nachhall.
        for (int i = 0; i < n; ++i)
            dry[(size_t) i] += erSum[(size_t) i];

        activeUnit()->process (dry.data(), wetL.data(), wetR.data(), n);

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

            outL[i] += (mid + side) * currentGain;
            outR[i] += (mid - side) * currentGain;
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
            case Type::diffuser:
            default:              return &diffuser;
        }
    }

    const ReverbUnit* activeUnit() const { return const_cast<TapBus*> (this)->activeUnit(); }

    // Die Stellwerte gehen an ALLE Bauarten, nicht nur an die aktive: sonst
    // stuende die eben eingeschaltete auf den Werten von vorhin und der
    // Typwechsel klaenge nach Sprung statt nach anderer Bauart.
    std::array<ReverbUnit*, 3> units() { return { &diffuser, &schroeder, &fdn }; }

    static constexpr int fadeLength     = 1024;   // rund 21 ms bei 48 kHz
    static constexpr int minStepSamples = 32;

    EarlyReflections early;
    AllpassDiffuser diffuser;
    SchroederReverb schroeder;
    FdnReverb       fdn;

    reverbparts::DelayLine preA, preB;

    std::vector<float> wetL, wetR, dry, erL, erR, erSum;

    Type   type         = Type::fdn;
    double sr           = 48000.0;
    int    targetLength = 1;
    int    fadePos      = -1;
    float  width        = 1.0f;
    float  currentGain  = 0.0f;
    float  targetGain   = 0.0f;
};
