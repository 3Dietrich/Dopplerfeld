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

    const Vec3 accel = (target - pos) * (omega * omega) - vel * (2.0 * omega);

    // Semi-implizites (symplektisches) Euler statt explizites Euler: erst v
    // mit dem neuen a fortschreiben, dann p mit dem NEUEN v - bei größerem
    // ω·dt (kleines τ oder grobe Tickrate) bleibt das System damit stabil
    // kritisch gedämpft, wo explizites Euler (p und v beide mit den alten
    // Werten) anfangen würde aufzuschwingen.
    vel += accel * dt;
    pos += vel * dt;

    outPos = pos;
    outVel = vel;
}
