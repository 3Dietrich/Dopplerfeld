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
#include "UI/RoundedSlider.h"
#include "UI/ScopeComponent.h"
#include "UI/Labels.h"
#include "Sources/EngineGenerator.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

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

    // Dieselbe Messung in die andere Richtung: der steilste ANSTIEG. Damit
    // laesst sich pruefen, ob ein abrupt einsetzender Vorbeiflug ("Knall-
    // Start") ueberhaupt eine Kante im Signal hinterlaesst - ein
    // Geschwindigkeitssprung springt in Amplitude und Tonhoehe, wird aber
    // innerhalb eines Solver-Segments interpoliert und koennte deshalb
    // verschmieren (@dpa 20260823: "Bisher ist noch nicht zu hoeren!").
    double worstRiseDb    = 0.0;
    double worstRiseAtSec = 0.0;
    double envSeconds     = 0.0;

    void noteLevelWindow (double rms)
    {
        double before = 0.0;

        for (double v : envHistory)
            before = std::max (before, v);

        envSeconds += (double) envWindow / 48000.0;

        if (before > envAudible)
        {
            const double drop = 20.0 * std::log10 ((rms + 1.0e-12) / before);
            worstDropDb = std::min (worstDropDb, drop);

            if (drop > worstRiseDb)
            {
                worstRiseDb    = drop;
                worstRiseAtSec = envSeconds;
            }
        }

        for (int i = envLookback - 1; i > 0; --i)
            envHistory[i] = envHistory[i - 1];

        envHistory[0] = rms;
    }

    // Groesster Sprung von einem Sample zum naechsten. Feiner als das
    // 1-ms-Fenster darueber und genau das Mass fuer eine KANTE: ob ein
    // Bewegungssprung stehen bleibt oder ueber ein Solver-Segment (1,33 ms)
    // zur Rampe verschmiert wird, entscheidet sich unterhalb dieses Fensters.
    double maxSampleStep = 0.0;
    double prevSample    = 0.0;
    bool   hadPrevSample = false;

    void noteSample (double x, double dt)
    {
        constexpr double audible = 1.0e-4;

        if (hadPrevSample)
            maxSampleStep = std::max (maxSampleStep, std::abs (x - prevSample));

        prevSample    = x;
        hadPrevSample = true;

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

        std::printf ("%-22s steilster Pegelsturz %6.1f dB in %d ms | steilster Anstieg %6.1f dB "
                     "(bei t=%.2fs) | groesster Samplesprung %.5f\n",
                     "", worstDropDb, envLookback, worstRiseDb, worstRiseAtSec, maxSampleStep);
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

// --- Prüfung der Beschriftungen auf verunglückte Umlaute ---
//
// juce::String nimmt bei einem nackten `const char*` an, es sei ASCII, also
// ein Zeichen je Byte. Die Quelldateien dieses Projekts sind aber UTF-8, und
// dort besteht ein "ü" aus den zwei Bytes C3 BC - aus denen werden auf diesem
// Weg die zwei Zeichen "Ã¼". Genau so stand "DÃ¼senantrieb" in der
// Betriebsart-Auswahl (@dpa 20260824: "mit Umlauten scheinst Du deine
// Probleme zu haben").
//
// Der Fehler ist nicht am Quelltext zu sehen, dort steht das "ü" richtig. Er
// ist erst an dem zu sehen, was auf dem Bildschirm ankommt - und deshalb wird
// hier der ECHTE Editor abgelaufen und jede sichtbare Zeichenkette geprüft.
//
// Erkannt wird an den beiden Zeichen Â (U+00C2) und Ã (U+00C3): sie sind die
// erste Hälfte jedes so verunglückten Zeichens und kommen in keinem
// deutschen oder englischen Text vor, den dieses Plugin anzeigt. Wer eines
// davon je legitim braucht, muss diese Prüfung erweitern - und hat dann auch
// den Anlass, darüber nachzudenken.
namespace
{
    bool looksMangled (const juce::String& text)
    {
        return text.containsChar ((juce::juce_wchar) 0x00C2)
            || text.containsChar ((juce::juce_wchar) 0x00C3);
    }

    // Sammelt jede Zeichenkette, die eine Component anzeigt oder als Hinweis
    // führt, und läuft dabei über alle Kinder.
    void collectVisibleText (juce::Component& c, std::vector<std::pair<juce::String, juce::String>>& out)
    {
        const auto where = c.getName().isNotEmpty() ? c.getName() : juce::String (typeid (c).name());

        if (auto* tooltipClient = dynamic_cast<juce::TooltipClient*> (&c))
            out.push_back ({ where + " (Hinweis)", tooltipClient->getTooltip() });

        if (auto* label = dynamic_cast<juce::Label*> (&c))
            out.push_back ({ where + " (Beschriftung)", label->getText() });

        if (auto* button = dynamic_cast<juce::Button*> (&c))
            out.push_back ({ where + " (Knopf)", button->getButtonText() });

        if (auto* combo = dynamic_cast<juce::ComboBox*> (&c))
            for (int i = 0; i < combo->getNumItems(); ++i)
                out.push_back ({ where + " (Auswahl)", combo->getItemText (i) });

        for (auto* child : c.getChildren())
            if (child != nullptr)
                collectVisibleText (*child, out);
    }

    // Prüft zusätzlich die Parameter selbst: ihre Namen, Einheiten und
    // Auswahltexte stehen in der Automationsliste des Hosts, also auch
    // an einer Stelle, die der Editor gar nicht durchläuft.
    void collectParameterText (juce::AudioProcessor& proc, std::vector<std::pair<juce::String, juce::String>>& out)
    {
        for (auto* param : proc.getParameters())
        {
            const auto name = param->getName (256);
            out.push_back ({ "Parameter " + name, name });
            out.push_back ({ "Parameter " + name + " (Einheit)", param->getLabel() });

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
                for (const auto& item : choice->choices)
                    out.push_back ({ "Parameter " + name + " (Auswahl)", item });
        }
    }
}

// --- Nachweis, dass die Druckstoesse der Rakete Stosswellen sind ---
//
// Seit @dpa 20260824 sind sie echte N-Wellen und keine Rauschstoesse mehr
// ("Die Druckstoesse sind Ueberschall, also donnernde N-Waves - ... jedenfalls
// klingen die Noise Decays laecherlich"). Der Unterschied ist messbar, und
// zwar an der STEILHEIT: eine N-Welle springt in wenigen Samples auf ihren
// vollen Wert, das Bruellen darunter ist gefiltertes Rauschen und kann das
// nicht.
//
// Geprueft wird deshalb der groesste Sprung von einem Sample zum naechsten,
// einmal mit und einmal ohne Stoesse. Ein Filter ueber dem Stoss haette
// genau diesen Sprung abgerundet - deswegen laeuft er ungefiltert.
namespace
{
    struct ShockMeasurement
    {
        double peak = 0.0;
        double maxStep = 0.0;   // groesster Sprung zwischen zwei Samples
    };

    // Geschaetzter spektraler Schwerpunkt eines Rauschsignals, ohne FFT.
    //
    // Fuer ein mittelwertfreies Signal gilt naeherungsweise
    //   f_zentrum ~ (fs / 2*pi) * RMS(erste Differenz) / RMS(Signal),
    // denn Differenzieren wichtet jede Frequenz mit ihrer eigenen Hoehe. Fuer
    // die Frage "klingt Vorlage A dunkler als Vorlage B" reicht das
    // vollkommen, und es kostet keine zusaetzliche Abhaengigkeit.
    double centroidHz (const std::vector<float>& samples, double rate)
    {
        double sumSq = 0.0, sumDiffSq = 0.0;

        for (size_t i = 1; i < samples.size(); ++i)
        {
            const double value = (double) samples[i];
            const double diff  = value - (double) samples[i - 1];

            sumSq     += value * value;
            sumDiffSq += diff * diff;
        }

        if (sumSq <= 0.0)
            return 0.0;

        return rate / (2.0 * juce::MathConstants<double>::pi) * std::sqrt (sumDiffSq / sumSq);
    }

    // Rendert eine der beiden Rausch-Betriebsarten und gibt ihren Schwerpunkt
    // zurueck. kind ist 1 (Duese) oder 2 (Rakete), s. Params::engineKind.
    double measureVoiceCentroid (double rate, int kind, int voiceIndex, float tone)
    {
        constexpr int block = 512;
        constexpr int blocks = 100;

        EngineGenerator gen;
        gen.prepare (rate, block);

        gen.setEngineKind (kind);
        gen.setKindLevelDb (0.0f);
        gen.setRpm (6000.0f);

        // Die Stoesse bleiben aus: gemessen wird die Klangfarbe des
        // Rauschens, und eine N-Welle waere darin ein Fremdkoerper.
        gen.setRocketShock (0.0f);

        if (kind == 1)
            gen.setJetVoice (voiceIndex, tone);
        else
            gen.setRocketVoice (voiceIndex, tone);

        std::vector<float> buffer ((size_t) block);
        std::vector<float> collected;
        collected.reserve ((size_t) (block * blocks));

        for (int b = 0; b < blocks; ++b)
        {
            gen.renderMono (buffer.data(), block);

            // Erster Block: Betriebsart-Blende laeuft noch hoch.
            if (b > 0)
                collected.insert (collected.end(), buffer.begin(), buffer.end());
        }

        return centroidHz (collected, rate);
    }

    ShockMeasurement measureRocket (double rate, float shockAmount)
    {
        constexpr int block = 512;
        constexpr int blocks = 200;   // rund 2 s bei 48 kHz

        EngineGenerator gen;
        gen.prepare (rate, block);

        gen.setEngineKind (2);          // Raketenantrieb, s. Params::engineKind
        gen.setKindLevelDb (0.0f);
        gen.setRocketShock (shockAmount);
        gen.setRocketShockShape (0.5f, 18.0f);
        gen.setRocketVoice (0, 0.5f);
        gen.setRpm (6000.0f);

        std::vector<float> buffer ((size_t) block);

        ShockMeasurement result;
        double previous = 0.0;

        for (int b = 0; b < blocks; ++b)
        {
            gen.renderMono (buffer.data(), block);

            for (int i = 0; i < block; ++i)
            {
                const double value = (double) buffer[(size_t) i];

                result.peak = std::max (result.peak, std::abs (value));

                // Der erste Block laeuft noch in die Betriebsart-Blende
                // hinein, dort ist jeder Sprung gedaempft - erst danach messen.
                if (b > 0)
                    result.maxStep = std::max (result.maxStep, std::abs (value - previous));

                previous = value;
            }
        }

        return result;
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

            // Beschriftungen prüfen. Einmal je Betriebsart des Motors, denn
            // das Motor-Panel zeigt in jeder eine andere Auswahl an Reglern -
            // ein falsch umgesetzter Umlaut in einem gerade unsichtbaren
            // Regler wäre sonst nicht dabei.
            {
                std::vector<std::pair<juce::String, juce::String>> texts;

                for (int kind = 0; kind < 5; ++kind)
                {
                    setParam (proc, Params::engineKind, (float) kind);

                    std::unique_ptr<juce::AudioProcessorEditor> kindEditor (proc.createEditor());
                    collectVisibleText (*kindEditor, texts);
                }

                collectParameterText (proc, texts);

                int mangled = 0;

                for (const auto& [where, text] : texts)
                {
                    if (! looksMangled (text))
                        continue;

                    ++mangled;

                    if (mangled <= 10)
                        std::printf ("  VERUNGLUECKTER UMLAUT in %s: \"%s\"\n",
                                     where.toRawUTF8(), text.toRawUTF8());
                }

                std::printf ("%-22s %d Beschriftungen geprueft, %d verunglueckt\n",
                             "Umlaute", (int) texts.size(), mangled);

                if (mangled > 0)
                    failed = true;

                setParam (proc, Params::engineKind, 0.0f);
            }

            // Klangformung von Duese und Rakete: unterscheiden sich die
            // Vorlagen wirklich, und wirkt der Klangfarbe-Regler?
            //
            // @dpa 20260824 hat beides bestellt ("einen Klangveraenderungsknob
            // und/oder eine Auswahl an vorgefertigten (multiband?) Filtern (am
            // besten beides)"). Eine Auswahl, deren Eintraege alle gleich
            // klingen, waere keine - deshalb wird hier nachgemessen und nicht
            // nur gebaut.
            {
                for (int kind = 1; kind <= 2; ++kind)
                {
                    const char* kindName = (kind == 1) ? "Duese" : "Rakete";

                    double lowest = 1.0e9, highest = 0.0;
                    juce::String line;

                    for (int voice = 0; voice < 5; ++voice)
                    {
                        const double centroid = measureVoiceCentroid (sampleRate, kind, voice, 0.5f);

                        lowest  = std::min (lowest, centroid);
                        highest = std::max (highest, centroid);

                        line << juce::String (centroid, 0) << " ";
                    }

                    // Klangfarbe-Regler am neutralen "Breit" (Index 4), damit
                    // die Vorlage selbst nichts dazu beitraegt.
                    const double dark   = measureVoiceCentroid (sampleRate, kind, 4, 0.0f);
                    const double middle = measureVoiceCentroid (sampleRate, kind, 4, 0.5f);
                    const double bright = measureVoiceCentroid (sampleRate, kind, 4, 1.0f);

                    std::printf ("%-22s Vorlagen (Hz): %s| Klangfarbe dunkel %.0f -> mitte %.0f -> hell %.0f\n",
                                 kindName, line.toRawUTF8(), dark, middle, bright);

                    // Die hellste Vorlage muss deutlich ueber der dunkelsten
                    // liegen, sonst ist die Auswahl ohne Wirkung.
                    if (lowest <= 0.0 || highest < lowest * 1.5)
                    {
                        std::printf ("  FEHLER: die Vorlagen von %s klingen praktisch gleich.\n", kindName);
                        failed = true;
                    }

                    // Und der Regler muss den Schwerpunkt monoton anheben.
                    if (! (dark < middle && middle < bright))
                    {
                        std::printf ("  FEHLER: der Klangfarbe-Regler von %s wirkt nicht durchgaengig.\n", kindName);
                        failed = true;
                    }
                }
            }

            // Druckstoesse der Rakete: sind es Stosswellen oder nur Rauschen?
            {
                const auto withShocks = measureRocket (sampleRate, 1.0f);
                const auto without    = measureRocket (sampleRate, 0.0f);

                // Verhaeltnis der Flankensteilheit. Eine N-Welle steigt in
                // zwei Samples auf ihren vollen Wert, das tiefpassgefilterte
                // Bruellen kann das nicht - der Unterschied muss deutlich
                // sein, sonst ist der Stoss unterwegs verrundet worden.
                const double ratio = without.maxStep > 0.0
                                   ? withShocks.maxStep / without.maxStep
                                   : 0.0;

                std::printf ("%-22s Flankensprung mit Stoessen %.4f gegen %.4f ohne (%.1f x) | Spitze %.3f gegen %.3f\n",
                             "Raketen-Stosswellen",
                             withShocks.maxStep, without.maxStep, ratio,
                             withShocks.peak, without.peak);

                if (ratio < 3.0)
                {
                    std::printf ("  FEHLER: die Druckstoesse sind nicht steiler als das Bruellen - "
                                 "das waeren keine Stosswellen.\n");
                    failed = true;
                }
            }

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

            // Laufen lassen, damit die Quelle klingt UND der Signalpuffer
            // weit genug zurueckreicht: der Vorbeiflug beginnt 243 m entfernt,
            // der Schall von dort ist 0,7 s unterwegs. Der Schnitt setzt die
            // Bahn mit vollstaendiger Vorgeschichte auf, aber gelesen wird
            // trotzdem aus dem Signalpuffer - was es dort noch nicht gibt,
            // kann auch nicht klingen. Im Betrieb laeuft der Puffer laengst
            // mit, hier muss er erst gefuellt werden.
            Stats settle;
            render (proc, buffer, 1.5, settle, [] (double) {});

            proc.triggerFlyBy();

            // Der Schnitt zuerst: der Sprung an den Startpunkt der Strecke ist
            // Umbau, keine Bewegung, und laeuft deshalb zwischen Aus- und
            // Einblende (@dpa 20260824, siehe CutState). Waehrend der Ausblende
            // steht die Quelle noch - erst danach faengt der Flug an, und erst
            // dort ist ein Tempo zu messen. 40 ms decken die 12 ms Blende samt
            // Blockraster sicher ab.
            Stats cutWindow;
            render (proc, buffer, 0.04, cutWindow, [] (double) {});

            // Der Einsatz bekommt ein eigenes, sehr kurzes Fenster. Fuer das
            // Anlauftempo ist genau dieser Moment der interessante: die Quelle
            // muss vom ersten Bahnpunkt an mit voller Fahrt fliegen, statt
            // anzulaufen.
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
        abruptRest.report  ("Vorbeiflug Knall, Rest");

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
    // 1g1b. Knall-Start: kommt der Bewegungssprung ueberhaupt an?
    //
    //      @dpa 20260823: "der Vorbeiflug 'Knall-Start' muesste ja mindestens
    //      subsonic zu hoeren sein ... Bisher ist noch nicht zu hoeren!"
    //      Gemessen war das auch so: der abrupte Start hinterliess keinen
    //      eigenen Einsatz im Signal, weil die N-Wellen-Schicht allein an
    //      M_r = 1 haengt und der Sprung selbst ueber ein Solver-Segment
    //      interpoliert wird.
    //
    //      Zwei Wege dagegen, hier beide gegen den Zustand ohne sie geprueft:
    //      "Sprungkante" (Params::jumpEdge) laesst die Kante stehen,
    //      "Sprungknall" (Params::jumpBoom) setzt eine Druckwelle darauf.
    //      Gemessen wird im Fenster, in dem die Kante beim Hoerer ankommt -
    //      die Quelle steht beim Start rund 243 m weit weg, das sind gut
    //      0,7 s Laufzeit.
    {
        auto jumpStart = [&] (double boom, Stats& arrivalStats)
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
            setParam (proc, Params::srcX, 0.95f);
            setParam (proc, Params::srcY, 0.95f);

            setParam (proc, Params::flyKind,     1.0f);
            setParam (proc, Params::flyStart,    1.0f);   // Knall-Start
            setParam (proc, Params::flyDistance, 40.0f);
            setParam (proc, Params::flyApproach, 240.0f);
            setParam (proc, Params::flySpeed,    200.0f);

            // Der Knall soll unterschallig ankommen - genau darum geht es.
            setParam (proc, Params::nWaveOn,  1.0f);
            setParam (proc, Params::jumpBoom, (float) boom);

            proc.prepareToPlay (sampleRate, blockSize);

            // Vorlauf wie beim Anlauf-Szenario: der Start liegt 243 m weit
            // weg, der Signalpuffer muss so weit zurueckreichen.
            Stats settle;
            render (proc, buffer, 1.5, settle, [] (double) {});

            proc.triggerFlyBy();

            // Bis kurz vor die Ankunft laufen lassen, dann messen. Die Passage
            // selbst liegt weit dahinter und wuerde die Messung sonst
            // dominieren.
            Stats beforeArrival;
            render (proc, buffer, 0.55, beforeArrival, [] (double) {});
            render (proc, buffer, 0.40, arrivalStats,  [] (double) {});
        };

        Stats plain, withBoom;

        jumpStart (0.0, plain);
        jumpStart (1.0, withBoom);

        plain.report   ("Knall-Start ohne Welle");
        withBoom.report ("Knall-Start + Druckwelle");

        auto rmsOf = [] (const Stats& st)
        {
            return std::sqrt (st.sumSquares[0] / std::max (1.0, (double) st.samples * 0.5));
        };

        std::printf ("%-22s Ankunft: Spitze ohne %.4f | mit Druckwelle %.4f\n",
                     "", plain.peak, withBoom.peak);

        // @dpa 20260824: "Durch die Regel 'waehrend N-Wave nicht ausser N' ist
        // das wie eine kurze Unterbrechung." Der Sprungknall senkt den uebrigen
        // Schall deshalb nicht mehr ab - der Motorton muss im Ankunftsfenster
        // also mindestens so laut bleiben wie ohne Knall, nicht leiser werden.
        std::printf ("%-22s Umfeld im Ankunftsfenster: RMS ohne %.5f | mit Druckwelle %.5f\n",
                     "", rmsOf (plain), rmsOf (withBoom));

        if (rmsOf (withBoom) < rmsOf (plain))
        {
            std::printf ("  FEHLER: die Druckwelle macht das Ankunftsfenster LEISER "
                         "(%.5f gegen %.5f) - sie schneidet sich ihr eigenes Loch.\n",
                         rmsOf (withBoom), rmsOf (plain));
            failed = true;
        }

        // Die Druckwelle ist der laute Weg: sie muss die Spitze im
        // Ankunftsfenster deutlich anheben, sonst wirkt der Regler nicht.
        if (withBoom.peak <= 2.0 * plain.peak)
        {
            std::printf ("FEHLGESCHLAGEN: Sprungknall hebt die Ankunft nicht an "
                         "(ohne %.4f, mit %.4f) - der Ausloeser greift nicht\n",
                         plain.peak, withBoom.peak);
            failed = true;
        }

        std::printf ("%-22s Ankunft: groesster Samplesprung ohne %.5f | mit Druckwelle %.5f\n",
                     "", plain.maxSampleStep, withBoom.maxSampleStep);
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
    // 1g3. Gewoehnliches Pegel-Panning (@dpa 20260819: "bitte noch ein normales
    //      Panning fuer die Kopfdrehung anbieten, also den Anteil des normalen
    //      pannings von 0 - 100%").
    //
    //      Geprueft wird der Pegelunterschied zwischen links und rechts bei
    //      einer ruhenden Quelle seitlich vom Hoerer. Drei Faelle, weil erst sie
    //      zusammen zeigen, dass der Regler das Richtige tut:
    //        - 0 %: das Stereobild kommt allein aus der Ohrgeometrie, beide
    //          Seiten sind praktisch gleich laut.
    //        - 100 %: die Seite, auf der die Quelle steht, ist deutlich lauter.
    //        - 100 % mit um 180 Grad gedrehtem Kopf: es kehrt sich um. Erst
    //          damit ist gezeigt, dass das Panning am KOPF haengt und nicht an
    //          der Weltachse.
    {
        auto sideTestField = [&] (float fieldSize, float panPercent, float yawDegrees, Stats& stats)
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, fieldSize);
            setParam (proc, Params::lisX,   0.5f);
            setParam (proc, Params::lisY,   0.5f);
            setParam (proc, Params::lisYaw, yawDegrees);
            setParam (proc, Params::srcX,   0.75f);   // Quelle rechts vom Hoerer
            setParam (proc, Params::srcY,   0.5f);
            setParam (proc, Params::panAmount, panPercent);
            setParam (proc, Params::groundReflectionOn, 0.0f);

            proc.prepareToPlay (sampleRate, blockSize);

            render (proc, buffer, 1.0, stats, [] (double) {});
        };

        auto sideTest = [&] (float panPercent, float yawDegrees, Stats& stats)
        {
            sideTestField (400.0f, panPercent, yawDegrees, stats);
        };

        Stats flat, panned, turned;

        sideTest (0.0f,   0.0f,   flat);
        sideTest (100.0f, 0.0f,   panned);
        sideTest (100.0f, 180.0f, turned);

        // Das Panning muss bei jeder Feldgroesse gleich stark sein (@dpa
        // 20260819: "Panning (also L/R) bei 'Field Size' < 100 ist defekt").
        //
        // Es haengt an der Richtung, und die Quelle steht in allen Faellen an
        // derselben normierten Stelle, also seitlich vom Hoerer. Gemessen wird
        // bei 50 %, weil dort beide Seiten noch Pegel haben; bei 100 % ist die
        // abgewandte Seite exakt stumm und ein Verhaeltnis nicht mehr bildbar.
        //
        // Die Hoehen von Quelle und Hoerer stehen in Metern und wachsen NICHT
        // mit der Feldgroesse mit. Auf einem kleinen Feld steht die Quelle
        // deshalb vor allem oben statt rechts - wer die Seitlichkeit im Raum
        // statt in der Waagerechten misst, verliert genau dort das Panning.
        {
            double firstDb   = 0.0;
            bool   haveFirst = false;

            for (float field : { 400.0f, 100.0f, 50.0f, 20.0f, 10.0f })
            {
                Stats s;
                sideTestField (field, 50.0f, 0.0f, s);

                const double half = std::max (1.0, (double) s.samples * 0.5);
                const double l  = std::max (1.0e-12, std::sqrt (s.sumSquares[0] / half));
                const double r  = std::max (1.0e-12, std::sqrt (s.sumSquares[1] / half));
                const double db = 20.0 * std::log10 (r / l);

                std::printf ("%-22s Feld %4.0f m -> R gegen L %+6.2f dB\n",
                             haveFirst ? "" : "Panning je Feld", field, db);

                if (! haveFirst)
                {
                    firstDb   = db;
                    haveFirst = true;
                }
                else if (std::abs (db - firstDb) > 1.0)
                {
                    std::printf ("FEHLGESCHLAGEN: bei %.0f m Feld pannt es %+.2f dB statt %+.2f dB "
                                 "wie bei 400 m - die Feldgroesse darf das Panning nicht "
                                 "veraendern\n", field, db, firstDb);
                    failed = true;
                }
            }
        }

        auto ratioDb = [] (const Stats& s)
        {
            const double half = std::max (1.0, (double) s.samples * 0.5);
            const double l = std::max (1.0e-12, std::sqrt (s.sumSquares[0] / half));
            const double r = std::max (1.0e-12, std::sqrt (s.sumSquares[1] / half));
            return 20.0 * std::log10 (r / l);
        };

        std::printf ("%-22s R gegen L: ohne Panning %+.2f dB | mit %+.2f dB | Kopf gedreht %+.2f dB\n",
                     "Panning", ratioDb (flat), ratioDb (panned), ratioDb (turned));

        if (std::abs (ratioDb (flat)) > 1.0)
        {
            std::printf ("FEHLGESCHLAGEN: ohne Panning stehen die Seiten schon %+.2f dB "
                         "auseinander - der Regler ist nicht mehr der Ausgangspunkt\n",
                         ratioDb (flat));
            failed = true;
        }

        if (ratioDb (panned) < 6.0)
        {
            std::printf ("FEHLGESCHLAGEN: bei 100 %% Panning ist die Quellseite nur %+.2f dB "
                         "lauter - das Panning wirkt nicht\n", ratioDb (panned));
            failed = true;
        }

        if (ratioDb (turned) > -6.0)
        {
            std::printf ("FEHLGESCHLAGEN: der um 180 Grad gedrehte Kopf hoert die Quelle mit "
                         "%+.2f dB immer noch rechts - das Panning haengt nicht am Kopf\n",
                         ratioDb (turned));
            failed = true;
        }
    }

    //==================================================================
    // 1g4. Einheitenumschaltung der Tempo-Regler (@dpa 20260819: "ich kann mit
    //      m/s schlecht rechnen ... ich muss m/s eingeben, ohne information was
    //      das in km/h oder Mach ist").
    //
    //      Geprueft wird das Textfeld selbst, nicht der gespeicherte Wert: der
    //      bleibt immer in m/s, und genau deshalb faellt es nicht auf, wenn die
    //      Umrechnung stillschweigend nicht mehr ankommt. Ein ueberschriebenes
    //      getTextFromValue() reicht dafuer schon aus (siehe RoundedSlider).
    {
        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        setParam (proc, Params::flySpeed, 358.07f);
        proc.prepareToPlay (sampleRate, blockSize);

        std::unique_ptr<juce::AudioProcessorEditor> owner (proc.createEditor());
        auto* editor = dynamic_cast<DopplerfeldEditor*> (owner.get());

        if (editor == nullptr)
        {
            std::printf ("FEHLGESCHLAGEN: kein Editor zum Pruefen der Einheiten\n");
            failed = true;
        }
        else
        {
            // Einmal im Kreis: der Schalter hat drei Stellungen, und in jeder
            // muss die Beschriftung des Reglers zur Stellung passen.
            // Slew Amax stellt eine Beschleunigung ein und haengt am selben
            // Schalter: eine Beschleunigung ist ein Tempo pro Sekunde, es kommt
            // also nur "/s" an die Einheit.
            for (int i = 0; i < 3; ++i)
            {
                const juce::String unit  = editor->speedUnitLabelForTest();
                const juce::String shown = editor->flySpeedTextForTest();
                const juce::String accel = editor->slewAmaxTextForTest();

                std::printf ("%-22s Schalter %-5s -> Fly Speed \"%s\", Slew Amax \"%s\"\n",
                             i == 0 ? "Reglereinheit" : "", unit.toRawUTF8(),
                             shown.toRawUTF8(), accel.toRawUTF8());

                if (! shown.contains (unit))
                {
                    std::printf ("FEHLGESCHLAGEN: der Schalter steht auf %s, der Regler zeigt "
                                 "\"%s\" - die Umrechnung kommt am Textfeld nicht an\n",
                                 unit.toRawUTF8(), shown.toRawUTF8());
                    failed = true;
                }

                if (! accel.containsChar ('/'))
                {
                    std::printf ("FEHLGESCHLAGEN: Slew Amax zeigt \"%s\" - dort fehlt die "
                                 "Einheit\n", accel.toRawUTF8());
                    failed = true;
                }

                editor->cycleSpeedUnitForTest();
            }

            // Die Rundungsregel selbst (@dpa 20260819): "Anzeigen mit sauvielen
            // nullen (1.00000000) oder gar darunter (0.9999997) ist fuer eine
            // 'Anzeige' Gift". Sie gilt fuer ALLE Regler, deshalb wird hier die
            // gemeinsame Funktion geprueft und nicht ein einzelner Regler.
            {
                struct Probe { double value; const char* expected; };

                const Probe probes[] =
                {
                    { 0.9999997,  "1.000" },
                    { 0.5,        "0.500" },
                    { 5.25,       "5.25"  },
                    { 50.27,      "50.3"  },
                    { 708.301,    "708"   },
                    { 2004.95,    "2005"  },
                };

                for (const auto& p : probes)
                {
                    const juce::String got = RoundedSlider::roundedText (p.value);

                    if (got != p.expected)
                    {
                        std::printf ("FEHLGESCHLAGEN: %g wird als \"%s\" angezeigt, erwartet "
                                     "\"%s\"\n", p.value, got.toRawUTF8(), p.expected);
                        failed = true;
                    }
                }

                std::printf ("%-22s 0,5 -> \"%s\" | 5,25 -> \"%s\" | 50,27 -> \"%s\" | "
                             "708,301 -> \"%s\"\n", "Stellenregel",
                             RoundedSlider::roundedText (0.5).toRawUTF8(),
                             RoundedSlider::roundedText (5.25).toRawUTF8(),
                             RoundedSlider::roundedText (50.27).toRawUTF8(),
                             RoundedSlider::roundedText (708.301).toRawUTF8());
            }

            // Und jetzt JEDER Regler der Oberflaeche, nicht nur die gerade
            // angefassten (@dpa 20260819: "bitte alle ALLE alle. immer.").
            //
            // Geprueft wird das, was tatsaechlich im Textfeld steht. Ein Regler
            // haengt an einem Parameter, und dessen Attachment bringt eine
            // eigene Textfunktion mit, die den Wert in voller Praezision
            // ausgibt - ohne diese Schleife faellt es nicht auf, wenn sie an
            // einem einzelnen Regler wieder durchschlaegt.
            {
                struct Walker
                {
                    int checked = 0, wrong = 0;

                    void visit (juce::Component& parent, bool& failedOut)
                    {
                        for (auto* child : parent.getChildren())
                        {
                            if (auto* slider = dynamic_cast<juce::Slider*> (child))
                                check (*slider, failedOut);

                            visit (*child, failedOut);
                        }
                    }

                    void check (juce::Slider& slider, bool& failedOut)
                    {
                        // Nicht nur den gerade eingestellten Wert: die
                        // Stellenzahl haengt an der Groessenordnung, und ein
                        // Fehler zeigt sich oft erst am oberen Ende. Abgetastet
                        // wird deshalb der ganze Weg des Reglers. Gesetzt wird
                        // dabei nichts - getTextFromValue beantwortet die Frage
                        // fuer jeden Wert direkt.
                        const double lo = slider.getMinimum();
                        const double hi = slider.getMaximum();

                        for (int step = 0; step <= 8; ++step)
                        {
                            const double value = lo + (hi - lo) * (double) step / 8.0;
                            const juce::String shown = slider.getTextFromValue (value);

                            // Zahlenteil abtrennen: eine Einheit haengt immer
                            // hinter einem Leerzeichen.
                            const juce::String number = shown.upToFirstOccurrenceOf (" ", false, false);
                            const int dot      = number.indexOfChar ('.');
                            const int decimals = dot < 0 ? 0 : number.length() - dot - 1;
                            const int expected = RoundedSlider::decimalsFor (number.getDoubleValue());

                            ++checked;

                            if (decimals != expected)
                            {
                                ++wrong;

                                if (wrong <= 5)
                                    std::printf ("FEHLGESCHLAGEN: Regler \"%s\" zeigt bei %g "
                                                 "\"%s\" - %d Nachkommastellen statt %d\n",
                                                 slider.getName().toRawUTF8(), value,
                                                 shown.toRawUTF8(), decimals, expected);

                                failedOut = true;
                            }
                        }
                    }
                };

                Walker walker;
                walker.visit (*editor, failed);

                std::printf ("%-22s %d Regler geprueft, %d davon mit falscher Stellenzahl\n",
                             "Alle Regler", walker.checked, walker.wrong);
            }
        }
    }

    //==================================================================
    // 1g5. Vorbeiflug knapp ueber Mach 1 (@dpa 20260819: "der Vorbeiflug mit
    //      358m/s hat aber immernoch das gleiche Problem, und zwar beim
    //      Auftreffen des Kegels ... bei diesem Speed noch kein Durchflug zu
    //      hoeren, weil in diesem Moment der CPU blockiert ist", CPU 135 %).
    //
    //      358 m/s sind Mach 1,04. Das ist der teuerste Punkt ueberhaupt: an
    //      der Mach-Front laeuft die Verzoegerung mit unendlicher Steigung
    //      durch, der Loeser muss dort seine Wurzeln aus einem beliebig steilen
    //      Verlauf holen, und genau in dem Moment trifft der Kegel ein.
    //
    //      Werte aus @dpas Bildschirmfoto. Gemessen wird die Blockzeit, nicht
    //      der Klang: ein Block, der laenger braucht als sein Zeitbudget,
    //      erzeugt eine Luecke, und genau die ist der fehlende Durchflug.
    {
        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        setParam (proc, Params::fieldMetres,    5000.0f);
        setParam (proc, Params::lisX,           0.4f);
        setParam (proc, Params::lisY,           0.5f);
        setParam (proc, Params::srcZ,           30.0f);
        setParam (proc, Params::smootherType,   1.0f);
        setParam (proc, Params::smootherTau,    0.531f);
        setParam (proc, Params::slewVmax,       1000.0f);
        setParam (proc, Params::slewAmax,       2004.95f);
        setParam (proc, Params::globalMaxSpeed, 687.62f);
        setParam (proc, Params::flyKind,        1.0f);
        setParam (proc, Params::flyStart,       0.0f);
        setParam (proc, Params::flyDistance,    589.711f);
        setParam (proc, Params::flyApproach,    708.301f);
        setParam (proc, Params::flySpeed,       358.07f);

        proc.prepareToPlay (sampleRate, blockSize);

        Stats settle;
        render (proc, buffer, 0.3, settle, [] (double) {});

        proc.triggerFlyBy();

        Stats flight;
        render (proc, buffer, 6.0, flight, [] (double) {});

        flight.report ("Vorbeiflug Mach 1,04");

        // Geprueft wird die Zahl der Loeser-Auswertungen, nicht die Wanduhr: die
        // schwankt auf einem beschaeftigten Rechner um Faktor zwei und taugt
        // deshalb nicht als Kriterium (siehe solverEvaluations). Die Zeit steht
        // trotzdem daneben, denn erst sie sagt, ob es fuer Ton reicht.
        const double budgetMicros = 1.0e6 * (double) blockSize / sampleRate;
        const double avgEvals     = flight.blocks > 0
                                      ? (double) flight.solverEvals / (double) flight.blocks
                                      : 0.0;
        const double spike        = avgEvals > 0.0
                                      ? (double) flight.worstBlockEvals / avgEvals
                                      : 0.0;

        std::printf ("%-22s Kaustik-Spitze %.1f x Schnitt (%llu gegen %.0f Auswertungen), "
                     "%.0f us gegen %.0f us Budget\n",
                     "", spike, (unsigned long long) flight.worstBlockEvals, avgEvals,
                     flight.worstMicros, budgetMicros);

        // Vier Mal der Schnitt ist die Groesse, um die es geht: so weit ragt die
        // Spitze an der Mach-Front ueber den laufenden Betrieb hinaus, und genau
        // daran haengt der Aussetzer.
        //
        // Das ist ein BEKANNTES OFFENES THEMA und laesst den Lauf deshalb nicht
        // fehlschlagen (@dpa 20260820: "lassen wir das mit der kaustik, ich habe
        // gerade andere Probleme. bitte nimm Kaustik ins todo und raus aus push
        // der Blockade"). Die Zahl wird weiter gemessen und ausgegeben, damit
        // eine Verschlechterung auffaellt und der Tag, an dem es jemand angeht,
        // eine Ausgangsgroesse hat.
        //
        // Stand der Diagnose: nicht die LAENGE eines einzelnen Scans ist die
        // Ursache. Ein testweise auf den Beginn der Geraden angehobenes
        // Suchfenster halbierte den Schnitt (12354 -> 7422 Auswertungen), liess
        // den teuersten Block aber bei exakt denselben 143994 Auswertungen. Die
        // Spitze kommt also aus mehreren Vollscans INNERHALB eines Blocks, nicht
        // aus einem einzelnen langen Scan - dort ist weiterzusuchen.
        if (spike > 4.0)
        {
            std::printf ("OFFEN (kein Fehlschlag): der teuerste Block kostet das %.1f-fache des "
                         "Schnitts (bei t=%.2fs, |M_r| dort %.2f) - an dieser Stelle setzt der Ton "
                         "aus. Steht als Kaustik-Lastspitze im TODO.\n",
                         spike, flight.worstBlockAtSec, flight.worstBlockMach);
        }
    }

    //==================================================================
    // 1g6. Zustand laden und wieder speichern (@dpa 20260819: "Output scheint
    //      nicht zu recallen..? es ist immer wenn ich neu starte auf +36").
    //
    //      Geprueft wird die Eigenschaft, an der es haengt: Speichern und
    //      Laden muss den Zustand unveraendert lassen, und zwar auch beim
    //      zweiten und dritten Mal. Ein Zustand aus einer aelteren Fassung
    //      wird beim Laden umgerechnet - passiert das bei JEDEM Laden erneut,
    //      wandert der Wert bei jedem Programmstart weiter, bis er am Anschlag
    //      steht.
    {
        auto outputGainOf = [] (DopplerfeldProcessor& p)
        {
            return (double) *p.apvts.getRawParameterValue (Params::outputGain);
        };

        // Ein gespeicherter Zustand aus der Fassung MIT eigenem Lauter-Regler.
        juce::MemoryBlock legacyState;

        {
            DopplerfeldProcessor proc;
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            setParam (proc, Params::outputGain, 6.0f);
            proc.getStateInformation (legacyState);

            // Den alten Regler von Hand einhaengen, wie ihn eine aeltere
            // Fassung geschrieben haette.
            auto xml = juce::AudioProcessor::getXmlFromBinary (legacyState.getData(),
                                                               (int) legacyState.getSize());
            if (xml != nullptr)
            {
                auto tree = juce::ValueTree::fromXml (*xml);
                juce::ValueTree old ("PARAM");
                old.setProperty ("id", "loudBoost", nullptr);
                old.setProperty ("value", 12.0, nullptr);
                tree.addChild (old, -1, nullptr);

                const auto rewritten = tree.createXml();
                legacyState.reset();
                juce::AudioProcessor::copyXmlToBinary (*rewritten, legacyState);
            }
        }

        // Dreimal laden und dazwischen jeweils neu speichern, wie es ein
        // Programmstart tut.
        DopplerfeldProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        juce::MemoryBlock state = legacyState;
        double gains[3] {};

        for (int round = 0; round < 3; ++round)
        {
            proc.setStateInformation (state.getData(), (int) state.getSize());
            gains[round] = outputGainOf (proc);

            state.reset();
            proc.getStateInformation (state);
        }

        std::printf ("%-22s Output Gain nach 1./2./3. Laden: %+.1f / %+.1f / %+.1f dB\n",
                     "Zustand laden", gains[0], gains[1], gains[2]);

        // Erste Runde rechnet um: 6 + 12 = 18 dB. Danach muss es dort stehen
        // bleiben.
        if (std::abs (gains[0] - 18.0) > 0.05)
        {
            std::printf ("FEHLGESCHLAGEN: der alte Lauter-Wert wird beim Laden nicht auf den "
                         "Ausgangspegel gerechnet (%+.1f statt %+.1f dB)\n", gains[0], 18.0);
            failed = true;
        }

        if (std::abs (gains[1] - gains[0]) > 0.05 || std::abs (gains[2] - gains[0]) > 0.05)
        {
            std::printf ("FEHLGESCHLAGEN: der Ausgangspegel wandert bei jedem Laden weiter "
                         "(%+.1f -> %+.1f -> %+.1f dB) - eingestellt bleibt so nichts\n",
                         gains[0], gains[1], gains[2]);
            failed = true;
        }
    }

    //==================================================================
    // 1g7. Liegt das Spiegelbild des Ohres wirklich bei -z? (@dpa 20260819:
    //      "Kannst Du nochmal pruefen ob mein Vorstellung: 'Ohrhoehe z mit -z
    //      der Grundflaechen reflection' ist? Mir scheint ich muss es halb hoch
    //      einstellen..")
    //
    //      Nachgerechnet an den Laufzeiten, die der Loeser selbst meldet. Steht
    //      die Quelle in der Hoehe hs und das Ohr in hl, im waagerechten Abstand
    //      d, dann ist
    //          Direktschall     R1 = sqrt(d^2 + (hs - hl)^2)
    //          ueber den Boden  R2 = sqrt(d^2 + (hs + hl)^2)
    //      genau dann, wenn das Spiegelbild bei -hl sitzt. Jede andere
    //      Spiegelebene ergibt eine andere Laufzeit - etwa eine Ebene auf halber
    //      Hoererhoehe, die R2 = sqrt(d^2 + hs^2) liefern wuerde.
    {
        constexpr double hs = 30.0;    // Quellhoehe
        constexpr double hl = 8.0;     // Ohrhoehe
        constexpr double c  = 343.0;

        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        setParam (proc, Params::fieldMetres, 400.0f);
        setParam (proc, Params::lisX, 0.25f);
        setParam (proc, Params::lisY, 0.5f);
        setParam (proc, Params::lisZ, (float) hl);
        setParam (proc, Params::srcX, 0.75f);
        setParam (proc, Params::srcY, 0.5f);
        setParam (proc, Params::srcZ, (float) hs);
        setParam (proc, Params::groundReflectionOn, 1.0f);
        setParam (proc, Params::earSpacing, 0.0f);   // beide Ohren auf den Kopfpunkt

        proc.prepareToPlay (sampleRate, blockSize);

        juce::MidiBuffer midi;
        FieldSnapshot    snapshot;

        Stats settle;
        render (proc, buffer, 1.0, settle, [] (double) {});
        proc.fillFieldSnapshot (snapshot);

        const double d = (snapshot.sourcePos - snapshot.listener.head).length() > 0.0
                           ? std::sqrt (std::pow (snapshot.sourcePos.x - snapshot.listener.head.x, 2.0)
                                      + std::pow (snapshot.sourcePos.y - snapshot.listener.head.y, 2.0))
                           : 0.0;

        const double expectedDirect = std::sqrt (d * d + (hs - hl) * (hs - hl)) / c;
        const double expectedGround = std::sqrt (d * d + (hs + hl) * (hs + hl)) / c;

        double gotDirect = 0.0, gotGround = 0.0;

        for (int i = 0; i < snapshot.pathCount; ++i)
        {
            const auto& p = snapshot.paths[(size_t) i];

            if (p.order == 0 && gotDirect == 0.0)
                gotDirect = p.delaySeconds;
            else if (p.surface == 1 && p.order == 1 && gotGround == 0.0)
                gotGround = p.delaySeconds;
        }

        std::printf ("%-22s Abstand %.1f m | Direktschall %.4f s (erwartet %.4f) | "
                     "ueber Boden %.4f s (erwartet %.4f)\n",
                     "Bodenspiegel", d, gotDirect, expectedDirect, gotGround, expectedGround);

        // Zwei Millisekunden Spielraum: die Laufzeit wird an einem Solver-Punkt
        // abgelesen, nicht exakt zum Zeitpunkt des Snapshots.
        if (std::abs (gotDirect - expectedDirect) > 0.002)
        {
            std::printf ("FEHLGESCHLAGEN: der Direktschall braucht %.4f s statt %.4f s\n",
                         gotDirect, expectedDirect);
            failed = true;
        }

        if (std::abs (gotGround - expectedGround) > 0.002)
        {
            const double halfHeight = std::sqrt (d * d + hs * hs) / c;

            std::printf ("FEHLGESCHLAGEN: der Bodenweg braucht %.4f s statt %.4f s - das "
                         "Spiegelbild sitzt nicht bei -%.1f m (eine Ebene auf halber Hoehe "
                         "ergaebe %.4f s)\n", gotGround, expectedGround, hl, halfHeight);
            failed = true;
        }
    }

    //==================================================================
    // 1g8. Wackeln die echten Klone einzeln? (@dpa 20260820: "die (echten)
    //      clone haben doch hoffentlich auch ihre eigenen Jitterkanaele?")
    //
    //      Nachweisbar ohne Ohr: bei ruhender Quelle und ruhendem Hoerer ist der
    //      Ausgang ohne Jitter ein stehender Klang. Wackeln die Klone einzeln,
    //      laufen ihre Laufzeiten gegeneinander und der Summenpegel atmet. Ein
    //      GEMEINSAMER Wackler wuerde das nicht tun: er verschoebe alle Klone
    //      gleich und liesse ihr Verhaeltnis zueinander unveraendert.
    {
        auto run = [&] (float jitterMetres, Stats& stats)
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, 200.0f);
            setParam (proc, Params::srcX, 0.7f);
            setParam (proc, Params::srcY, 0.5f);
            // Beide Regler: cloneTotal ist die Gesamtzahl, cloneReal wie viele
            // davon echt gerechnet werden. Ohne die Gesamtzahl entstehen gar
            // keine Klone, und der Jitter haette nichts, woran er wackeln
            // koennte.
            setParam (proc, Params::cloneTotal,  10.0f);
            setParam (proc, Params::cloneSpread, 6.0f);
            setParam (proc, Params::srcJitterAmount, jitterMetres);
            setParam (proc, Params::srcJitterRateHz, 3.0f);

            proc.prepareToPlay (sampleRate, blockSize);
            render (proc, buffer, 4.0, stats, [] (double) {});
        };

        Stats still, wobbly;

        run (0.0f, still);
        run (2.0f, wobbly);

        // Kommen die Klone ueberhaupt an? (@dpa 20260820: "sie sind derzeit
        // weder hoerbar noch sichtbar", nachdem er sie aufgedreht hatte.)
        // Geprueft wird der Weg, den das Plugin geht: der Snapshot meldet, wie
        // viele echte Klone gerechnet werden und wo sie sitzen.
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            setParam (proc, Params::fieldMetres, 200.0f);
            setParam (proc, Params::cloneTotal,  8.0f);
            setParam (proc, Params::cloneSpread, 5.0f);
            proc.prepareToPlay (sampleRate, blockSize);

            juce::MidiBuffer midi;
            FieldSnapshot    snapshot;
            Stats            warm;

            render (proc, buffer, 0.5, warm, [] (double) {});
            proc.fillFieldSnapshot (snapshot);

            std::printf ("%-22s eingestellt 8 -> gerechnet %d, angezeigt %d\n",
                         "Klone kommen an", snapshot.realCloneCount, snapshot.clonePositionCount);

            if (snapshot.realCloneCount != 8)
            {
                std::printf ("FEHLGESCHLAGEN: 8 echte Klone eingestellt, gerechnet werden %d\n",
                             snapshot.realCloneCount);
                failed = true;
            }

            if (snapshot.clonePositionCount != 8)
            {
                std::printf ("FEHLGESCHLAGEN: 8 echte Klone gerechnet, aber %d Positionen fuer die "
                             "Anzeige - der Schwarm bleibt unsichtbar\n",
                             snapshot.clonePositionCount);
                failed = true;
            }

            // Und sie muessen auseinanderliegen, nicht alle auf der Quelle.
            double maxDistance = 0.0;

            for (int i = 0; i < snapshot.clonePositionCount; ++i)
                maxDistance = std::max (maxDistance,
                                        (snapshot.clonePositions[(size_t) i] - snapshot.sourcePos).length());

            std::printf ("%-22s weitester Klon %.2f m von der Quelle (Streuung 5 m)\n",
                         "", maxDistance);

            // Schlagen sie so weit aus wie die Quelle? (@dpa 20260820: "sie
            // laufen nicht durch die Glaettung und haben deshalb viel groessere
            // weiten ausschlage als das original!")
            //
            // Der Quell-Jitter wird auf das Ziel addiert und laeuft DANACH durch
            // die Bewegungsglaettung, wird dort also gedaempft. Der Klon-Jitter
            // muss denselben Weg nehmen, sonst wackeln die Klone in voller
            // Amplitude um eine Quelle, die nur noch einen Rest davon zeigt.
            {
                DopplerfeldProcessor p3;

                p3.setRateAndBufferSizeDetails (sampleRate, blockSize);
                setParam (p3, Params::fieldMetres, 20.0f);
                setParam (p3, Params::cloneTotal,  4.0f);
                setParam (p3, Params::cloneSpread, 2.0f);
                setParam (p3, Params::smootherType, 1.0f);
                setParam (p3, Params::smootherTau,  0.5f);
                setParam (p3, Params::srcJitterAmount, 1.0f);
                setParam (p3, Params::srcJitterRateHz, 2.0f);
                p3.prepareToPlay (sampleRate, blockSize);

                juce::MidiBuffer midi3;
                FieldSnapshot    snap3;

                Vec3 srcMin {  1e9,  1e9,  1e9 }, srcMax { -1e9, -1e9, -1e9 };
                Vec3 relMin {  1e9,  1e9,  1e9 }, relMax { -1e9, -1e9, -1e9 };

                for (int b = 0; b < (int) (6.0 * sampleRate / blockSize); ++b)
                {
                    buffer.clear();
                    p3.processBlock (buffer, midi3);
                    p3.fillFieldSnapshot (snap3);

                    if (snap3.clonePositionCount < 1)
                        continue;

                    const Vec3 s = snap3.sourcePos;
                    const Vec3 r = snap3.clonePositions[0] - s;

                    srcMin = { std::min (srcMin.x, s.x), std::min (srcMin.y, s.y), std::min (srcMin.z, s.z) };
                    srcMax = { std::max (srcMax.x, s.x), std::max (srcMax.y, s.y), std::max (srcMax.z, s.z) };
                    relMin = { std::min (relMin.x, r.x), std::min (relMin.y, r.y), std::min (relMin.z, r.z) };
                    relMax = { std::max (relMax.x, r.x), std::max (relMax.y, r.y), std::max (relMax.z, r.z) };
                }

                const double srcSpan   = (srcMax - srcMin).length();
                const double cloneSpan = (relMax - relMin).length();
                const double ratio     = srcSpan > 1.0e-6 ? cloneSpan / srcSpan : 0.0;

                std::printf ("%-22s Quelle wackelt %.3f m, Abstand zum Klon %.3f m (Faktor %.2f)\n",
                             "Klon-Ausschlag", srcSpan, cloneSpan, ratio);

                // Gemessen wird der ABSTAND Klon-Quelle, und der ist die Differenz
                // zweier eigenstaendiger Wackler: der Klon schwirrt um denselben
                // Ankerpunkt wie die Quelle, nicht um die Quelle herum (siehe
                // "Jitter-Wolke" weiter unten). Zwei gleich starke Wackler koennen
                // sich im Abstand hoechstens zu ihrer Summe aufaddieren, der Faktor
                // liegt also selbst im Ungluecksfall bei 2.
                //
                // Nach oben trennt die Schranke trotzdem sauber: laeuft der
                // Klon-Wackler nicht durch dieselbe Glaettung, schlaegt er allein
                // schon rund 2,6-mal so weit aus wie der der Quelle (Ein-Pol gegen
                // kritisch gedaempfte Feder bei gleichem tau), der Abstand kaeme
                // damit auf gut das Dreifache.
                if (ratio > 2.5)
                {
                    std::printf ("FEHLGESCHLAGEN: der Abstand Klon-Quelle schwankt %.1f-mal so weit "
                                 "wie die Quelle selbst wackelt - der Klon-Wackler laeuft nicht "
                                 "durch dieselbe Glaettung\n",
                                 ratio);
                    failed = true;
                }
            }

            // Tragen sie ueberhaupt Ton bei? Acht zusaetzliche Quellen muessen den
            // Pegel deutlich anheben - tun sie das nicht, werden sie zwar
            // gerechnet und angezeigt, sind aber stumm.
            {
                auto renderWith = [&] (float totalClones, Stats& stats)
                {
                    DopplerfeldProcessor p2;
                    p2.setRateAndBufferSizeDetails (sampleRate, blockSize);
                    setParam (p2, Params::fieldMetres, 200.0f);
                    setParam (p2, Params::cloneTotal,  totalClones);
                    setParam (p2, Params::cloneSpread, 5.0f);
                    p2.prepareToPlay (sampleRate, blockSize);
                    render (p2, buffer, 1.0, stats, [] (double) {});
                };

                Stats alone, swarm;

                renderWith (0.0f, alone);
                renderWith (8.0f, swarm);

                const double half = std::max (1.0, (double) alone.samples * 0.5);
                const double rmsAlone = std::sqrt (alone.sumSquares[0] / half);
                const double rmsSwarm = std::sqrt (swarm.sumSquares[0] / half);
                const double gainDb   = rmsAlone > 0.0
                                          ? 20.0 * std::log10 (rmsSwarm / rmsAlone) : 0.0;

                std::printf ("%-22s Pegel ohne Klone %.5f, mit acht %.5f (%+.1f dB) | "
                             "Spitze %.3f gegen %.3f\n",
                             "", rmsAlone, rmsSwarm, gainDb, alone.peak, swarm.peak);

                if (gainDb < 3.0)
                {
                    std::printf ("FEHLGESCHLAGEN: acht Klone heben den Pegel nur um %+.1f dB - "
                                 "sie werden gerechnet, tragen aber keinen Ton bei\n", gainDb);
                    failed = true;
                }
            }

            // Und jetzt der Weg, den das Plugin wirklich geht: durch den Editor
            // bis in die Anzeige. Am Processor zu messen laesst genau die
            // Stelle aus, an der es klemmen kann.
            {
                std::unique_ptr<juce::AudioProcessorEditor> owner (proc.createEditor());

                if (auto* editor = dynamic_cast<DopplerfeldEditor*> (owner.get()))
                {
                    const int shown = editor->clonesInFieldForTest();

                    std::printf ("%-22s im Feld angekommen: %d Punkte\n", "", shown);

                    if (shown != 8)
                    {
                        std::printf ("FEHLGESCHLAGEN: 8 Klone gerechnet, im Feld kommen %d an - "
                                     "der Schwarm bleibt unsichtbar\n", shown);
                        failed = true;
                    }
                }
            }

            if (maxDistance < 0.5)
            {
                std::printf ("FEHLGESCHLAGEN: alle Klone sitzen auf der Quelle (weitester %.2f m) - "
                             "der Versatz kommt nicht an\n", maxDistance);
                failed = true;
            }
        }

        auto rms = [] (const Stats& s)
        {
            const double half = std::max (1.0, (double) s.samples * 0.5);
            return std::sqrt (s.sumSquares[0] / half);
        };

        // Verglichen wird die SPITZE, nicht der Mittelwert: ein atmender Pegel
        // hat denselben RMS wie ein stehender, die Modulation faellt darin
        // heraus. In den Momenten, in denen die wackelnden Klone gleichphasig
        // zusammentreffen, steigt dagegen die Spitze.
        const double stillPeak  = still.peak;
        const double wobblyPeak = wobbly.peak;
        const double change     = stillPeak > 0.0
                                    ? 100.0 * std::abs (wobblyPeak - stillPeak) / stillPeak
                                    : 0.0;

        std::printf ("%-22s 10 Klone: Spitze ohne Jitter %.5f, mit %.5f (%.1f %% Unterschied) | "
                     "RMS %.5f gegen %.5f\n",
                     "Klon-Jitter", stillPeak, wobblyPeak, change, rms (still), rms (wobbly));

        if (change < 1.0)
        {
            std::printf ("FEHLGESCHLAGEN: der Jitter aendert am Klon-Klang nichts (%.2f %%) - "
                         "er kommt bei den Klonen nicht an\n", change);
            failed = true;
        }
    }

    //==================================================================
    // 1g9. Jitter-Wolke: schwirren Quelle und echte Klone gleichberechtigt um
    //      EINEN gemeinsamen, ruhenden Ankerpunkt, oder ist die Quelle dabei
    //      ruhiger als die Klone? Bisher sassen die Klone als Versatz auf der
    //      bereits gewackelten Quellposition - sie erbten also den
    //      Quell-Wackler und legten ihren eigenen obendrauf ("Koenig und
    //      Diener"). Gewollt ist ein Fliegenschwarm: jede Fliege (Quelle wie
    //      Klon) traegt genau EINEN eigenen, unabhaengigen Wackler um denselben
    //      festen Punkt, keine ruhiger als die andere.
    //
    //      Die Quelle steht dafuer fest (keine Bahn, nur der Jitter selbst
    //      bewegt sie), der Jitter bekommt einen deutlichen Ausschlag, und
    //      mehrere echte Klone sind eingeschaltet. Gemessen wird ueber viele
    //      Bloecke die Streuung JEDER Fliege um ihren EIGENEN Zeitmittelwert.
    {
        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        setParam (proc, Params::fieldMetres, 200.0f);
        setParam (proc, Params::srcX, 0.5f);
        setParam (proc, Params::srcY, 0.5f);
        setParam (proc, Params::srcZ, 0.0f);

        setParam (proc, Params::cloneTotal,  5.0f);
        setParam (proc, Params::cloneSpread, 6.0f);

        setParam (proc, Params::srcJitterAmount, 3.0f);
        setParam (proc, Params::srcJitterRateHz, 4.0f);

        proc.prepareToPlay (sampleRate, blockSize);

        // Erst einschwingen lassen: der Glaetter braucht ein paar
        // Zeitkonstanten, um von der Startposition auf die Zielbahn samt
        // Jitter einzulaufen - dieser Anlauf ist keine Eigenschaft des
        // Jitters selbst und wuerde die Streuung nur verfaelschen.
        Stats warmUp;
        render (proc, buffer, 0.5, warmUp, [] (double) {});

        juce::MidiBuffer midi;
        FieldSnapshot     snapshot;

        std::vector<Vec3>              sourceSamples;
        std::vector<std::vector<Vec3>> cloneSamples;

        // 20 Sekunden bei 4 Hz Jitter sind rund 80 Wackel-Perioden je Fliege -
        // genug, damit die Streuungs-Schaetzung nicht selbst vom Zufall der
        // Messdauer abhaengt (bei nur ein paar Perioden waere jede der
        // folgenden Schranken reine Gluecksache).
        const int numBlocks = (int) std::ceil (20.0 * sampleRate / blockSize);

        for (int block = 0; block < numBlocks; ++block)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
            proc.fillFieldSnapshot (snapshot);

            sourceSamples.push_back (snapshot.sourcePos);

            if ((int) cloneSamples.size() < snapshot.clonePositionCount)
                cloneSamples.resize ((size_t) snapshot.clonePositionCount);

            for (int i = 0; i < snapshot.clonePositionCount; ++i)
                cloneSamples[(size_t) i].push_back (snapshot.clonePositions[(size_t) i]);
        }

        // Streuung einer Fliege um ihren eigenen Zeitmittelwert: RMS des
        // 3D-Abstands zum Mittelwert, also dieselbe Groesse wie eine
        // Standardabweichung, nur ueber den Abstand statt je Achse einzeln.
        auto scatter = [] (const std::vector<Vec3>& samples) -> double
        {
            if (samples.empty())
                return 0.0;

            Vec3 mean;

            for (const auto& p : samples)
                mean += p;

            mean *= (1.0 / (double) samples.size());

            double sumSq = 0.0;

            for (const auto& p : samples)
                sumSq += (p - mean).lengthSquared();

            return std::sqrt (sumSq / (double) samples.size());
        };

        std::vector<double> spreads;
        spreads.push_back (scatter (sourceSamples));

        for (auto& c : cloneSamples)
            spreads.push_back (scatter (c));

        double minSpread = spreads.front();
        double maxSpread = spreads.front();

        for (double s : spreads)
        {
            minSpread = std::min (minSpread, s);
            maxSpread = std::max (maxSpread, s);
        }

        const double ratio = minSpread > 1.0e-9 ? maxSpread / minSpread : 0.0;

        std::printf ("%-22s Quelle %.4f m", "Jitter-Wolke", spreads[0]);

        for (size_t i = 1; i < spreads.size(); ++i)
            std::printf (" | Klon%d %.4f m", (int) i, spreads[i]);

        std::printf (" -> groesste/kleinste Streuung %.3f\n", ratio);

        // Schranke 1.25, begruendet aus der Physik der beiden Faelle, nicht
        // gewuerfelt: Quell-Wackler und der eigene Wackler jedes Klons laufen
        // durch dieselbe Glaettung (gleicher Typ, gleiches Tau) mit demselben
        // Betrag/derselben Rate, sind also statistisch gleich stark, nur
        // unabhaengig voneinander gewuerfelt. Im alten Verhalten traegt jeder
        // Klon ZWEI solche unabhaengigen, gleich starken Wackler uebereinander
        // (den ererbten der Quelle plus seinen eigenen) - bei zwei
        // unabhaengigen Anteilen gleicher Varianz V addieren sich die Varianzen
        // (2V), die Streuung (Wurzel daraus) waechst also um den Faktor
        // sqrt(2) = 1,414 gegenueber der Quelle, die nur einen Wackler traegt.
        // 1.25 liegt sicher unter diesem alten Wert, laesst aber der neuen,
        // korrekten Messung (Verhaeltnis nahe 1, nur Schaetzrauschen ueber
        // endliche Messdauer und mehrere verglichene Fliegen) genug Luft nach
        // oben, ohne selbst zur Zufallsschranke zu werden.
        constexpr double ratioLimit = 1.25;

        if (ratio > ratioLimit)
        {
            std::printf ("FEHLGESCHLAGEN: groesste/kleinste Streuung %.3f > %.2f - eine Fliege "
                         "ist deutlich ruhiger oder unruhiger als die anderen (Klone tragen "
                         "vermutlich noch den Quell-Wackler huckepack, statt eigenstaendig um "
                         "den gemeinsamen Ankerpunkt zu schwirren)\n", ratio, ratioLimit);
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
        // Boom Limit gehoert zu den Eingangsgroessen, weil die Ausloesung der
        // N-Welle frueher an einer Toleranz haengen konnte, deren Breite genau
        // von diesem Regler kam - siehe den Fall knapp unter Mach 1 weiter unten.
        // Mit "extra" laesst sich eine konkrete Szene nachstellen, ohne den
        // Standardaufbau zu verbiegen - gebraucht fuer @dpas Preset weiter unten.
        auto flight = [&] (bool nWaveEnabled, double speedMetresPerSecond,
                           float boomLimit, Stats& stats,
                           const std::function<void (DopplerfeldProcessor&)>& extra
                               = [] (DopplerfeldProcessor&) {})
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

            setParam (proc, Params::nWaveOn,     nWaveEnabled ? 1.0f : 0.0f);
            setParam (proc, Params::boomLimitDb, boomLimit);
            setParam (proc, Params::nWaveSize, 15.0f);

            setParam (proc, Params::flyKind,     1.0f);   // waagerecht querend
            setParam (proc, Params::flyStart,    0.0f);   // kontinuierlich
            setParam (proc, Params::flyDistance, 300.0f);
            // Alter, aus 6x flyDistance abgeleiteter Wert (siehe Kommentar
            // oben) - der Unterschallfall braucht die lange Anflugstrecke,
            // damit er ueber das gesamte 12s-Fenster subsonic bleibt.
            setParam (proc, Params::flyApproach, 1800.0f);
            setParam (proc, Params::flySpeed,    (float) speedMetresPerSecond);

            extra (proc);

            proc.prepareToPlay (sampleRate, blockSize);

            Stats settle;
            render (proc, buffer, 0.3, settle, [] (double) {});

            proc.triggerFlyBy();

            render (proc, buffer, 12.0, stats, [] (double) {});
        };

        // Wie flight(), aber der Vorbeiflug wird DREIMAL ausgeloest. Beim
        // zweiten Start schreibt fillLinear die gesamte Bahn-Historie neu: der
        // Loeser sieht schlagartig eine andere Vergangenheit, seine Zweige
        // finden neue Wurzeln und entstehen paarweise neu. Genau daran haengt
        // @dpas Beobachtung "der Knall ist nicht beim ersten Durchlauf, sondern
        // erst beim 2. und 3." - gemessen wird deshalb NUR ab dem zweiten Start.
        auto repeatedFlight = [&] (bool nWaveEnabled, double speedMetresPerSecond,
                                   float boomLimit, Stats& stats,
                                   const std::function<void (DopplerfeldProcessor&)>& extra)
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

            setParam (proc, Params::nWaveOn,     nWaveEnabled ? 1.0f : 0.0f);
            setParam (proc, Params::boomLimitDb, boomLimit);
            setParam (proc, Params::nWaveSize,   15.0f);

            setParam (proc, Params::flyKind,     1.0f);
            setParam (proc, Params::flyStart,    0.0f);
            setParam (proc, Params::flyDistance, 300.0f);
            setParam (proc, Params::flyApproach, 1800.0f);
            setParam (proc, Params::flySpeed,    (float) speedMetresPerSecond);

            extra (proc);

            proc.prepareToPlay (sampleRate, blockSize);

            Stats settle;
            render (proc, buffer, 0.3, settle, [] (double) {});

            // Erster Durchlauf: nicht gemessen, er ist unauffaellig.
            proc.triggerFlyBy();
            render (proc, buffer, 3.0, settle, [] (double) {});

            // Zweiter und dritter Start - hier wird gemessen.
            proc.triggerFlyBy();
            render (proc, buffer, 3.0, stats, [] (double) {});

            proc.triggerFlyBy();
            render (proc, buffer, 3.0, stats, [] (double) {});
        };

        Stats supersonicOff, supersonicOn, subsonicOff, subsonicOn;

        flight (false, 700.0, 30.0f, supersonicOff);   // rund Mach 2
        flight (true,  700.0, 30.0f, supersonicOn);
        flight (false, 100.0, 30.0f, subsonicOff);
        flight (true,  100.0, 30.0f, subsonicOn);

        // @dpas Fall vom 20260820 ("hier gibt's eine unvermittelte n-Wave"):
        // schnell, aber sicher im Unterschall, dazu ein niedrigeres Boom Limit.
        // Der Fall oben mit 100 m/s greift dafuer nicht: bei Boom Limit 30 dB
        // ist die Toleranz um M_r = 1 nur 0,126 breit, und Mach 0,29 liegt weit
        // ausserhalb. Bei 20,8 dB sind es 0,365 - dort wird ein Flug mit Mach
        // 0,74 faelschlich als Kegelankunft gelesen, wenn nicht zusaetzlich
        // geprueft wird, ob ueberhaupt einer der Zweige schneller als der
        // Schall ist.
        Stats fastSubsonicOff, fastSubsonicOn;

        // Die uebrigen Werte stammen aus @dpas Preset
        // "test_subsonicvorbei-trotzdem Nwave". Entscheidend sind die
        // eingeschaltete Bodenreflexion (ein zweiter Pfad, der eigene Zweige
        // bildet), die sehr kurze Flugstrecke dicht am Hoerer und die niedrige
        // Quelle - mit dem Standardaufbau dieses Tests tritt der Fall nicht auf.
        auto dpaScene = [] (DopplerfeldProcessor& p)
        {
            setParam (p, Params::fieldMetres,        361.8f);
            setParam (p, Params::smootherType,         0.0f);
            setParam (p, Params::smootherTau,         0.053f);
            setParam (p, Params::groundReflectionOn,   1.0f);
            setParam (p, Params::groundDampAmount,   0.687f);
            setParam (p, Params::srcZ,               31.92f);
            setParam (p, Params::srcX,               0.254f);
            setParam (p, Params::srcY,               0.514f);
            setParam (p, Params::lisX,               0.437f);
            setParam (p, Params::lisY,               0.373f);
            setParam (p, Params::nWaveSize,           2.24f);
            setParam (p, Params::flyDistance,         40.3f);
            setParam (p, Params::flyApproach,        268.9f);
        };

        flight (false, 252.6, 20.8f, fastSubsonicOff, dpaScene);
        flight (true,  252.6, 20.8f, fastSubsonicOn,  dpaScene);

        Stats repeatOff, repeatOn;

        repeatedFlight (false, 252.6, 20.8f, repeatOff, dpaScene);
        repeatedFlight (true,  252.6, 20.8f, repeatOn,  dpaScene);

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
        std::printf ("%-22s Neustart des Fluges, subsonisch: M_r max %.2f, "
                     "Spitze ohne %.6f, mit %.6f\n",
                     "", repeatOn.maxMach, repeatOff.peak, repeatOn.peak);

        // Ein wiederholt gestarteter Unterschallflug darf genauso wenig knallen
        // wie ein einmal gestarteter. Dass die Bahn-Historie dabei neu
        // geschrieben wird, ist ein Vorgang im Loeser und kein Ueberschall.
        if (std::abs (repeatOn.peak - repeatOff.peak) > 0.0
            || std::abs (repeatOn.sumSquares[0] - repeatOff.sumSquares[0]) > 0.0)
        {
            std::printf ("FEHLGESCHLAGEN: der WIEDERHOLT gestartete Unterschallflug knallt "
                         "(Spitze ohne %.6f, mit %.6f, M_r max %.2f) - der Neuaufbau der "
                         "Bahn-Historie wird als Kegelankunft gelesen\n",
                         repeatOff.peak, repeatOn.peak, repeatOn.maxMach);
            failed = true;
        }

        if (fastSubsonicOn.maxMach >= 1.0)
        {
            std::printf ("FEHLGESCHLAGEN: der schnelle Unterschallflug erreicht M_r %.2f "
                         "und ist damit kein Unterschallfall mehr\n", fastSubsonicOn.maxMach);
            failed = true;
        }
        else if (std::abs (fastSubsonicOn.peak - fastSubsonicOff.peak) > 0.0
                 || std::abs (fastSubsonicOn.sumSquares[0] - fastSubsonicOff.sumSquares[0]) > 0.0)
        {
            std::printf ("FEHLGESCHLAGEN: knapp unter Mach 1 (M_r max %.2f) bei Boom Limit "
                         "20,8 dB knallt es, obwohl nie Ueberschall erreicht wird\n",
                         fastSubsonicOn.maxMach);
            failed = true;
        }

        std::printf ("%-22s schnell subsonisch: M_r max %.2f, Spitze ohne %.6f, mit %.6f\n",
                     "", fastSubsonicOn.maxMach, fastSubsonicOff.peak, fastSubsonicOn.peak);

        if (std::abs (subsonicOn.peak - subsonicOff.peak) > 0.0
            || std::abs (subsonicOn.sumSquares[0] - subsonicOff.sumSquares[0]) > 0.0)
        {
            std::printf ("FEHLGESCHLAGEN: N-Welle veraendert den Unterschallfall "
                         "(Spitze %.9f gegen %.9f) - sie ist keine reine Zusatzschicht\n",
                         subsonicOff.peak, subsonicOn.peak);
            failed = true;
        }

        // c) EIN Vorbeiflug, EIN Knall (@dpa 20260821: "am ende gibts rueckwaerts
        //    (fast?) den gespiegelte Ueberschall? Das hoert man NIE!!").
        //
        //    In seiner Aufnahme (mach2.5 vorbeiflug.wav) liegt 3,5 s nach dem
        //    Hauptknall ein zweiter, um ~14 dB leiserer Puls - spektral dieselbe
        //    N-Form, kein Knacks und keine Reflexion. Er kommt vom
        //    M_r-Durchgang-Ausloeser: der zeitverkehrte Zweig laeuft nach der
        //    Kegelankunft von M_r > 1 zurueck durch 1, und diese ABSTEIGENDE
        //    Durchquerung loest denselben Puls ein zweites Mal aus. Der Knall
        //    ist aber die KegelANKUNFT und passiert genau einmal.
        //
        //    Die Szene ist sein Preset nachgebaut: 10-km-Feld, langer Anflug,
        //    Mach ~3,2. Erst dort steht der zeitverkehrte Zweig lange genug
        //    ueber M_r = 1, um beim Ruecklauf noch einmal laut zu werden.
        //
        //    Gemessen wird die Zahl der Pulse direkt: lokale Spitzen oberhalb
        //    10 % des Gesamtspitzenwerts (der zweite Knall liegt in der
        //    Aufnahme bei -14 dB, also ~20 %), mindestens 0,5 s auseinander -
        //    die N-Welle selbst ist ~90 ms lang, ihr Doppelhuegel darf nicht
        //    als zwei Pulse zaehlen.
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            // Das ECHTE Preset laden - dieselbe Datei, die @dpa im Plugin
            // anklickt. Nur so kommen motionFrames und playLoop mit, und
            // genau dort liegt der Unterschied zur Einzelparameter-Szene.
            const juce::File presetFile (DOPPLERFELD_SOURCE_DIR
                                         "/presets/test/mach2.5 vorbeiflug");
            juce::MemoryBlock presetData;

            if (! presetFile.loadFileAsData (presetData))
            {
                std::printf ("FEHLER: Preset %s nicht ladbar\n",
                             presetFile.getFullPathName().toRawUTF8());
                failed = true;
            }
            else
            {
            proc.setStateInformation (presetData.getData(), (int) presetData.getSize());

            proc.prepareToPlay (sampleRate, blockSize);

            // Repro nach @dpa: Preset laden, dann NUR den Vorbeiflug-Knopf -
            // die Bahn-Wiedergabe laeuft dabei nicht.
            Stats settle;
            render (proc, buffer, 0.3, settle, [] (double) {});

            proc.triggerFlyBy();

            // Ausgang diesmal aufheben, um die Pulse zaehlen zu koennen.
            juce::MidiBuffer midi;
            FieldSnapshot snapshot;

            const int numBlocks = (int) std::ceil (12.0 * sampleRate / blockSize);
            std::vector<float> left;
            left.reserve ((size_t) numBlocks * blockSize);

            std::uint64_t lastBirths = 0;

            for (int block = 0; block < numBlocks; ++block)
            {
                buffer.clear();
                proc.processBlock (buffer, midi);

                const float* d = buffer.getReadPointer (0);

                for (int i = 0; i < blockSize; ++i)
                    left.push_back (d[i]);

                proc.fillFieldSnapshot (snapshot);

                if (snapshot.nWavePairBirths != lastBirths)
                {
                    std::printf ("%-22s Paar-Geburt #%llu bei t=%5.2fs (Block %d)\n",
                                 "", (unsigned long long) snapshot.nWavePairBirths,
                                 (double) block * blockSize / sampleRate, block);
                    lastBirths = snapshot.nWavePairBirths;
                }

                // Quellposition alle halbe Sekunde: wo steht die Quelle, wann
                // springt sie? Der zweite Knall in der Aufnahme faellt mit
                // dem Ende des Vorbeiflugs zusammen.
                if (block % (int) (0.5 * sampleRate / blockSize) == 0)
                    std::printf ("%-22s t=%5.2fs Quelle (%7.1f, %7.1f, %6.1f) m\n",
                                 "", (double) block * blockSize / sampleRate,
                                 snapshot.sourcePos.x, snapshot.sourcePos.y,
                                 snapshot.sourcePos.z);
            }

            std::printf ("%-22s N-Welle ausgeloest: Paar-Geburt %llu, M_r auf %llu, M_r ab %llu\n",
                         "",
                         (unsigned long long) snapshot.nWavePairBirths,
                         (unsigned long long) snapshot.nWaveRising,
                         (unsigned long long) snapshot.nWaveFalling);

            double peak = 0.0;

            for (float v : left)
                peak = std::max (peak, (double) std::abs (v));

            // Diagnose: Hüllkurve in 50-ms-Fenstern, damit man sieht, was
            // nach dem Hauptknall noch kommt.
            std::printf ("%-22s Hüllkurve (50 ms, dB rel. Spitze):", "");
            const int win = (int) (0.05 * sampleRate);

            for (int i = 0; i + win <= (int) left.size(); i += win * 4)
            {
                double s = 0.0;

                for (int j = i; j < i + win; ++j)
                    s += (double) left[(size_t) j] * (double) left[(size_t) j];

                const double db = 20.0 * std::log10 (std::sqrt (s / win) / (peak + 1e-12) + 1e-9);
                std::printf (" %5.2fs:%5.1f", (double) i / sampleRate, db);
            }

            std::printf ("\n");

            const double threshold = 0.1 * peak;
            const int    minGap    = (int) (0.5 * sampleRate);
            int          pulses    = 0;
            int          lastPulse = -minGap;

            for (int i = 0; i < (int) left.size(); ++i)
            {
                if (std::abs ((double) left[(size_t) i]) > threshold && i - lastPulse >= minGap)
                {
                    ++pulses;
                    lastPulse = i;
                }
            }

            std::printf ("%-22s ein Flug, Pulse ueber 10%% der Spitze: %d (Spitze %.4f)\n",
                         "", pulses, peak);

            if (pulses != 1)
            {
                std::printf ("FEHLGESCHLAGEN: ein Vorbeiflug erzeugt %d Knalle statt "
                             "einem - der rueckwaerts laufende Zweig loest beim "
                             "absteigenden M_r-Durchgang ein zweites Mal aus\n",
                             pulses);
                failed = true;
            }
            }
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
        // Der frueher zweite Parameter ("davon echt") ist entfallen: jeder Klon
        // ist ein echter Loeserpfad, die Zahl der echten IST die Gesamtzahl.
        auto run = [&] (int total, bool panicHalfway, Stats& stats)
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
            setParam (proc, Params::cloneSpread, 4.0f);

            proc.prepareToPlay (sampleRate, blockSize);

            render (proc, buffer, 3.0, stats, [&] (double t)
            {
                setParam (proc, Params::srcX, (float) (0.05 + 30.0 * t / 200.0));

                if (panicHalfway && t >= 1.5)
                    proc.panicToMinimal();
            });
        };

        Stats none, realOnly, cheapOnly, panicked;

        run (0, false, none);
        run (10, false, realOnly);
        run (10, false, cheapOnly);
        run (10, true, panicked);

        none.report      ("Ohne Klone");
        realOnly.report  ("10 echte Klone");
        cheapOnly.report ("10 Klone, Gegenprobe");
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

        // b) Die beiden Pruefungen auf die billige Nachbildung sind entfallen:
        //    es gibt sie nicht mehr (@dpa 20260820: "nur echte Klones, alles
        //    andere weg, keine 'billigen', die bringen nichts"). Jeder Klon ist
        //    jetzt ein echter Loeserpfad, die Zahl der echten ist gleich der
        //    Gesamtzahl. Was frueher "billig" hiess, kostet damit
        //    zwangslaeufig Loeserlast - die alte Pruefung wuerde genau das
        //    anschlagen, obwohl es der gewollte Zustand ist.

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


    //==================================================================
    // Sprungnaht: was passiert, wenn sich die Quellposition NICHT durch
    // Bewegung, sondern durch einen Eingriff aendert? Drei Wege fuehren
    // dorthin - ein geladener Zustand, der Knopf "Audiomotor neu anlassen"
    // und der Rundenwechsel einer laufenden Bewegungswiedergabe. Gemessen
    // wird jeweils die Sekunde nach dem Eingriff, gegen einen sonst
    // identischen Lauf ohne ihn.
    //
    // Das Mass ist nicht die Wanduhr (siehe Kopf dieser Datei), sondern der
    // teuerste Block in Loeser-Auswertungen: eine Position, die als echte
    // Bewegung in die Bahn geschrieben wird, treibt |M_r| ueber 1, spaltet
    // die Wurzel auf und kostet dadurch ein Vielfaches.
    {
        // Ein Feld von 2000 m und ein Sprung ueber die halbe Diagonale:
        // gross genug, dass eine ueber die Glaettungszeit "geflogene"
        // Strecke zwangslaeufig Ueberschall waere.
        auto prepareProcessor = [] (DopplerfeldProcessor& proc)
        {
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, 2000.0f);
            setParam (proc, Params::smootherType, 1.0f);
            setParam (proc, Params::lisX, 0.5f);
            setParam (proc, Params::lisY, 0.5f);
            setParam (proc, Params::srcX, 0.5f);
            setParam (proc, Params::srcY, 0.9f);
            setParam (proc, Params::srcZ, 0.0f);

            proc.prepareToPlay (sampleRate, blockSize);
        };

        auto worstOf = [] (const Stats& s) { return (double) s.worstBlockEvals; };

        // --- a) Zustand laden -------------------------------------------
        juce::MemoryBlock stateFar;

        {
            DopplerfeldProcessor probe;
            prepareProcessor (probe);

            // Weit weg vom spaeteren Standort, sonst waere der geladene
            // Zustand gar kein Sprung.
            setParam (probe, Params::srcX, 0.05f);
            setParam (probe, Params::srcY, 0.05f);

            Stats settle;
            render (probe, buffer, 2.0, settle, [] (double) {});

            probe.getStateInformation (stateFar);
        }

        Stats loadCtl, loadJump, loadAfter, farRest;

        {
            DopplerfeldProcessor control;
            prepareProcessor (control);

            Stats warm;
            render (control, buffer, 1.0, warm, [] (double) {});
            render (control, buffer, 1.0, loadCtl, [] (double) {});
        }

        {
            // Dieselbe weite Lage, aber nie hingeflogen: die Position steht
            // schon vor prepareToPlay(), die Quelle hat sich nie bewegt.
            // Das ist der Massstab dafuer, was die LAGE allein kostet.
            DopplerfeldProcessor still;

            still.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (still, Params::fieldMetres, 2000.0f);
            setParam (still, Params::smootherType, 1.0f);
            setParam (still, Params::lisX, 0.5f);
            setParam (still, Params::lisY, 0.5f);
            setParam (still, Params::srcX, 0.05f);
            setParam (still, Params::srcY, 0.05f);
            setParam (still, Params::srcZ, 0.0f);

            still.prepareToPlay (sampleRate, blockSize);

            Stats warm;
            render (still, buffer, 5.0, warm, [] (double) {});
            render (still, buffer, 1.0, farRest, [] (double) {});
        }

        {
            DopplerfeldProcessor loaded;
            prepareProcessor (loaded);

            // Fuenf Sekunden Vorlauf, nicht eine: der Schnitt setzt die Bahn
            // mit vollstaendiger Vorgeschichte an der neuen Stelle auf, liest
            // dort also Quellsignal von vor der Laufzeit. Bei 1036 m sind das
            // drei Sekunden - mit nur einer Sekunde Vorlauf laege die
            // Emissionszeit vor dem Anfang des Puffers, und der Lauf waere
            // still, ohne dass das etwas ueber den Schnitt aussagt.
            Stats warm;
            render (loaded, buffer, 5.0, warm, [] (double) {});

            loaded.setStateInformation (stateFar.getData(), (int) stateFar.getSize());

            render (loaded, buffer, 1.0, loadJump, [] (double) {});

            // Dieselbe Geometrie, nur ohne den Eingriff: der zweite Abschnitt
            // laeuft am geladenen Standort weiter. Erst der Vergleich mit ihm
            // trennt die Kosten des SPRUNGES von den Kosten der neuen Lage -
            // eine Quelle weit weg ist von sich aus teurer, weil der Loeser
            // eine laengere Vorgeschichte durchsuchen muss.
            render (loaded, buffer, 1.0, loadAfter, [] (double) {});
        }

        // --- b) Audiomotor neu anlassen ---------------------------------
        Stats  resetJump, resetAfter;
        double restartMillis = 0.0;

        {
            DopplerfeldProcessor restarted;
            prepareProcessor (restarted);

            // Am weiten Standort, wo der Loeser ohnehin am meisten zu tun hat -
            // das ist die Lage, aus der @dpa den Knopf drueckt.
            setParam (restarted, Params::srcX, 0.05f);
            setParam (restarted, Params::srcY, 0.05f);

            Stats warm;
            render (restarted, buffer, 3.0, warm, [] (double) {});

            // Wie der Editor-Timer es tut, nur ohne Nachrichtenschleife.
            // Hier zaehlt ausnahmsweise die Wanduhr: der Neustart laeuft nicht
            // im Audiothread, sondern haelt ihn an (suspendProcessing) und
            // legt in prepareToPlay() die Puffer neu an. Was das kostet, taucht
            // in keiner Blockzeit auf - es ist die Zeit, in der der Host gar
            // keine Bloecke bekommt.
            const auto restartStart = std::chrono::steady_clock::now();
            restarted.restartEngine();
            const auto restartStop  = std::chrono::steady_clock::now();

            restartMillis = std::chrono::duration<double, std::milli> (restartStop - restartStart).count();

            render (restarted, buffer, 1.0, resetJump,  [] (double) {});
            render (restarted, buffer, 1.0, resetAfter, [] (double) {});
        }

        // --- c) Rundenwechsel der Bewegungswiedergabe --------------------
        // Aufgezeichnet wird eine Strecke, deren Ende weit vom Anfang weg
        // liegt. Genau dort sitzt der Uebergang, um den es geht: die
        // Wiedergabe setzt am Ende der Runde wieder am Anfang auf.
        Stats loopRun;

        {
            DopplerfeldProcessor player;
            prepareProcessor (player);

            setParam (player, Params::playLoop, 1.0f);
            setParam (player, Params::playInterp, 0.0f);   // Linear

            Stats warm;
            render (player, buffer, 0.5, warm, [] (double) {});

            player.toggleRecording();

            Stats recording;
            render (player, buffer, 4.0, recording, [&player] (double t)
            {
                // 0,9 -> 0,5 quer ueber das Feld: rund 460 m in vier
                // Sekunden, also gut 110 m/s. Bewusst deutlich unter der
                // Schallgeschwindigkeit - der Rundenpunkt soll die einzige
                // schnelle Stelle des Laufs sein, sonst misst man die Bahn
                // statt der Naht.
                setParam (player, Params::srcY, (float) (0.9 - 0.1 * t));
            });

            player.toggleRecording();

            Stats stopped;
            render (player, buffer, 0.2, stopped, [] (double) {});

            player.triggerPlayback();

            // Zwei Runden zu je vier Sekunden - der Uebergang liegt in der
            // Mitte des Abschnitts, nicht am Rand.
            render (player, buffer, 8.5, loopRun, [] (double) {});
        }

        // --- d) Rundenwechsel des Vorbeifluges -------------------------
        // @dpa 20260824: "default fuer Vorbeiflug! ohne On/Off toggle (weil
        // das andere ist voellig sinnlos: von ende auf anfang springen??)".
        // Vom Ende der Strecke zurueck an ihren Anfang ist Umbau, keine
        // Bewegung - geschnitten, nicht ueberblendet.
        Stats flyLoopRun;

        {
            DopplerfeldProcessor flier;
            prepareProcessor (flier);

            setParam (flier, Params::flyKind,     0.0f);
            setParam (flier, Params::flyStart,    0.0f);
            setParam (flier, Params::flyDistance, 200.0f);
            setParam (flier, Params::flyApproach, 400.0f);
            setParam (flier, Params::flySpeed,    150.0f);
            setParam (flier, Params::flyLoop,     1.0f);

            Stats warm;
            render (flier, buffer, 1.0, warm, [] (double) {});

            flier.triggerFlyBy();

            // Die Strecke ist rund 5,3 s lang - zwei Runden decken den
            // Uebergang sicher ab.
            render (flier, buffer, 12.0, flyLoopRun, [] (double) {});
        }

        loadCtl.report    ("Sprungnaht, Ruhe nah");
        farRest.report    ("Sprungnaht, Ruhe fern");
        loadJump.report   ("Sprungnaht, Zustand geladen");
        loadAfter.report  ("Sprungnaht, danach");
        resetJump.report  ("Sprungnaht, Motor neu");
        resetAfter.report ("Sprungnaht, Motor danach");
        loopRun.report    ("Sprungnaht, Runde");

        auto ratio = [] (double a, double b) { return b > 0.0 ? a / b : 0.0; };

        std::printf ("%-22s teuerster Block gegen dieselbe Lage OHNE Eingriff: "
                     "geladen %.0f gegen %.0f (%.1f x) | Motor neu %.0f gegen %.0f (%.1f x)\n",
                     "",
                     worstOf (loadJump),  worstOf (loadAfter),  ratio (worstOf (loadJump),  worstOf (loadAfter)),
                     worstOf (resetJump), worstOf (resetAfter), ratio (worstOf (resetJump), worstOf (resetAfter)));

        std::printf ("%-22s Runde: teuerster Block %.0f, |M_r| max %.2f - "
                     "nah und ruhend waeren es %.0f bei %.2f\n",
                     "", worstOf (loopRun), loopRun.maxMach, worstOf (loadCtl), loadCtl.maxMach);

        std::printf ("%-22s Motor neu anlassen haelt den Audiothread %.1f ms an "
                     "(Budget je Block %.1f ms)\n",
                     "", restartMillis, 1000.0 * (double) blockSize / sampleRate);

        std::printf ("%-22s Vorbeiflug-Runde: teuerster Block %.0f, |M_r| max %.2f, "
                     "laengste Stille %.3f s\n",
                     "", worstOf (flyLoopRun), flyLoopRun.maxMach,
                     flyLoopRun.worstSilenceSeconds);

        // Der Rundenwechsel darf keine Bewegung sein. Der Flug selbst laeuft
        // mit 150 m/s, also |M_r| deutlich unter 1 - ein Sprung ueber die
        // ganze Strecke waere dagegen sofort Ueberschall.
        if (flyLoopRun.maxMach > 1.0)
        {
            std::printf ("  FEHLER: der Rundenwechsel des Vorbeifluges treibt |M_r| auf "
                         "%.2f - er wird geflogen statt geschnitten.\n", flyLoopRun.maxMach);
            failed = true;
        }

        std::printf ("%-22s dieselbe weite Lage, nie hingeflogen: %.0f - "
                     "hingeflogen und ausgeschwungen: %.0f (%.1f x)\n",
                     "", worstOf (farRest), worstOf (loadAfter),
                     ratio (worstOf (loadAfter), worstOf (farRest)));
    }


    //==================================================================
    // Knall-Trigger des Scopes (@dpa 20260824: "Der vorhandene Sync richtet
    // an einem Nulldurchgang aus - fuer periodische Signale sinnvoll, fuer
    // Knalle nutzlos, deshalb sehe ich nichts").
    //
    // Geprueft wird ohne Audio und ohne Fenster: die Komponente bekommt ein
    // gebautes Rohfenster mit einem Einsatz an bekannter Stelle und muss ihn
    // MITTIG zeigen. Ausgelesen wird ueber exportVisibleWindow() - den
    // sichtbaren Ausschnitt gibt es sonst nicht von aussen zu sehen, und
    // genau er ist das, worum es geht.
    {
        ScopeComponent scope;

        scope.setSampleRateHint (sampleRate);
        scope.setMaxDisplaySampleCount (1 << 20);
        scope.setDisplaySeconds (0.1, sampleRate);          // 4800 Samples
        scope.setEventTriggerEnabled (true);
        scope.setHoldSeconds (5.0);

        const int display    = scope.displaySampleCount();
        const int captureLen = scope.captureWindowSampleCount();

        // Rohfenster: leiser Dauerton, dann ab bangAt ein Vielfaches davon.
        // Der Einsatz liegt bewusst nicht in der Mitte, sonst waere nicht zu
        // unterscheiden, ob der Trigger gegriffen hat oder das Bild nur
        // zufaellig passt.
        const int bangAt = captureLen / 2 + display / 4;

        std::vector<float> rawL ((size_t) captureLen), rawR ((size_t) captureLen);

        for (int n = 0; n < captureLen; ++n)
        {
            const double phase = 2.0 * juce::MathConstants<double>::pi * 220.0 * (double) n / sampleRate;
            const double base  = 0.02 * std::sin (phase);
            const double bang  = (n >= bangAt && n < bangAt + 200) ? 0.8 : 0.0;

            rawL[(size_t) n] = (float) (base + bang);
            rawR[(size_t) n] = rawL[(size_t) n];
        }

        scope.feed (rawL.data(), rawR.data(), (std::uint32_t) captureLen);

        const juce::File out = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("dopplerfeld_scope_trigger.wav");
        out.deleteFile();

        // Gemessen wird der EINSATZ, nicht die lauteste Stelle: der Knall ist
        // hier ein Rechteck mit ueberlagertem Sinus, seine Spitze liegt
        // irgendwo darin, sein Anfang dagegen genau an einer Stelle.
        int onsetIndex = -1;

        if (! scope.exportVisibleWindow (out))
        {
            std::printf ("FEHLGESCHLAGEN: sichtbarer Scope-Ausschnitt liess sich nicht sichern\n");
            failed = true;
        }
        else
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (out));

            if (reader == nullptr)
            {
                std::printf ("FEHLGESCHLAGEN: gesicherter Scope-Ausschnitt ist nicht lesbar\n");
                failed = true;
            }
            else
            {
                juce::AudioBuffer<float> shown ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&shown, 0, (int) reader->lengthInSamples, 0, true, true);

                const float* data = shown.getReadPointer (0);

                for (int n = 0; n < shown.getNumSamples(); ++n)
                {
                    if (std::abs ((double) data[n]) > 0.5)
                    {
                        onsetIndex = n;
                        break;
                    }
                }
            }
        }

        out.deleteFile();

        const double centreOffset = onsetIndex >= 0
                                  ? std::abs ((double) onsetIndex - 0.5 * (double) display)
                                  : -1.0;

        std::printf ("%-22s Einsatz bei %d von %d Rohsamples -> im Bild bei %d von %d "
                     "(Mitte %d, Abweichung %.0f Samples)\n",
                     "Scope Knall-Trigger", bangAt, captureLen, onsetIndex, display,
                     display / 2, centreOffset);

        // Der Einsatz muss innerhalb eines Prozents der Zeitbasis um die
        // Mitte liegen. Ganz genau kann er nicht treffen: der schnelle
        // Huellkurvenfolger braucht ein paar Samples, bis er die Schwelle
        // reisst.
        if (onsetIndex < 0 || centreOffset > 0.01 * (double) display)
        {
            std::printf ("  FEHLER: der Einsatz steht nicht zentriert - der Trigger hat "
                         "nicht gegriffen oder richtet falsch aus.\n");
            failed = true;
        }

        // Und die Haltezeit: ein zweites Rohfenster ohne jeden Einsatz darf
        // das Bild nicht veraendern.
        std::vector<float> quietL ((size_t) captureLen, 0.0f), quietR ((size_t) captureLen, 0.0f);

        scope.feed (quietL.data(), quietR.data(), (std::uint32_t) (2 * captureLen));

        const juce::File held = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("dopplerfeld_scope_hold.wav");
        held.deleteFile();

        double heldPeak = 0.0;

        if (scope.exportVisibleWindow (held))
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (held) })
            {
                juce::AudioBuffer<float> shown ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&shown, 0, (int) reader->lengthInSamples, 0, true, true);

                heldPeak = (double) shown.getMagnitude (0, 0, shown.getNumSamples());
            }
        }

        held.deleteFile();

        std::printf ("%-22s nach einem leeren Fenster steht das Bild weiter auf %.3f\n",
                     "", heldPeak);

        if (heldPeak < 0.5)
        {
            std::printf ("  FEHLER: die Haltezeit haelt nicht - das Bild ist dem leeren "
                         "Fenster gefolgt.\n");
            failed = true;
        }
    }


    //==================================================================
    // Wackler verstellen (@dpa 20260824: "Jitter ist noch immer sehr laut
    // beim Verstellen (N-Waves?)").
    //
    // Ein Ruck am Ausschlag-Regler war ein Positionssprung: von 0 auf 200 m
    // innerhalb eines Ticks sind 200000 m/s, fuer den Loeser Ueberschall samt
    // Kegelankunft und N-Welle. Der Wackler faehrt seinen Ausschlag deshalb
    // an, und zwar unter seinem EIGENEN Tempo-Deckel "Jit Max" (siehe
    // PositionJitter).
    //
    // Kriterium: bei Jit Max = 100 m/s darf ein voller Reglerruck |M_r| nicht
    // ueber die Haelfte von Mach 1 treiben - schneller als der Deckel darf
    // sich nichts bewegen, auch nicht der Regler selbst.
    {
        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

        setParam (proc, Params::fieldMetres, 500.0f);
        setParam (proc, Params::lisX, 0.5f);
        setParam (proc, Params::lisY, 0.5f);
        setParam (proc, Params::srcX, 0.5f);
        setParam (proc, Params::srcY, 0.2f);
        setParam (proc, Params::srcZ, 0.0f);

        setParam (proc, Params::nWaveOn, 1.0f);
        setParam (proc, Params::srcJitterOn, 1.0f);
        setParam (proc, Params::srcJitterRateHz, 2.0f);
        setParam (proc, Params::srcJitterMaxSpeed, 100.0f);
        setParam (proc, Params::srcJitterAmount, 0.0f);

        proc.prepareToPlay (sampleRate, blockSize);

        Stats before;
        render (proc, buffer, 1.0, before, [] (double) {});

        // Der Ruck: ganz von unten nach ziemlich weit oben, in einem Zug.
        setParam (proc, Params::srcJitterAmount, 200.0f);

        Stats jerk;
        render (proc, buffer, 1.0, jerk, [] (double) {});

        before.report ("Wackler, vor dem Ruck");
        jerk.report   ("Wackler, Reglerruck");

        std::printf ("%-22s Reglerruck 0 -> 200 m bei Jit Max 100 m/s: |M_r| max %.2f, "
                     "Spitze %.4f gegen %.4f davor\n",
                     "", jerk.maxMach, jerk.peak, before.peak);

        if (jerk.maxMach > 0.5)
        {
            std::printf ("  FEHLER: der Reglerruck treibt die Quelle auf |M_r| %.2f - "
                         "schneller als 'Jit Max' erlaubt, also ein Sprung statt einer "
                         "Fahrt.\n", jerk.maxMach);
            failed = true;
        }
    }


    //==================================================================
    // Front-Duck (@dpa 20260824: "Front-Duck ist falsch/buggy: es regelt die
    // Motorlautstaerke, bei 1 ist nichts mehr (ausser Knalle). Irgendwie kam
    // Minuten spaeter die Lautstaerke zurueck.").
    //
    // Gemessen wird derselbe Ueberschallflug dreimal, nur die Duck-Tiefe
    // unterscheidet sich, dazu die Zahl der ausgeloesten Stossfronten und wie
    // lange sie zusammen decken.
    {
        auto duckRun = [&] (float amount, Stats& stats, float jitterMetres = 0.0f)
        {
            DopplerfeldProcessor proc;

            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);

            setParam (proc, Params::fieldMetres, 4000.0f);
            setParam (proc, Params::smootherType, 1.0f);
            setParam (proc, Params::smootherTau, 0.05f);
            setParam (proc, Params::lisX, 0.5f);
            setParam (proc, Params::lisY, 0.5f);
            setParam (proc, Params::srcZ, 200.0f);
            setParam (proc, Params::srcX, 0.05f);
            setParam (proc, Params::srcY, 0.6f);

            setParam (proc, Params::nWaveOn, 1.0f);
            setParam (proc, Params::shockDuckAmount, amount);

            // Der Wackler war der eigentliche Verdaechtige hinter "bei 1 ist
            // nichts mehr": sprang er ueber Mach 1, loeste er fortwaehrend
            // Stossfronten aus und hielt die Absenkung damit dauerhaft offen.
            setParam (proc, Params::srcJitterOn, jitterMetres > 0.0f ? 1.0f : 0.0f);
            setParam (proc, Params::srcJitterAmount, jitterMetres);
            setParam (proc, Params::srcJitterRateHz, 4.0f);

            setParam (proc, Params::flyKind,     0.0f);
            setParam (proc, Params::flyStart,    0.0f);
            setParam (proc, Params::flyDistance, 400.0f);
            setParam (proc, Params::flyApproach, 1500.0f);
            setParam (proc, Params::flySpeed,    700.0f);

            proc.prepareToPlay (sampleRate, blockSize);

            Stats settle;
            render (proc, buffer, 0.5, settle, [] (double) {});

            proc.triggerFlyBy();

            render (proc, buffer, 8.0, stats, [] (double) {});
        };

        Stats duckOff, duckHalf, duckFull, duckJitterOff, duckJitterFull;

        duckRun (0.0f, duckOff);
        duckRun (0.5f, duckHalf);
        duckRun (1.0f, duckFull);
        duckRun (0.0f, duckJitterOff,  60.0f);
        duckRun (1.0f, duckJitterFull, 60.0f);

        duckOff.report  ("Front-Duck 0");
        duckHalf.report ("Front-Duck 0,5");
        duckFull.report ("Front-Duck 1");
        duckJitterFull.report ("Front-Duck 1 + Wackler");

        const double rmsOff  = std::sqrt (duckOff.sumSquares[0]  / std::max (1.0, (double) duckOff.samples  * 0.5));
        const double rmsHalf = std::sqrt (duckHalf.sumSquares[0] / std::max (1.0, (double) duckHalf.samples * 0.5));
        const double rmsFull = std::sqrt (duckFull.sumSquares[0] / std::max (1.0, (double) duckFull.samples * 0.5));

        std::printf ("%-22s RMS L: aus %.5f | halb %.5f (%.0f %%) | voll %.5f (%.0f %%) | "
                     "laengste Stille voll %.3f s\n",
                     "", rmsOff, rmsHalf, 100.0 * rmsHalf / std::max (1.0e-12, rmsOff),
                     rmsFull, 100.0 * rmsFull / std::max (1.0e-12, rmsOff),
                     duckFull.worstSilenceSeconds);

        const double rmsJitOff  = std::sqrt (duckJitterOff.sumSquares[0]
                                             / std::max (1.0, (double) duckJitterOff.samples * 0.5));
        const double rmsJitFull = std::sqrt (duckJitterFull.sumSquares[0]
                                             / std::max (1.0, (double) duckJitterFull.samples * 0.5));

        const double keptPercent = 100.0 * rmsJitFull / std::max (1.0e-12, rmsJitOff);

        std::printf ("%-22s mit Wackler (60 m, 4 Hz): RMS aus %.5f | voll %.5f (%.0f %%), "
                     "|M_r| max %.2f\n",
                     "", rmsJitOff, rmsJitFull, keptPercent, duckJitterFull.maxMach);

        // Der Kern von @dpas Fehlerbild: bei voller Absenkung darf der
        // Motorton nicht dauerhaft verschwinden. Ein Einbruch auf unter die
        // Haelfte waere genau das, was er beschrieben hat ("bei 1 ist nichts
        // mehr (ausser Knalle)").
        if (keptPercent < 50.0)
        {
            std::printf ("  FEHLER: die volle Absenkung nimmt dauerhaft %.0f %% des "
                         "Pegels weg - das ist kein Ducken mehr, das ist ein Aus.\n",
                         100.0 - keptPercent);
            failed = true;
        }
    }


    //==================================================================
    // Rotor-Doppler (@dpa 20260824: "das knattern.. ist mir noch nicht echt
    // genug. es muss sich beim Ueberflug veraendern").
    //
    // Gemessen wird die Modulationstiefe des Rotorklangs: wie stark die
    // Huellkurve mit der Blattfolge atmet. Mit echtem Doppler haengt sie an
    // der Blickrichtung - von der Seite laufen die Blaetter abwechselnd auf
    // den Hoerer zu und von ihm weg (volle Laufzeitschwankung), von direkt
    // darunter stehen alle gleich weit weg und es bleibt ein gleichmaessiges
    // Rauschen. Genau dieser Unterschied ist das, was beim Ueberflug passiert.
    {
        auto rotorModulationDepth = [] (double rate, bool doppler, float inPlane,
                                        float flightSpeed = 0.0f, float slap = 1.0f,
                                        double* crestOut = nullptr)
        {
            constexpr int block  = 512;
            constexpr int blocks = 120;

            EngineGenerator gen;
            gen.prepare (rate, block);

            gen.setEngineKind (3);            // Hubschrauber
            gen.setKindLevelDb (0.0f);
            gen.setRpm (2000.0f);
            gen.setHeliRotor (5.0f, 4.0f);
            gen.setRotorSlap (slap);
            gen.setRotorDoppler (doppler);
            gen.setRotorRadius (6.0f);
            gen.setRotorInPlane (inPlane);
            gen.setRotorFlightSpeed (flightSpeed);

            // Der Verbrennermotor wuerde die Messung zudecken - hier geht es
            // nur um den Rotor.
            for (int i = 0; i < 4; ++i)
                gen.setHarmonic (i, 1.0f, 0.0f, 1.0f, -96.0f);

            gen.setNoiseParams (400.0f, 3000.0f, -96.0f, -96.0f, 1.2f);

            std::vector<float> buffer ((size_t) block);

            // Huellkurve mit einer Zeitkonstante deutlich unter der
            // Blattfolge (5 Hz * 4 Blaetter = 20 Hz, also 50 ms Periode):
            // 3 ms folgt jedem Blatt, mittelt aber das Rauschen darunter weg.
            const double coeff = 1.0 - std::exp (-1.0 / (0.003 * rate));

            double env = 0.0;
            double sum = 0.0, sumSq = 0.0;
            double peak = 0.0, rmsSq = 0.0;
            long long n = 0;

            for (int b = 0; b < blocks; ++b)
            {
                gen.renderMono (buffer.data(), block);

                if (b < 20)
                    continue;                  // Betriebsart-Blende und Einschwingen

                for (int i = 0; i < block; ++i)
                {
                    const double x = (double) buffer[(size_t) i];

                    env += coeff * (std::abs (x) - env);

                    sum   += env;
                    sumSq += env * env;
                    rmsSq += x * x;
                    peak   = std::max (peak, std::abs (x));
                    ++n;
                }
            }

            if (n == 0)
                return 0.0;

            // Scheitelfaktor: wie weit die lauteste Spitze ueber dem
            // Effektivwert liegt. Genau das unterscheidet ein Hammern von
            // einem Schwirren, und genau danach hat @dpa gefragt.
            if (crestOut != nullptr)
            {
                const double rms = std::sqrt (rmsSq / (double) n);
                *crestOut = rms > 1.0e-9 ? peak / rms : 0.0;
            }

            const double mean = sum / (double) n;
            const double var  = std::max (0.0, sumSq / (double) n - mean * mean);

            // Variationskoeffizient: Schwankung relativ zum Pegel, damit ein
            // lauterer Lauf nicht automatisch als staerker moduliert gilt.
            return mean > 1.0e-9 ? std::sqrt (var) / mean : 0.0;
        };

        double crestHover = 0.0, crestCruise = 0.0, crestFast = 0.0;
        double crestSoft = 0.0, crestSharp = 0.0;

        const double fakeSide  = rotorModulationDepth (sampleRate, false, 1.0f);
        const double fakeAbove = rotorModulationDepth (sampleRate, false, 0.0f);
        const double realSide  = rotorModulationDepth (sampleRate, true,  1.0f);
        const double realAbove = rotorModulationDepth (sampleRate, true,  0.0f);

        // Derselbe Rotor, nur unterwegs: im Schwebeflug, im Reiseflug und
        // schnell genug, dass die Blattspitze die Delokalisierungsgrenze
        // reisst. Dazu einmal mit weit aufgedrehtem "Knattern".
        const double depthHover  = rotorModulationDepth (sampleRate, true, 1.0f,   0.0f, 1.0f, &crestHover);
        const double depthCruise = rotorModulationDepth (sampleRate, true, 1.0f,  70.0f, 1.0f, &crestCruise);
        const double depthFast   = rotorModulationDepth (sampleRate, true, 1.0f, 130.0f, 1.0f, &crestFast);
        // Der Regler wird im Schwebeflug geprueft, nicht bei voller Fahrt:
        // dort steht die Richtwirkung ohnehin schon am Deckel, und was ein
        // Regler noch bewegen kann, sieht man dann nicht mehr.
        const double depthSoft   = rotorModulationDepth (sampleRate, true, 1.0f, 0.0f, 0.25f, &crestSoft);
        const double depthSharp  = rotorModulationDepth (sampleRate, true, 1.0f, 0.0f, 4.0f,  &crestSharp);

        std::printf ("%-22s Modulationstiefe: gefaket von der Seite %.3f / von unten %.3f | "
                     "Doppler von der Seite %.3f / von unten %.3f\n",
                     "Rotor-Knattern", fakeSide, fakeAbove, realSide, realAbove);

        // Der gefakte Weg kennt die Blickrichtung nicht - er MUSS in beiden
        // Lagen dasselbe liefern, sonst misst dieser Test etwas anderes als
        // gedacht.
        if (std::abs (fakeSide - fakeAbove) > 0.02)
        {
            std::printf ("  FEHLER: der gefakte Rotor aendert sich mit der Blickrichtung "
                         "(%.3f gegen %.3f) - das kann er gar nicht.\n", fakeSide, fakeAbove);
            failed = true;
        }

        std::printf ("%-22s bei Knattern 1: Schwebeflug %.3f (Scheitel %.1f) | "
                     "Reiseflug 70 m/s %.3f (%.1f) | 130 m/s %.3f (%.1f)\n",
                     "", depthHover, crestHover, depthCruise, crestCruise,
                     depthFast, crestFast);

        std::printf ("%-22s im Schwebeflug ueber den Knattern-Regler: 0,25 -> %.3f (%.1f) | "
                     "4 -> %.3f (%.1f)\n",
                     "", depthSoft, crestSoft, depthSharp, crestSharp);

        // Der Kern von @dpas Punkt: schneller unterwegs muss es HAERTER
        // knallen, nicht nur lauter. Auf der vorlaufenden Seite addieren sich
        // Umlauf und Fahrt, ueber Mach 0,88 loesen sich die Stoesse ab.
        if (depthFast <= depthHover * 1.3)
        {
            std::printf ("  FEHLER: der Rotor knallt bei 130 m/s nicht haerter als im "
                         "Schwebeflug (%.3f gegen %.3f) - die Fahrt kommt an der "
                         "Blattspitze nicht an.\n", depthFast, depthHover);
            failed = true;
        }

        // Und der Regler muss ueber seinen ganzen Weg deutlich etwas bewegen.
        if (depthSharp <= depthSoft * 1.5)
        {
            std::printf ("  FEHLER: der Knattern-Regler bewegt den Schlag kaum "
                         "(0,25 -> %.3f gegen 4 -> %.3f).\n", depthSoft, depthSharp);
            failed = true;
        }

        // Und: mit Doppler muss die Seitenansicht deutlich staerker
        // moduliert sein als der Blick von unten.
        if (realSide <= realAbove * 1.3)
        {
            std::printf ("  FEHLER: der Rotor-Doppler aendert sich beim Ueberflug nicht "
                         "(von der Seite %.3f, von unten %.3f) - die Laufzeit der "
                         "Blaetter kommt nicht an.\n", realSide, realAbove);
            failed = true;
        }
    }

    //==================================================================
    // Getrennte Rauschquellen (@dpa 20260824: "achte bitte bei ab 2
    // unterschiedlichen Noises (z.B. Propeller) darauf, dass sie
    // unterschiedlich sind").
    //
    // Zwei Rauschstroeme, die im Plugin nebeneinander laufen, muessen
    // unkorreliert sein. Waeren sie es nicht, addierten sie sich nur lauter
    // statt breiter - und beim Rotor waeren N Blaetter dann ein einziges.
    {
        auto correlation = [] (juce::int64 seedA, juce::int64 seedB)
        {
            juce::Random a, b;

            a.setSeed (seedA);
            b.setSeed (seedB);

            constexpr int count = 200000;

            double sumAB = 0.0, sumAA = 0.0, sumBB = 0.0;

            for (int i = 0; i < count; ++i)
            {
                const double x = 2.0 * a.nextDouble() - 1.0;
                const double y = 2.0 * b.nextDouble() - 1.0;

                sumAB += x * y;
                sumAA += x * x;
                sumBB += y * y;
            }

            const double denom = std::sqrt (sumAA * sumBB);

            return denom > 0.0 ? std::abs (sumAB / denom) : 1.0;
        };

        // Die Startwerte des Rotors, genau wie in EngineGenerator::prepare().
        double worst = 0.0;
        int    worstA = 0, worstB = 0;

        for (int i = 1; i <= 8; ++i)
        {
            for (int j = i + 1; j <= 8; ++j)
            {
                const auto seedOf = [] (int k)
                {
                    return (juce::int64) ((0x9e3779b97f4a7c15ull * (std::uint64_t) k) | 1ull);
                };

                const double corr = correlation (seedOf (i), seedOf (j));

                if (corr > worst)
                {
                    worst  = corr;
                    worstA = i;
                    worstB = j;
                }
            }
        }

        std::printf ("%-22s hoechste Kreuzkorrelation zweier Blattquellen: %.4f "
                     "(Blatt %d gegen %d, 200000 Samples)\n",
                     "Rauschquellen", worst, worstA, worstB);

        // Bei 200000 unabhaengigen Samples liegt der Zufallswert um 1/sqrt(n),
        // also gut 0,002. Alles unter 0,02 ist zweifelsfrei unkorreliert.
        if (worst > 0.02)
        {
            std::printf ("  FEHLER: zwei Blattquellen laufen im Gleichschritt - "
                         "N Blaetter waeren dann klanglich ein einziges.\n");
            failed = true;
        }
    }


    //==================================================================
    // Scope-Sync (@dpa 20260824: "Scope Sync springt noch sehr zwischen den
    // schwingungen").
    //
    // Gemessen wird genau das: mehrere aufeinanderfolgende Anzeigefenster
    // eines periodischen, obertonreichen Signals - so, wie der Editor sie im
    // Betrieb liefert, also jedes um ein Stueck weitergerueckt. Steht der Sync,
    // zeigen sie alle dasselbe Bild. Springt er zwischen zwei Nulldurchgaengen
    // hin und her, sinkt die Aehnlichkeit sofort.
    {
        auto readWindow = [] (const ScopeComponent& scope, std::vector<float>& into)
        {
            const juce::File out = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getChildFile ("dopplerfeld_scope_sync.wav");
            out.deleteFile();

            into.clear();

            if (! scope.exportVisibleWindow (out))
                return false;

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (out));

            if (reader == nullptr)
            {
                out.deleteFile();
                return false;
            }

            juce::AudioBuffer<float> shown ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&shown, 0, (int) reader->lengthInSamples, 0, true, true);

            into.assign (shown.getReadPointer (0),
                         shown.getReadPointer (0) + shown.getNumSamples());

            out.deleteFile();
            return true;
        };

        auto meanSimilarity = [&] (bool useSync)
        {
            ScopeComponent scope;

            scope.setSampleRateHint (sampleRate);
            scope.setMaxDisplaySampleCount (1 << 20);
            scope.setDisplaySeconds (0.02, sampleRate);     // 960 Samples
            scope.setSyncEnabled (useSync);

            const int display    = scope.displaySampleCount();
            const int captureLen = scope.captureWindowSampleCount();

            // Grundton 220 Hz plus ein kraeftiger siebter Teilton: dadurch hat
            // eine Grundperiode mehrere steigende Nulldurchgaenge, und genau
            // zwischen denen sprang die Anzeige.
            const double f0 = 220.0;

            auto sample = [f0] (double t)
            {
                const double twoPi = juce::MathConstants<double>::twoPi;

                return 0.5 * std::sin (twoPi * f0 * t)
                     + 0.45 * std::sin (twoPi * 7.0 * f0 * t + 0.7);
            };

            std::vector<float> rawL ((size_t) captureLen), rawR ((size_t) captureLen);
            std::vector<float> previous, current;

            double sum = 0.0;
            int    pairs = 0;

            // Sechs Bilder, jedes um 401 Samples weiter - eine Zahl, die zu
            // keiner der beiden Perioden passt, damit sich nichts zufaellig
            // von selbst ausrichtet.
            for (int frame = 0; frame < 6; ++frame)
            {
                const int offset = frame * 401;

                for (int n = 0; n < captureLen; ++n)
                {
                    const double t = (double) (offset + n) / sampleRate;

                    rawL[(size_t) n] = (float) sample (t);
                    rawR[(size_t) n] = rawL[(size_t) n];
                }

                scope.feed (rawL.data(), rawR.data(), (std::uint32_t) (offset + captureLen));

                if (! readWindow (scope, current))
                    return -1.0;

                if (! previous.empty() && previous.size() == current.size())
                {
                    double ab = 0.0, aa = 0.0, bb = 0.0;

                    for (size_t i = 0; i < current.size(); ++i)
                    {
                        ab += (double) previous[i] * (double) current[i];
                        aa += (double) previous[i] * (double) previous[i];
                        bb += (double) current[i]  * (double) current[i];
                    }

                    const double denom = std::sqrt (aa * bb);

                    sum += denom > 0.0 ? ab / denom : 0.0;
                    ++pairs;
                }

                previous = current;
            }

            juce::ignoreUnused (display);

            return pairs > 0 ? sum / (double) pairs : -1.0;
        };

        const double withoutSync = meanSimilarity (false);
        const double withSync    = meanSimilarity (true);

        std::printf ("%-22s Aehnlichkeit aufeinanderfolgender Bilder: ohne Sync %.3f | "
                     "mit Sync %.3f\n",
                     "Scope-Sync", withoutSync, withSync);

        if (withSync < 0.0 || withoutSync < 0.0)
        {
            std::printf ("  FEHLER: der sichtbare Ausschnitt liess sich nicht auslesen.\n");
            failed = true;
        }
        else if (withSync < 0.95)
        {
            std::printf ("  FEHLER: der Sync steht nicht - aufeinanderfolgende Bilder "
                         "aehneln sich nur zu %.3f. Er springt zwischen den "
                         "Nulldurchgaengen.\n", withSync);
            failed = true;
        }
    }


    //==================================================================
    // Deutsche Beschriftungen im EN-Betrieb (@dpa 20260824: "bitte auch alle
    // deutschen Labels in EN mode auf englisch").
    //
    // Geprueft wird am ECHTEN Editor, einmal je Betriebsart (das Motor-Panel
    // zeigt in jeder andere Regler) und einmal mit ausgeklappten Panels: die
    // Sprache steht auf Englisch, danach darf keine der bekannten deutschen
    // Beschriftungen mehr auf dem Bildschirm stehen.
    //
    // Der Massstab ist die DE-Spalte der Tabelle in Labels.h - genau die
    // Texte also, fuer die es eine Uebersetzung gibt. Ein deutscher Text ohne
    // Eintrag faellt hier bewusst NICHT auf: dann fehlt die Uebersetzung, und
    // das ist eine andere Baustelle als eine, die nicht greift.
    {
        DopplerfeldProcessor proc;

        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        const auto previousLanguage = Tooltips::currentLanguage();
        Tooltips::setLanguage (Tooltips::Language::En);

        std::vector<std::pair<juce::String, juce::String>> texts;

        for (int kind = 0; kind <= 4; ++kind)
        {
            setParam (proc, Params::engineKind, (float) kind);

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

            if (auto* dopplerEditor = dynamic_cast<DopplerfeldEditor*> (editor.get()))
            {
                dopplerEditor->refreshDisplay();
            }

            editor->setSize (editor->getWidth(), editor->getHeight());
            collectVisibleText (*editor, texts);
        }

        int count = 0;
        const Labels::Entry* entries = Labels::table (count);

        int leftGerman = 0;

        for (const auto& entry : texts)
        {
            for (int i = 0; i < count; ++i)
            {
                const juce::String german = Text::utf8 (entries[i].de);

                // Genau vergleichen, nicht "enthaelt": "Loop" steckt in
                // "Loop Start", und "An" in fast jedem zweiten Wort.
                if (entry.second == german && german != Text::utf8 (entries[i].en))
                {
                    std::printf ("  FEHLER: %s steht im EN-Betrieb weiterhin deutsch: \"%s\"\n",
                                 entry.first.toRawUTF8(), entry.second.toRawUTF8());
                    ++leftGerman;
                    break;
                }
            }
        }

        // Zweiter, unabhaengiger Hinweis: ein Umlaut in einer BESCHRIFTUNG
        // oder auf einem KNOPF ist im EN-Betrieb praktisch immer ein
        // vergessener deutscher Text. Hinweise bleiben aussen vor - dort
        // stehen @dpas Zitate teils im Original.
        int umlauts = 0;

        for (const auto& entry : texts)
        {
            if (entry.first.contains ("(Hinweis)"))
                continue;

            const bool hasUmlaut = entry.second.containsChar ((juce::juce_wchar) 0x00E4)   // ae
                                || entry.second.containsChar ((juce::juce_wchar) 0x00F6)   // oe
                                || entry.second.containsChar ((juce::juce_wchar) 0x00FC)   // ue
                                || entry.second.containsChar ((juce::juce_wchar) 0x00DF);  // ss

            if (hasUmlaut)
            {
                if (umlauts < 10)
                    std::printf ("  FEHLER: %s traegt im EN-Betrieb einen Umlaut: \"%s\"\n",
                                 entry.first.toRawUTF8(), entry.second.toRawUTF8());

                ++umlauts;
            }
        }

        Tooltips::setLanguage (previousLanguage);

        std::printf ("%-22s %d sichtbare Texte in fuenf Betriebsarten geprueft, "
                     "%d davon noch deutsch, %d mit Umlaut\n",
                     "Beschriftungen EN", (int) texts.size(), leftGerman, umlauts);

        if (umlauts > 0)
            failed = true;

        if (leftGerman > 0)
            failed = true;
    }

    std::printf (failed ? "FEHLGESCHLAGEN\n" : "OK\n");
    return failed ? 1 : 0;
}
