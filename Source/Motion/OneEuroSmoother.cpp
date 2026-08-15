#include "OneEuroSmoother.h"
#include <algorithm>
#include <cassert>

namespace
{
    // Lokal statt <numbers> oder JUCE-Konstante, damit diese Datei komplett
    // JUCE-frei bleibt (siehe Klassenkommentar in MotionSmoother.h).
    constexpr double kTwoPi = 6.283185307179586;
}

OneEuroSmoother::OneEuroSmoother (double minCutoffHzIn, double betaIn, double derivCutoffHzIn)
{
    setMinCutoffHz (minCutoffHzIn);
    setBeta (betaIn);
    setDerivativeCutoffHz (derivCutoffHzIn);
    currentCutoffHz = minCutoffHz;
}

void OneEuroSmoother::setMinCutoffHz (double hz)         { minCutoffHz = std::max (1.0e-4, hz); }
void OneEuroSmoother::setBeta (double b)                 { beta = std::max (0.0, b); }
void OneEuroSmoother::setDerivativeCutoffHz (double hz)  { derivCutoffHz = std::max (1.0e-4, hz); }

void OneEuroSmoother::prepare (double tickRateHz)
{
    dt = 1.0 / std::max (1.0, tickRateHz);
}

void OneEuroSmoother::reset (Vec3 p)
{
    target = p;
    xPrev  = p;
    xHat   = p;
    dxHat  = {};
    currentCutoffHz = minCutoffHz;
}

void OneEuroSmoother::setTarget (Vec3 p)
{
    target = p;
}

// alpha aus Cutoff und dt (Standardformel des 1€-Filters): τ = 1/(2π·fc),
// alpha = 1/(1 + τ/dt). Für fc → 0 geht alpha → 0 (maximale Glättung), für
// fc → ∞ geht alpha → 1 (Filter reicht das Signal unverändert durch).
double OneEuroSmoother::alphaFor (double cutoffHz, double dtIn)
{
    const double tau = 1.0 / (kTwoPi * cutoffHz);
    return 1.0 / (1.0 + tau / dtIn);
}

void OneEuroSmoother::tick (Vec3& outPos, Vec3& outVel)
{
    assert (dt > 0.0);   // prepare() muss vor dem ersten Tick gelaufen sein

    // 1. Stufe: rohe Geschwindigkeitsschätzung aus der Differenz zum
    // letzten rohen Zielwert, selbst tiefpassgefiltert mit festem
    // derivCutoffHz - das ist die "speed-based" Stufe, die dem Filter
    // seinen Namen gibt.
    const Vec3 rawDeriv = (target - xPrev) * (1.0 / dt);
    dxHat += (rawDeriv - dxHat) * alphaFor (derivCutoffHz, dt);

    // 2. Cutoff wächst mit der geschätzten Geschwindigkeit: schnelle
    // Bewegung -> hoher Cutoff -> wenig Verzögerung; langsame Bewegung ->
    // Cutoff nah an minCutoffHz -> starke Glättung gegen Zittern.
    currentCutoffHz = minCutoffHz + beta * dxHat.length();

    // 3. Positionsfilter mit dem adaptiven Cutoff.
    const Vec3 prevXHat = xHat;
    xHat += (target - xHat) * alphaFor (currentCutoffHz, dt);

    xPrev = target;

    outVel = (xHat - prevXHat) * (1.0 / dt);
    outPos = xHat;
}
