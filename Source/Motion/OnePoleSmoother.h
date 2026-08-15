#pragma once

#include "MotionSmoother.h"

// Exponentieller Glätter erster Ordnung (Plan 3.8). Einfachste Variante,
// kein Überschwingen möglich - aber v wird NICHT als eigener Zustand
// geglättet, sondern jeden Tick direkt aus der Positionsänderung
// abgeleitet. Sobald sich das Ziel ändert, ändert sich damit auch die
// Steigung von p(t) an der Stelle "jetzt" sprunghaft (der Pfad ist C0-,
// aber nicht C1-stetig). Da der Doppler direkt an v hängt, ist das hörbar -
// deshalb nicht der Plan-Default, siehe CriticallyDampedSpring dafür.
class OnePoleSmoother : public MotionSmoother
{
public:
    explicit OnePoleSmoother (double tauSeconds = 0.1);

    void setTau (double tauSeconds);

    void prepare (double tickRateHz) override;
    void reset (Vec3 pos) override;
    void setTarget (Vec3 pos) override;
    void tick (Vec3& outPos, Vec3& outVel) override;
    double naturalTauSeconds() const override { return tau; }

private:
    double tau = 0.1;
    double dt  = 0.0;

    Vec3 target;
    Vec3 pos;
};
