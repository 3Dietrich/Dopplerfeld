#include "OnePoleSmoother.h"
#include <algorithm>
#include <cassert>
#include <cmath>

OnePoleSmoother::OnePoleSmoother (double tauSeconds)
{
    setTau (tauSeconds);
}

void OnePoleSmoother::setTau (double tauSeconds)
{
    // 0 würde über exp(-dt/0) formal noch gegen 0 laufen, aber lieber
    // explizit klemmen als sich auf das Grenzverhalten von exp() zu
    // verlassen (und eine Division durch 0 in setTau selbst zu riskieren,
    // falls die Formel je umgestellt wird).
    tau = std::max (1.0e-4, tauSeconds);
}

void OnePoleSmoother::prepare (double tickRateHz)
{
    dt = 1.0 / std::max (1.0, tickRateHz);
}

void OnePoleSmoother::reset (Vec3 p)
{
    pos = p;
    target = p;
}

void OnePoleSmoother::setTarget (Vec3 p)
{
    target = p;
}

void OnePoleSmoother::tick (Vec3& outPos, Vec3& outVel)
{
    assert (dt > 0.0);   // prepare() muss vor dem ersten Tick gelaufen sein

    const Vec3 prev = pos;

    // p += (target - p) * (1 - exp(-dt/tau)) - die exakte Lösung der DGL
    // dp/dt = (target-p)/tau für konstantes target über das Intervall dt,
    // nicht nur die Euler-Näherung davon.
    const double alpha = 1.0 - std::exp (-dt / tau);
    pos += (target - pos) * alpha;

    // v NICHT als eigener Zustand geglättet (siehe Klassenkommentar in der
    // .h) - einfach die tatsächliche Positionsänderung dieses Ticks durch
    // dt. Das ist bewusst der Nachteil dieser einfachsten Variante.
    outVel = (pos - prev) * (1.0 / dt);
    outPos = pos;
}
