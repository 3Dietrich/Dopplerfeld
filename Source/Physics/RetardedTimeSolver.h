#pragma once

#include "Medium.h"
#include "SourceTrajectory.h"
#include "Vec3.h"

// Eine gefundene Wurzel der Retarded-Time-Gleichung (Plan 2.10). Jede Wurzel
// ist ein eigener Hörweg: bei Unterschall gibt es genau eine, im Mach-Kegel
// kommt ein Paar dazu.
struct Root
{
    double t_e        = 0.0;  // Emissionszeit [s]
    double R          = 0.0;  // Abstand |L(t_h) - M(t_e)| [m]
    double machRadial = 0.0;  // M_r = û·v_M / c, positiv bei Annäherung
    int    id         = -1;   // stabile Zweig-Identität über Aufrufe hinweg
};

// Löst F(t_e) = c*(t_h - t_e) - |L(t_h) - M(t_e)| = 0 (Plan 2.3).
//
// Reines C++, keine JUCE-Abhängigkeit (Plan 3.4), damit der physikalisch
// kritischste Teil offline gegen die geschlossene Lösung aus Plan 2.5 geprüft
// werden kann (Tests/solver_check.cpp).
//
// Der Löser trägt Zustand: eine kleine Liste bekannter Zweige mit ihrer
// letzten Wurzel. Daraus kommen zwei Dinge - ein guter Startwert für den
// nächsten Solver-Punkt (billig statt Vollscan, Plan 2.10 Schritt 2) und
// stabile ids, an denen die Envelope-Logik in H5 erkennt, ob ein Zweig
// derselbe geblieben, neu erschienen oder verschwunden ist.
//
// Eine Instanz gehört zu genau einem Empfangspunkt (Ohr, Spiegelquelle), weil
// der Zweigzustand an dessen Geschichte hängt.
class RetardedTimeSolver
{
public:
    // K aus Plan 2.6: mehr als vier gleichzeitige Zweige werden verworfen.
    static constexpr int maxBranches = 4;

    // Vergisst alle bekannten Zweige. Nach einem Positionssprung oder einem
    // prepareToPlay aufzurufen, damit keine Wurzel aus der alten Geometrie
    // nachgeführt wird.
    void reset();

    // Rückgabe: Anzahl der nach outRoots geschriebenen Wurzeln, aufsteigend
    // nach t_e sortiert (älteste Emission zuerst). Bei Überlauf werden die
    // ältesten verworfen, weil die jüngeren Emissionen den aktuellen Klang
    // tragen.
    int solve (const SourceTrajectory& traj,
               const MediumState& medium,
               Vec3 receiverPos,
               double t_h,
               Root* outRoots,
               int maxRoots);

    // Untergrenze der Scan-Schrittweite (Plan 2.10: "minStep ist die
    // Trajektorien-Rasterweite, damit der Scan terminiert"). Der Löser kennt
    // das Raster der Trajektorie nicht, deshalb hier einstellbar; der Default
    // entspricht der 1-kHz-Rasterrate aus Plan 2.12.
    void   setMinScanStep (double seconds);
    double minScanStep() const { return minStep; }

    // Zähler für verworfene Wurzeln über K hinaus (Plan 2.6: "Ein Überlauf
    // wird verworfen und im Debug-Build gezählt").
    int  droppedRoots() const { return droppedRootCount; }
    void clearDroppedRoots() { droppedRootCount = 0; }

private:
    // Höchstzahl gleichzeitig gesammelter Vorzeichenwechsel. Bewusst größer
    // als maxBranches, damit der Scan erst zählt und dann verwirft, statt
    // mitten im Fenster abzubrechen.
    static constexpr int scanCapacity = 8;

    struct Branch
    {
        int    id         = -1;
        double t_e        = 0.0;
        double machRadial = 0.0;
        double R          = 0.0;
        double lastT_h    = 0.0;
    };

    Branch branches[maxBranches];
    int    branchCount      = 0;
    int    nextId           = 0;
    int    droppedRootCount = 0;

    double minStep = 1.0e-3;   // 1 kHz Trajektorienraster
};
