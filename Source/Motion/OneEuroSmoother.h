#pragma once

#include "MotionSmoother.h"

// Adaptiver 1€-Filter (Plan 3.8), nach Casiez/Roussel/Vogel, "1€ Filter: A
// Simple Speed-based Low-pass Filter for Noisy Input in Interactive
// Systems" (CHI 2012). Bei langsamer Bewegung stark glättend (gegen
// Zittern), bei schneller Bewegung wenig verzögernd (gegen spürbaren Lag).
// Der Cutoff des Positions-Tiefpasses wächst dafür mit dem Betrag der
// (selbst tiefpassgefilterten) Geschwindigkeitsschätzung:
// cutoff = minCutoff + beta·|dx/dt|.
//
// Vektor-Verallgemeinerung: EIN gemeinsamer, aus |dx/dt| abgeleiteter
// Cutoff für alle drei Achsen, nicht drei unabhängige Filter mit je eigenem
// Cutoff - sonst würde eine reine x-Bewegung die y/z-Filter anders
// verzögern als es derselben physischen Bewegung entspricht.
class OneEuroSmoother : public MotionSmoother
{
public:
    OneEuroSmoother (double minCutoffHzIn = 1.0, double betaIn = 0.5, double derivCutoffHzIn = 1.0);

    void setMinCutoffHz (double hz);
    void setBeta (double b);
    void setDerivativeCutoffHz (double hz);

    void prepare (double tickRateHz) override;
    void reset (Vec3 pos) override;
    void setTarget (Vec3 pos) override;
    void tick (Vec3& outPos, Vec3& outVel) override;

    // 1/cutoff des aktuell wirksamen (adaptiven) Positionsfilters - anders
    // als bei den ersten drei Implementierungen keine feste Konstante,
    // sondern eine Momentaufnahme, die mit der Bewegungsgeschwindigkeit
    // schwankt.
    double naturalTauSeconds() const override { return 1.0 / currentCutoffHz; }

private:
    static double alphaFor (double cutoffHz, double dtIn);

    double minCutoffHz   = 1.0;
    double beta          = 0.5;
    double derivCutoffHz = 1.0;
    double dt = 0.0;

    Vec3 target;

    // Zustand des Filters, direkt nach dem Paper benannt: xPrev ist der
    // zuletzt gesehene Rohwert (für die Ableitung), dxHat die gefilterte
    // Ableitung, xHat der gefilterte Ausgabewert.
    Vec3 xPrev;
    Vec3 dxHat;
    Vec3 xHat;

    double currentCutoffHz = 1.0;
};
