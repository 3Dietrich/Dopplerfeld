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
#include "PluginEditor.h"
#include "Params.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace
{
constexpr double decelSeconds = 5.0;
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

    // Zweig-Todesmessung (@dpa 20260819): mit welchem Hüllkurvenwert stirbt ein
    // Zweig, den der Löser nicht mehr meldet? Er wird über rampSeconds (1 ms)
    // auf null gefahren, unabhängig von seinem Pegel. Liegt der Mittelwert nahe
    // 1, schneidet die Anti-Klick-Rampe mitten im vollen Ton ab - das wäre der
    // Abbruch am Ende der Überschall-Hälfte. Nahe 0 hieße, der Zweig war
    // ohnehin schon ausgeklungen und die Rampe ist unschuldig.
    std::uint64_t branchDeaths     = 0;
    std::uint64_t loudBranchDeaths = 0;
    std::uint64_t branchEvictions  = 0;
    std::uint64_t causticDeaths    = 0;
    double        tauMeanMs        = 0.0;
    double        tauMaxMs         = 0.0;
    std::uint64_t abruptDeaths     = 0;
    std::uint64_t trackLost        = 0;
    std::uint64_t newIds           = 0;
    std::uint64_t newIdsNear       = 0;
    std::uint64_t orderMatches     = 0;
    std::array<std::uint64_t, 8> rootHist {};
    std::uint64_t countFlips       = 0;
    std::uint64_t collapsedTracks  = 0;

    // Beobachtete Quellgeschwindigkeit, aus dem Snapshot. Fuer die Pruefung,
    // dass ein Vorbeiflug vom ersten Moment an mit voller Geschwindigkeit
    // fliegt und nicht erst anlaeuft.
    double speedMin =  1.0e30;
    double speedMax = -1.0e30;
    double speedFirst = -1.0;

    // Wann die Ausreisser auftraten, in Sekunden ab dem Anfang dieses
    // Abschnitts. Ohne den Zeitpunkt sagt ein Tempofenster nur, DASS die Bahn
    // krumm ist - mit ihm laesst sich die Stelle im Flug benennen.
    // Wann die lauteste Stelle des Abschnitts kam - erst damit sagt eine
    // Spitze, WOVON sie stammt (Vorbeiflug, Umblendung, Einsatz).
    double peakAtSec = 0.0;

    double speedMinAtSec = 0.0;
    double speedMaxAtSec = 0.0;

    // Teuerster Einzelblock, in Loeser-Auswertungen statt Wanduhrzeit, samt
    // dem M_r, das in diesem Moment anlag. Beantwortet die Frage, WO die
    // Lastspitze sitzt - die Wanduhrzahl allein sagt nur, dass es eine gibt.
    std::uint64_t worstBlockEvals = 0;
    double        worstBlockMach  = 0.0;
    double        worstBlockAtSec = 0.0;
    std::uint64_t handovers        = 0;
    std::uint64_t tightPairs       = 0;
    std::uint64_t adjacentPairs    = 0;
    std::uint64_t droppedRoots     = 0;
    double        deathEnvMean     = 0.0;
    double        deathEnvMax      = 0.0;

    // --- Pegelsturz-Messung (@dpa 20260819) ---
    //
    // Genau das, was @dpa als "Abbruch" beschreibt und was bisher nur er hoeren
    // konnte: der Pegel faellt innerhalb weniger Millisekunden um zig dB, obwohl
    // der Klang weitergeht. An seiner Aufnahme von Hand nachgemessen waren es
    // ueber 20 dB in 0,75 ms.
    //
    // Gemessen wird der schlimmste Abfall zwischen dem lautesten der letzten
    // envLookback Fenster und dem aktuellen Fenster - also ueber ein Zeitfenster
    // von envLookback Millisekunden. Kurze Wellenform-Nulldurchgaenge fallen
    // dabei nicht ins Gewicht, weil ueber ein ganzes Millisekunden-Fenster
    // effektivwertgemittelt wird.
    //
    // Der Wert ist NICHT absolut zu lesen. Er wird gegen dasselbe Mass im
    // Unterschall-Szenario verglichen, das nachweislich in Ordnung klingt -
    // siehe die Pruefung in main().
    static constexpr int    envWindow   = 48;      // 1 ms bei 48 kHz
    static constexpr int    envLookback = 2;       // bis 2 ms zurueck
    static constexpr double envAudible  = 1.0e-3;  // darunter ist der Sturz belanglos

    double envWinSum   = 0.0;
    int    envWinCount = 0;
    double envHistory[envLookback] {};
    double worstDropDb = 0.0;

    void noteLevelWindow (double rms)
    {
        double before = 0.0;

        for (double v : envHistory)
            before = std::max (before, v);

        if (before > envAudible)
        {
            const double drop = 20.0 * std::log10 ((rms + 1.0e-12) / before);
            worstDropDb = std::min (worstDropDb, drop);
        }

        for (int i = envLookback - 1; i > 0; --i)
            envHistory[i] = envHistory[i - 1];

        envHistory[0] = rms;
    }

    void noteSample (double x, double dt)
    {
        constexpr double audible = 1.0e-4;

        envWinSum += x * x;

        if (++envWinCount >= envWindow)
        {
            noteLevelWindow (std::sqrt (envWinSum / (double) envWindow));
            envWinSum   = 0.0;
            envWinCount = 0;
        }

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

        const double seconds    = blocks > 0 ? (double) blocks * blockSize / sampleRate : 0.0;
        const double perSecond  = seconds > 0.0 ? (double) branchDeaths / seconds : 0.0;
        const double loudShare  = branchDeaths > 0
                                 ? 100.0 * (double) loudBranchDeaths / (double) branchDeaths
                                 : 0.0;

        std::printf ("%-22s Zweig-Tode %8llu (%6.1f /s) | env beim Tod Ø %.3f max %.3f | env >= 0,5: %5.1f %% | verdraengt %llu\n",
                     "", (unsigned long long) branchDeaths, perSecond,
                     deathEnvMean, deathEnvMax, loudShare,
                     (unsigned long long) branchEvictions);

        std::printf ("%-22s steilster Pegelsturz %6.1f dB in %d ms\n",
                     "", worstDropDb, envLookback);
        std::printf ("%-22s davon an der Kaustik %llu (%5.1f %%) | Ausklang tau Ø %6.3f ms max %6.3f ms\n",
                     "", (unsigned long long) causticDeaths,
                     branchDeaths > 0 ? 100.0 * (double) causticDeaths / (double) branchDeaths : 0.0,
                     tauMeanMs, tauMaxMs);
        std::printf ("%-22s HARTE ABBRUECHE %llu von %llu lauten Toden (%5.1f %%)\n",
                     "", (unsigned long long) abruptDeaths,
                     (unsigned long long) loudBranchDeaths,
                     loudBranchDeaths > 0
                        ? 100.0 * (double) abruptDeaths / (double) loudBranchDeaths : 0.0);
        std::printf ("%-22s Ursachen: Nachfuehrung verloren %llu | neue Identitaet %llu "
                     "| Wurzeln verworfen %llu\n",
                     "", (unsigned long long) trackLost, (unsigned long long) newIds,
                     (unsigned long long) droppedRoots);
        std::printf ("%-22s davon knapp an einem bekannten Zweig vorbei: %llu (%5.1f %%)\n",
                     "", (unsigned long long) newIdsNear,
                     newIds > 0 ? 100.0 * (double) newIdsNear / (double) newIds : 0.0);
        std::uint64_t totalCalls = 0;

        for (int k = 0; k < 8; ++k)
            totalCalls += rootHist[(size_t) k];

        std::printf ("%-22s teuerster Block %8llu Auswertungen bei t=%5.2fs, |M_r| dort %.2f "
                     "(Schnitt %6.0f)\n",
                     "", (unsigned long long) worstBlockEvals, worstBlockAtSec, worstBlockMach,
                     blocks > 0 ? (double) solverEvals / blocks : 0.0);

        std::printf ("%-22s Wurzeln je Aufruf:", "");

        for (int k = 0; k <= 5; ++k)
            std::printf (" %d:%4.1f%%", k,
                         totalCalls > 0 ? 100.0 * (double) rootHist[(size_t) k] / (double) totalCalls : 0.0);

        std::printf (" | eng: %4.1f %% | Wechsel der Wurzelzahl: %llu\n",
                     adjacentPairs > 0 ? 100.0 * (double) tightPairs / (double) adjacentPairs : 0.0,
                     (unsigned long long) countFlips);
        std::printf ("%-22s zwei Zweige auf derselben Wurzel: %llu | Zustand weitergereicht: %llu\n",
                     "", (unsigned long long) collapsedTracks, (unsigned long long) handovers);
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

                if (std::abs (x) > stats.peak)
                {
                    stats.peak      = std::abs (x);
                    stats.peakAtSec = (double) block * blockSize / sampleRate;
                }

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

        // Zweig-Todesmessung: die Zähler im Pfad laufen seit prepareToPlay()
        // aufwärts, hier zählt der Endstand des Laufs. Nicht aufaddieren -
        // sonst stünde die Summe über alle Blöcke statt der Zahl der Todesfälle.
        stats.branchDeaths     = snapshot.branchDeaths;
        stats.loudBranchDeaths = snapshot.loudBranchDeaths;
        stats.deathEnvMean     = snapshot.branchDeathEnvMean;
        stats.deathEnvMax      = snapshot.branchDeathEnvMax;
        stats.branchEvictions  = snapshot.branchEvictions;
        stats.causticDeaths    = snapshot.causticDeaths;
        stats.tauMeanMs        = snapshot.deathTauMeanMs;
        stats.tauMaxMs         = snapshot.deathTauMaxMs;
        stats.abruptDeaths     = snapshot.abruptDeaths;
        stats.trackLost        = snapshot.trackLost;
        stats.newIds           = snapshot.newIds;
        stats.newIdsNear       = snapshot.newIdsNear;
        stats.orderMatches     = snapshot.orderMatches;
        stats.rootHist         = snapshot.rootHist;
        stats.countFlips       = snapshot.countFlips;
        stats.collapsedTracks  = snapshot.collapsedTracks;

        {
            const std::uint64_t blockEvals = proc.solverEvaluations() - evalsBefore - stats.solverEvals;

            if (blockEvals > stats.worstBlockEvals)
            {
                stats.worstBlockEvals = blockEvals;
                stats.worstBlockAtSec = (double) block * blockSize / sampleRate;

                double m = 0.0;

                for (int i = 0; i < snapshot.pathCount; ++i)
                    m = std::max (m, std::abs (snapshot.paths[(size_t) i].machRadial));

                stats.worstBlockMach = m;
            }

            stats.solverEvals = proc.solverEvaluations() - evalsBefore;
        }

        // Das Tempofenster gilt der geflogenen Bahn. Nach dem Ende der Strecke
        // steht die Quelle (das ist so gewollt, siehe holdSourceTargetAt) -
        // dieser Stillstand ist keine Schwankung des Fluges und darf das
        // Fenster nicht aufreissen.
        if (proc.isFlyingBy())
        {
            const double atSec = (double) block * blockSize / sampleRate;

            if (snapshot.sourceSpeed < stats.speedMin)
            {
                stats.speedMin      = snapshot.sourceSpeed;
                stats.speedMinAtSec = atSec;
            }

            if (snapshot.sourceSpeed > stats.speedMax)
            {
                stats.speedMax      = snapshot.sourceSpeed;
                stats.speedMaxAtSec = atSec;
            }
        }

        if (stats.speedFirst < 0.0)
            stats.speedFirst = snapshot.sourceSpeed;
        stats.handovers        = snapshot.handovers;
        stats.tightPairs       = snapshot.tightPairs;
        stats.adjacentPairs    = snapshot.adjacentPairs;
        stats.droppedRoots     = snapshot.droppedRoots;
    }

    stats.solverEvals = proc.solverEvaluations() - evalsBefore;
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

        Stats stats;

        // Gerader Vorbeiflug mit 30 m/s in 10 m Abstand - der Fall aus der
        // H5-Abnahme (Frequenzhub etwa ±9 %). Eine Kreisbahn um den Hörer
        // wäre als Lastfall zwar dieselbe Rechnung, hätte aber konstruktions-
        // bedingt v_r = 0 und damit gar keinen Doppler.
        render (proc, buffer, 6.0, stats, [&proc] (double t)
        {
            setParam (proc, Params::srcX, (float) (0.05 + 30.0 * t / 200.0));
        });

        // Rauchtest der Oberfläche: einmal bauen, layouten und beide
        // Feldansichten in ein Bild zeichnen. Ohne Fenster - es geht darum, dass
        // Aufbau und Zeichnen mit einem ECHTEN Snapshot zusammenpassen.
        //
        // Die perspektivische Ansicht ist der Grund, warum das mehr als eine
        // Formalie ist: sie rechnet eine Projektion mit einer Division durch die
        // Tiefe, und Punkte hinter der Kamera sind dabei der Normalfall, nicht
        // die Ausnahme - genau die Sorte Code, die still ein NaN in eine
        // Zeichenroutine schiebt.
        {
            // Erst eine Lage herstellen, in der es etwas zu zeichnen gibt: eine
            // Quelle über dem Boden (sonst zeigt die perspektivische Ansicht
            // keine Höhe) und eine eingeschaltete Wand. Danach kurz rendern,
            // damit ein Snapshot mit Spur und Wellenfronten vorliegt. Beides
            // läuft nach der eigentlichen Messung und geht in eigene Zähler,
            // damit die Zahlen des Normalfalls unberührt bleiben.
            setParam (proc, Params::srcZ, 25.0f);
            setParam (proc, Params::wall1On, 1.0f);

            Stats displayWarmUp;
            render (proc, buffer, 0.3, displayWarmUp, [] (double) {});

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

            // Anzeige einmal nachführen, wie es sonst der 30-Hz-Timer täte.
            // Ohne das zeichnet die Feldanzeige einen genullten Snapshot, und
            // dann läge in der Projektion jeder Punkt im Ursprung - geprüft wäre
            // damit praktisch nichts. Eine Nachrichtenschleife läuft hier nicht,
            // deshalb der direkte Aufruf.
            if (auto* dopplerEditor = dynamic_cast<DopplerfeldEditor*> (editor.get()))
                dopplerEditor->refreshDisplay();

            auto paintOnce = [&editor]
            {
                juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
                juce::Graphics g (image);
                editor->paintEntireComponent (g, true);
            };

            paintOnce();

            // Umschalten über den Knopf in der Kopfzeile: so wird der Weg
            // geprüft, den ein Benutzer nimmt, und nicht nur die Zeichenroutine
            // selbst. Der Rückruf wird direkt gerufen statt über
            // triggerClick() - das stellt eine Nachricht in die Schlange, und
            // hier läuft keine.
            for (auto* child : editor->getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (child))
                    if (button->getButtonText().startsWith ("Ansicht") && button->onClick)
                        button->onClick();

            paintOnce();

            setParam (proc, Params::srcZ, 0.0f);
            setParam (proc, Params::wall1On, 0.0f);
        }

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
    // 1f. Mehrfachreflexion, und zwar gleich im ungünstigsten Fall: zwei
    //     parallele, nahezu schallharte Wände plus Boden, Dämpfung fast null,
    //     Bounce Gain am Anschlag. Das ist die Aufstellung, bei der ein
    //     Rückkopplungsnetz aufschwingen würde.
    //
    //     Die Spiegelquellen-Methode kann das nicht: jeder Weg LIEST den
    //     geteilten Quellsignalpuffer und schreibt additiv auf den Ausgang, kein
    //     Pfad schreibt je zurück. Es gibt keine Schleife, in der sich eine
    //     Verstärkung aufsammeln könnte. Der Test hält das fest, statt es nur zu
    //     behaupten - geprüft wird, dass der Ausgang nach zehn Sekunden nicht
    //     größer ist als am Anfang.
    {
        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        setParam (proc, Params::fieldMetres, 60.0f);
        setParam (proc, Params::smootherType, 1.0f);
        setParam (proc, Params::lisX, 0.5f);
        setParam (proc, Params::lisY, 0.5f);
        setParam (proc, Params::lisZ, 1.75f);
        setParam (proc, Params::srcX, 0.5f);
        setParam (proc, Params::srcY, 0.5f);
        setParam (proc, Params::srcZ, 2.0f);

        // Alles an, alles hart, Limiter aus - der würde einen Aufschwinger
        // gerade zudecken.
        setParam (proc, Params::groundReflectionOn, 1.0f);
        setParam (proc, Params::groundDampAmount, 0.0f);
        setParam (proc, Params::limiterOn, 0.0f);

        setParam (proc, Params::wall1On, 1.0f);
        setParam (proc, Params::wall1X, 0.5f);
        setParam (proc, Params::wall1Y, 0.05f);
        setParam (proc, Params::wall1Angle, 0.0f);
        setParam (proc, Params::wall1Tilt, 0.0f);
        setParam (proc, Params::wall1Damp, 0.0f);

        setParam (proc, Params::wall2On, 1.0f);
        setParam (proc, Params::wall2X, 0.5f);
        setParam (proc, Params::wall2Y, 0.95f);
        setParam (proc, Params::wall2Angle, 0.0f);
        setParam (proc, Params::wall2Tilt, 0.0f);
        setParam (proc, Params::wall2Damp, 0.0f);

        setParam (proc, Params::reflect2ndOn, 1.0f);
        setParam (proc, Params::bounceGain, 0.95f);

        proc.prepareToPlay (sampleRate, blockSize);

        // Erst zwei Sekunden zum Einschwingen, dann zwei Messfenster mit
        // deutlichem Abstand. Die Quelle steht still - jede Zunahme wäre
        // ausschließlich der Reflexionsstruktur zuzuschreiben.
        Stats warmUp, early, late;

        render (proc, buffer, 2.0, warmUp, [] (double) {});
        render (proc, buffer, 1.0, early,  [] (double) {});
        render (proc, buffer, 8.0, warmUp, [] (double) {});
        render (proc, buffer, 1.0, late,   [] (double) {});

        early.report ("Flatterecho, fruehes s");
        late.report  ("Flatterecho, spaetes s");

        const double rmsEarly = std::sqrt (early.sumSquares[0] / ((double) early.samples * 0.5));
        const double rmsLate  = std::sqrt (late.sumSquares[0]  / ((double) late.samples  * 0.5));

        std::printf ("%-22s RMS früh %.6f -> spät %.6f (Faktor %.3f), Spitze %.4f\n",
                     "", rmsEarly, rmsLate,
                     rmsEarly > 0.0 ? rmsLate / rmsEarly : 0.0,
                     std::max (early.peak, late.peak));

        if (early.nonFinite > 0 || late.nonFinite > 0)
        {
            std::printf ("FEHLGESCHLAGEN: NaN/Inf bei Mehrfachreflexion\n");
            failed = true;
        }

        // Zehn Sekunden später darf es nicht lauter geworden sein. Etwas Luft
        // nach oben, weil die Quelle rauschanteile hat und zwei Sekunden RMS
        // nie exakt gleich ausfallen - aber weit unter allem, was ein
        // Aufschwingen wäre.
        if (rmsEarly > 0.0 && rmsLate > 1.2 * rmsEarly)
        {
            std::printf ("FEHLGESCHLAGEN: Pegel wächst über die Zeit (Faktor %.3f) - "
                         "die Reflexionsstruktur schaukelt sich auf\n", rmsLate / rmsEarly);
            failed = true;
        }

        // Und der Absolutwert muss in einer Größenordnung bleiben, die ein
        // Limiter noch fangen kann.
        if (std::max (early.peak, late.peak) > 100.0)
        {
            std::printf ("FEHLGESCHLAGEN: Ausgangsspitze %.2f - viel zu laut\n",
                         std::max (early.peak, late.peak));
            failed = true;
        }
    }

    //==================================================================
    // 1g. Vorbeiflug-Generatoren. Geprüft wird der Unterschied, um den es bei
    //     den beiden Startvarianten physikalisch geht.
    //
    //     Bei "kontinuierlich" ist die Trajektorie rückwärts mit derselben
    //     Geraden vorbelegt. Der Löser findet deshalb ab dem ersten Moment eine
    //     Wurzel mit M_r > 0 - die Quelle fliegt schon auf den Hörer zu, der
    //     Ton ist von Anfang an hochgestimmt.
    //
    //     Bei "Knall-Start" ist die Vorgeschichte konstant: was der Hörer
    //     zuerst bekommt, ist der Schall einer RUHENDEN Quelle (M_r = 0), weil
    //     die Bewegung gerade erst eingesetzt hat und ihr Schall noch unterwegs
    //     ist.
    //
    //     Gemessen wird deshalb M_r und NICHT der Pegel. Der Pegel taugt hier
    //     nämlich überhaupt nicht, und zwar aus einem hübschen Grund: bei
    //     gleichförmiger Annäherung ist die retardierte Entfernung
    //     R_e = R_0/(1 - M_r), und der Fokussierungsfaktor 1/(1 - M_r) hebt
    //     sich mit ihr exakt weg - A = 1/(R_e (1 - M_r)) = 1/R_0. Beide
    //     Varianten sind im Anflug also gleich laut, sie klingen nur
    //     verschieden hoch. (Gemessen: RMS 0,01746 gegen 0,01754.)
    {
        auto flight = [&] (bool abruptStart, Stats& onsetStats, Stats& earlyStats, Stats& restStats)
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, 300.0f);
            setParam (proc, Params::smootherType, 1.0f);
            setParam (proc, Params::smootherTau, 0.05f);
            setParam (proc, Params::lisX, 0.5f);
            setParam (proc, Params::lisY, 0.5f);
            setParam (proc, Params::lisZ, 1.75f);
            setParam (proc, Params::srcZ, 30.0f);

            // Die Quelle steht VOR dem Flug weit ab vom Hörer. Stand sie in
            // der Feldmitte, stand sie genau auf dem Hörer - der Ausblendsatz
            // war dann beim Umschalten lauter als der ganze Vorbeiflug und
            // taugte als Vergleichsmassstab nicht mehr.
            setParam (proc, Params::srcX, 0.95f);
            setParam (proc, Params::srcY, 0.95f);

            setParam (proc, Params::flyKind,     1.0f);   // waagerecht querend
            setParam (proc, Params::flyStart,    abruptStart ? 1.0f : 0.0f);
            setParam (proc, Params::flyDistance, 40.0f);
            // Anflugstrecke seit der Entkopplung (Fly Dist steuert nur noch
            // den seitlichen Abstand) explizit gesetzt - alter, aus 6x
            // flyDistance abgeleiteter Wert, damit dieses Szenario dieselbe
            // Geometrie/Zeitachse prueft wie zuvor.
            setParam (proc, Params::flyApproach, 240.0f);
            setParam (proc, Params::flySpeed,    200.0f);

            proc.prepareToPlay (sampleRate, blockSize);

            // Kurz laufen lassen, damit die Quelle klingt, dann starten.
            Stats settle;
            render (proc, buffer, 0.5, settle, [] (double) {});

            proc.triggerFlyBy();

            // Der Einsatz bekommt ein eigenes, sehr kurzes Fenster. Im ersten
            // Block blendet die vorher stehende Quelle aus, die direkt beim
            // Hörer stand - das ist die lauteste Stelle des ganzen Anflugs
            // (gemessen 0,0508 im ersten Block gegen 0,0301 bei der Passage)
            // und als Vergleichsmassstab fuer "war da ein Vorbeiflug?"
            // unbrauchbar. Fuer das Anlauftempo ist genau dieser Moment aber
            // der interessante, deshalb wird er gemessen statt uebersprungen.
            render (proc, buffer, 0.05, onsetStats, [] (double) {});

            // Erstes halbes Sekundenfenster: die Quelle ist noch weit weg, der
            // Vorbeiflug liegt noch vor uns, und nur die Vorgeschichte
            // unterscheidet die beiden Varianten.
            render (proc, buffer, 0.45, earlyStats, [] (double) {});
            render (proc, buffer, 2.5,  restStats,  [] (double) {});
        };

        Stats smoothOnset, smoothEarly, smoothRest;
        Stats abruptOnset, abruptEarly, abruptRest;

        flight (false, smoothOnset, smoothEarly, smoothRest);
        flight (true,  abruptOnset, abruptEarly, abruptRest);

        smoothOnset.report ("Vorbeiflug weich, Einsatz");
        smoothEarly.report ("Vorbeiflug weich, Start");
        abruptOnset.report ("Vorbeiflug Knall, Einsatz");
        abruptEarly.report ("Vorbeiflug Knall, Start");

        // Ein Vorbeiflug fliegt vom ERSTEN Moment an mit voller Geschwindigkeit
        // (@dpa 20260819: "es scheint mit Mach1 zu starten ... wieder mit rotem
        // CPU peak, natuerlich mit Aussetzern").
        //
        // Laeuft die Quelle stattdessen an, durchfaehrt sie dabei alle
        // Geschwindigkeiten bis zur eingestellten - bei einem Ueberschallflug
        // also auch Mach 1. Das schreibt eine zusaetzliche Mach-Front in die
        // Trajektorie, die spaeter als zweite Welle eintrifft, mit allem was
        // dazugehoert: Kaustik, Lastspitze, Aussetzer.
        //
        // Geprueft wird auf der Geschwindigkeit selbst, nicht am Klang: sie
        // muss ab dem ersten Snapshot bei den eingestellten 200 m/s stehen und
        // ueber den ganzen Anflug dort bleiben.
        {
            constexpr double flySpeedMps = 200.0;
            constexpr double tolerance   = 0.05;   // 5 %

            std::printf ("%-22s Quelltempo beim Start %7.1f m/s (Soll %.0f) | Fenster %7.1f .. %7.1f m/s\n",
                         "Vorbeiflug Anlauf", smoothOnset.speedFirst, flySpeedMps,
                         smoothOnset.speedMin, smoothOnset.speedMax);

            // Und dasselbe fuer den Rest des Fluges. Damit ist unterscheidbar,
            // ob die Bahn nur beim Start krumm ist oder durchgehend: nur eine
            // wirklich gerade Bahn mit konstantem Tempo laesst den Loeser alle
            // Wurzeln finden (nachgewiesen in solver_check gegen die
            // geschlossene Loesung).
            std::printf ("%-22s im weiteren Flug: Fenster %7.1f m/s (bei t=%.2fs) .. %7.1f m/s (bei t=%.2fs)"
                         " (%+.1f %% Schwankung)\n",
                         "", smoothRest.speedMin, smoothRest.speedMinAtSec,
                         smoothRest.speedMax, smoothRest.speedMaxAtSec,
                         smoothRest.speedMin > 0.0
                            ? 100.0 * (smoothRest.speedMax - smoothRest.speedMin) / flySpeedMps
                            : 0.0);

            if (std::abs (smoothOnset.speedFirst - flySpeedMps) > tolerance * flySpeedMps)
            {
                std::printf ("FEHLGESCHLAGEN: der Vorbeiflug startet mit %.1f m/s statt %.0f - "
                             "die Quelle laeuft erst an und durchfaehrt dabei jede "
                             "Geschwindigkeit darunter\n",
                             smoothOnset.speedFirst, flySpeedMps);
                failed = true;
            }

            if (smoothOnset.speedMin < flySpeedMps * (1.0 - tolerance)
                || smoothOnset.speedMax > flySpeedMps * (1.0 + tolerance))
            {
                std::printf ("FEHLGESCHLAGEN: das Quelltempo schwankt im Anflug zwischen %.1f und "
                             "%.1f m/s, erlaubt sind %.0f +/- %.0f %%\n",
                             smoothOnset.speedMin, smoothOnset.speedMax,
                             flySpeedMps, 100.0 * tolerance);
                failed = true;
            }
        }
        smoothRest.report  ("Vorbeiflug weich, Rest");

        std::printf ("%-22s M_r im Startfenster: weich %.2f, Knall %.2f\n",
                     "", smoothOnset.maxMach, abruptOnset.maxMach);

        const long long nonFinite = smoothOnset.nonFinite + smoothEarly.nonFinite + smoothRest.nonFinite
                                  + abruptOnset.nonFinite + abruptEarly.nonFinite + abruptRest.nonFinite;

        if (nonFinite > 0)
        {
            std::printf ("FEHLGESCHLAGEN: NaN/Inf beim Vorbeiflug\n");
            failed = true;
        }

        if (smoothRest.peak <= 0.0 || abruptRest.peak <= 0.0)
        {
            std::printf ("FEHLGESCHLAGEN: Vorbeiflug bleibt stumm\n");
            failed = true;
        }

        // Der Vorbeiflug muss auch wirklich stattfinden: nahe am Hörer ist er
        // ein Vielfaches lauter als in der Anflugphase.
        if (smoothRest.peak <= 2.0 * smoothEarly.peak)
        {
            std::printf ("FEHLGESCHLAGEN: kein Vorbeiflug erkennbar (Spitze Anflug %.4f bei t=%.2fs, "
                         "Vorbeiflug %.4f bei t=%.2fs)\n",
                         smoothEarly.peak, smoothEarly.peakAtSec,
                         smoothRest.peak, smoothRest.peakAtSec);
            failed = true;
        }

        // Der Kern: die vorbelegte Vorgeschichte muss dem Löser von der ersten
        // Probe an eine fliegende Quelle liefern. Erwartet sind rund 0,58
        // (200 m/s fast frontal), also deutlich über 0,4.
        if (smoothEarly.maxMach < 0.4)
        {
            std::printf ("FEHLGESCHLAGEN: kontinuierlicher Start liefert kein M_r > 0 "
                         "(%.2f) - die Vorgeschichte ist nicht vorbelegt\n",
                         smoothEarly.maxMach);
            failed = true;
        }

        // Und der Gegenbeweis: der Knall-Start darf im selben Fenster GAR
        // keine Bewegung zeigen, sonst unterscheiden sich die Varianten nicht.
        if (abruptEarly.maxMach > 0.05)
        {
            std::printf ("FEHLGESCHLAGEN: Knall-Start zeigt schon Bewegung (M_r %.2f) - "
                         "die Vorgeschichte ist nicht konstant\n", abruptEarly.maxMach);
            failed = true;
        }
    }

    //==================================================================
    // 1g2. Vorbeiflug: liegt die geflogene Bahn wirklich zwischen Start- und
    //      Endpunkt? (@dpa 20260819: "ist der Start und endpunkt des
    //      vorbeifluges oft falsch", Preset woandersVorbeiflug.)
    //
    //      Die Werte stammen aus genau diesem Preset. Zwei Dinge machen es
    //      heikel, und beide sind darin eingestellt:
    //
    //        - Der gemeinsame Tempo-Deckel (Max Speed 428,9 m/s) liegt unter
    //          der Fluggeschwindigkeit (1107,2 m/s). Der Generator darf dann
    //          nicht mit dem Reglerwert weiterzaehlen, sonst ist er am Ende der
    //          Strecke, waehrend die Quelle noch mittendrin ist.
    //        - Der Glaetter hat eine sehr lange Zeitkonstante (0,64 s). Sein
    //          Nachlauf betraegt bei diesem Tempo mehrere hundert Meter und
    //          muss vorn ausgeglichen werden, ohne die Strecke hinten zu
    //          verkuerzen.
    //
    //      Gemessen wird die Geometrie, nicht der Klang: Startpunkt, Endpunkt
    //      und Flugdauer.
    {
        constexpr double flySpeedMps = 1107.19;
        constexpr double maxSpeedMps = 428.88;
        constexpr double approachM   = 1095.58;

        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        setParam (proc, Params::fieldMetres,    5927.6f);
        setParam (proc, Params::lisX,           0.5036f);
        setParam (proc, Params::lisY,           0.2929f);
        setParam (proc, Params::lisZ,           1.926f);
        setParam (proc, Params::srcX,           0.2937f);
        setParam (proc, Params::srcY,           0.4224f);
        setParam (proc, Params::srcZ,           19.885f);
        setParam (proc, Params::smootherType,   1.0f);
        setParam (proc, Params::smootherTau,    0.6369f);
        setParam (proc, Params::globalMaxSpeed, (float) maxSpeedMps);
        setParam (proc, Params::flyKind,        1.0f);    // waagerecht querend
        setParam (proc, Params::flyStart,       0.0f);
        setParam (proc, Params::flyDistance,    382.92f);
        setParam (proc, Params::flyApproach,    (float) approachM);
        setParam (proc, Params::flySpeed,       (float) flySpeedMps);

        proc.prepareToPlay (sampleRate, blockSize);

        juce::MidiBuffer midi;
        FieldSnapshot    snapshot;

        Stats settle;
        render (proc, buffer, 0.3, settle, [] (double) {});

        proc.fillFieldSnapshot (snapshot);
        const double tTrigger = snapshot.now;

        proc.triggerFlyBy();

        Vec3   plannedStart, plannedEnd, firstSeen, atEnd;
        bool   sawStart  = false;
        double flightSec = -1.0;

        const int numBlocks = (int) std::ceil (20.0 * sampleRate / blockSize);

        for (int block = 0; block < numBlocks; ++block)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
            proc.fillFieldSnapshot (snapshot);

            const double atSec = (double) block * blockSize / sampleRate;

            // Der erste Snapshot, der wirklich NACH dem Auslösen entstanden
            // ist. Der davor liegende zeigt noch die alte Position und haette
            // die Startmessung um die ganze Sprungweite verfaelscht.
            if (snapshot.flyByActive && ! sawStart && snapshot.now > tTrigger)
            {
                sawStart = true;
                firstSeen = snapshot.sourcePos;

                // Der Startpunkt ergibt sich aus den beiden Punkten, die der
                // Snapshot fuer die Wegvorschau ohnehin mitfuehrt: der
                // naechste Punkt liegt in der Mitte der Strecke.
                plannedEnd   = snapshot.flyByPlannedEnd;
                plannedStart = snapshot.flyByNearestPoint * 2.0 - plannedEnd;
            }

            if (sawStart && ! snapshot.flyByActive && flightSec < 0.0)
                flightSec = atSec;
        }

        proc.fillFieldSnapshot (snapshot);
        atEnd = snapshot.sourcePos;

        const double pathLength  = (plannedEnd - plannedStart).length();
        const double startError  = (firstSeen - plannedStart).length();
        const double endError    = (atEnd - plannedEnd).length();
        const double expectedSec = pathLength / std::min (flySpeedMps, maxSpeedMps);

        std::printf ("%-22s Bahn %.0f m | Start daneben %6.1f m | Ende daneben %6.1f m | "
                     "Dauer %.2f s (erwartet %.2f s)\n",
                     "Vorbeiflug Geometrie", pathLength, startError, endError,
                     flightSec, expectedSec);

        if (! sawStart)
        {
            std::printf ("FEHLGESCHLAGEN: der Vorbeiflug ist gar nicht angelaufen\n");
            failed = true;
        }

        // Startpunkt: die Quelle wird beim Auslösen dorthin gesetzt. Die
        // Toleranz deckt allein ab, dass der Snapshot mit ~30 Hz erscheint und
        // die Quelle in dieser Zeit schon ein Stueck geflogen ist.
        if (startError > 0.03 * pathLength)
        {
            std::printf ("FEHLGESCHLAGEN: der Vorbeiflug beginnt %.1f m neben seinem Startpunkt "
                         "(Bahn %.0f m)\n", startError, pathLength);
            failed = true;
        }

        // Endpunkt: hier ist alles ausgeschwungen, die Quelle muss genau auf
        // dem geplanten Ende stehen.
        if (endError > 1.0)
        {
            std::printf ("FEHLGESCHLAGEN: der Vorbeiflug endet %.1f m neben seinem Endpunkt "
                         "(Bahn %.0f m)\n", endError, pathLength);
            failed = true;
        }

        // Dauer: der schaerfste der drei Punkte. Endet der Flug zu frueh, hat
        // die Quelle die Strecke nicht geflogen, sondern ist den Rest nur noch
        // ausgeschlichen.
        if (flightSec < 0.0 || std::abs (flightSec - expectedSec) > 0.1 * expectedSec)
        {
            std::printf ("FEHLGESCHLAGEN: der Vorbeiflug dauert %.2f s statt %.2f s - die Quelle "
                         "fliegt die Strecke nicht ab\n", flightSec, expectedSec);
            failed = true;
        }
    }

    //==================================================================
    // 1h. N-Wellen-Schicht. Zwei Dinge sind hier zu zeigen, und das zweite ist
    //     das wichtigere:
    //
    //     a) Bei einem Überschall-Vorbeiflug muss sie hörbar etwas beitragen -
    //        sonst löst der M_r=1-Test nie aus und die ganze Schicht wäre tot.
    //     b) Bei einem UNTERSCHALL-Vorbeiflug muss der Ausgang exakt
    //        unverändert bleiben. Das ist die Zusicherung, dass es sich um eine
    //        additive Zusatzschicht handelt und nicht um einen Eingriff in die
    //        bestehende Amplitudenformel: ohne Machfront gibt es keine
    //        Auslösung, also auch kein einziges verändertes Sample.
    {
        auto flight = [&] (bool nWaveEnabled, double speedMetresPerSecond, Stats& stats)
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, 2000.0f);
            setParam (proc, Params::smootherType, 1.0f);
            setParam (proc, Params::smootherTau, 0.05f);
            setParam (proc, Params::lisX, 0.5f);
            setParam (proc, Params::lisY, 0.5f);
            setParam (proc, Params::lisZ, 1.75f);
            setParam (proc, Params::srcZ, 200.0f);
            setParam (proc, Params::limiterOn, 0.0f);

            setParam (proc, Params::nWaveOn,   nWaveEnabled ? 1.0f : 0.0f);
            setParam (proc, Params::nWaveSize, 15.0f);

            setParam (proc, Params::flyKind,     1.0f);   // waagerecht querend
            setParam (proc, Params::flyStart,    0.0f);   // kontinuierlich
            setParam (proc, Params::flyDistance, 300.0f);
            // Alter, aus 6x flyDistance abgeleiteter Wert (siehe Kommentar
            // oben) - der Unterschallfall braucht die lange Anflugstrecke,
            // damit er ueber das gesamte 12s-Fenster subsonic bleibt.
            setParam (proc, Params::flyApproach, 1800.0f);
            setParam (proc, Params::flySpeed,    (float) speedMetresPerSecond);

            proc.prepareToPlay (sampleRate, blockSize);

            Stats settle;
            render (proc, buffer, 0.3, settle, [] (double) {});

            proc.triggerFlyBy();

            render (proc, buffer, 12.0, stats, [] (double) {});
        };

        Stats supersonicOff, supersonicOn, subsonicOff, subsonicOn;

        flight (false, 700.0, supersonicOff);   // rund Mach 2
        flight (true,  700.0, supersonicOn);
        flight (false, 100.0, subsonicOff);
        flight (true,  100.0, subsonicOn);

        supersonicOff.report ("Mach2-Flug, N aus");
        supersonicOn.report  ("Mach2-Flug, N an");
        subsonicOn.report    ("100 m/s, N an");

        const long long nonFinite = supersonicOn.nonFinite + subsonicOn.nonFinite;

        if (nonFinite > 0)
        {
            std::printf ("FEHLGESCHLAGEN: NaN/Inf in der N-Wellen-Schicht\n");
            failed = true;
        }

        // a) Im Überschall muss sich der Ausgang messbar ändern.
        const double peakOff = supersonicOff.peak;
        const double peakOn  = supersonicOn.peak;

        std::printf ("%-22s Ueberschall: Spitze ohne %.4f, mit %.4f | Unterschall: %.6f / %.6f\n",
                     "", peakOff, peakOn, subsonicOff.peak, subsonicOn.peak);

        if (supersonicOn.maxMach <= 1.0)
        {
            std::printf ("FEHLGESCHLAGEN: der Testflug erreicht keinen Ueberschall "
                         "(M_r max %.2f) - der Fall greift nicht\n", supersonicOn.maxMach);
            failed = true;
        }
        else if (std::abs (peakOn - peakOff) <= 1.0e-6 * std::max (peakOff, 1.0e-9))
        {
            std::printf ("FEHLGESCHLAGEN: N-Welle aendert den Ueberschallflug nicht "
                         "(Spitze ohne %.6f, mit %.6f) - sie loest nie aus\n",
                         peakOff, peakOn);
            failed = true;
        }

        // b) Im Unterschall muss der Ausgang bitgleich sein. Bewusst ohne
        //    Toleranz: ohne Machfront gibt es keinen Auslöser, und damit darf
        //    sich kein einziges Sample unterscheiden.
        if (std::abs (subsonicOn.peak - subsonicOff.peak) > 0.0
            || std::abs (subsonicOn.sumSquares[0] - subsonicOff.sumSquares[0]) > 0.0)
        {
            std::printf ("FEHLGESCHLAGEN: N-Welle veraendert den Unterschallfall "
                         "(Spitze %.9f gegen %.9f) - sie ist keine reine Zusatzschicht\n",
                         subsonicOff.peak, subsonicOn.peak);
            failed = true;
        }
    }

    //==================================================================
    // 1i. Klone ("Schrot"-Muster). Vier Dinge sind hier zu zeigen, und die
    //     letzten zwei sind die wichtigen:
    //
    //     a) Echte Klone verändern den Ausgang und kosten Löserlast, und zwar
    //        linear mit ihrer Anzahl - das ist die Zusicherung, auf die sich der
    //        Regler beruft.
    //     b) Billige Klone verändern den Ausgang, kosten aber NULL Löserlast.
    //        Wenn dort auch nur eine Auswertung mehr anfällt, ist die
    //        Nachbildung keine Nachbildung, sondern ein zweiter Löser.
    //     c) Der Notaus muss die Last wirklich auf den Direktpfad zurückholen.
    //     d) Nichts davon darf entgleisen.
    {
        auto run = [&] (int total, int real, bool panicHalfway, Stats& stats)
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

            setParam (proc, Params::cloneTotal,  (float) total);
            setParam (proc, Params::cloneReal,   (float) real);
            setParam (proc, Params::cloneAuto,   0.0f);   // Automatik hier aus
            setParam (proc, Params::cloneSpread, 4.0f);
            setParam (proc, Params::cloneLevel,  0.5f);

            proc.prepareToPlay (sampleRate, blockSize);

            render (proc, buffer, 3.0, stats, [&] (double t)
            {
                setParam (proc, Params::srcX, (float) (0.05 + 30.0 * t / 200.0));

                if (panicHalfway && t >= 1.5)
                    proc.panicToMinimal();
            });
        };

        Stats none, realOnly, cheapOnly, panicked;

        run (0,  0,  false, none);
        run (10, 10, false, realOnly);
        run (10, 0,  false, cheapOnly);
        run (10, 10, true,  panicked);

        none.report      ("Ohne Klone");
        realOnly.report  ("10 echte Klone");
        cheapOnly.report ("10 billige Klone");
        panicked.report  ("10 echte + Notaus");

        auto perBlock = [] (const Stats& s)
        {
            return s.blocks > 0 ? (double) s.solverEvals / s.blocks : 0.0;
        };

        auto rmsOf = [] (const Stats& s)
        {
            return std::sqrt (s.sumSquares[0] / ((double) s.samples * 0.5));
        };

        const double evalsNone  = perBlock (none);
        const double evalsReal  = perBlock (realOnly);
        const double evalsCheap = perBlock (cheapOnly);
        const double evalsPanic = perBlock (panicked);

        std::printf ("%-22s Löser/Block: ohne %.0f, echt %.0f (%.1fx), billig %.0f, "
                     "nach Notaus %.0f\n",
                     "", evalsNone, evalsReal,
                     evalsNone > 0.0 ? evalsReal / evalsNone : 0.0,
                     evalsCheap, evalsPanic);

        const long long nonFinite = realOnly.nonFinite + cheapOnly.nonFinite + panicked.nonFinite;

        if (nonFinite > 0)
        {
            std::printf ("FEHLGESCHLAGEN: NaN/Inf mit Klonen\n");
            failed = true;
        }

        // a) Zehn echte Klone sind elf Quellen, also rund elffache Löserlast.
        //    Grosszügige Schranken: die Klone liegen an anderen Stellen und
        //    brauchen dort nicht genau gleich viele Schritte.
        if (evalsNone <= 0.0 || evalsReal < 6.0 * evalsNone || evalsReal > 16.0 * evalsNone)
        {
            std::printf ("FEHLGESCHLAGEN: zehn echte Klone kosten %.1fx statt der "
                         "erwarteten ~11x\n", evalsNone > 0.0 ? evalsReal / evalsNone : 0.0);
            failed = true;
        }

        if (std::abs (rmsOf (realOnly) - rmsOf (none)) <= 1.0e-6 * rmsOf (none))
        {
            std::printf ("FEHLGESCHLAGEN: echte Klone ändern den Ausgang nicht\n");
            failed = true;
        }

        // b) Die billige Nachbildung darf KEINE einzige Löserauswertung
        //    zusätzlich verursachen - sonst ist sie keine.
        if (std::abs (evalsCheap - evalsNone) > 0.0)
        {
            std::printf ("FEHLGESCHLAGEN: billige Klone kosten Löserlast (%.0f statt %.0f "
                         "pro Block) - sie sind keine Nachbildung\n", evalsCheap, evalsNone);
            failed = true;
        }

        if (std::abs (rmsOf (cheapOnly) - rmsOf (none)) <= 1.0e-6 * rmsOf (none))
        {
            std::printf ("FEHLGESCHLAGEN: billige Klone ändern den Ausgang nicht\n");
            failed = true;
        }

        // c) Der Notaus muss wirken. Über den ganzen Lauf gemittelt liegt die
        //    Last danach zwischen "voll" und "ohne" - geprüft wird deshalb, dass
        //    sie deutlich unter dem vollen Fall liegt.
        if (evalsPanic >= 0.75 * evalsReal)
        {
            std::printf ("FEHLGESCHLAGEN: Notaus senkt die Löserlast nicht "
                         "(%.0f gegen %.0f pro Block)\n", evalsPanic, evalsReal);
            failed = true;
        }
    }

    //==================================================================
    // 1j. Bewegungsaufzeichnung im gespeicherten Zustand (@dpa: "Recorded muss
    //     in state! und State laden muss Record laden! Und wenn Play beim save
    //     aktiv war, soll es beim laden direkt play'en").
    //
    //     Drei Zusicherungen, die keine einzelne Klasse allein prüfen kann:
    //
    //     a) Die Frames landen wirklich im Zustand - der Blob wird dadurch um
    //        mindestens ihre Rohgröße größer als einer ohne Aufnahme.
    //     b) Ein geladener Zustand füllt Recorder UND Player, und die Frames
    //        kommen unverändert zurück (derselbe Zustand, zweimal gespeichert,
    //        trägt dieselben Rohdaten).
    //     c) Lief die Wiedergabe beim Speichern, läuft sie nach dem Laden von
    //        selbst wieder.
    {
        // Das Frames-Attribut aus dem Zustandsblob herausschneiden.
        // copyXmlToBinary legt 8 Byte Kopf, dann den XML-Text und dann eine 0
        // ab; getXmlFromBinary ist geschützt und hier nicht erreichbar. Ein
        // Vergleich der ganzen Blobs wäre die schwächere Prüfung: er hinge
        // zusätzlich an der Textform jedes Reglerwerts.
        auto framesAttribute = [] (const juce::MemoryBlock& mb) -> juce::String
        {
            if (mb.getSize() < 10)
                return {};

            const juce::String text (static_cast<const char*> (mb.getData()) + 8,
                                     mb.getSize() - 9);

            const juce::String key ("base64:motionFrames=\"");
            const int at = text.indexOf (key);

            if (at < 0)
                return {};

            const int from = at + key.length();
            const int to   = text.indexOfChar (from, '"');

            return to > from ? text.substring (from, to) : juce::String();
        };

        auto configure = [] (DopplerfeldProcessor& p)
        {
            p.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (p, Params::fieldMetres, 200.0f);
            setParam (p, Params::smootherType, 1.0f);
            setParam (p, Params::lisX, 0.5f);
            setParam (p, Params::lisY, 0.5f);
            setParam (p, Params::srcX, 0.2f);
            setParam (p, Params::srcY, 0.5f);
        };

        juce::MemoryBlock savedWithout, saved, savedAgain, savedWhilePlaying;

        int  framesRecorded   = 0;
        int  framesLoaded     = 0;
        bool playingAtSave    = false;
        bool playingAfterLoad = false;

        Stats recordStats, loadStats, playStats;

        {
            DopplerfeldProcessor proc;
            configure (proc);
            proc.prepareToPlay (sampleRate, blockSize);

            // Vergleichsgröße für a): derselbe Zustand ohne Aufnahme.
            proc.getStateInformation (savedWithout);

            proc.toggleRecording();

            render (proc, buffer, 1.0, recordStats, [&proc] (double t)
            {
                setParam (proc, Params::srcX, (float) (0.2 + 20.0 * t / 200.0));
            });

            // Der Stopp wird wie der Start erst im nächsten Block abgeholt -
            // dort wandert der Clip auch in den Player.
            proc.toggleRecording();
            render (proc, buffer, 0.1, recordStats, [] (double) {});

            framesRecorded = proc.recordedFrameCount();
            proc.getStateInformation (saved);

            proc.triggerPlayback();
            render (proc, buffer, 0.1, playStats, [] (double) {});

            playingAtSave = proc.isPlayingMotion();
            proc.getStateInformation (savedWhilePlaying);
        }

        {
            // Reihenfolge wie im Host: Zustand setzen, DANN vorbereiten. Genau
            // deshalb darf setStateInformation nicht selbst in den Player
            // schreiben - prepareToPlay käme danach und würde es überholen.
            DopplerfeldProcessor proc;
            configure (proc);
            proc.setStateInformation (saved.getData(), (int) saved.getSize());
            proc.prepareToPlay (sampleRate, blockSize);

            render (proc, buffer, 0.1, loadStats, [] (double) {});

            framesLoaded = proc.recordedFrameCount();
            proc.getStateInformation (savedAgain);
        }

        {
            DopplerfeldProcessor proc;
            configure (proc);
            proc.setStateInformation (savedWhilePlaying.getData(), (int) savedWhilePlaying.getSize());
            proc.prepareToPlay (sampleRate, blockSize);

            render (proc, buffer, 0.1, playStats, [] (double) {});

            playingAfterLoad = proc.isPlayingMotion();
        }

        const juce::String rawSaved      = framesAttribute (saved);
        const juce::String rawSavedAgain = framesAttribute (savedAgain);

        std::printf ("%-22s Zustand ohne Aufnahme %zu B, mit Aufnahme %zu B | Frames "
                     "aufgezeichnet %d, geladen %d | Play nach Laden %s\n",
                     "Bewegung im State", savedWithout.getSize(), saved.getSize(),
                     framesRecorded, framesLoaded, playingAfterLoad ? "ja" : "nein");

        if (framesRecorded < 100)
        {
            std::printf ("FEHLGESCHLAGEN: nur %d Frames aufgezeichnet - der Testfall "
                         "greift nicht\n", framesRecorded);
            failed = true;
        }

        // a) Drei double je Frame sind 24 Byte; base64 macht daraus mehr, nie
        //    weniger. Der Zustand muss also mindestens um so viel wachsen.
        if (saved.getSize() < savedWithout.getSize() + (size_t) framesRecorded * 24)
        {
            std::printf ("FEHLGESCHLAGEN: Zustand waechst nur um %zu Byte, %d Frames "
                         "brauchen mindestens %zu - die Aufzeichnung steht nicht drin\n",
                         saved.getSize() - savedWithout.getSize(), framesRecorded,
                         (size_t) framesRecorded * 24);
            failed = true;
        }

        // b) Recorder und Player sind gefüllt, und die Rohdaten sind dieselben.
        if (framesLoaded != framesRecorded)
        {
            std::printf ("FEHLGESCHLAGEN: nach dem Laden %d Frames statt %d\n",
                         framesLoaded, framesRecorded);
            failed = true;
        }

        if (rawSaved.isEmpty() || rawSaved != rawSavedAgain)
        {
            std::printf ("FEHLGESCHLAGEN: Frames kommen veraendert zurueck (%d gegen %d "
                         "Zeichen Rohdaten)\n", rawSaved.length(), rawSavedAgain.length());
            failed = true;
        }

        // c) War die Wiedergabe beim Speichern an, muss sie nach dem Laden
        //    wieder laufen - ohne dass jemand Play drückt.
        if (! playingAtSave)
        {
            std::printf ("FEHLGESCHLAGEN: Wiedergabe lief beim Speichern nicht - der "
                         "Testfall greift nicht\n");
            failed = true;
        }
        else if (! playingAfterLoad)
        {
            std::printf ("FEHLGESCHLAGEN: Wiedergabe laeuft nach dem Laden nicht wieder an\n");
            failed = true;
        }

        if (loadStats.nonFinite + playStats.nonFinite > 0)
        {
            std::printf ("FEHLGESCHLAGEN: NaN/Inf nach dem Laden einer Aufzeichnung\n");
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

    //==================================================================
    // 1i. DIAGNOSE (noch ohne Assertion): @dpa berichtet bei ~2000 km/h
    //     (556 m/s) einen kurzen hohen Pegel, der nach rund 250 ms
    //     abstandsabhängig abbricht - bei ~1500 km/h (417 m/s) nicht. Misst
    //     längste Stille und Löserlast über den gesamten Vorbeiflug, für
    //     mehrere Vorbeiflugabstände.
    {
        auto flight = [&] (double speedMps, double distanceM, Stats& stats)
        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, 4000.0f);
            setParam (proc, Params::smootherType, 1.0f);   // Critically Damped Spring (App-Default)
            setParam (proc, Params::smootherTau, 0.05f);
            setParam (proc, Params::lisX, 0.5f);
            setParam (proc, Params::lisY, 0.5f);
            setParam (proc, Params::lisZ, 1.75f);
            setParam (proc, Params::srcZ, 0.0f);

            setParam (proc, Params::flyKind,     0.0f);   // durch den Bildschirm
            setParam (proc, Params::flyStart,    0.0f);   // kontinuierlich
            setParam (proc, Params::flyDistance, (float) distanceM);
            setParam (proc, Params::flyApproach, 300.0f);
            setParam (proc, Params::flySpeed,    (float) speedMps);

            proc.prepareToPlay (sampleRate, blockSize);

            Stats settle;
            render (proc, buffer, 0.3, settle, [] (double) {});

            proc.triggerFlyBy();

            // Nur die aktive Flugdauer (2*halfLength/speed) - KEIN Puffer
            // danach, sonst mischt sich die (erwartungsgemaess leise) Stille
            // NACH Flugende in "laengste Stille" hinein und verwaesserst die
            // Messung.
            const double flightSeconds = 2.0 * 300.0 / speedMps;
            render (proc, buffer, flightSeconds, stats, [] (double) {});
        };

        for (double distanceM : { 5.0, 10.0, 20.0, 50.0, 100.0, 300.0 })
        {
            Stats fast, slow;
            flight (555.56, distanceM, fast);
            flight (416.67, distanceM, slow);

            char title1[32];
            char title2[32];
            std::snprintf (title1, sizeof (title1), "2000kmh d=%.0fm", distanceM);
            std::snprintf (title2, sizeof (title2), "1500kmh d=%.0fm", distanceM);
            fast.report (title1);
            slow.report (title2);
        }
    }

    //==================================================================
    // Kaustik-Abbruch (@dpa 20260819): faellt der Pegel schlagartig ab, wenn
    // ein Ueberschall-Zweig verschwindet?
    //
    // Das ist der Fall, den @dpa bisher als einziger pruefen konnte ("da gibt
    // es wieder diesen Abbruch"). Zwei Dinge machen ihn hier messbar statt
    // hoerbar:
    //
    //   1. TONALE Quelle. Die Rauschanteile werden abgeschaltet. Mit der
    //      normalen Motorquelle schwankt ein Millisekunden-Effektivwert von
    //      selbst um zwanzig und mehr dB - darin verschwindet jeder echte
    //      Sturz. Ohne Rauschen ist die Huellkurve glatt und ein Sturz sticht
    //      heraus. Die N-Wellen-Schicht ist ebenfalls aus: sie ist eine eigene,
    //      additive Schicht und wuerde die Luecke zuschuetten, um die es geht.
    //
    //   2. VERGLEICH statt absoluter Schwelle. Dieselbe Bahn wird zweimal
    //      geflogen, einmal mit Ueberschall und einmal knapp darunter. Der
    //      Unterschall-Lauf ist der Nachweis, dass Bahn, Bremsen und Messung
    //      selbst keinen Sturz erzeugen. Erst der Abstand zwischen beiden ist
    //      die Aussage.
    //
    // Die Bahn bremst gleichmaessig von v0 auf null, fliegt also bei v0 ueber
    // der Schallgeschwindigkeit genau einmal durch Mach 1 - der Moment, in dem
    // die zusaetzlichen Zweige zusammenlaufen und verschwinden.
    {
        constexpr double fieldM   = 2000.0;
        constexpr double lateralM = 150.0;

        auto flyDecelerating = [&] (double v0, Stats& stats)
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, (float) fieldM);
            setParam (proc, Params::smootherType, 1.0f);
            setParam (proc, Params::lisX, 0.5f);
            setParam (proc, Params::lisY, 0.5f);
            setParam (proc, Params::lisZ, 1.75f);
            setParam (proc, Params::srcZ, 50.0f);

            // Tonale Quelle, siehe Kommentar oben.
            setParam (proc, Params::noiseGainLo, 0.0f);
            setParam (proc, Params::noiseGainHi, 0.0f);
            setParam (proc, Params::nWaveOn, 0.0f);
            setParam (proc, Params::limiterOn, 0.0f);

            const double startX = 0.05;
            const double travel = v0 * decelSeconds / 2.0;   // gleichmaessig auf null gebremst

            setParam (proc, Params::srcX, (float) startX);
            setParam (proc, Params::srcY,
                      (float) (0.5 + lateralM / (fieldM * DopplerfeldProcessor::fieldAspect)));

            proc.prepareToPlay (sampleRate, blockSize);

            render (proc, buffer, decelSeconds, stats, [&proc, v0, startX, travel] (double t)
            {
                // x(t) = v0*(t - t^2/(2T)), also v(t) = v0*(1 - t/T).
                const double u = std::min (1.0, t / decelSeconds);
                const double x = v0 * decelSeconds * (u - 0.5 * u * u);

                setParam (proc, Params::srcX, (float) (startX + x / fieldM));

                (void) travel;
            });
        };

        Stats supersonic, subsonic;

        flyDecelerating (700.0, supersonic);   // Mach 2,04 herunter auf null
        flyDecelerating (300.0, subsonic);     // bleibt durchgehend unter Mach 1

        supersonic.report ("Bremsflug Mach 2,04");
        subsonic.report   ("Bremsflug Mach 0,87");

        std::printf ("%-22s Pegelsturz Ueberschall %.1f dB gegen Unterschall %.1f dB "
                     "(Abstand %.1f dB)\n",
                     "", supersonic.worstDropDb, subsonic.worstDropDb,
                     supersonic.worstDropDb - subsonic.worstDropDb);

        if (supersonic.maxMach <= 1.0)
        {
            std::printf ("FEHLGESCHLAGEN: der Bremsflug erreicht keinen Ueberschall "
                         "(M_r max %.2f) - der Fall greift nicht\n", supersonic.maxMach);
            failed = true;
        }

        // Das eigentliche Kriterium. Nicht am Summensignal gemessen - dort
        // schwankt der Pegel von selbst zu stark, um einen Abbruch von einer
        // gewoehnlichen Pegelbewegung zu unterscheiden (der Unterschall-Lauf
        // zeigt hier sogar den STEILEREN Sturz, obwohl er nachweislich in
        // Ordnung klingt). Gemessen wird stattdessen am Zweig selbst:
        //
        //     Ein Zweig, der mit env >= 0,5 stirbt, darf nicht in unter 2 ms
        //     auf null gehen.
        //
        // Das ist der Abbruch als pruefbare Aussage. Verdraengungen zaehlen
        // mit, sie sind selbst ein sofortiges Abschneiden.
        if (supersonic.abruptDeaths > 0)
        {
            std::printf ("FEHLGESCHLAGEN: %llu von %llu lauten Zweigtoden brechen in unter "
                         "2 ms ab (%.1f %%) - der Pegel faellt beim Verschwinden eines "
                         "Zweigs schlagartig ab\n",
                         (unsigned long long) supersonic.abruptDeaths,
                         (unsigned long long) supersonic.loudBranchDeaths,
                         supersonic.loudBranchDeaths > 0
                            ? 100.0 * (double) supersonic.abruptDeaths
                                    / (double) supersonic.loudBranchDeaths : 0.0);
            failed = true;
        }

        if (subsonic.abruptDeaths > 0)
        {
            std::printf ("FEHLGESCHLAGEN: schon im Unterschall brechen %llu laute Zweigtode "
                         "hart ab - dort darf gar keiner sterben\n",
                         (unsigned long long) subsonic.abruptDeaths);
            failed = true;
        }
    }

    std::printf (failed ? "FEHLGESCHLAGEN\n" : "OK\n");
    return failed ? 1 : 0;
}
