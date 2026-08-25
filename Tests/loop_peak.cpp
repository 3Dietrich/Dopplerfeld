// Messprogramm zum CPU-Ausschlag am Rundenpunkt einer Bewegungswiedergabe
// (@dpa 20260825: "wenn der Abspielloop von vorne anfaengt ist die CPU meist
// >100% und Soundaussetzer sind 90% vorhanden").
//
// Laedt ein echtes Preset (Standardfall: sein Flighter-Preset), spielt die
// Aufzeichnung in Dauerschleife und misst die Rechenzeit je Block. Ausgegeben
// werden die teuersten Bloecke mit Zeitstempel - liegen sie am Rundenpunkt,
// ist der Schnitt die Ursache und nicht der Flug an sich.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target loop_peak && build/loop_peak

#include "Params.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

struct BlockCost
{
    double        seconds = 0.0;   // Zeitpunkt im Lauf
    double        micros  = 0.0;   // gemessene Rechenzeit
    std::uint64_t evals   = 0;     // Loeser-Auswertungen in diesem Block
    double        mach    = 0.0;   // Quellgeschwindigkeit in Mach
};
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Ueber ein Suchmuster statt ueber den vollen Namen: die Presetnamen
    // enthalten Zeichen wie "\u00b2", die als Kommandozeilen- oder
    // Quelltextliteral je nach Kodierung nicht ankommen.
    const juce::String pattern = argc > 1 ? juce::String::fromUTF8 (argv[1]) + "*"
                                          : juce::String ("Flighter*");

    // Auch in presets/test suchen: dort liegen @dpas Faelle zum Nachstellen.
    const auto folder  = juce::File (DOPPLERFELD_SOURCE_DIR).getChildFile ("presets");
    auto       matches = folder.findChildFiles (juce::File::findFiles, true, pattern);

    if (matches.isEmpty())
    {
        std::printf ("Kein Preset zu \"%s\" in %s\n", pattern.toRawUTF8(),
                     folder.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto file = matches.getFirst();

    juce::MemoryBlock state;

    if (! file.loadFileAsData (state))
    {
        std::printf ("Preset nicht lesbar: %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }

    const double seconds = argc > 2 ? std::atof (argv[2]) : 20.0;

    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);
    proc.setStateInformation (state.getData(), (int) state.getSize());

    // Gegenprobe: Wackler aus, sonst alles wie im Preset.
    if (std::getenv ("DOPPLERFELD_NOJITTER") != nullptr)
    {
        if (auto* p = proc.apvts.getParameter (Params::srcJitterAmount))
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f));

        std::printf ("(Wackler ausgeschaltet)\n");
    }

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    // Einschwingen lassen, damit der erste Block (Puffer anlegen, Filter
    // aufsetzen) nicht als Spitze mitzaehlt.
    for (int i = 0; i < 20; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }

    // Drittes Argument "fly": statt der Aufzeichnung den Vorbeiflug starten -
    // dessen Dauerschleife hat dieselbe Naht am Rundenpunkt.
    const bool useFlyBy = argc > 3 && juce::String (argv[3]).equalsIgnoreCase ("fly");

    if (useFlyBy)
        proc.triggerFlyBy();
    else
        proc.triggerPlayback();

    // Rundenlaenge aus der Aufzeichnung: so laesst sich zuordnen, ob eine
    // Lastspitze am Rundenpunkt sitzt oder irgendwo im Flug.
    // Das Wiedergabetempo skaliert sie mit: es steht im Preset und ist bei
    // schnellen Aufnahmen der Grund, warum der Rundenpunkt ueberhaupt in einem
    // kurzen Lauf vorkommt.
    const auto* speedParam = proc.apvts.getRawParameterValue (Params::playSpeed);
    const double playSpeed = speedParam != nullptr ? (double) speedParam->load() : 1.0;

    const double roundSeconds = proc.recordedFrameCount() > 1
        ? (double) (proc.recordedFrameCount() - 1)
            / DopplerfeldProcessor::motionRecordRateHz / std::max (0.01, playSpeed)
        : 0.0;

    const double budgetMicros = 1.0e6 * blockSize / sampleRate;
    const int    numBlocks    = (int) (seconds * sampleRate / blockSize);

    std::vector<BlockCost> costs;
    costs.reserve ((size_t) numBlocks);

    double peakSample = 0.0;

    // Mitschnitt, damit sich derselbe Lauf mit denselben Werkzeugen ansehen
    // laesst wie eine Aufnahme aus dem Plugin.
    juce::AudioBuffer<float> recorded (1, numBlocks * blockSize);
    recorded.clear();

    std::uint64_t lastTrackLost = 0, lastNewIds = 0, lastDeaths = 0;

    for (int block = 0; block < numBlocks; ++block)
    {
        buffer.clear();

        const auto evalsBefore = proc.solverEvaluations();

        const auto t0 = std::chrono::steady_clock::now();
        proc.processBlock (buffer, midi);
        const auto t1 = std::chrono::steady_clock::now();

        const double micros = std::chrono::duration<double, std::micro> (t1 - t0).count();

        // Wanduhrzeit UND Auswertungen: die Zeit ist das, was der Hoerer merkt,
        // die Auswertungen sind das, was sich auf einer beschaeftigten Maschine
        // reproduzieren laesst.
        FieldSnapshot snap;
        proc.fillFieldSnapshot (snap);

        // Zweigereignisse mitschreiben: ein verlorener oder neu vergebener
        // Zweig heisst, dass die Verzoegerung eines Pfades springt - genau
        // das, was als kurzer Hickser zu hoeren waere.
        if (snap.trackLost != lastTrackLost || snap.newIds != lastNewIds
            || snap.branchDeaths != lastDeaths)
        {
            std::printf ("    [%6.3f s] Zweige: verloren %+lld, neu %+lld, tot %+lld"
                         "  (|M_r| %.2f, Mach %.2f)\n",
                         (double) block * blockSize / sampleRate,
                         (long long) (snap.trackLost - lastTrackLost),
                         (long long) (snap.newIds - lastNewIds),
                         (long long) (snap.branchDeaths - lastDeaths),
                         snap.paths[0].machRadial,
                         snap.speedOfSound > 0.0 ? snap.sourceSpeed / snap.speedOfSound : 0.0);

            lastTrackLost = snap.trackLost;
            lastNewIds    = snap.newIds;
            lastDeaths    = snap.branchDeaths;
        }

        costs.push_back ({ (double) block * blockSize / sampleRate, micros,
                           proc.solverEvaluations() - evalsBefore,
                           snap.speedOfSound > 0.0 ? snap.sourceSpeed / snap.speedOfSound : 0.0 });

        const float* data = buffer.getReadPointer (0);

        for (int i = 0; i < blockSize; ++i)
            peakSample = std::max (peakSample, (double) std::abs (data[i]));

        recorded.copyFrom (0, block * blockSize, buffer, 0, 0, blockSize);
    }

    {
        const auto out = juce::File (DOPPLERFELD_SOURCE_DIR).getChildFile ("build")
                             .getChildFile ("loop_peak_mitschnitt.wav");
        out.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());

        if (stream != nullptr)
        {
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (stream.release(), sampleRate, 1, 32, {}, 0));

            if (writer != nullptr)
            {
                writer->writeFromAudioSampleBuffer (recorded, 0, recorded.getNumSamples());
                writer.reset();
                std::printf ("Mitschnitt   %s\n", out.getFullPathName().toRawUTF8());
            }
        }
    }

    double sum = 0.0;
    for (const auto& c : costs)
        sum += c.micros;

    auto sorted = costs;
    std::sort (sorted.begin(), sorted.end(),
               [] (const BlockCost& a, const BlockCost& b) { return a.evals > b.evals; });

    std::uint64_t evalSum = 0;
    for (const auto& c : costs)
        evalSum += c.evals;

    int overBudget = 0;
    for (const auto& c : costs)
        if (c.micros > budgetMicros)
            ++overBudget;

    std::printf ("Preset      %s\n", file.getFileName().toRawUTF8());
    std::printf ("Aufnahme    %d Frames, Tempo %.2f -> eine Runde %.2f s\n",
                 proc.recordedFrameCount(), playSpeed, roundSeconds);

    if (roundSeconds > 0.0 && roundSeconds < seconds)
    {
        std::printf ("Rundenpunkte");
        for (double t = roundSeconds; t < seconds; t += roundSeconds)
            std::printf (" %.2f s", t);
        std::printf ("\n");
    }
    std::printf ("Lauf        %.1f s, %d Bloecke a %d Samples (Budget %.0f us je Block)\n",
                 seconds, numBlocks, blockSize, budgetMicros);
    std::printf ("Last        Mittel %.0f us (%.1f %% des Budgets), Spitze %.0f us (%.0f %%)\n",
                 sum / (double) costs.size(), 100.0 * (sum / (double) costs.size()) / budgetMicros,
                 sorted.front().micros, 100.0 * sorted.front().micros / budgetMicros);
    std::printf ("Ueber Budget %d von %d Bloecken (%.1f %%), Ausgang Spitze %.4f\n",
                 overBudget, (int) costs.size(),
                 100.0 * overBudget / (double) costs.size(), peakSample);

    std::printf ("Loeser      %llu Auswertungen gesamt (%.0f je Block), teuerster Block %llu\n",
                 (unsigned long long) evalSum, (double) evalSum / (double) costs.size(),
                 (unsigned long long) sorted.front().evals);

    // Der Rundenpunkt im Vergleich zum ruhigen Lauf davor: das ist die Zahl,
    // um die es hier geht, und anders als die Wanduhrzeit ist sie auf einer
    // beschaeftigten Maschine reproduzierbar.
    if (roundSeconds > 0.0 && roundSeconds < seconds)
    {
        auto windowStats = [&] (double from, double to)
        {
            std::uint64_t worst = 0;
            double        sum   = 0.0;
            int           n     = 0;

            for (const auto& c : costs)
                if (c.seconds >= from && c.seconds < to)
                {
                    worst = std::max (worst, c.evals);
                    sum  += (double) c.evals;
                    ++n;
                }

            return std::pair<std::uint64_t, double> { worst, n > 0 ? sum / n : 0.0 };
        };

        const auto before = windowStats (std::max (0.0, roundSeconds - 10.0), roundSeconds);
        const auto after  = windowStats (roundSeconds, std::min (seconds, roundSeconds + 8.0));


        std::printf ("Rundenpunkt zehn Sekunden davor: Mittel %.0f, teuerster %llu\n",
                     before.second, (unsigned long long) before.first);
        std::printf ("            acht Sekunden danach: Mittel %.0f (%.1f x), teuerster %llu (%.1f x)\n",
                     after.second, before.second > 0.0 ? after.second / before.second : 0.0,
                     (unsigned long long) after.first,
                     before.first > 0 ? (double) after.first / (double) before.first : 0.0);
    }


    std::printf ("Teuerste Bloecke (nach Auswertungen):\n");
    for (int i = 0; i < 12 && i < (int) sorted.size(); ++i)
        std::printf ("    t=%6.2f s  %10llu Auswertungen  %8.0f us  (%.0f %% des Budgets)\n",
                     sorted[(size_t) i].seconds,
                     (unsigned long long) sorted[(size_t) i].evals,
                     sorted[(size_t) i].micros,
                     100.0 * sorted[(size_t) i].micros / budgetMicros);

    // Halbsekunden-Profil: zeigt, ob die Spitzen an einer Stelle sitzen (dem
    // Rundenpunkt) oder ueber den ganzen Lauf verteilt sind.
    std::printf ("Profil je halbe Sekunde (teuerster Block: Auswertungen/Mach):\n   ");
    for (double t = 0.0; t < seconds; t += 0.5)
    {
        std::uint64_t worst = 0;
        for (const auto& c : costs)
            if (c.seconds >= t && c.seconds < t + 0.5)
                worst = std::max (worst, c.evals);

        double machWorst = 0.0;
        for (const auto& c : costs)
            if (c.seconds >= t && c.seconds < t + 0.5)
                machWorst = std::max (machWorst, c.mach);

        std::printf (" %6llu/%.1f", (unsigned long long) worst, machWorst);

        if ((int) (t * 2) % 10 == 9)
            std::printf ("\n   ");
    }
    std::printf ("\n");

    return 0;
}
