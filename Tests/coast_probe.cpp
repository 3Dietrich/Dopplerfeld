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

void run (const char* label, double dragSpeed, int smootherType)
{
    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    constexpr double fieldMetres = 1000.0;

    setParam (proc, Params::fieldMetres,   (float) fieldMetres);
    setParam (proc, Params::smootherType,  (float) smootherType);
    setParam (proc, Params::srcJitterOn,   0.0f);
    setParam (proc, Params::srcX,          0.2f);
    setParam (proc, Params::srcY,          0.5f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    for (int i = 0; i < 40; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }

    const double blockSeconds = (double) blockSize / sampleRate;

    // Erst ziehen: das Ziel wandert wie unter der Maus, die Quelle folgt ihm
    // geglaettet. Ohne diesen Teil faengt der Nachlauf aus dem Stand an und
    // die Naht, um die es geht, kaeme gar nicht vor.
    double normX = 0.2;

    FieldSnapshot snap;
    Vec3   last    = {};
    double lastNow = 0.0;

    proc.fillFieldSnapshot (snap);
    last    = snap.sourcePos;
    lastNow = snap.now;

    double dragMeasured = 0.0;

    for (int block = 0; block < (int) (1.5 / blockSeconds); ++block)
    {
        normX += dragSpeed * blockSeconds / fieldMetres;
        setParam (proc, Params::srcX, (float) juce::jlimit (0.0, 1.0, normX));

        buffer.clear();
        proc.processBlock (buffer, midi);

        proc.fillFieldSnapshot (snap);

        const double dt = snap.now - lastNow;

        if (dt > 0.0)
        {
            dragMeasured = (snap.sourcePos - last).length() / dt;
            last    = snap.sourcePos;
            lastNow = snap.now;
        }
    }

    // Loslassen.
    proc.startSourceCoast();

    std::vector<std::pair<double, double>> speeds;   // (Zeit seit Loslassen, m/s)

    double total    = 0.0;
    double stopTime = -1.0;
    double elapsed  = 0.0;

    for (int block = 0; block < 200; ++block)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

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

    std::printf ("\n=== %s  (Ziel %.1f m/s, Glaetter %d)\n", label, dragSpeed, smootherType);

    if (speeds.empty())
    {
        std::printf ("    keine Messpunkte\n");
        return;
    }

    // Die Naht: das Tempo im letzten Moment des Ziehens gegen das erste nach
    // dem Loslassen. Ein Sprung hier waere ein Knick in f' - genau der, der
    // nicht sein darf.
    const double firstAfter = speeds.front().second;

    std::printf ("    Tempo beim Ziehen %.2f m/s, erster Wert danach %.2f m/s "
                 "-> Naht %+.1f %%\n",
                 dragMeasured, firstAfter,
                 dragMeasured > 1.0e-9 ? 100.0 * (firstAfter - dragMeasured) / dragMeasured : 0.0);

    std::printf ("    Weg %.2f m, Stillstand nach %.2f s\n",
                 total, stopTime < 0.0 ? (double) speeds.size() * blockSeconds : stopTime);

    std::printf ("    Tempo (m/s) je 0,1 s:");

    double nextMark = 0.0;

    for (const auto& [t, v] : speeds)
    {
        if (t + 1.0e-9 < nextMark)
            continue;

        std::printf (" %.1f", v);
        nextMark += 0.1;

        if (nextMark > 2.0)
            break;
    }

    std::printf ("\n");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    run ("langsam gezogen",        5.0, 1);
    run ("zuegig gezogen",        20.0, 1);
    run ("schnell gezogen",       80.0, 1);
    run ("zuegig, Slew Limiter",  20.0, 2);
    run ("zuegig, One-Pole",      20.0, 0);

    return 0;
}
