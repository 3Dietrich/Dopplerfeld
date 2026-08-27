// Messprogramm zu "manchmal zu leise" (@dpa 20260827: "zu leise und ich kann
// einfach nirgends finden, warum ... Wenn es bei Dir Sound macht schalte
// 'MachChaos' ein und dann nochmal das erste").
//
// Laedt Presets als Dateien - genau so, wie es der Zustandsstreifen tut - und
// misst, was danach herauskommt. Der Punkt ist die REIHENFOLGE: ein Preset
// allein geladen kann still bleiben und nach einem anderen davor toenen. Was
// dabei haengenbleibt, ist ein Zustand, den das Laden nicht zuruecksetzt, und
// den findet man nur, indem man Ketten faehrt statt einzelner Laeufe.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target preset_probe && build/preset_probe

#include "Params.h"
#include "PluginProcessor.h"

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

struct Level
{
    double peak = 0.0;
    double rms  = 0.0;
};

// Rendert Sekunden und liefert Spitze und Effektivwert. Der Anlauf wird
// mitgemessen: gerade um ihn geht es, wenn ein Preset "erst nach Minuten"
// laut wird.
Level render (DopplerfeldProcessor& proc, double seconds)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    Level level;
    double sumSquares = 0.0;
    long long samples = 0;

    const int blocks = (int) std::ceil (seconds * sampleRate / blockSize);

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        const float* l = buffer.getReadPointer (0);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double v = (double) l[i];

            level.peak = std::max (level.peak, std::abs (v));
            sumSquares += v * v;
            ++samples;
        }
    }

    if (samples > 0)
        level.rms = std::sqrt (sumSquares / (double) samples);

    return level;
}

void report (const char* what, const Level& l)
{
    std::printf ("  %-46s Spitze %.5f | RMS %.5f%s\n", what, l.peak, l.rms,
                 l.rms < 1.0e-4 ? "   <-- STILL" : "");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const char* quiet = "mach2.5 vorbei";
    const char* loud  = "MachChaos";

    // 1. Allein geladen, in einem frischen Processor.
    {
        std::printf ("\n=== %s allein\n", quiet);

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        if (! loadPreset (proc, quiet))
            return 1;

        report ("erste 4 s",  render (proc, 4.0));
        report ("naechste 8 s", render (proc, 8.0));
        report ("naechste 30 s", render (proc, 30.0));
    }

    // 2. Dasselbe, aber mit dem anderen Preset davor - @dpas Rezept.
    {
        std::printf ("\n=== %s, danach %s\n", loud, quiet);

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        if (! loadPreset (proc, loud))
            return 1;

        report ("MachChaos, 4 s", render (proc, 4.0));

        if (! loadPreset (proc, quiet))
            return 1;

        report ("danach erste 4 s",  render (proc, 4.0));
        report ("naechste 8 s", render (proc, 8.0));
    }

    // 2b. Woran haengt die Lautstaerke? Die Absenkung waehrend der N-Welle
    //     legt ALLES ausser der Welle selbst still (PropagationPath::
    //     shockDuckAt). Bei einem Flug, der den Kegel immer wieder ueber den
    //     Hoerer streichen laesst, kann daraus Dauerstille zwischen Knallen
    //     werden - genau das Bild "ausser dem Knack ist da nichts".
    {
        std::printf ("\n=== %s: woran haengt der Pegel\n", quiet);

        auto measure = [&] (const char* what, auto&& tweak)
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (! loadPreset (proc, quiet))
                return;

            // Erst einschwingen lassen: bei 6 km Feld braucht der Schall
            // Sekunden bis zum Hoerer.
            render (proc, 6.0);

            tweak (proc);

            report (what, render (proc, 10.0));
        };

        measure ("wie geladen", [] (DopplerfeldProcessor&) {});

        measure ("ohne N-Welle (also ohne Absenkung)", [] (DopplerfeldProcessor& p)
        {
            if (auto* q = p.apvts.getParameter (Params::nWaveOn))
                q->setValueNotifyingHost (0.0f);
        });

        measure ("Fahne aus (-60 dB)", [] (DopplerfeldProcessor& p)
        {
            if (auto* q = p.apvts.getParameter (Params::extraPathGainDb))
                q->setValueNotifyingHost (q->convertTo0to1 (-60.0f));
        });
    }

    // 2c. Die eigentliche Probe: haengt das Ergebnis eines Ladevorgangs davon
    //     ab, was VORHER geladen war? Verglichen wird nicht der Klang,
    //     sondern jeder einzelne Parameterwert - ein Unterschied dort ist die
    //     Ursache, der Klang nur die Folge.
    {
        std::printf ("\n=== Reihenfolge: aendert ein Vorgaenger das Ergebnis?\n");

        juce::Array<juce::File> files;

        for (const auto& e : juce::RangedDirectoryIterator (presetFolder(), false, "*",
                                                            juce::File::findFiles))
            if (! e.getFile().isHidden() && e.getFile().getSize() > 8)
                files.add (e.getFile());

        std::sort (files.begin(), files.end(),
                   [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });

        // Was gar kein Zustand ist, gehoert nicht in diese Pruefung - es wird
        // ja zu Recht nicht geladen. Im Ordner lag eine MP3.
        {
            DopplerfeldProcessor check;
            juce::Array<juce::File> states;

            for (const auto& f : files)
            {
                juce::MemoryBlock block;

                if (f.loadFileAsData (block)
                    && check.stateBlockIsOurs (block.getData(), (int) block.getSize()))
                    states.add (f);
                else
                    std::printf ("  %-42s kein Zustand, uebersprungen\n",
                                 f.getFileName().toRawUTF8());
            }

            files = states;
        }

        // Werte eines Presets, geladen in einen frischen Processor.
        auto valuesAfter = [&] (const juce::Array<juce::File>& sequence)
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            for (const auto& f : sequence)
            {
                juce::MemoryBlock block;

                if (f.loadFileAsData (block))
                    proc.setStateInformation (block.getData(), (int) block.getSize());
            }

            std::vector<std::pair<juce::String, float>> out;

            for (auto* parameter : proc.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                    out.push_back ({ ranged->paramID, ranged->getValue() });

            return out;
        };

        int affected = 0;

        for (const auto& target : files)
        {
            // Als Vorgaenger eines, das moeglichst viel setzt.
            juce::Array<juce::File> before;
            before.add (presetFolder().getChildFile ("Hubschrauber"));
            before.add (target);

            juce::Array<juce::File> alone;
            alone.add (target);

            const auto a = valuesAfter (alone);
            const auto b = valuesAfter (before);

            int differing = 0;

            for (size_t i = 0; i < a.size() && i < b.size(); ++i)
                if (std::abs (a[i].second - b[i].second) > 1.0e-6f)
                    ++differing;

            if (differing > 0)
            {
                ++affected;
                std::printf ("  %-42s %d Parameter haengen am Vorgaenger\n",
                             target.getFileName().toRawUTF8(), differing);
            }
        }

        std::printf ("  %s\n", affected == 0
                     ? "keines: jedes Preset laedt unabhaengig davon, was vorher lief."
                     : "ERBLICH BELASTET - siehe oben.");
    }

    // 3. Alle Presets der Reihe nach, je vier Sekunden. Eine Uebersicht, an
    //    der auffaellt, welche still bleiben - und ob es an ihnen liegt oder
    //    an dem, was vorher geladen war.
    {
        std::printf ("\n=== alle Presets der Reihe nach (je 4 s)\n");

        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        juce::Array<juce::File> files;

        for (const auto& e : juce::RangedDirectoryIterator (presetFolder(), false, "*",
                                                            juce::File::findFiles))
            if (! e.getFile().isHidden() && e.getFile().getSize() > 8)
                files.add (e.getFile());

        std::sort (files.begin(), files.end(),
                   [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });

        for (const auto& f : files)
        {
            juce::MemoryBlock block;

            if (! f.loadFileAsData (block))
                continue;

            proc.setStateInformation (block.getData(), (int) block.getSize());
            report (f.getFileName().toRawUTF8(), render (proc, 4.0));
        }
    }

    std::printf ("\n");
    return 0;
}
