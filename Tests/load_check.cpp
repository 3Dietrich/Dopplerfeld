// Lasttest aus Plan Abschnitt 6 (H13). Zwei Szenarien:
//
//   1. Normalfall - kleines Feld, Unterschall, kreisende Quelle. Das ist die
//      Betriebsart, in der das Plugin in einem Host laufen muss.
//   2. Extremfall - n = 10000 m, Slew-Limiter mit v_max = 1000 m/s, also eine
//      Bewegung, die kurzzeitig Überschall erreicht, danach Richtungsumkehr
//      und zum Schluss ein Feldgrößenwechsel (Geometrie-Crossfade, dabei
//      laufen zwei komplette Lösersätze gleichzeitig).
//
// Geprüft wird, was keine einzelne Komponente allein prüfen kann: dass der
// Ausgang endlich bleibt (kein NaN/Inf), dass nichts abstürzt, dass der
// Überschallfall im Zusammenspiel wirklich auftritt (M_r > 1 samt zusätzlichem
// Wurzelpaar) und wie sich die Blockzeiten dabei verhalten.
//
// Kein Audiogerät: der Processor wird direkt gebaut und blockweise mit
// synthetischen Puffern gefüttert, wie es ein Host täte.

#include "PluginProcessor.h"
#include "Params.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

void setParam (DopplerfeldProcessor& proc, const char* id, float value)
{
    auto* parameter = proc.apvts.getParameter (id);

    if (parameter == nullptr)
    {
        std::printf ("FEHLER: Parameter %s existiert nicht\n", id);
        std::exit (1);
    }

    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

struct Stats
{
    int       blocks     = 0;
    double    totalMicros = 0.0;
    double    worstMicros = 0.0;
    double    peak        = 0.0;
    double    sumSquares[2] { 0.0, 0.0 };
    long long samples     = 0;
    long long nonFinite   = 0;
    double    maxMach     = 0.0;
    int       maxBranches = 0;

    void report (const char* title) const
    {
        const double budget    = (double) blockSize / sampleRate * 1.0e6;
        const double mean      = blocks > 0 ? totalMicros / blocks : 0.0;
        const double perChan   = samples > 0 ? (double) samples * 0.5 : 1.0;
        const double rmsLeft   = std::sqrt (sumSquares[0] / perChan);
        const double rmsRight  = std::sqrt (sumSquares[1] / perChan);

        std::printf ("%-22s Blöcke %4d | Block Ø %8.1f us  max %9.1f us  (Budget %.0f us, Ø %6.1f%%)\n",
                     title, blocks, mean, worstMicros, budget, 100.0 * mean / budget);
        std::printf ("%-22s Ausgang Spitze %.4f, RMS L %.5f / R %.5f | nicht-endlich %lld | |M_r| max %.2f | Zweige max %d\n",
                     "", peak, rmsLeft, rmsRight, nonFinite, maxMach, maxBranches);
    }
};

// Rendert numBlocks Blöcke und misst dabei mit. moveSource bekommt die
// laufende Zeit in Sekunden und darf Parameter verstellen - genau wie ein
// Benutzer, der an M zieht, oder ein Host, der automatisiert.
template <typename MoveFn>
void render (DopplerfeldProcessor& proc, juce::AudioBuffer<float>& buffer,
             double seconds, Stats& stats, MoveFn&& moveSource)
{
    juce::MidiBuffer midi;
    FieldSnapshot    snapshot;

    const int numBlocks = (int) std::ceil (seconds * sampleRate / blockSize);

    for (int block = 0; block < numBlocks; ++block)
    {
        moveSource ((double) block * blockSize / sampleRate);

        buffer.clear();

        const auto start = std::chrono::steady_clock::now();
        proc.processBlock (buffer, midi);
        const auto stop  = std::chrono::steady_clock::now();

        const double micros = std::chrono::duration<double, std::micro> (stop - start).count();

        ++stats.blocks;
        stats.totalMicros += micros;
        stats.worstMicros  = std::max (stats.worstMicros, micros);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* data = buffer.getReadPointer (ch);

            for (int i = 0; i < blockSize; ++i)
            {
                const double x = (double) data[i];

                ++stats.samples;

                if (! std::isfinite (x))
                {
                    ++stats.nonFinite;
                    continue;
                }

                stats.peak = std::max (stats.peak, std::abs (x));

                if (ch < 2)
                    stats.sumSquares[ch] += x * x;
            }
        }

        // Wie der Editor: Anzeigedaten aus der Doppelpufferung abholen.
        proc.fillFieldSnapshot (snapshot);

        for (int i = 0; i < snapshot.pathCount; ++i)
        {
            stats.maxMach     = std::max (stats.maxMach, std::abs (snapshot.paths[(size_t) i].machRadial));
            stats.maxBranches = std::max (stats.maxBranches, snapshot.paths[(size_t) i].activeBranches);
        }
    }
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioBuffer<float> buffer (2, blockSize);

    bool failed = false;

    //==================================================================
    // 1. Normalfall: 200 m Feld, kritisch gedämpfte Feder, Quelle kreist mit
    //    rund 30 m/s um den Hörer - der Vorbeiflug aus der H5-Abnahme.
    {
        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        setParam (proc, Params::fieldMetres, 200.0f);
        setParam (proc, Params::smootherType, 1.0f);   // CriticallyDampedSpring
        setParam (proc, Params::lisX, 0.5f);
        setParam (proc, Params::lisY, 0.5f);

        // Startpunkt der Kreisbahn schon vor prepareToPlay setzen: sonst
        // beginnt der erste Block mit einem 30-m-Sprung, den der Glätter mit
        // mehreren hundert m/s nachholt - das wäre ein Startgeräusch des
        // Tests, keine Eigenschaft der Bahn.
        setParam (proc, Params::srcX, 0.05f);
        setParam (proc, Params::srcY, 0.5f + (float) (10.0 / (200.0 * DopplerfeldProcessor::fieldAspect)));

        proc.prepareToPlay (sampleRate, blockSize);

        // Rauchtest der Oberfläche: einmal bauen, layouten und in ein Bild
        // zeichnen. Ohne Fenster - es geht nur darum, dass Aufbau und
        // Zeichnen mit einem echten Snapshot zusammenpassen.
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

            juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            juce::Graphics g (image);
            editor->paintEntireComponent (g, true);
        }

        Stats stats;

        // Gerader Vorbeiflug mit 30 m/s in 10 m Abstand - der Fall aus der
        // H5-Abnahme (Frequenzhub etwa ±9 %). Eine Kreisbahn um den Hörer
        // wäre als Lastfall zwar dieselbe Rechnung, hätte aber konstruktions-
        // bedingt v_r = 0 und damit gar keinen Doppler.
        render (proc, buffer, 6.0, stats, [&proc] (double t)
        {
            setParam (proc, Params::srcX, (float) (0.05 + 30.0 * t / 200.0));
        });

        stats.report ("Normalfall n=200");

        if (stats.nonFinite > 0 || stats.peak <= 0.0)
            failed = true;

        // Beide Ohren sind eigene Empfangspunkte (Plan 3.5). Wären sie im
        // Zusammenbau auf denselben Kanal verdrahtet, bliebe rechts still -
        // und wären sie derselbe Pfad, wäre der Unterschied exakt null.
        const double rmsLeft  = std::sqrt (stats.sumSquares[0] / ((double) stats.samples * 0.5));
        const double rmsRight = std::sqrt (stats.sumSquares[1] / ((double) stats.samples * 0.5));

        if (rmsLeft <= 0.0 || rmsRight <= 0.0)
        {
            std::printf ("FEHLGESCHLAGEN: ein Kanal ist still (L %.6f, R %.6f)\n", rmsLeft, rmsRight);
            failed = true;
        }

        if (std::abs (rmsLeft - rmsRight) <= 0.0)
        {
            std::printf ("FEHLGESCHLAGEN: beide Kanäle sind identisch, die Ohren sind nicht getrennt\n");
            failed = true;
        }
    }

    //==================================================================
    // 2. Extremfall: größtes Feld, Überschallflug quer hindurch, Umkehr,
    //    Feldgrößenwechsel.
    {
        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        setParam (proc, Params::fieldMetres, 10000.0f);
        setParam (proc, Params::smootherType, 2.0f);    // SlewLimiter
        setParam (proc, Params::slewVmax, 1000.0f);     // knapp Mach 3
        setParam (proc, Params::slewAmax, 5000.0f);
        setParam (proc, Params::lisX, 0.5f);
        setParam (proc, Params::lisY, 0.5f);
        setParam (proc, Params::srcX, 0.02f);
        setParam (proc, Params::srcY, 0.5f);

        proc.prepareToPlay (sampleRate, blockSize);

        // Ruhephase, damit der Löser eine vollständige Historie hat.
        Stats idle;
        render (proc, buffer, 2.0, idle, [] (double) {});
        idle.report ("Extrem, ruhend");

        // Überschallflug quer durchs Feld, an der Hörerposition vorbei.
        Stats flight;
        setParam (proc, Params::srcX, 0.98f);
        render (proc, buffer, 8.0, flight, [] (double) {});
        flight.report ("Extrem, Mach ~3");

        // Richtungsumkehr: die Zweige verschwinden und tauchen neu auf.
        Stats reverse;
        setParam (proc, Params::srcX, 0.02f);
        render (proc, buffer, 1.5, reverse, [] (double) {});
        reverse.report ("Extrem, Umkehr");

        // Feldgrößenwechsel: Geometrie-Crossfade, zwei Lösersätze parallel.
        Stats resize;
        setParam (proc, Params::fieldMetres, 100.0f);
        render (proc, buffer, 1.0, resize, [] (double) {});
        resize.report ("Extrem, Feldwechsel");

        const long long nonFinite = idle.nonFinite + flight.nonFinite + reverse.nonFinite + resize.nonFinite;
        const double    maxMach   = std::max (std::max (idle.maxMach, flight.maxMach),
                                              std::max (reverse.maxMach, resize.maxMach));

        if (nonFinite > 0)
        {
            std::printf ("FEHLGESCHLAGEN: NaN/Inf im Ausgang\n");
            failed = true;
        }

        if (maxMach <= 1.0)
        {
            std::printf ("FEHLGESCHLAGEN: kein Überschall erreicht, der Testfall greift nicht\n");
            failed = true;
        }

        if (flight.peak <= 0.0)
        {
            std::printf ("FEHLGESCHLAGEN: Ausgang ist still\n");
            failed = true;
        }
    }

    std::printf (failed ? "FEHLGESCHLAGEN\n" : "OK\n");
    return failed ? 1 : 0;
}
