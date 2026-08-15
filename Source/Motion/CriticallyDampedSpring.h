#pragma once

#include "MotionSmoother.h"

// Kritisch gedämpfte Feder zweiter Ordnung (Plan 3.8) - der Plan-Default.
// a = ω²(target - p) - 2ω·v, mit ω = 1/τ (ζ=1, die schnellste Annäherung
// ohne Überschwingen). Anders als OnePoleSmoother ist v hier ein echter
// Integrationszustand statt aus einer Positionsdifferenz nachträglich
// rekonstruiert zu werden - Position UND Geschwindigkeit bleiben damit
// stetig. Ein nur C0-stetiger Pfad würde einen Tonhöhensprung erzeugen,
// deshalb ist das der Default.
class CriticallyDampedSpring : public MotionSmoother
{
public:
    explicit CriticallyDampedSpring (double tauSeconds = 0.1);

    void setTau (double tauSeconds);

    void prepare (double tickRateHz) override;
    void reset (Vec3 pos) override;
    void setTarget (Vec3 pos) override;
    void tick (Vec3& outPos, Vec3& outVel) override;
    double naturalTauSeconds() const override { return tau; }

private:
    double tau   = 0.1;
    double omega = 10.0;   // 1/tau, gecacht statt in tick() jedes Mal neu geteilt
    double dt    = 0.0;

    Vec3 target;
    Vec3 pos;
    Vec3 vel;
};
