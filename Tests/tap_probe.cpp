// Messprogramm zu den Abgriffpunkten: hoert ein Abgriffpunkt wirklich von
// SEINEM Ort, oder haengt sein Hall doch am Hoerer?
//
// Das ist die Frage, an der die ganze Bauweise haengt. Ein gewoehnlicher
// Master-Hall wuerde lauter, wenn die Quelle dem HOERER naeher kommt. Ein
// Abgriffpunkt muss lauter werden, wenn sie dem PUNKT naeher kommt - auch dann,
// wenn sie sich dabei vom Hoerer entfernt.
//
// Der Aufbau trennt beides sauber: Hoerer links, Abgriffpunkt rechts, die
// Quelle wandert von links nach rechts. Der Direktschall muss dabei leiser
// werden, der Hallanteil lauter.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target tap_probe && build/tap_probe

#include "Params.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

// Die Quelle wandert von der Hoererseite zur Seite des Abgriffpunkts.
constexpr float listenerX = 0.15f;
constexpr float startX    = 0.25f;   // nicht auf dem Hoerer: dort ginge 1/r gegen unendlich
constexpr float tapX      = 0.85f;
constexpr int   steps     = 60;
constexpr int   blocksPerStep = 12;

void setParam (DopplerfeldProcessor& proc, const juce::String& id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        std::printf ("  (Parameter %s gibt es nicht)\n", id.toRawUTF8());
}

// Pegelverlauf eines Durchlaufs: je Schritt der Effektivwert des Ausgangs.
std::vector<double> run (bool tapOn)
{
    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    setParam (proc, Params::fieldMetres, 400.0f);
    setParam (proc, Params::lisX, listenerX);
    setParam (proc, Params::lisY, 0.5f);
    setParam (proc, Params::srcY, 0.5f);
    setParam (proc, Params::srcX, startX);

    using namespace Params::TapPart;

    setParam (proc, Params::tapId (0, on),       tapOn ? 1.0f : 0.0f);
    setParam (proc, Params::tapId (0, x),        tapX);
    setParam (proc, Params::tapId (0, y),        0.5f);
    setParam (proc, Params::tapId (0, z),        2.0f);
    setParam (proc, Params::tapId (0, type),     2.0f);    // FDN
    setParam (proc, Params::tapId (0, room),     40.0f);
    setParam (proc, Params::tapId (0, decay),    1.5f);
    setParam (proc, Params::tapId (0, damp),     0.3f);
    setParam (proc, Params::tapId (0, gain),     0.0f);     // 0 dB, damit man ihn sieht
    setParam (proc, Params::tapId (0, width),    1.0f);
    setParam (proc, Params::tapId (0, predelay), 1.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    // Anfahren: Motor, Glaettung und Hall auf Betriebstemperatur.
    for (int i = 0; i < 120; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }

    std::vector<double> levels;

    for (int s = 0; s < steps; ++s)
    {
        const float t = (float) s / (float) (steps - 1);
        setParam (proc, Params::srcX, startX + (tapX - startX) * t);

        double sum   = 0.0;
        int    count = 0;

        for (int b = 0; b < blocksPerStep; ++b)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const float* d = buffer.getReadPointer (ch);

                for (int i = 0; i < blockSize; ++i)
                {
                    sum += (double) d[i] * (double) d[i];
                    ++count;
                }
            }
        }

        levels.push_back (count > 0 ? std::sqrt (sum / (double) count) : 0.0);
    }

    return levels;
}

double toDb (double x) { return x > 1.0e-9 ? 20.0 * std::log10 (x) : -180.0; }

} // namespace

int main()
{
    std::printf ("tap_probe: Hoerer bei x=%.2f, Abgriffpunkt bei x=%.2f, Feld 400 m\n\n",
                 listenerX, tapX);

    const std::vector<double> without = run (false);
    const std::vector<double> with    = run (true);

    std::printf ("  %-8s %10s %10s %12s %12s\n",
                 "Weg", "ohne Punkt", "mit Punkt", "Hallanteil", "Hall/Direkt");
    std::printf ("  %-8s %10s %10s %12s %12s\n",
                 "---", "----------", "---------", "----------", "-----------");

    // Das aussagekraeftige Mass ist nicht der Hallpegel fuer sich, sondern sein
    // Verhaeltnis zum Direktschall. Der Hallpegel allein haengt auch am
    // Motorpegel und an der Einschwingzeit; das Verhaeltnis sagt genau das aus,
    // worum es geht - waechst der Anteil des Punktes, waehrend die Quelle sich
    // ihm naehert und vom Hoerer entfernt?
    std::vector<double> ratio;

    for (size_t i = 0; i < with.size(); ++i)
    {
        const double a = without[i];
        const double b = with[i];

        // Energetisch, weil Hall und Direktschall unkorreliert sind: sie
        // addieren sich in der Leistung, nicht in der Amplitude.
        const double diff = b * b > a * a ? std::sqrt (b * b - a * a) : 0.0;

        ratio.push_back (toDb (diff) - toDb (a));

        if (i % 10 == 0 || i + 1 == with.size())
            std::printf ("  %6.0f %% %9.1f %9.1f %11.1f %11.1f\n",
                         (double) i / (double) (with.size() - 1) * 100.0,
                         toDb (a), toDb (b), toDb (diff), ratio.back());
    }

    // Gemittelt ueber je fuenf Schritte, damit ein einzelner Ausreisser nicht
    // das Urteil traegt. Der Anfang beginnt nach dem Einschwingen: der Hall
    // eines 280 m entfernten Punktes braucht erst einmal seine 0,8 s Vorlauf,
    // bevor er ueberhaupt etwas von sich gibt.
    auto mean = [&] (size_t from, size_t to)
    {
        double sum = 0.0;
        int    n   = 0;

        for (size_t i = from; i < to && i < ratio.size(); ++i)
            if (ratio[i] > -170.0)
            {
                sum += ratio[i];
                ++n;
            }

        return n > 0 ? sum / (double) n : -180.0;
    };

    const double early = mean (10, 15);
    const double late  = mean (ratio.size() - 5, ratio.size());

    std::printf ("\n  Hall gegen Direktschall: am Anfang %.1f dB, am Ende %.1f dB, also %+.1f dB\n",
                 early, late, late - early);
    std::printf ("  Der Direktschall selbst faellt dabei um %.1f dB\n",
                 toDb (without.front()) - toDb (without.back()));

    if (late - early > 10.0)
        std::printf ("\n  Der Abgriffpunkt hoert von seinem eigenen Ort.\n");
    else
        std::printf ("\n  ACHTUNG: der Hallanteil folgt dem Ort des Punktes NICHT.\n");

    return 0;
}
