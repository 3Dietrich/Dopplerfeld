// Messprogramm zum lauten Ausbruch nach einem Preset-Wechsel (@dpa 20260828:
// "umschalten von 600kmh-Drone@600m² nach drone@1km² kommt immer ein lauter
// Burst").
//
// Gemessen wird blockweise: erst laeuft das erste Preset ein, dann wird das
// zweite geladen, und danach steht fuer jeden Block die Spitze in der Liste.
// So sieht man nicht nur DASS es lauter wird, sondern WANN - der Zeitpunkt
// sagt, woher der Pegel kommt (sofort = Verstaerkungskette, nach der
// Laufzeit = stehengebliebener Signalpuffer).
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target burst_probe && build/burst_probe

#include "Params.h"
#include "Util/Utf8.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

juce::File presetFolder()
{
    return juce::File (DOPPLERFELD_SOURCE_DIR).getChildFile ("presets");
}

bool loadPreset (DopplerfeldProcessor& proc, const juce::String& name)
{
    const juce::File f = presetFolder().getChildFile (name);

    juce::MemoryBlock block;

    if (! f.existsAsFile() || ! f.loadFileAsData (block))
    {
        std::printf ("  FEHLT: %s\n", f.getFullPathName().toRawUTF8());
        return false;
    }

    proc.setStateInformation (block.getData(), (int) block.getSize());
    return true;
}

// Spitze je Block, in der Reihenfolge der Zeit.
std::vector<double> renderBlocks (DopplerfeldProcessor& proc, double seconds)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    const int blocks = (int) std::ceil (seconds * sampleRate / blockSize);

    std::vector<double> peaks;
    peaks.reserve ((size_t) blocks);

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        double peak = 0.0;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* d = buffer.getReadPointer (ch);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                peak = std::max (peak, std::abs ((double) d[i]));
        }

        peaks.push_back (peak);
    }

    return peaks;
}

// Die lauteste Stelle mit Zeitangabe, plus eine grobe Kurve: je Zehntelsekunde
// eine Spalte, damit der Verlauf in eine Zeile passt.
void report (const char* what, const std::vector<double>& peaks)
{
    const double blockSeconds = (double) blockSize / sampleRate;

    size_t loudest = 0;

    for (size_t i = 0; i < peaks.size(); ++i)
        if (peaks[i] > peaks[loudest])
            loudest = i;

    std::printf ("  %-34s Spitze %.4f bei %.2f s\n",
                 what, peaks[loudest], (double) loudest * blockSeconds);
}

// Verlauf in Zeitfenstern: pro Fenster die Spitze, damit man den Ausbruch
// zeitlich einordnen kann.
void timeline (const std::vector<double>& peaks, double windowSeconds)
{
    const double blockSeconds = (double) blockSize / sampleRate;
    const size_t perWindow    = std::max ((size_t) 1,
                                          (size_t) std::lround (windowSeconds / blockSeconds));

    std::printf ("    Verlauf (%.2f s je Wert): ", windowSeconds);

    for (size_t i = 0; i < peaks.size(); i += perWindow)
    {
        double peak = 0.0;

        for (size_t j = i; j < std::min (peaks.size(), i + perWindow); ++j)
            peak = std::max (peak, peaks[j]);

        std::printf ("%.3f ", peak);
    }

    std::printf ("\n");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Die Namen tragen ein Hochzwei; utf8() gibt JUCE die Bytes so, wie sie
    // in der Datei stehen (siehe Util/Utf8.h).
    const juce::String first  = Text::utf8 ("600kmh-Drone@600m\xc2\xb2");
    const juce::String second = Text::utf8 ("drone@1km\xc2\xb2");

    // Vergleichsmass: das zweite Preset in einem frischen Processor. So klingt
    // es, wenn nichts von vorher haengengeblieben ist.
    {
        std::printf ("\n=== %s allein (frischer Processor)\n", second.toRawUTF8());

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        if (! loadPreset (proc, second))
            return 1;

        const auto peaks = renderBlocks (proc, 12.0);
        report ("erste 12 s", peaks);
        timeline (peaks, 0.5);
    }

    // Der gemeldete Fall: erst das eine, dann das andere.
    {
        std::printf ("\n=== %s -> %s\n", first.toRawUTF8(), second.toRawUTF8());

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        if (! loadPreset (proc, first))
            return 1;

        const auto before = renderBlocks (proc, 8.0);
        report ("erstes Preset, 8 s", before);

        if (! loadPreset (proc, second))
            return 1;

        const auto after = renderBlocks (proc, 12.0);
        report ("nach dem Wechsel, 12 s", after);
        timeline (after, 0.5);
    }

    // Liegt es am Unterschied der beiden Presets oder am Wechsel als solchem?
    // Dasselbe Preset zweimal geladen aendert keinen einzigen Wert - bleibt
    // der Ausbruch trotzdem, steckt er im Umschaltvorgang.
    for (const juce::String& name : { first, second })
    {
        std::printf ("\n=== %s -> dasselbe nochmal\n", name.toRawUTF8());

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        if (! loadPreset (proc, name))
            return 1;

        report ("eingelaufen, 8 s", renderBlocks (proc, 8.0));

        if (! loadPreset (proc, name))
            return 1;

        const auto again = renderBlocks (proc, 6.0);
        report ("nach dem erneuten Laden", again);
        timeline (again, 0.5);
    }

    // Welcher Teil der Kette traegt den Ausbruch? Jede Zeile schaltet nach dem
    // Laden genau eine Sache ab und misst die ersten Sekunden danach.
    {
        std::printf ("\n=== %s -> %s: woran haengt der Ausbruch\n",
                     first.toRawUTF8(), second.toRawUTF8());

        auto measure = [&] (const char* what, auto&& tweak)
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (! loadPreset (proc, first))
                return;

            renderBlocks (proc, 8.0);

            if (! loadPreset (proc, second))
                return;

            tweak (proc);

            const auto peaks = renderBlocks (proc, 6.0);
            report (what, peaks);
            timeline (peaks, 0.5);
        };

        auto setParam = [] (DopplerfeldProcessor& p, const char* id, float value)
        {
            if (auto* q = p.apvts.getParameter (id))
                q->setValueNotifyingHost (q->convertTo0to1 (value));
        };

        measure ("wie geladen", [] (DopplerfeldProcessor&) {});

        measure ("Ausgangspegel wie im ersten Preset (12 dB)",
                 [&] (DopplerfeldProcessor& p) { setParam (p, Params::outputGain, 12.0f); });

        measure ("ohne N-Welle", [&] (DopplerfeldProcessor& p)
        {
            if (auto* q = p.apvts.getParameter (Params::nWaveOn))
                q->setValueNotifyingHost (0.0f);
        });

        measure ("ohne Waende", [&] (DopplerfeldProcessor& p)
        {
            setParam (p, Params::wall1Gain, 0.0f);
            setParam (p, Params::wall2Gain, 0.0f);
        });

        // Kommt der Ausbruch aus dem neuen Quellsignal oder aus dem, was vom
        // alten noch im Signalpuffer steht? Der Eingang liefert im Testlauf
        // nichts, die Quelle ist damit still - was dann noch zu hoeren ist,
        // stammt aus dem Puffer.
        measure ("Quelle nach dem Wechsel still (Eingang)", [] (DopplerfeldProcessor& p)
        {
            p.selectSourceKind (DopplerfeldProcessor::SourceKind::AudioIn);
        });

        // Der Startknall haengt nicht am N-Wellen-Schalter, sondern an der
        // Sprungmarke - deshalb eine eigene Zeile dafuer.
        measure ("Quelle still, ohne Startknall", [&] (DopplerfeldProcessor& p)
        {
            p.selectSourceKind (DopplerfeldProcessor::SourceKind::AudioIn);
            setParam (p, Params::jumpBoom, 0.0f);
        });

        measure ("Quelle still, ohne Klone", [&] (DopplerfeldProcessor& p)
        {
            p.selectSourceKind (DopplerfeldProcessor::SourceKind::AudioIn);
            setParam (p, Params::cloneTotal, 0.0f);
            setParam (p, Params::cloneRealLevel, 0.0f);
        });
    }

    // Gegenprobe: das erste Preset VOR dem Wechsel still stellen, dann laufen
    // lassen, bis der Signalpuffer nichts Lautes mehr enthaelt, und erst
    // danach umschalten. Bleibt der Ausbruch dann aus, steckt er im Puffer.
    {
        std::printf ("\n=== Puffer vor dem Wechsel geleert\n");

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        if (! loadPreset (proc, first))
            return 1;

        renderBlocks (proc, 8.0);

        proc.selectSourceKind (DopplerfeldProcessor::SourceKind::AudioIn);
        report ("erstes Preset still, 4 s", renderBlocks (proc, 4.0));

        if (! loadPreset (proc, second))
            return 1;

        const auto peaks = renderBlocks (proc, 6.0);
        report ("danach das zweite Preset", peaks);
        timeline (peaks, 0.5);
    }

    // Der zweite Ausbruch: er kommt auch dann, wenn gar nichts Altes im Puffer
    // steht - beim ersten Laden in einem frischen Processor. Fein aufgeloest,
    // damit man seine Dauer sieht, und einmal mit stiller Quelle als Gegenprobe.
    {
        std::printf ("\n=== %s frisch geladen, fein aufgeloest\n", second.toRawUTF8());

        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (! loadPreset (proc, second))
                return 1;

            const auto peaks = renderBlocks (proc, 3.0);
            report ("Motor wie im Preset", peaks);
            timeline (peaks, 0.1);
        }

        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (! loadPreset (proc, second))
                return 1;

            proc.selectSourceKind (DopplerfeldProcessor::SourceKind::AudioIn);

            const auto peaks = renderBlocks (proc, 3.0);
            report ("Quelle still", peaks);
            timeline (peaks, 0.1);
        }
    }

    // Der Impuls ganz am Anfang, samplegenau: bei stiller Quelle und leerem
    // Puffer kann er nur in der Ausbreitung oder der Ausgangsstufe entstehen.
    {
        std::printf ("\n=== %s frisch geladen: erste Samples (Quelle still)\n",
                     second.toRawUTF8());

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        if (! loadPreset (proc, second))
            return 1;

        proc.selectSourceKind (DopplerfeldProcessor::SourceKind::AudioIn);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer         midi;

        for (int b = 0; b < 12; ++b)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);

            const float* d = buffer.getReadPointer (0);

            double peak = 0.0;
            int    at   = 0;

            for (int i = 0; i < blockSize; ++i)
                if (std::abs ((double) d[i]) > peak)
                {
                    peak = std::abs ((double) d[i]);
                    at   = i;
                }

            std::printf ("    Block %2d  Spitze %.4f bei Sample %3d   erste 8: ",
                         b, peak, at);

            for (int i = 0; i < 8; ++i)
                std::printf ("%+.4f ", d[i]);

            std::printf ("\n");
        }
    }

    // Was vom Ausbruch nach dem Leeren des Puffers noch uebrig ist: die ersten
    // Bloecke nach dem Wechsel, blockweise mit der Stelle der Spitze.
    {
        std::printf ("\n=== %s -> %s: erste Bloecke nach dem Wechsel\n",
                     first.toRawUTF8(), second.toRawUTF8());

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        if (! loadPreset (proc, first))
            return 1;

        renderBlocks (proc, 8.0);

        // Der Ruhepegel des ERSTEN Presets unmittelbar davor - nur an ihm
        // laesst sich ablesen, ob das, was nach dem Wechsel noch kommt, seine
        // Ausblende ist oder etwas Neues.
        {
            const auto tail = renderBlocks (proc, 0.2);
            double peak = 0.0;
            for (double v : tail) peak = std::max (peak, v);
            std::printf ("    (erstes Preset kurz davor: Spitze %.4f)\n", peak);
        }

        if (! loadPreset (proc, second))
            return 1;

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer         midi;

        for (int b = 0; b < 10; ++b)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);

            const float* d = buffer.getReadPointer (0);

            double peak = 0.0;
            int    at   = 0;

            for (int i = 0; i < blockSize; ++i)
                if (std::abs ((double) d[i]) > peak)
                {
                    peak = std::abs ((double) d[i]);
                    at   = i;
                }

            std::printf ("    Block %2d (%5.1f ms)  Spitze %.4f bei Sample %3d\n",
                         b, (double) b * blockSize * 1000.0 / sampleRate, peak, at);
        }
    }

    // Derselbe Fall ohne Presetdateien: ein Zustand, der sich nur im
    // Ausgangspegel unterscheidet. Er isoliert den Pegelsprung von allem
    // anderen, was zwei echte Presets trennt.
    {
        std::printf ("\n=== nur der Ausgangspegel springt (0 dB -> 24 dB)\n");

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        auto setParam = [&proc] (const char* id, float value)
        {
            if (auto* q = proc.apvts.getParameter (id))
                q->setValueNotifyingHost (q->convertTo0to1 (value));
        };

        setParam (Params::fieldMetres, 300.0f);
        setParam (Params::limiterOn,   0.0f);
        setParam (Params::outputGain,  0.0f);
        setParam (Params::lisX, 0.5f);
        setParam (Params::lisY, 0.5f);
        setParam (Params::srcX, 0.5f);
        setParam (Params::srcY, 0.2f);

        proc.prepareToPlay (sampleRate, blockSize);

        renderBlocks (proc, 3.0);

        const auto quiet = renderBlocks (proc, 0.5);
        double quietPeak = 0.0;
        for (double v : quiet) quietPeak = std::max (quietPeak, v);

        juce::MemoryBlock louder;
        setParam (Params::outputGain, 24.0f);
        proc.getStateInformation (louder);
        setParam (Params::outputGain, 0.0f);

        renderBlocks (proc, 0.3);

        proc.setStateInformation (louder.getData(), (int) louder.getSize());

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer         midi;

        std::printf ("    vorher Spitze %.4f\n", quietPeak);

        for (int b = 0; b < 40; ++b)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);

            const float* d = buffer.getReadPointer (0);

            double peak = 0.0;

            for (int i = 0; i < blockSize; ++i)
                peak = std::max (peak, std::abs ((double) d[i]));

            if (peak > 0.0001 || b < 4)
                std::printf ("    Block %2d (%5.1f ms)  Spitze %.4f\n",
                             b, (double) b * blockSize * 1000.0 / sampleRate, peak);
        }
    }

    return 0;
}
