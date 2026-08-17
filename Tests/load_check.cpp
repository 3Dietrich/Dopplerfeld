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
    double    worstAtSeconds = 0.0;   // wann der schlechteste Block lag
    double    peak        = 0.0;
    double    sumSquares[2] { 0.0, 0.0 };
    long long samples     = 0;
    long long nonFinite   = 0;
    double    maxMach     = 0.0;
    int       maxBranches = 0;

    // Löser-Auswertungen über den ganzen Lauf (siehe
    // RetardedTimeSolver::residualEvaluations). Die Wanduhrzahlen darüber
    // schwanken auf einem beschäftigten Rechner um Faktor zwei; diese Zahl ist
    // auf derselben Codebasis reproduzierbar und deshalb das Maß, an dem
    // Löser-Regressionen wirklich auffallen.
    std::uint64_t solverEvals = 0;

    // Längste zusammenhängende Stille im linken Kanal, in Sekunden, ab dem
    // ersten Ton gezählt. Genau das ist das Symptom, über das @dpa berichtet
    // ("der Ton setzt aus") - und genau das übersehen die bisherigen
    // Kriterien: NaN/Inf-Zähler und Gesamtspitze bleiben sauber, wenn das
    // Plugin mittendrin ein paar Sekunden lang schweigt und danach
    // weitermacht. Die Anlaufstille vor dem ersten Eintreffen des Schalls
    // (Laufzeit!) zählt nicht mit.
    double worstSilenceSeconds = 0.0;
    double silenceRun          = 0.0;
    bool   heardAnything       = false;

    void noteSample (double x, double dt)
    {
        constexpr double audible = 1.0e-4;

        if (std::abs (x) >= audible)
        {
            heardAnything = true;
            silenceRun    = 0.0;
            return;
        }

        if (! heardAnything)
            return;

        silenceRun         += dt;
        worstSilenceSeconds = std::max (worstSilenceSeconds, silenceRun);
    }

    void report (const char* title) const
    {
        const double budget    = (double) blockSize / sampleRate * 1.0e6;
        const double mean      = blocks > 0 ? totalMicros / blocks : 0.0;
        const double perChan   = samples > 0 ? (double) samples * 0.5 : 1.0;
        const double rmsLeft   = std::sqrt (sumSquares[0] / perChan);
        const double rmsRight  = std::sqrt (sumSquares[1] / perChan);

        std::printf ("%-22s Blöcke %4d | Block Ø %8.1f us  max %9.1f us (bei t=%5.2fs)  (Budget %.0f us, Ø %6.1f%%)\n",
                     title, blocks, mean, worstMicros, worstAtSeconds, budget, 100.0 * mean / budget);
        std::printf ("%-22s Ausgang Spitze %.4f, RMS L %.5f / R %.5f | nicht-endlich %lld | |M_r| max %.2f | Zweige max %d\n",
                     "", peak, rmsLeft, rmsRight, nonFinite, maxMach, maxBranches);
        std::printf ("%-22s Löser-Auswertungen %10llu  (%.0f pro Block) | längste Stille %.3f s\n",
                     "", (unsigned long long) solverEvals,
                     blocks > 0 ? (double) solverEvals / blocks : 0.0,
                     worstSilenceSeconds);
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

    // Nur die Arbeit DIESES Abschnitts zählen: bei mehreren render()-Aufrufen
    // auf demselben Processor (Extremfall-Szenario) wäre eine reine
    // Endabfrage sonst die Summe aller vorherigen mit.
    const std::uint64_t evalsBefore = proc.solverEvaluations();

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

        if (micros > stats.worstMicros)
        {
            stats.worstMicros    = micros;
            stats.worstAtSeconds = (double) block * blockSize / sampleRate;
        }

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

                if (ch == 0)
                    stats.noteSample (x, 1.0 / sampleRate);
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

    stats.solverEvals += proc.solverEvaluations() - evalsBefore;
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
    // 1b. Realistisch nahe Mach 1: kein extremer Testparameter, sondern das,
    //     was ein normaler Maus-Drag mit Default-Tau (50ms) auf einem
    //     kleinen Feld auslösen kann (@dpa-Diagnose: "näher an der
    //     Schallgeschwindigkeit" tritt CPU>100% schon im normalen Gebrauch
    //     auf, nicht nur bei künstlichem Mach-3-Test). Quelle springt alle
    //     150ms zwischen zwei 40m entfernten Punkten - der Spring-Smoother
    //     erreicht dabei Geschwindigkeiten im Bereich der Schallgeschw.,
    //     DAUERHAFT statt als einzelner Ausreißer (das ist der Fall, den
    //     der Stride-Fix NICHT adressiert, siehe Commit "Stride-Hangover").
    {
        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        setParam (proc, Params::fieldMetres, 150.0f);
        setParam (proc, Params::smootherType, 1.0f);   // CriticallyDampedSpring, Default
        setParam (proc, Params::smootherTau, 0.05f);   // Default aus Params.cpp
        setParam (proc, Params::lisX, 0.5f);
        setParam (proc, Params::lisY, 0.5f);
        setParam (proc, Params::srcX, 0.1f);
        setParam (proc, Params::srcY, 0.5f);

        proc.prepareToPlay (sampleRate, blockSize);

        Stats stats;
        render (proc, buffer, 8.0, stats, [&proc] (double t)
        {
            const bool half = std::fmod (t, 0.3) < 0.15;
            setParam (proc, Params::srcX, half ? 0.1f : 0.9f);   // 40m Sprung alle 150ms
        });

        stats.report ("Realistisch nahe Mach1");
        std::printf ("%-22s Physik %.1f%% / Quelle %.1f%% (vom Budget, letzter Block)\n",
                     "", (double) proc.cpuLoadPhysicsPercent(), (double) proc.cpuLoadSourcePercent());
    }

    //==================================================================
    // 1c. Bodenreflexion: derselbe Vorbeiflug zweimal, einmal ohne und einmal
    //     mit Spiegelpfaden. Geprüft wird zweierlei - dass die zusätzlichen
    //     Pfade den Ausgang nicht entgleisen lassen (NaN/Inf), und dass sie
    //     überhaupt etwas beitragen. Ohne den zweiten Teil würde ein
    //     versehentlich nie gerechneter Spiegelpfad stumm durchrutschen.
    //
    //     Die Quelle liegt dabei 20 m über dem Boden, nicht auf ihm: bei
    //     srcZ = 0 fiele die Spiegelquelle mit der echten zusammen und die
    //     Reflexion wäre nur eine Verdopplung ohne eigene Laufzeit.
    {
        auto flyBy = [&] (bool groundOn, Stats& stats)
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, 200.0f);
            setParam (proc, Params::smootherType, 1.0f);
            setParam (proc, Params::lisX, 0.5f);
            setParam (proc, Params::lisY, 0.5f);
            setParam (proc, Params::lisZ, 1.75f);
            setParam (proc, Params::srcX, 0.05f);
            setParam (proc, Params::srcY, 0.5f + (float) (10.0 / (200.0 * DopplerfeldProcessor::fieldAspect)));
            setParam (proc, Params::srcZ, 20.0f);

            setParam (proc, Params::groundReflectionOn, groundOn ? 1.0f : 0.0f);
            setParam (proc, Params::groundDampAmount, 0.5f);

            proc.prepareToPlay (sampleRate, blockSize);

            render (proc, buffer, 6.0, stats, [&proc] (double t)
            {
                setParam (proc, Params::srcX, (float) (0.05 + 30.0 * t / 200.0));
            });
        };

        Stats without, with;

        flyBy (false, without);
        flyBy (true,  with);

        without.report ("Boden aus, srcZ=20m");
        with.report    ("Boden an,  srcZ=20m");

        if (with.nonFinite > 0 || with.peak <= 0.0)
            failed = true;

        // Der Spiegelpfad ist ein zweiter Ausbreitungsweg mit eigener Laufzeit;
        // seine Summe mit dem Direktschall muss messbar anders sein als der
        // Direktschall allein.
        const double rmsWithout = std::sqrt (without.sumSquares[0] / ((double) without.samples * 0.5));
        const double rmsWith    = std::sqrt (with.sumSquares[0]    / ((double) with.samples    * 0.5));

        if (std::abs (rmsWith - rmsWithout) <= 1.0e-6 * std::max (rmsWithout, 1.0e-9))
        {
            std::printf ("FEHLGESCHLAGEN: Bodenreflexion ändert den Ausgang nicht "
                         "(RMS ohne %.6f, mit %.6f)\n", rmsWithout, rmsWith);
            failed = true;
        }

        // Vier Pfade statt zwei: die Anzeige muss die Spiegelpfade auch
        // ausweisen, sonst stimmt die Statuszeile nicht mit dem überein, was
        // gerechnet wird.
        if (with.maxBranches <= 0)
        {
            std::printf ("FEHLGESCHLAGEN: mit Bodenreflexion meldet kein Pfad aktive Zweige\n");
            failed = true;
        }
    }

    //==================================================================
    // 1d. Bodenreflexion UNTER LAST: dasselbe "realistisch nahe Mach 1" wie in
    //     1b, aber mit eingeschalteten Spiegelpfaden. Das ist der Fall, den
    //     @dpa live getroffen hat (Boden an + schnelle Bewegung -> Ton setzt
    //     aus) und den bisher kein Szenario abgedeckt hat: 1b fährt schnell,
    //     aber ohne Boden, 1c fährt mit Boden, aber mit gemütlichen 30 m/s.
    //
    //     Gemessen wird nicht nur der Mittelwert, sondern vor allem der
    //     schlechteste Einzelblock: ein Aussetzer entsteht am einzelnen Block,
    //     der sein Budget reißt, nicht am Durchschnitt.
    {
        auto fastRun = [&] (bool groundOn, Stats& stats)
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, 150.0f);
            setParam (proc, Params::smootherType, 1.0f);
            setParam (proc, Params::smootherTau, 0.05f);
            setParam (proc, Params::lisX, 0.5f);
            setParam (proc, Params::lisY, 0.5f);
            setParam (proc, Params::lisZ, 1.75f);
            setParam (proc, Params::srcX, 0.1f);
            setParam (proc, Params::srcY, 0.5f);
            setParam (proc, Params::srcZ, 20.0f);

            setParam (proc, Params::groundReflectionOn, groundOn ? 1.0f : 0.0f);
            setParam (proc, Params::groundDampAmount, 0.5f);

            proc.prepareToPlay (sampleRate, blockSize);

            render (proc, buffer, 8.0, stats, [&proc] (double t)
            {
                const bool half = std::fmod (t, 0.3) < 0.15;
                setParam (proc, Params::srcX, half ? 0.1f : 0.9f);
            });
        };

        Stats without, with;

        fastRun (false, without);
        fastRun (true,  with);

        without.report ("Mach1, Boden aus");
        with.report    ("Mach1, Boden an");

        if (with.nonFinite > 0 || with.peak <= 0.0)
            failed = true;

        // Das Symptom selbst: der Ton darf nicht wegbleiben.
        if (with.worstSilenceSeconds > 0.05)
        {
            std::printf ("FEHLGESCHLAGEN: Ton setzt %.3f s lang aus (Bodenreflexion bei hoher "
                         "Geschwindigkeit)\n", with.worstSilenceSeconds);
            failed = true;
        }

        // Bewusst NICHT über die Wanduhr: die schwankt auf einem beschäftigten
        // Rechner um Faktor zwei und macht den Test damit zum Würfelspiel.
        // Gemessen wird die Löserarbeit, und zwar zweierlei.
        //
        // Erstens: die Spiegelpfade dürfen nichts kosten, was über das
        // Verdoppeln der Pfadanzahl hinausgeht. Ein Spiegelpfad, der
        // systematisch teurer wäre als der Direktpfad (zu großes Suchfenster,
        // verlorene Zweigidentitäten, ständiges Neusäen), fiele hier auf.
        const double evalsWith    = with.blocks    > 0 ? (double) with.solverEvals    / with.blocks    : 0.0;
        const double evalsWithout = without.blocks > 0 ? (double) without.solverEvals / without.blocks : 0.0;

        if (evalsWithout > 0.0 && evalsWith > 2.5 * evalsWithout)
        {
            std::printf ("FEHLGESCHLAGEN: Bodenreflexion kostet %.1fx statt der erwarteten 2x "
                         "(%.0f statt %.0f Löser-Auswertungen pro Block)\n",
                         evalsWith / evalsWithout, evalsWith, evalsWithout);
            failed = true;
        }

        // Zweitens eine absolute Obergrenze als Regressionsbremse. Gemessen
        // sind rund 6700 Auswertungen pro Block; die Schranke liegt bewusst
        // knapp genug, dass ein Rückfall in den alten Zustand (rund 17000,
        // Vollscan an jedem Solver-Punkt) sie reißt.
        constexpr double evalBudget = 11000.0;

        if (evalsWith > evalBudget)
        {
            std::printf ("FEHLGESCHLAGEN: %.0f Löser-Auswertungen pro Block, erlaubt sind %.0f\n",
                         evalsWith, evalBudget);
            failed = true;
        }
    }

    //==================================================================
    // 1e. Wände: zwei frei platzierbare unendliche Ebenen, eine senkrecht und
    //     eine geneigt. Geprüft wird dasselbe wie beim Boden - dass sie den
    //     Ausgang überhaupt verändern (eine nie gerechnete Wand rutschte sonst
    //     stumm durch), dass nichts entgleist, und dass sie nur das kosten, was
    //     ein weiteres Pfadpaar eben kostet.
    //
    //     Die geneigte Wand ist der eigentliche Grund für diesen Fall: sie ist
    //     die einzige Stelle, an der eine Spiegelebene weder achsenparallel
    //     noch senkrecht steht, und damit der einzige Test der allgemeinen
    //     Householder-Spiegelung im vollständigen Zusammenspiel.
    {
        auto flyBy = [&] (bool wallsOn, Stats& stats)
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, 200.0f);
            setParam (proc, Params::smootherType, 1.0f);
            setParam (proc, Params::lisX, 0.5f);
            setParam (proc, Params::lisY, 0.5f);
            setParam (proc, Params::lisZ, 1.75f);
            setParam (proc, Params::srcX, 0.05f);
            setParam (proc, Params::srcY, 0.5f + (float) (10.0 / (200.0 * DopplerfeldProcessor::fieldAspect)));
            setParam (proc, Params::srcZ, 5.0f);

            // Boden aus, damit hier wirklich nur die Wände gemessen werden.
            setParam (proc, Params::groundReflectionOn, 0.0f);

            // Wand 1 senkrecht, quer hinter dem Hörer.
            setParam (proc, Params::wall1On,    wallsOn ? 1.0f : 0.0f);
            setParam (proc, Params::wall1X,     0.5f);
            setParam (proc, Params::wall1Y,     0.9f);
            setParam (proc, Params::wall1Angle, 0.0f);
            setParam (proc, Params::wall1Tilt,  0.0f);
            setParam (proc, Params::wall1Damp,  0.3f);

            // Wand 2 schräg im Grundriss UND geneigt - die allgemeine Lage.
            setParam (proc, Params::wall2On,    wallsOn ? 1.0f : 0.0f);
            setParam (proc, Params::wall2X,     0.15f);
            setParam (proc, Params::wall2Y,     0.15f);
            setParam (proc, Params::wall2Angle, 37.0f);
            setParam (proc, Params::wall2Tilt,  25.0f);
            setParam (proc, Params::wall2Damp,  0.2f);

            proc.prepareToPlay (sampleRate, blockSize);

            render (proc, buffer, 6.0, stats, [&proc] (double t)
            {
                setParam (proc, Params::srcX, (float) (0.05 + 30.0 * t / 200.0));
            });
        };

        Stats without, with;

        flyBy (false, without);
        flyBy (true,  with);

        without.report ("Waende aus");
        with.report    ("Waende an (1 schraeg)");

        if (with.nonFinite > 0 || with.peak <= 0.0)
            failed = true;

        if (with.worstSilenceSeconds > 0.05)
        {
            std::printf ("FEHLGESCHLAGEN: Ton setzt %.3f s lang aus (Wandreflexionen)\n",
                         with.worstSilenceSeconds);
            failed = true;
        }

        const double rmsWithout = std::sqrt (without.sumSquares[0] / ((double) without.samples * 0.5));
        const double rmsWith    = std::sqrt (with.sumSquares[0]    / ((double) with.samples    * 0.5));

        if (std::abs (rmsWith - rmsWithout) <= 1.0e-6 * std::max (rmsWithout, 1.0e-9))
        {
            std::printf ("FEHLGESCHLAGEN: Wände ändern den Ausgang nicht "
                         "(RMS ohne %.6f, mit %.6f)\n", rmsWithout, rmsWith);
            failed = true;
        }

        // Zwei Wände sind zwei zusätzliche Pfadpaare, also das Dreifache des
        // Direktschalls. Deutlich mehr hieße, dass eine Wand ihren Löser
        // teurer betreibt als der Direktpfad - genau die Sorte Fehler, die man
        // sonst erst im Hörtest als Aussetzer bemerkt.
        const double evalsWith    = with.blocks    > 0 ? (double) with.solverEvals    / with.blocks    : 0.0;
        const double evalsWithout = without.blocks > 0 ? (double) without.solverEvals / without.blocks : 0.0;

        if (evalsWithout > 0.0 && evalsWith > 3.75 * evalsWithout)
        {
            std::printf ("FEHLGESCHLAGEN: zwei Wände kosten %.1fx statt der erwarteten 3x "
                         "(%.0f statt %.0f Löser-Auswertungen pro Block)\n",
                         evalsWith / evalsWithout, evalsWith, evalsWithout);
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
