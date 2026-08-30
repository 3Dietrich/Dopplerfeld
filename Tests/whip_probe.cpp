// Messprogramm zu den Doppelhieben im Peitschentest (@dpa 20260830: "es
// kommen garantiert schnelle trigger hintereinander (0-4ms) vom Jitter.
// bug?").
//
// Laedt ein Preset, rendert es und misst die ABSTAENDE zwischen den Schlaegen:
// wie viele Paare liegen unter einer Millisekunde, wie viele bis vier. Dazu
// die Sprungweite des Wacklers, damit man sieht, was ihn ausloest.
//
//   cmake --build build --target whip_probe && build/whip_probe [Preset] [s]

#include "Params.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
// Feine Bloecke: 32 Samples sind 0,67 ms, und genau in dieser Groessenordnung
// liegen die Abstaende, um die es geht. Mit den ueblichen 512 (10,7 ms) waere
// ein Doppelhieb ein einziger Block.
constexpr int    blockSize  = 32;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String name = argc > 1 ? juce::String (argv[1]) : juce::String ("peitschentest");
    const double seconds    = argc > 2 ? std::atof (argv[2]) : 5.0;

    // Was fuer diesen Lauf abgeschaltet wird, damit sich trennen laesst, WER
    // die engen Paare macht: der Wackler, die N-Welle oder der Sprungknall.
    const juce::String off = argc > 3 ? juce::String (argv[3]) : juce::String();

    // Nur EIN Ohr auswerten: im Mono-Mix erschiene der Laufzeitunterschied
    // zwischen den Ohren (bis 0,5 ms bei 17 cm Abstand) selbst als Doppelschlag.
    const int channel = 0;

    const juce::File f = juce::File (DOPPLERFELD_SOURCE_DIR).getChildFile ("presets").getChildFile (name);

    juce::MemoryBlock block;

    if (! f.existsAsFile() || ! f.loadFileAsData (block))
    {
        std::printf ("FEHLT: %s\n", f.getFullPathName().toRawUTF8());
        return 1;
    }

    DopplerfeldProcessor proc;

    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);
    proc.setStateInformation (block.getData(), (int) block.getSize());

    auto set = [&proc] (const juce::String& id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Sperrzeit von der Kommandozeile, damit sich ihre Wirkung im selben
    // Preset vergleichen laesst.
    if (argc > 5)
        set (Params::boomHoldMs, (float) std::atof (argv[5]));

    if (off.contains ("jitter"))   set (Params::srcJitterOn, 0.0f);
    if (off.contains ("nwave"))    set (Params::nWaveOn,     0.0f);
    if (off.contains ("jump"))     set (Params::jumpBoom,    0.0f);

    if (const auto* v = proc.apvts.getRawParameterValue (Params::boomHoldMs))
        std::printf ("(Parameter boomHoldMs steht auf %.0f ms)\n", (double) v->load());

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    FieldSnapshot snap;
    double        lastMach = 0.0;

    std::uint64_t boomsRising = 0, boomsFalling = 0, boomsPairs = 0;

    std::vector<float> out;
    std::vector<double> crossings;   // Sample-Positionen, an denen M_r die Eins kreuzt

    const int blocks = (int) (seconds * sampleRate / blockSize);

    out.reserve ((size_t) (blocks * blockSize));

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        for (int i = 0; i < blockSize; ++i)
            out.push_back (buffer.getSample (channel, i));

        // Groesstes |M_r| ueber alle Direktschall-Wege dieses Blocks.
        proc.fillFieldSnapshot (snap);

        boomsRising  = snap.nWaveRising;
        boomsFalling = snap.nWaveFalling;
        boomsPairs   = snap.nWavePairBirths;

        double worst = 0.0;

        for (int p = 0; p < snap.pathCount; ++p)
            worst = std::max (worst, std::abs (snap.paths[(size_t) p].machRadial));

        if ((worst > 1.0) != (lastMach > 1.0))
            crossings.push_back ((double) b * blockSize);

        lastMach = worst;
    }

    // Schlaege finden. NICHT ueber einzelne Spitzen im Signal: die Halbwellen
    // eines lauten Dauertons liegen selbst 1 bis 2 ms auseinander und saehen
    // dann wie Doppelhiebe aus - genau die Groessenordnung, um die es hier
    // geht. Gesucht ist der TRANSIENT: eine Huellkurve, die innerhalb von
    // zwei Millisekunden sprunghaft steigt.
    float peak = 0.0f;

    for (float v : out)
        peak = std::max (peak, std::abs (v));

    const float threshold = argc > 4 ? (float) std::atof (argv[4]) : 0.15f;

    // Huellkurve: Spitzenwert ueber ein gleitendes halbes Millisekundenfenster.
    const int    window = (int) (0.0005 * sampleRate);
    std::vector<float> env (out.size(), 0.0f);

    for (size_t i = 0; i < out.size(); ++i)
    {
        float m = 0.0f;

        for (int k = 0; k < window && i + (size_t) k < out.size(); ++k)
            m = std::max (m, std::abs (out[i + (size_t) k]));

        env[i] = m;
    }

    const int lookback = (int) (0.002 * sampleRate);

    std::vector<int> hits;

    for (size_t i = (size_t) lookback; i < env.size(); ++i)
    {
        const float before = env[i - (size_t) lookback];

        // Sprung um mindestens das Dreifache und ueber die Schwelle: das ist
        // ein Anschlag und kein weiterlaufender Ton.
        if (env[i] > threshold && env[i] > 3.0f * std::max (0.001f, before))
        {
            if (! hits.empty() && (int) i - hits.back() < (int) (0.0003 * sampleRate))
                continue;

            hits.push_back ((int) i);
        }
    }

    std::printf ("%s, %.1f s, Sperre %.0f ms, linkes Ohr%s%s, Spitze %.3f, Schwelle %.3f\n",
                 name.toRawUTF8(), seconds, argc > 5 ? std::atof (argv[5]) : 0.0,
                 off.isEmpty() ? "" : ", ohne ", off.toRawUTF8(), peak, threshold);
    std::printf ("%d Schlaege gefunden\n", (int) hits.size());
    std::printf ("N-Wellen-Durchgaenge: aufsteigend %llu, absteigend %llu, Kegelankunft %llu\n\n",
                 (unsigned long long) boomsRising, (unsigned long long) boomsFalling,
                 (unsigned long long) boomsPairs);

    struct Bin { double loMs, hiMs; int count; };

    Bin bins[]
    {
        { 0.0,  1.0, 0 }, { 1.0,  2.0, 0 }, { 2.0,  4.0, 0 }, { 4.0,  8.0, 0 },
        { 8.0, 16.0, 0 }, { 16.0, 32.0, 0 }, { 32.0, 1.0e9, 0 }
    };

    for (size_t i = 1; i < hits.size(); ++i)
    {
        const double gapMs = (hits[i] - hits[i - 1]) / sampleRate * 1000.0;

        for (auto& bin : bins)
            if (gapMs >= bin.loMs && gapMs < bin.hiMs)
                ++bin.count;
    }

    std::printf ("Abstand zum vorigen Schlag:\n");

    for (const auto& bin : bins)
    {
        char label[32];

        if (bin.hiMs > 1.0e8)
            std::snprintf (label, sizeof label, "ab %.0f ms", bin.loMs);
        else
            std::snprintf (label, sizeof label, "%.0f - %.0f ms", bin.loMs, bin.hiMs);

        std::printf ("  %-12s %4d  %s\n", label, bin.count, juce::String::repeatedString ("#", juce::jmin (60, bin.count)).toRawUTF8());
    }

    // Woher die Schlaege kommen: ein Knall entsteht, wo M_r die Eins kreuzt -
    // dort laeuft die Kaustik ueber das Ohr. Kreuzt M_r kurz hin und gleich
    // wieder zurueck, gibt es ZWEI Fronten im Abstand weniger Millisekunden.
    std::printf ("\n%d Kreuzungen von M_r = 1 (je %.2f ms Aufloesung)\n",
                 (int) crossings.size(), blockSize / sampleRate * 1000.0);

    {
        int close = 0;

        for (size_t i = 1; i < crossings.size(); ++i)
            if ((crossings[i] - crossings[i - 1]) / sampleRate * 1000.0 < 4.0)
                ++close;

        std::printf ("davon %d im Abstand unter 4 ms zur vorigen\n", close);

        std::printf ("alle Abstaende (ms): ");

        for (size_t i = 1; i < crossings.size(); ++i)
            std::printf ("%.1f ", (crossings[i] - crossings[i - 1]) / sampleRate * 1000.0);

        std::printf ("\n");

        int shownCross = 0;

        for (size_t i = 1; i < crossings.size() && shownCross < 12; ++i)
        {
            const double gapMs = (crossings[i] - crossings[i - 1]) / sampleRate * 1000.0;

            if (gapMs < 4.0)
            {
                std::printf ("  t = %7.3f s   naechste Kreuzung nach %5.2f ms\n",
                             crossings[i - 1] / sampleRate, gapMs);
                ++shownCross;
            }
        }
    }

    // Wie EIN Schlag aussieht: der lauteste, drei Millisekunden davor und
    // danach, in Schritten von 0,1 ms. Daran laesst sich unterscheiden, ob
    // zwei Anschlaege kommen oder ob ein Anschlag zwei Spitzen hat.
    {
        size_t loudest = 0;

        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i]) > std::abs (out[loudest]))
                loudest = i;

        std::printf ("\nForm des lautesten Schlages bei t = %.3f s:\n", loudest / sampleRate);

        const int step = (int) (0.0001 * sampleRate);
        const int span = (int) (0.003 * sampleRate);

        for (int k = -span; k <= span; k += step)
        {
            const long long idx = (long long) loudest + k;

            if (idx < 0 || idx >= (long long) out.size())
                continue;

            const float v = out[(size_t) idx];
            const int   bar = (int) (std::abs (v) / std::max (0.001f, peak) * 40.0f);

            std::printf ("  %+6.2f ms  %+7.3f  %s%s\n", k / sampleRate * 1000.0, v,
                         v < 0 ? "-" : "+",
                         juce::String::repeatedString ("#", juce::jmin (40, bar)).toRawUTF8());
        }
    }

    // Die ersten engen Paare ausschreiben, damit man sieht, wo sie liegen.
    std::printf ("\nEnge Paare (unter 4 ms), die ersten zwanzig:\n");

    int shown = 0;

    for (size_t i = 1; i < hits.size() && shown < 20; ++i)
    {
        const double gapMs = (hits[i] - hits[i - 1]) / sampleRate * 1000.0;

        if (gapMs < 4.0)
        {
            std::printf ("  t = %7.3f s   Abstand %5.2f ms   Pegel %.3f -> %.3f\n",
                         hits[i - 1] / sampleRate, gapMs,
                         std::abs (out[(size_t) hits[i - 1]]), std::abs (out[(size_t) hits[i]]));
            ++shown;
        }
    }

    return 0;
}
