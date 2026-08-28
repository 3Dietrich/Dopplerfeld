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

#include <chrono>
#include <initializer_list>
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

    // Das ursprungliche Beispiel war "mach2.5 vorbei". Presets kommen und
    // gehen, waehrend @dpa arbeitet - deshalb ein Ersatz, statt hier
    // auszusteigen.
    auto firstThatExists = [] (std::initializer_list<const char*> names) -> const char*
    {
        for (const char* n : names)
            if (presetFolder().getChildFile (n).existsAsFile())
                return n;

        return *names.begin();
    };

    const char* quiet = firstThatExists ({ "mach2.5 vorbei", "woanders Vorbeiflug",
                                           "spacerocket flyby" });
    const char* loud  = firstThatExists ({ "MachChaos", "hektische 30m Mach3" });

    std::printf ("  (leises Beispiel: %s | lautes: %s)\n", quiet, loud);

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

        // Die Gegenrichtung: Fahne GANZ AUF. Sie senkt alle Hoerwege ausser
        // dem juengsten, und im Ueberschall kommt der Motorton nach der
        // Kegelankunft ueberwiegend ueber genau die - steht sie zu, bleibt
        // vom Vorbeiflug nur der Knall.
        measure ("Fahne auf (0 dB)", [] (DopplerfeldProcessor& p)
        {
            if (auto* q = p.apvts.getParameter (Params::extraPathGainDb))
                q->setValueNotifyingHost (q->convertTo0to1 (0.0f));
        });

        measure ("Fahne auf (+12 dB)", [] (DopplerfeldProcessor& p)
        {
            if (auto* q = p.apvts.getParameter (Params::extraPathGainDb))
                q->setValueNotifyingHost (q->convertTo0to1 (12.0f));
        });
    }

    // 2d. Der leise Teil: der Motorton zwischen den Knallen. Gemessen wird er
    //     OHNE N-Welle, sonst deckt sie ihn zu. Jede Zeile aendert genau eine
    //     Sache gegen "wie geladen" - so steht in der Ausgabe, welcher Regler
    //     ihn wirklich hebt und welcher nichts tut.
    {
        std::printf ("\n=== %s: was hebt den Ton ZWISCHEN den Knallen\n", quiet);

        auto motorOnly = [&] (const char* what, auto&& tweak)
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (! loadPreset (proc, quiet))
                return;

            auto set = [&proc] (const char* id, float v)
            {
                if (auto* q = proc.apvts.getParameter (id))
                    q->setValueNotifyingHost (q->convertTo0to1 (v));
            };

            set (Params::nWaveOn, 0.0f);
            render (proc, 6.0);
            tweak (set);
            report (what, render (proc, 10.0));
        };

        motorOnly ("wie geladen, nur ohne N-Welle", [] (auto&) {});
        motorOnly ("Motorpegel +18 dB",       [] (auto& set) { set (Params::engineLevelDb, 30.0f); });
        motorOnly ("Luftdaempfung aus",       [] (auto& set) { set (Params::airAbsorbAmount, 0.0f); });
        motorOnly ("Abstandskurve flach",     [] (auto& set) { set (Params::distanceCurve, -1.0f); });
        motorOnly ("Abstandskurve steil",     [] (auto& set) { set (Params::distanceCurve, 1.0f); });
        motorOnly ("Boom Limit 6 dB",         [] (auto& set) { set (Params::boomLimitDb, 6.0f); });
        motorOnly ("Ausgang +10 dB",          [] (auto& set) { set (Params::outputGain, 25.5f); });
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

    // 2e. Last nach dem Umschalten (@dpa 20260828: "die Umschaltung von
    //     'mach2.5 vorbei' nach 'MachChaos' endet oft im roten CPU Bereich und
    //     es erholt sich kaum, aufgrund der starken Bewegungen in 'mach2.5
    //     vorbei' ... Wenn ich 'Engine Restart'e geht's dann meist").
    //
    //     Verglichen wird dieselbe Last dreimal: MachChaos in einem frischen
    //     Processor, MachChaos nach dem bewegten Vorgaenger, und MachChaos
    //     nach dem Vorgaenger PLUS Engine-Neustart. Bleibt die mittlere Zahl
    //     oben und faellt die dritte zurueck, gehoert die Last einem Zustand,
    //     den das Laden nicht raeumt - und dann steht auch gleich da, was der
    //     Neustart raeumt und das Laden nicht.
    {
        std::printf ("\n=== Last nach dem Umschalten auf %s\n", loud);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer         midi;

        // Zusaetzlich zur Loeserarbeit die Wanduhr: nur sie sagt, ob es fuer
        // den roten Bereich reicht. Sie schwankt auf einem beschaeftigten
        // Rechner, deshalb steht sie NEBEN der Auswertungszahl und nicht
        // statt ihrer.
        const double budgetMicros = 1.0e6 * (double) blockSize / sampleRate;

        double lastWorstPercent = 0.0;
        double lastMeanPercent  = 0.0;

        auto evalsPerBlock = [&] (DopplerfeldProcessor& proc, double seconds)
        {
            const std::uint64_t before = proc.solverEvaluations();
            const int blocks = (int) std::ceil (seconds * sampleRate / blockSize);

            double worst = 0.0, total = 0.0;

            for (int b = 0; b < blocks; ++b)
            {
                buffer.clear();

                const auto t0 = std::chrono::steady_clock::now();
                proc.processBlock (buffer, midi);
                const double micros = std::chrono::duration<double, std::micro> (
                                          std::chrono::steady_clock::now() - t0).count();

                worst  = std::max (worst, micros);
                total += micros;
            }

            lastWorstPercent = 100.0 * worst / budgetMicros;
            lastMeanPercent  = 100.0 * (total / blocks) / budgetMicros;

            return (double) (proc.solverEvaluations() - before) / (double) blocks;
        };

        // Zeitverlauf statt einer Zahl: "erholt sich kaum" ist eine Aussage
        // ueber den VERLAUF, und eine einzelne Mittelung ueber vier Sekunden
        // kann ihn nicht zeigen.
        auto course = [&] (const char* what, bool withPredecessor, bool restart)
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (withPredecessor)
            {
                if (! loadPreset (proc, quiet))
                    return;

                evalsPerBlock (proc, 8.0);
            }

            if (! loadPreset (proc, loud))
                return;

            if (restart)
                proc.requestEngineRestart();

            std::printf ("  %-38s", what);

            for (int sec = 0; sec < 10; ++sec)
                std::printf (" %6.0f", evalsPerBlock (proc, 1.0));

            std::printf ("\n");
        };

        std::printf ("  %-38s %s\n", "", " je Sekunde ab dem Laden, Auswertungen/Block");
        course ("frisch",                     false, false);
        course ("nach 'mach2.5 vorbei'",      true,  false);
        course ("dito + Engine Restart",      true,  true);

        // a) frisch
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (! loadPreset (proc, loud))
                return 1;

            evalsPerBlock (proc, 2.0);   // einschwingen
            const double e = evalsPerBlock (proc, 4.0);
            std::printf ("  %-46s %8.0f Auswertungen je Block | Budget Ø %.0f %%, "
                         "schlechtester Block %.0f %%\n",
                         "frisch geladen", e, lastMeanPercent, lastWorstPercent);
        }

        // b) nach dem bewegten Vorgaenger
        double afterSwitch = 0.0;
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (! loadPreset (proc, quiet))
                return 1;

            evalsPerBlock (proc, 8.0);   // den Vorgaenger wirklich laufen lassen

            if (! loadPreset (proc, loud))
                return 1;

            evalsPerBlock (proc, 2.0);
            afterSwitch = evalsPerBlock (proc, 4.0);

            std::printf ("  %-46s %8.0f Auswertungen je Block | Budget Ø %.0f %%, "
                         "schlechtester Block %.0f %%\n",
                         "nach 'mach2.5 vorbei'", afterSwitch,
                         lastMeanPercent, lastWorstPercent);
        }

        // c) dasselbe, danach Engine-Neustart
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (! loadPreset (proc, quiet))
                return 1;

            evalsPerBlock (proc, 8.0);

            if (! loadPreset (proc, loud))
                return 1;

            evalsPerBlock (proc, 2.0);

            proc.requestEngineRestart();
            evalsPerBlock (proc, 2.0);

            const double e = evalsPerBlock (proc, 4.0);
            std::printf ("  %-46s %8.0f Auswertungen je Block | Budget Ø %.0f %%, "
                         "schlechtester Block %.0f %%\n",
                         "nach 'mach2.5 vorbei' + Engine Restart", e,
                         lastMeanPercent, lastWorstPercent);
        }
    }

    // 2f. Auszeiten bei starken Ausschlaegen (@dpa 20260828: "'spacerocket
    //     flyby' - was laeuft hier verkehrt? Die Ausschlaege produzieren
    //     Auszeiten .. bei jedem Durchgang irgendwie anders.. an anderer
    //     Stelle?").
    //
    //     Zwei Erklaerungen kommen in Frage, und sie sehen von aussen gleich
    //     aus: entweder reisst der Block sein Zeitbudget (dann liegt die
    //     Auszeit jedes Mal woanders, weil die Wanduhr mitspielt), oder der
    //     Loeser verliert die Hoerwege (dann liegt sie IMMER an derselben
    //     Stelle). Deshalb laeuft dasselbe Preset hier mehrfach: bleibt die
    //     Stelle gleich, ist es die Rechnung; wandert sie, ist es die Last.
    {
        const char* rocket = "spacerocket flyby";

        std::printf ("\n=== %s: Auszeiten, drei Durchgaenge\n", rocket);

        const double budgetMicros = 1.0e6 * (double) blockSize / sampleRate;

        for (int run = 0; run < 3; ++run)
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (! loadPreset (proc, rocket))
                break;

            // Der Vorbeiflug wird von Hand gestartet - im Preset steht
            // motionWasPlaying=0, dafuer flyLoop=1. Ohne diesen Anstoss steht
            // die Quelle still und der ganze Lauf ist ein anderer Fall als
            // @dpas.
            {
                juce::AudioBuffer<float> warm (2, blockSize);
                juce::MidiBuffer wm;

                for (int i = 0; i < 40; ++i) { warm.clear(); proc.processBlock (warm, wm); }
            }

            proc.triggerFlyBy();

            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer         midi;

            const int blocks = (int) std::ceil (10.0 * sampleRate / blockSize);

            double worstMicros = 0.0, overBudget = 0.0;
            double silenceRun = 0.0, worstSilence = 0.0, worstSilenceAt = 0.0;
            bool   heard = false;
            bool   everPlayed = false;
            double lastPlayingAt = -1.0;

            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();

                const auto t0 = std::chrono::steady_clock::now();
                proc.processBlock (buf, midi);
                const double micros = std::chrono::duration<double, std::micro> (
                                          std::chrono::steady_clock::now() - t0).count();

                worstMicros = std::max (worstMicros, micros);

                if (micros > budgetMicros)
                    overBudget += 1.0;

                double peak = 0.0;

                for (int i = 0; i < blockSize; ++i)
                    peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));

                const double now = (double) b * blockSize / sampleRate;

                FieldSnapshot live;
                proc.fillFieldSnapshot (live);

                if (live.flyByActive)
                {
                    everPlayed = true;
                    lastPlayingAt = now;
                }

                if (peak > 1.0e-4)
                {
                    // Erst als "gehoert" zaehlen, wenn wirklich Ton da ist -
                    // sonst gilt schon der Anlaufknacks als Anfang, und die
                    // Laufzeit bis zum ersten Ton erscheint als Auszeit.
                    if (peak > 1.0e-3)
                        heard = true;

                    if (heard && silenceRun >= 0.04)
                        std::printf ("  Lauf %d:   Luecke %5.0f ms bei t=%5.2f s\n",
                                     run + 1, silenceRun * 1000.0, now - silenceRun);

                    silenceRun = 0.0;
                }
                else if (heard)
                {
                    silenceRun += (double) blockSize / sampleRate;

                    if (silenceRun > worstSilence)
                    {
                        worstSilence   = silenceRun;
                        worstSilenceAt = now - silenceRun;
                    }
                }
            }

            // Bewegt sich die Quelle ueberhaupt? Ohne laufende Aufzeichnung
            // waere der ganze Lauf ein anderer Fall als @dpas, und die
            // Aussage "keine Auszeiten" waere wertlos.
            FieldSnapshot sn;
            proc.fillFieldSnapshot (sn);

            std::printf ("  Lauf %d: laengste Auszeit %.3f s bei t=%.2f s | schlechtester "
                         "Block %.0f %% des Budgets | %.0f Bloecke drueber | "
                         "Wiedergabe %s, |M_r| %.2f, Zweige %d\n",
                         run + 1, worstSilence, worstSilenceAt,
                         100.0 * worstMicros / budgetMicros, overBudget,
                         everPlayed ? "Flug lief" : "Flug NIE GESTARTET",
                         sn.pathCount > 0 ? sn.paths[0].machRadial : 0.0,
                         sn.pathCount > 0 ? sn.paths[0].activeBranches : 0);

            if (everPlayed)
                std::printf ("  Lauf %d:   Wiedergabe zuletzt aktiv bei t=%.2f s\n",
                             run + 1, lastPlayingAt);
        }
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
