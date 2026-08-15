#include "CriticallyDampedSpring.h"
#include <algorithm>
#include <cassert>

CriticallyDampedSpring::CriticallyDampedSpring (double tauSeconds)
{
    setTau (tauSeconds);
}

void CriticallyDampedSpring::setTau (double tauSeconds)
{
    tau = std::max (1.0e-4, tauSeconds);
    omega = 1.0 / tau;
}

void CriticallyDampedSpring::prepare (double tickRateHz)
{
    dt = 1.0 / std::max (1.0, tickRateHz);
}

void CriticallyDampedSpring::reset (Vec3 p)
{
    pos = p;
    target = p;
    vel = {};
}

void CriticallyDampedSpring::setTarget (Vec3 p)
{
    target = p;
}

void CriticallyDampedSpring::tick (Vec3& outPos, Vec3& outVel)
{
    assert (dt > 0.0);   // prepare() muss vor dem ersten Tick gelaufen sein

    // Semi-implizites (symplektisches) Euler statt explizites Euler: erst v
    // mit dem neuen a fortschreiben, dann p mit dem NEUEN v - deutlich
    // stabiler als explizites Euler (dort beide mit den alten Werten), aber
    // NICHT unconditional stabil: bei ω·dt ≳ 1 kippt auch dieses Schema um
    // (Determinante der Iterationsmatrix verlässt den Einheitskreis). Genau
    // dieser Fall ist über Params::smootherTau real erreichbar (Minimum
    // 1 ms bei 1000-Hz-Trajektorienrate, Plan 3.11/2.10 => ω·dt = 1). Deshalb
    // pro Tick in so viele gleich große Teilschritte zerlegen, dass jeder
    // Teilschritt komfortabel im stabilen Bereich bleibt (Grenze empirisch
    // bei ω·dt_sub = 0,5) - für den Normalfall (kleines ω·dt) bleibt es bei
    // genau einem Schritt, also unverändertes Verhalten.
    const double omegaDt = omega * dt;
    const int steps = std::max (1, (int) std::ceil (omegaDt / 0.5));
    const double subDt = dt / (double) steps;

    for (int i = 0; i < steps; ++i)
    {
        const Vec3 accel = (target - pos) * (omega * omega) - vel * (2.0 * omega);
        vel += accel * subDt;
        pos += vel * subDt;
    }

    outPos = pos;
    outVel = vel;
}
