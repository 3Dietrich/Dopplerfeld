#pragma once

#include "Medium.h"
#include "PathTransform.h"
#include "RetardedTimeSolver.h"
#include "SourceSignalBuffer.h"
#include "SourceTrajectory.h"
#include "Vec3.h"

#include <atomic>

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

    // Phase 2 (Plan 2.7 / Abschnitt 7). In Phase 1 ohne Wirkung, damit später
    // kein Aufrufer geändert werden muss.
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
    };

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
    int    freeSlot() const;
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

    bool   nearFieldOn     = false;
    double dominantFreqHz  = 0.0;

    pathdetail::DisplayValue<int>    dispBranches;
    pathdetail::DisplayValue<double> dispDelay;
    pathdetail::DisplayValue<double> dispMach;
};
