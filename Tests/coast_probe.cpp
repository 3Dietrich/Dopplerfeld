// Messprogramm zum Nachlauf (@dpa 20260825: "es soll nicht die Kante zum Stopp
// abrunden, sondern speed mit langsamem doppeltem Lopass zur Null").
//
// Laesst die Quelle mit einer Anfangsgeschwindigkeit los und misst, was danach
// in der Bewegungskette ankommt: Geschwindigkeit je Zeitschritt, zurueckgelegter
// Weg und die Dauer bis zum Stillstand. Gemessen wird an der Position, nicht am
// Modell - so steht in der Ausgabe, was der Loeser spaeter auch sieht.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target coast_probe && build/coast_probe

#include "Params.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

void setParam (DopplerfeldProcessor& proc, const char* id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void run (const char* label, double startSpeed, int smootherType)
{
    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    setParam (proc, Params::fieldMetres,   1000.0f);
    setParam (proc, Params::smootherType,  (float) smootherType);
    setParam (proc, Params::srcJitterOn,   0.0f);
    setParam (proc, Params::srcX,          0.5f);
    setParam (proc, Params::srcY,          0.5f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    for (int i = 0; i < 40; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }

    FieldSnapshot before;
    proc.fillFieldSnapshot (before);

    proc.startSourceCoast ({ startSpeed, 0.0, 0.0 });

    // Gemessen wird an der Uhr des Schnappschusses (snap.now), nicht je Block:
    // der Schnappschuss wird nicht in jedem Block neu gefuellt, und ein fester
    // Blocktakt als Zeitbasis ergaebe abwechselnd Nullen und Doppelschritte.
    std::vector<std::pair<double, double>> speeds;   // (Zeit seit Loslassen, m/s)

    Vec3   last     = before.sourcePos;
    double lastNow  = before.now;
    double total    = 0.0;
    double stopTime = -1.0;
    double elapsed  = 0.0;

    const double blockSeconds = (double) blockSize / sampleRate;

    for (int block = 0; block < 200; ++block)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        FieldSnapshot snap;
        proc.fillFieldSnapshot (snap);

        const double dt = snap.now - lastNow;

        if (dt <= 0.0)
            continue;

        const double step  = (snap.sourcePos - last).length();
        const double speed = step / dt;

        elapsed += dt;
        speeds.push_back ({ elapsed, speed });

        total  += step;
        last    = snap.sourcePos;
        lastNow = snap.now;

        if (stopTime < 0.0 && elapsed > 0.05 && speed < 0.05)
            stopTime = elapsed;
    }

    std::printf ("\n=== %s  (Start %.1f m/s, Glaetter %d)\n", label, startSpeed, smootherType);
    std::printf ("    Weg %.2f m (Theorie 2*v0*tau = %.2f m), Stillstand nach %.2f s\n",
                 total, 2.0 * startSpeed * 0.45,
                 stopTime < 0.0 ? (double) speeds.size() * blockSeconds : stopTime);

    // Der Verlauf in Zehntelsekunden: hier zeigt sich, ob die Geschwindigkeit
    // mit einer Kante losfaellt oder weich anfaengt und ausklingt.
    std::printf ("    Tempo (m/s) je 0,1 s:");

    double nextMark = 0.0;

    for (const auto& [t, v] : speeds)
    {
        if (t + 1.0e-9 < nextMark)
            continue;

        std::printf (" %.1f", v);
        nextMark += 0.1;

        if (nextMark > 2.5)
            break;
    }

    std::printf ("\n");

    // Groesster Tempowechsel je Sekunde - eine Kante zeigt sich hier als
    // Ausreisser. Bezogen auf die Anfangsgeschwindigkeit ist der Wert
    // vergleichbar, egal wie schnell losgelassen wurde.
    //
    // Der erste Messpunkt zaehlt nicht mit: hier steht die Quelle vor dem
    // Loslassen still, in Wirklichkeit kommt sie mit genau dieser
    // Geschwindigkeit aus der Mausbewegung. Der Sprung von null auf v0 ist
    // also ein Artefakt des Messaufbaus und keine Kante im Nachlauf.
    double steepest = 0.0;

    for (size_t i = 2; i < speeds.size(); ++i)
    {
        const double dt = speeds[i].first - speeds[i - 1].first;

        if (dt > 0.0)
            steepest = std::max (steepest, std::abs (speeds[i].second - speeds[i - 1].second) / dt);
    }

    std::printf ("    steilster Tempowechsel: %.1f m/s je Sekunde "
                 "(%.0f %% der Anfangsgeschwindigkeit je Sekunde)\n",
                 steepest, startSpeed > 0.0 ? 100.0 * steepest / startSpeed : 0.0);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    run ("langsam losgelassen",  5.0, 1);
    run ("zuegig losgelassen",  20.0, 1);
    run ("schnell losgelassen", 80.0, 1);
    run ("zuegig, Slew Limiter", 20.0, 2);

    return 0;
}
