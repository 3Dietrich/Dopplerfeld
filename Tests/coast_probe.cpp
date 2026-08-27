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

void run (const char* label, double dragSpeed, int smootherType,
          double tauSeconds = 0.15, double brakeSeconds = 0.0)
{
    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    constexpr double fieldMetres = 1000.0;

    setParam (proc, Params::fieldMetres,   (float) fieldMetres);
    setParam (proc, Params::smootherType,  (float) smootherType);
    setParam (proc, Params::smootherTau,   (float) tauSeconds);
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

    const int dragBlocks  = (int) (1.5 / blockSeconds);
    const int brakeBlocks  = (int) (brakeSeconds / blockSeconds);

    for (int block = 0; block < dragBlocks; ++block)
    {
        // Eine echte Geste endet nicht auf voller Fahrt: die Hand wird
        // langsamer, bevor die Taste losgeht. Genau darin unterscheiden sich
        // die Glaetter - ein traeger haengt beim Abbremsen noch zurueck,
        // waehrend seine eigene Geschwindigkeit schon faellt.
        const int left  = dragBlocks - block;
        const double f  = brakeBlocks > 0 && left <= brakeBlocks
                        ? (double) left / (double) brakeBlocks
                        : 1.0;

        normX += f * dragSpeed * blockSeconds / fieldMetres;
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

    // Wo die Maus beim Loslassen stand, und wo die Quelle stattdessen steht.
    // Genau diese Luecke ist der Punkt (@dpa 20260827: "bei Critically Damped
    // Spring und Slew Limiter bremst es direkt ab dort wo es gerade ist ...
    // koennen wir versuchen wie es ist wenn es (ungefaehr!) bis zum
    // Mouserelease punkt laeuft?").
    const double releaseTargetX = juce::jlimit (0.0, 1.0, normX) * fieldMetres;
    const double atReleaseX     = snap.sourcePos.x;
    const double gapAtRelease   = releaseTargetX - atReleaseX;

    // Loslassen.
    proc.startSourceCoast();

    std::vector<std::pair<double, double>> speeds;   // (Zeit seit Loslassen, m/s)

    double total    = 0.0;
    double stopTime = -1.0;
    double elapsed  = 0.0;

    // Lang genug, dass auch ein traeger Glaetter fertig wird: der Nachlauf
    // uebergibt ihm den Rest des Rueckstands, und bei einer Sekunde
    // Glaettungszeit dauert dessen Anfahrt mehrere Sekunden.
    for (int block = 0; block < 700; ++block)
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

    std::printf ("    Rueckstand beim Loslassen %.2f m | am Ende %+.2f m vom "
                 "Loslasspunkt (%.0f %% aufgeholt)\n",
                 gapAtRelease, last.x - releaseTargetX,
                 std::abs (gapAtRelease) > 1.0e-9
                     ? 100.0 * (last.x - atReleaseX) / gapAtRelease : 0.0);

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

    // Ohne Abbremsen, kurze Glaettung - der Fall, den die Probe bisher hatte.
    run ("glatt, One-Pole",        20.0, 0);
    run ("glatt, Feder",           20.0, 1);
    run ("glatt, Slew Limiter",    20.0, 2);

    // Mit Abbremsen und @dpas Glaettungszeit (rund 1 s): die echte Geste.
    run ("Bremse tau1, One-Pole",  20.0, 0, 1.0, 0.15);
    run ("Bremse tau1, Feder",     20.0, 1, 1.0, 0.15);
    run ("Bremse tau1, Slew",      20.0, 2, 1.0, 0.15);
    run ("Bremse tau1, One-Euro",  20.0, 3, 1.0, 0.15);

    return 0;
}
