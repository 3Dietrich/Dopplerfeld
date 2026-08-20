#pragma once

#include "Medium.h"
#include "PathTransform.h"
#include "RetardedTimeSolver.h"
#include "SourceSignalBuffer.h"
#include "SourceTrajectory.h"
#include "Vec3.h"

#include <atomic>
#include <algorithm>
#include <cstdint>

namespace pathdetail
{

// std::atomic ist weder kopier- noch verschiebbar, PropagationPath liegt aber
// laut Plan 3.6 in einem std::vector. Dieser Wrapper verhält sich beim
// Kopieren/Verschieben wie ein gewöhnlicher Wert und macht damit die implizit
// erzeugten Konstruktoren von PropagationPath wieder benutzbar - ohne dass die
// Anzeigewerte (Audiothread schreibt, Message-Thread liest) zu nackten
// double-Membern mit formalem Datenrennen werden müssten.
template <typename T>
class DisplayValue
{
public:
    DisplayValue() = default;
    DisplayValue (const DisplayValue& o) { store (o.load()); }
    DisplayValue& operator= (const DisplayValue& o) { store (o.load()); return *this; }

    T    load() const { return v.load (std::memory_order_relaxed); }
    void store (T x)  { v.store (x, std::memory_order_relaxed); }

private:
    std::atomic<T> v { T{} };
};

} // namespace pathdetail

// Ein Ausbreitungsweg von der Quelle zu genau einem Empfangspunkt (Plan 3.4).
// Beliebig oft instanziierbar: zwei Ohren in Phase 1, dazu später Boden- und
// Wandspiegelungen als weitere Instanzen mit anderem PathTransform.
//
// Der Pfad besitzt weder Trajektorie noch Signal - beide kommen als const-Ref
// herein. Das ist die Bedingung dafür, dass sich Instanzen billig
// vervielfachen lassen (12 Leser auf einem Datensatz, Plan 2.12).
//
// Eigener Zustand pro Instanz:
//   - ein RetardedTimeSolver (dessen Zweigzustand hängt an der Geschichte
//     genau dieses Empfangspunkts, siehe RetardedTimeSolver.h)
//   - pro Zweig ein Luftdämpfungsfilter und ein Envelope. Beide gehören zum
//     Zweig, nicht zum Pfad (Plan 2.9): ein neu erscheinender Zweig fängt bei
//     Null an, statt den Zustand eines fremden Zweigs zu erben.
//
// JUCE-frei wie der Rest der Physics-Schicht, damit der physikalisch heikle
// Teil (Fokussierungsfaktor, Hermite-Ableitungen) offline prüfbar bleibt.
class PropagationPath
{
public:
    // Ein Slot pro möglichem Wurzelzweig - mehr Zweige als der Löser liefern
    // kann, gibt es nicht (K = 4 aus Plan 2.6).
    static constexpr int maxBranchSlots = RetardedTimeSolver::maxBranches;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setTransform (const PathTransform& t) { transform = t; }
    const PathTransform& getTransform() const  { return transform; }

    // Boom-Limit als Regularisierung gegen die Divergenz bei M_r -> 1
    // (Plan 2.7): eps = 10^(-L_dB/20), Default L_dB = 30 -> eps ~ 0,0316.
    void   setBoomLimitDb (double dB);
    double boomLimitDb() const { return boomDb; }

    // Entfernungsabhängigkeit der Amplitude (@dpa-Skizze "Amp-Verlauf": drei
    // Kurven über Distanz gegen Amplitude, die mittlere neutral). Statt starr
    // A_geo = 1/R rechnet der Pfad 1/R^k mit einstellbarem k.
    //
    // curve = 0 ergibt genau k = 1, also das physikalisch richtige
    // Kugelwellen-Gesetz und damit exakt das bisherige Verhalten. Positive
    // Werte machen k größer (der Pegel fällt schneller mit der Entfernung ab),
    // negative kleiner (er fällt flacher ab und trägt weiter).
    //
    // k bleibt hart über null: bei k = 0 wäre die Amplitude von der Entfernung
    // völlig unabhängig, ein Vorbeiflug hätte dann keine Ferne mehr.
    void   setDistanceCurve (double curve);
    double distanceExponentValue() const { return distanceExponent; }

    // Anteil der Luftdämpfung, 0 = aus (Filter durchgereicht), 1 = voll
    // (Plan 3.11, Parameter airAbsorbAmount). Skaliert den One-Pole-Koeffi-
    // zienten in Richtung Bypass, damit "aus" wirklich aus ist und nicht bloß
    // der 18-kHz-Fall.
    void setAirAbsorptionAmount (double amount01);

    // fc(R) = clamp(fc0*(R_ref/R)^k, 200, 18000) - Defaults aus Plan 2.9.
    void setAirAbsorption (double fc0Hz, double refMetres, double exponent);

    // Zusätzliche Höhendämpfung für gespiegelte Pfade (Boden, später Wände).
    // Eigener Grad neben der Luftdämpfung, weil beide Verschiedenes
    // beschreiben: die Luftdämpfung modelliert die geflogene STRECKE und
    // wächst deshalb mit R, die Reflexionsdämpfung modelliert die FLÄCHE und
    // fällt einmal pro Reflexion an - unabhängig davon, wie weit der Schall
    // danach noch fliegt. Boden schluckt Höhen deutlich stärker als Luft.
    //
    // amount01 blendet wie airAbsorptionAmount gegen Bypass: 0 heißt wirklich
    // aus (ideal harte Fläche), nicht bloß "der milde Fall". fcHz ist die
    // Eckfrequenz bei voller Stärke; sie bleibt eine Modellkonstante des
    // Aufrufers, damit hier kein fester Wert im Weg steht, falls später
    // Wandmaterialien dazukommen.
    void setReflectionDamping (double amount01, double fcHz);

    // Solver-Rate (Plan 2.11). Bei erkanntem Überschall im Fenster wird
    // automatisch auf supersonic umgeschaltet, weil M_r dort schnell wechselt.
    void setSolverStride (int normalStride, int supersonicStride);

    // Der Löser braucht die Rasterweite der Trajektorie als Untergrenze für
    // seine Scan-Schrittweite (siehe RetardedTimeSolver::setMinScanStep). Der
    // Pfad kennt die Trajektorie nur als const-Ref und fragt sie nicht danach,
    // deshalb reicht der Besitzer der Puffer (DopplerEngine) sie hier durch.
    void setTrajectoryGridSeconds (double seconds);

    // Zeitlicher Mindestabstand zweier Vollscans des Lösers (Suche nach NEU
    // entstandenen Zweigen). Bekannte Zweige werden davon unberührt an jedem
    // Solver-Punkt nachgeführt - es geht ausschließlich darum, wie schnell
    // eine Kegelankunft bemerkt wird.
    //
    // Der Grund für die Trennung: im Überschall läuft der Löser auf Stride 8,
    // also alle 167 us bei 48 kHz, und jeder dieser Aufrufe scannte bisher das
    // komplette Suchfenster neu ab, obwohl in 167 us fast nie ein Zweig
    // hinzukommt. Der Scan ist dabei der mit Abstand teuerste Posten des
    // ganzen Plugins.
    void setDiscoveryIntervalSeconds (double seconds);


    // Anti-Klick-Rampe beim Erscheinen/Verschwinden eines Zweigs (Plan 3.7,
    // letzter Absatz): 0,5 bis 2 ms. Das ist ausdrücklich kein globaler
    // Crossfade - der würde den Knall wegmitteln, den man hören will.
    void setBranchRampSeconds (double seconds);

    // Druckwellen-/N-Wellen-Schicht für den Überschallknall.
    //
    // Ausdrücklich ADDITIV und getrennt vom bestehenden Amplituden-Mechanismus:
    // die Formel A = 1/(R·sqrt((1-M_r)²+eps²)) bleibt unangetastet, die N-Welle
    // kommt oben drauf. Sie ist damit auch klar getrennt von "Boom Limit"
    // (reine Amplitudendeckelung über eps, keine Pulsform) und vom
    // Master-Softclip.
    //
    // Ausgelöst wird sie an der GEBURT eines Zweigpaars (zwei neue Zweige im
    // selben Solver-Segment, beide nahe M_r = 1 - die Kegelankunft) sowie
    // zusätzlich, wenn der M_r eines bereits bestehenden Zweigs die 1
    // durchquert (z.B. Beschleunigen durch Mach 1 bei laufendem Zweig). Siehe
    // ausführliche Begründung an den beiden Auslösestellen in process().
    //
    // sizeMetres ist die Ausdehnung des Körpers und bestimmt sowohl die
    // Pulsdauer (größer = tiefer und länger, kleiner = kürzer und knackiger)
    // als auch die Amplitude (größer = lauter, siehe triggerNWave()).
    void setNWave (bool shouldBeEnabled, double sizeMetres, double gainLinear);

    // Phase 2 (Plan 2.7 / Abschnitt 7). In Phase 1 ohne Wirkung, damit später
    // kein Aufrufer geändert werden muss.
    // Klassisches Pegel-Panning zusaetzlich zur Ohrgeometrie (@dpa 20260819:
    // "bitte noch ein normales Panning fuer die Kopfdrehung anbieten, also den
    // Anteil des normalen pannings von 0 - 100%").
    //
    // Die Ohrgeometrie allein verschiebt das Stereobild bei einer Kopfdrehung
    // fast nur ueber die Laufzeit. Der Pegelunterschied zwischen den beiden
    // Ohren ist bei einer weit entfernten Quelle winzig - der Kopf dreht sich,
    // im Stereobild passiert wenig. Das hier legt den Pegelunterschied darueber,
    // den ein gewoehnlicher Panorama-Regler machen wuerde.
    //
    // Gerechnet wird mit der Richtung, aus der der Schall TATSAECHLICH kommt,
    // also von der retardierten Quellposition zum Ohr. Nur so eilt das
    // Stereobild dem Klang nicht voraus: bei 400 m/s liegt zwischen der Stelle,
    // an der die Quelle gerade ist, und der, aus der man sie hoert, ein
    // erheblicher Winkel. Jede Spiegelung bekommt darueber automatisch ihre
    // eigene Richtung - eine Wandreflexion von links kommt von links.
    //
    // amount 0 laesst alles wie zuvor, 1 ist volles Panorama. right ist die
    // Rechts-Achse des Kopfes in Weltkoordinaten (siehe Listener.h), rightEar
    // waehlt das Ohr. Nach setTransform() aufrufen.
    void setPanning (double amount, Vec3 right, bool rightEar);

private:
    // Pegelfaktor dieses Ohres fuer Schall, der aus Richtung incoming kommt
    // (Einheitsvektor, vom Ohr zur Quelle). Siehe setPanning().
    double panoramaGain (Vec3 incoming) const;

public:

    void setNearFieldEnabled (bool shouldBeEnabled) { nearFieldOn = shouldBeEnabled; }
    void setDominantFrequencyHz (double hz) { dominantFreqHz = hz; }

    // Der einzige Rechenaufruf. Liest aus den geteilten Puffern, schreibt
    // additiv auf out (mehrere gleichzeitig aktive Zweige summieren sich - so
    // entsteht der Überschall-Doppelschlag ohne Sondercode, Plan 2.8).
    //
    // receiverPos gilt zu blockStartTime, receiverVel für den ganzen Block:
    // daraus wird die Ohrposition an jedem Solver-Punkt linear extrapoliert,
    // wie Plan 3.5 es verlangt ("pro Solver-Punkt neu ausgewertet, nicht pro
    // Block").
    void process (const SourceTrajectory&   traj,
                  const SourceSignalBuffer& sig,
                  const MediumState&        medium,
                  Vec3   receiverPos,
                  Vec3   receiverVel,
                  double blockStartTime,
                  float* out,
                  int    numSamples);

    // Nur für die Anzeige (Plan 3.12), lock-frei gelesen.
    int    numActiveBranches() const { return dispBranches.load(); }
    double lastDelaySeconds() const  { return dispDelay.load(); }
    double lastMachRadial() const    { return dispMach.load(); }

    // Messung zur Frage "mit welchem Hüllkurvenwert stirbt ein Zweig?"
    // (@dpa 20260819, Abbruch am Ende der Überschall-Hälfte).
    //
    // Ein Zweig, den der Löser nicht mehr meldet, wird über rampSeconds auf
    // null gefahren - unabhängig davon, wie laut er in diesem Moment war. Ist
    // die Rampe die Ursache des Abbruchs, muss env beim Übergang von "gemeldet"
    // auf "nicht mehr gemeldet" nahe 1 liegen; wäre der Zweig ohnehin schon
    // ausgeklungen, läge er nahe 0.
    //
    // Gezählt wird der Übergang selbst, nicht das spätere Freigeben des Slots:
    // env ist dort noch der Wert VOR der Abwärtsrampe.
    struct BranchDeathStats
    {
        std::uint64_t deaths     = 0;   // Übergänge gemeldet -> nicht mehr gemeldet
        std::uint64_t loudDeaths = 0;   // davon mit env >= 0,5
        double        envSum     = 0.0; // Summe der env-Werte, für den Mittelwert
        double        envMax     = 0.0;

        // Wie oft ein neu ankommender Zweig einen noch ausklingenden verdrängt
        // hat, weil kein Steckplatz frei war (siehe freeSlot()). Der Ausklang
        // hält einen Platz länger besetzt als die alte 1-ms-Rampe, deshalb ist
        // das die Zahl, an der man sieht, ob der Ausklang sich selbst im Weg
        // steht. Bleibt sie klein, ist der Fall theoretisch.
        std::uint64_t evictions = 0;

        // Wie viele der Tode ueberhaupt den Kaustik-Ausklang bekommen haben
        // (deathTau > 0), und wie lang der dann war. Ohne diese zwei Zahlen
        // laesst sich nicht unterscheiden, ob der Ausklang wirkt oder ob er
        // rechnerisch existiert und praktisch immer auf rampSeconds faellt.
        std::uint64_t causticDeaths = 0;
        double        tauSum        = 0.0;
        double        tauMax        = 0.0;

        // Die Pruefgroesse: Tode mit env >= 0,5, die schneller als
        // abruptSeconds auf null gegangen sind. Das ist der Abbruch, gezaehlt
        // statt gehoert. Null davon ist das Ziel.
        std::uint64_t abruptDeaths = 0;

        // Todesursachen aus dem Loeser, siehe RetardedTimeSolver.
        std::uint64_t trackLost   = 0;
        std::uint64_t newIds      = 0;
        std::uint64_t newIdsNear  = 0;
        std::uint64_t orderMatches = 0;
        std::uint64_t rootHist[8] {};
        std::uint64_t countFlips    = 0;
        std::uint64_t collapsed     = 0;
        std::uint64_t handovers     = 0;
        std::uint64_t tightPairs    = 0;
        std::uint64_t adjacentPairs = 0;
        std::uint64_t droppedRoots = 0;
    };

    BranchDeathStats branchDeaths() const
    {
        BranchDeathStats s;
        s.deaths     = deathCount.load();
        s.loudDeaths = deathLoudCount.load();
        s.envSum     = deathEnvSum.load();
        s.envMax     = deathEnvMax.load();
        s.evictions     = evictionCount.load();
        s.causticDeaths = causticCount.load();
        s.tauSum        = deathTauSum.load();
        s.tauMax        = deathTauMax.load();
        s.abruptDeaths  = abruptCount.load();
        s.trackLost     = solver.trackLostCount();
        s.newIds        = solver.newIdCount();
        s.newIdsNear    = solver.newIdNearCount();
        s.orderMatches  = solver.orderMatchCount();

        for (int i = 0; i < 8; ++i)
            s.rootHist[i] = solver.rootCountBucket (i);

        s.countFlips    = solver.rootCountFlips();
        s.collapsed     = solver.collapsedTrackCount();
        s.handovers     = handoverCount.load();
        s.tightPairs    = solver.tightPairCount();
        s.adjacentPairs = solver.adjacentPairCount();
        s.droppedRoots  = (std::uint64_t) std::max (0, solver.droppedRoots());
        return s;
    }

    int maxBlockSize() const { return maxBlockSamples; }

    // Lastmaß des Lösers dieses Pfades (siehe
    // RetardedTimeSolver::residualEvaluations). Nur für Messungen.
    std::uint64_t solverEvaluations() const { return solver.residualEvaluations(); }

private:
    // Zustand eines Wurzelzweigs am zuletzt gerechneten Solver-Punkt.
    struct Branch
    {
        int    id   = -1;
        bool   used = false;

        double tau  = 0.0;   // τ = t_h − t_e [s]
        double dTau = 0.0;   // dτ/dt_h, analytisch (Plan 2.11)
        double amp  = 0.0;   // A = 1/(R·sqrt((1−M_r)² + eps²))
        double R    = 0.0;
        double mach = 0.0;

        double lpCoeff = 1.0;   // Luftdämpfung, pro Solver-Punkt aktualisiert
        double lpZ     = 0.0;   // Filterzustand - gehört zum Zweig (Plan 2.9)
        double refZ    = 0.0;   // Reflexionsdämpfung, ebenfalls je Zweig
        double env     = 0.0;   // Anti-Klick-Rampe, 0..1

        // Ob der Löser diesen Zweig im VORIGEN Solver-Segment gemeldet hat.
        // Daran hängt die Flanke "gemeldet -> nicht mehr gemeldet": sie zählt
        // die Todesmessung (siehe branchDeaths()) und legt gleichzeitig die
        // Länge des Ausklangs fest (siehe deathTau).
        bool wasAlive = false;

        // |dM_r/dt| aus den letzten beiden Solver-Punkten, in 1/s. Das ist die
        // Geschwindigkeit, mit der dieser Hörweg durch die Kaustik läuft, und
        // damit das Maß für die Breite des Übergangs - siehe deathTau.
        double machRate = 0.0;

        // Zeitkonstante des Ausklangs, in Sekunden, beim Tod einmal aus
        // machRate berechnet und danach fest. Siehe maxDeathTailSeconds.
        double deathTau = 0.0;

        // env im Moment des Todes und die seither vergangenen Samples. Daraus
        // entsteht die eigentliche Pruefgroesse: ein LAUTER Zweig, der in
        // wenigen Millisekunden auf null geht, ist genau der Abbruch. Siehe
        // abruptDeaths in BranchDeathStats.
        double deathEnvValue   = 0.0;
        int    deathSampleCount = 0;

        // Verzögerung im Moment des Todes. NICHT b.tau nehmen: das läuft
        // während des Ausklangs mit der zuletzt bekannten Steigung weiter, und
        // die ist an der Kaustik so steil, dass der Wert schon nach einem
        // Solver-Segment zig Millisekunden entfernt liegt. Der Vergleich für
        // die Zustandsübergabe braucht den Stand von damals.
        double deathTauValue   = 0.0;

        // --- N-Wellen-Schicht, siehe setNWave() ---
        //
        // machSeen wird beim ersten Solver-Punkt eines Zweigs gesetzt; ohne
        // einen gültigen Vorwert gibt es keine Durchquerung zu erkennen, und
        // ein frisch geborener Zweig würde sonst bei jeder Geburt auslösen.
        bool   machSeen  = false;
        double prevMach  = 0.0;

        // Laufzeit seit der Auslösung in Sekunden; negativ heißt "keine Welle".
        double nPhase    = -1.0;
        double nDuration = 0.0;
        double nRise     = 0.0;
        double nAmp      = 0.0;
    };

    // Wert der N-Welle zum Phasenzeitpunkt: steiler Anstieg auf +A, linearer
    // Abfall durch null, steiler Rücksprung von -A auf 0. Die klassische
    // N-Form, nicht bloß ein abklingender Impuls.
    static double nWaveAt (const Branch& b);

    // Zielzustand eines Zweigs am Ende des laufenden Solver-Segments.
    struct Target
    {
        bool   present = false;
        double tau     = 0.0;
        double dTau    = 0.0;
        double amp     = 0.0;
        double R       = 0.0;
        double mach    = 0.0;
        double lpCoeff = 1.0;
    };

    void   seedAt (const SourceTrajectory& traj, const MediumState& medium,
                   Vec3 recvPos, Vec3 recvVel, double t_h);
    void   evaluateRoot (const SourceTrajectory& traj, const Root& root,
                         Vec3 recvPos, Vec3 recvVel, double c, Target& out) const;
    int    findSlot (int id) const;
    int    freeSlot();

    // Setzt Dauer, Anstiegszeit, Amplitude und Phase des N-Wellen-Pulses auf
    // Zweig b und startet ihn (nPhase = 0). Gemeinsamer Code für beide
    // Auslöser (Paar-Geburt an der Kegelankunft und M_r-Durchgang eines
    // bereits bestehenden Zweigs, siehe process()) - beide sollen exakt
    // denselben Puls erzeugen, keine zwei leicht auseinanderlaufenden Formeln.
    void triggerNWave (Branch& b, double c) const;

    double lowpassCoeff (double R) const;

    // Phase-2-Vorbereitung (Plan 2.7). Liefert in Phase 1 konstant 0, der
    // Aufruf steht aber bereits an der richtigen Stelle.
    double nearFieldGain (double R, double dominantFrequencyHz) const;

    RetardedTimeSolver solver;
    PathTransform      transform;

    Branch branches[maxBranchSlots];

    double sr              = 0.0;
    int    maxBlockSamples = 0;

    double lastSolveTime     = 0.0;
    double lastDiscoveryTime = 0.0;
    bool   seeded            = false;

    // 0,5 ms. Obergrenze der Entdeckungslatenz für eine Kegelankunft und damit
    // eine Modellkonstante wie die Rampendauer, kein Regler. Die Größe ist an
    // der Toleranz gewählt, die solver_check für die Kegelankunft ansetzt
    // (3 ms) - mit reichlich Abstand darunter.
    double discoverySeconds = 0.5e-3;


    // Exponent von R in A_geo = 1/R^k, siehe setDistanceCurve().
    //
    // plainInverseR ist die Abkürzung für k = 1: dort wird std::pow gar nicht
    // gerufen, sondern R direkt benutzt. Nicht aus Sparsamkeit, sondern damit
    // der Standardfall garantiert bitgleich zum reinen 1/R bleibt, statt an der
    // Genauigkeit von pow(R, 1.0) zu hängen.
    double distanceExponent = 1.0;
    bool   plainInverseR    = true;

    // Exponent an den beiden Reglerenden. Unsymmetrisch um 1, weil "flacher"
    // sich schon bei kleiner Änderung deutlich hört, "schärfer" aber Luft nach
    // oben braucht.
    static constexpr double distanceExponentSteep = 2.5;
    static constexpr double distanceExponentFlat  = 0.3;

    // Regularisierung und Untergrenzen (Plan 2.7).
    double boomDb    = 30.0;
    double eps       = 0.0316227766016838;   // 10^(-30/20)
    double minRadius = 0.05;                 // R_min [m]

    // Luftdämpfung (Plan 2.9).
    double airFc0      = 18000.0;
    double airRefM     = 10.0;
    double airExponent = 0.7;
    double airAmount   = 1.0;

    // Reflexionsdämpfung. 0 = aus, dann wird der Filter gar nicht erst
    // durchlaufen (der Direktschall zahlt für dieses Bauteil also nichts).
    double reflectAmount = 0.0;
    double reflectFcHz   = 800.0;

    int baseStride       = 64;   // Plan 2.11: 64 Samples, 750 Hz bei 48 kHz
    int supersonicStride = 8;    // Plan 2.11: adaptiv feiner bei Überschall

    double trajGridSeconds = 1.0e-3;   // 1 kHz Rasterrate (Plan 2.12)
    double rampSeconds     = 1.0e-3;   // Mitte von 0,5..2 ms (Plan 3.7)

    // Ausklang eines Zweigs, den der Löser nicht mehr meldet.
    //
    // Der EINSATZ eines Zweigs bleibt die lineare Rampe aus Plan 3.7: eine
    // Kegelankunft ist eine echte Stoßfront, die darf steil sein. Sein ENDE
    // ist aber etwas anderes. Zwei Wurzeln laufen an der Mach-Front zusammen
    // und verschwinden dort - und zwar bei ihrer GRÖSSTEN Amplitude, weil der
    // Fokussierungsfaktor 1/sqrt((1-M_r)²+eps²) genau dort sein Maximum hat.
    // Sie mit derselben festen Rampe auf null zu fahren, schneidet den Klang
    // bei vollem Pegel ab; gemessen (Zweig-Tod-Zählwerk, load_check) stirbt ein
    // Zweig im Überschall mit env im Mittel 0,69 bis 0,95 und Maximum 1,000.
    // Das ist der von @dpa beschriebene Abbruch am Ende der Überschall-Hälfte.
    //
    // Physikalisch endet das Feld an einer Faltungskaustik nicht, es geht mit
    // einem Ausläufer in den Schattenbereich weiter. Dessen Breite ist keine
    // freie Wahl: sie hängt davon ab, wie schnell die Geometrie durch die
    // Kaustik läuft. Deshalb
    //
    //     tau = eps / |dM_r/dt|
    //
    // also die Zeit, die M_r braucht, um sich um genau eine
    // Regularisierungsbreite zu bewegen. eps ist dabei nicht neu erfunden,
    // sondern dasselbe eps, mit dem "Boom Limit" die Divergenz schon glättet -
    // der Ausläufer bekommt damit exakt die Breite, auf die das Modell den
    // Kaustik-Übergang ohnehin festgelegt hat. Ein Durchflug knapp durch Mach 1
    // ist damit von selbst kurz und knackig, ein langsames Hineingleiten von
    // selbst weich, ohne dass irgendwo eine Zeit eingestellt werden müsste.
    //
    // Nach unten begrenzt rampSeconds (kürzer als die Anti-Klick-Rampe darf der
    // Ausklang nie werden, sonst wäre er wieder ein Knacks). Nach oben begrenzt
    // maxDeathTailSeconds: bei dM_r/dt gegen null ginge tau gegen unendlich,
    // der Zweig bliebe für immer hörbar und würde einen Steckplatz belegen.
    // 100 ms ist grosszügig gewählt - das Zehn- bis Hundertfache dessen, was
    // die Messung als typischen Kaustik-Durchlauf zeigt - und steht hier
    // ausdrücklich sichtbar, statt als stiller Deckel im Code zu verschwinden.
    static constexpr double maxDeathTailSeconds = 0.1;

    // Wie viele Regularisierungsbreiten um M_r = 1 herum noch als "an der
    // Kaustik" gelten. Nur dort bekommt ein sterbender Zweig den Ausläufer;
    // ausserhalb ist der Fokussierungsfaktor unauffällig und die alte lineare
    // Anti-Klick-Rampe bleibt, wie sie war (siehe Auswertung im .cpp).
    //
    // Ohne diese Eingrenzung bekamen auch Tode einen langen Ausklang, die gar
    // nichts mit der Mach-Front zu tun haben (verlorene Nachführung) - gemessen
    // an den Verdraengungen im load_check hielten die dann Steckplätze besetzt,
    // bis ein neu ankommender Zweig sie hart hinauswarf. Das hätte den
    // abgeschnittenen Zweig nur an eine andere Stelle verschoben.
    static constexpr double causticWidths = 4.0;

    // Ausklang fuer einen Zweig, der NICHT an der Kaustik stirbt, sondern weil
    // der Loeser seine Wurzel verloren hat.
    //
    // Schall hoert nicht abrupt auf. Verschwindet ein Zweig bei voller
    // Huellkurve, ist die naheliegende Erklaerung deshalb nicht "die Quelle ist
    // verstummt", sondern "wir haben sie aus den Augen verloren" - ein
    // Loeserereignis, kein akustisches. Ihn dann in einer Millisekunde
    // wegzublenden loescht echtes Signal: gemessen fiel der Pegel nach einem
    // Ueberschall-Vorbeiflug binnen einer halben Sekunde um 15 bis 20 dB,
    // waehrend die groessere Entfernung in derselben Zeit nur 2,4 dB erklaert
    // (@dpa 20260820: "warum ist das Rückwärts noch laut und danach ist
    // ploetzlich stille.. das kann doch nicht wahr sein!").
    //
    // Der Zweig laeuft dabei mit seiner zuletzt bekannten Steigung weiter, das
    // ist eine Extrapolation und wird mit der Zeit ungenauer - deshalb kurz
    // genug, dass daraus kein Hall wird, und nur fuer Zweige, die laut genug
    // sterben, dass ihr Fehlen als Loch auffaellt.
    static constexpr double lostBranchTailSeconds = 0.08;
    static constexpr double lostBranchMinEnv      = 0.05;

    // Unterhalb dieses Hüllkurvenwerts gilt ein sterbender Zweig als fertig und
    // sein Steckplatz wird frei. Nötig, weil ein exponentieller Ausklang die
    // Null nie erreicht. -80 dB liegt unter allem, was neben dem Direktschall
    // noch hörbar wäre.
    static constexpr double envFloor = 1.0e-4;

    // Ab wann ein Ausklang als "schlagartig" gilt. 2 ms deshalb, weil @dpas
    // Aufnahme den Abbruch mit ueber 20 dB in 0,75 ms zeigt und die alte feste
    // Rampe 1 ms lang war - beides liegt klar darunter, ein Ausklang, der der
    // Kaustik folgt, klar darueber.
    static constexpr double abruptSeconds = 2.0e-3;

    // Wie nah zwei Verzögerungen liegen müssen, damit ein neu gemeldeter Zweig
    // als Fortsetzung eines sterbenden gilt und dessen Zustand übernimmt.
    //
    // 2 ms sind bei 343 m/s rund 70 cm Wegunterschied. Zwei wirklich
    // verschiedene Hörwege liegen im Feldmassstab weiter auseinander; liegen
    // sie enger, sind sie ohnehin im Begriff zu verschmelzen und ein
    // Zustandsübergang zwischen ihnen ist unhörbar.
    static constexpr double handoverTauSeconds = 2.0e-3;

    // Wie lange ein gestorbener Zweig als Fortsetzungskandidat gilt. Danach ist
    // der Klang ohnehin abgeklungen und eine Übergabe würde einen alten
    // Filterzustand in einen neuen Hörweg tragen.
    static constexpr double handoverMaxAgeSeconds = 20.0e-3;

    // Panorama-Anteil samt Kopfachse und Ohr, siehe setPanning().
    double panAmount   = 0.0;
    Vec3   panRight    { 1.0, 0.0, 0.0 };
    bool   panRightEar = false;

    bool   nearFieldOn     = false;
    double dominantFreqHz  = 0.0;

    // N-Wellen-Schicht. Default aus - eine Druckwelle, die immer mitläuft,
    // wollen die wenigsten.
    bool   nWaveOn        = false;
    double nWaveSizeM     = 15.0;

    // Regelbarer Pegel des Knalls, linear (siehe Params::nWaveGainDb).
    double nWaveGain      = 1.0;

    // Spitzendruck der N-Welle in einem Meter Abstand. Modellkonstante, kein
    // Regler: die Regler sind An/Aus und Größe. Der Wert ist so gewählt, dass
    // die Welle in typischer Vorbeiflug-Entfernung in derselben Größenordnung
    // liegt wie der Direktschall - beurteilen muss ihn @dpas Ohr.
    // Pegel der Druckwelle bei nWaveRefMetres. Gross, weil ein Ueberschallknall
    // die Szene beherrscht statt sich einzureihen: bei 8.0 kam auf 500 m eine
    // Amplitude von 0,036 heraus, also 8 dB UNTER dem Motorgeraeusch derselben
    // Szene (gemessene Signalspitze -21 dB) - @dpa hoerte dort folgerichtig
    // "ein bisschen ziusch.. aber nichts was an einen Schlag oder Druck oder
    // gar nur Lautheit erinnert". Ein realer Knall liegt Zehnerpotenzen darueber.
    //
    // Mit 40.0 sind es auf 500 m rund 0,18, also etwa 6 dB ueber der Szene.
    // In der Naehe uebersteuert das und laeuft in den Limiter - das ist kein
    // Versehen: ein Knall aus 20 m IST ohrenbetaeubend, und der Limiter ist
    // sichtbar und abschaltbar, statt die Welle vorher heimlich klein zu halten.
    static constexpr double nWaveLevel = 40.0;

    // Abstandsgesetz der N-Welle, siehe ausführliche Begründung an der
    // Verwendungsstelle. Der Exponent 3/4 ist der Standardwert für eine
    // nichtlinear alternde N-Welle (gegenüber 1 für gewöhnlichen Kugelschall);
    // die Bezugsentfernung hält den bisher eingehörten Nahbereich fest.
    static constexpr double nWaveDistanceExponent = 0.75;
    static constexpr double nWaveRefMetres        = 20.0;

    // Größenkopplung der Lautstärke (@dpa: "die N-Welle ist das Druckabbild
    // des Körpers ... Größerer Körper = lauterer Knall"). Ausführliche
    // Begründung an der Verwendungsstelle in triggerNWave().
    //
    // nWaveSizeRefMetres ist bewusst dieselbe Zahl wie der Skew-Mittelpunkt
    // und Default des "N-Wave Size"-Reglers (Params.cpp, 15 m): dort ist der
    // Kopplungsfaktor exakt 1 und der bisher eingehörte Klang bleibt bei
    // mittlerer Reglerstellung unverändert - kein Presets-Sprung.
    static constexpr double nWaveSizeRefMetres = 15.0;
    static constexpr double nWaveSizeExponent  = 0.75;

    pathdetail::DisplayValue<int>    dispBranches;
    pathdetail::DisplayValue<double> dispDelay;
    pathdetail::DisplayValue<double> dispMach;

    // Todesmessung, siehe branchDeaths(). Audiothread schreibt, Message-Thread
    // liest - dieselbe Bauart wie die Anzeigewerte darüber, damit
    // PropagationPath kopierbar bleibt (std::vector in DopplerEngine).
    pathdetail::DisplayValue<std::uint64_t> deathCount;
    pathdetail::DisplayValue<std::uint64_t> deathLoudCount;
    pathdetail::DisplayValue<double>        deathEnvSum;
    pathdetail::DisplayValue<double>        deathEnvMax;
    pathdetail::DisplayValue<std::uint64_t> evictionCount;
    pathdetail::DisplayValue<std::uint64_t> causticCount;
    pathdetail::DisplayValue<double>        deathTauSum;
    pathdetail::DisplayValue<double>        deathTauMax;
    pathdetail::DisplayValue<std::uint64_t> abruptCount;
    pathdetail::DisplayValue<std::uint64_t> handoverCount;
};
