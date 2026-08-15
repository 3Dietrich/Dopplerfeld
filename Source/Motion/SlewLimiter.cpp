#include "SlewLimiter.h"
#include <algorithm>
#include <cassert>

SlewLimiter::SlewLimiter (double vMaxIn, double aMaxIn)
{
    setVMax (vMaxIn);
    setAMax (aMaxIn);
}

void SlewLimiter::setVMax (double vMaxIn) { vMax = std::max (1.0e-4, vMaxIn); }
void SlewLimiter::setAMax (double aMaxIn) { aMax = std::max (1.0e-4, aMaxIn); }

void SlewLimiter::prepare (double tickRateHz)
{
    dt = 1.0 / std::max (1.0, tickRateHz);
}

void SlewLimiter::reset (Vec3 p)
{
    pos = p;
    target = p;
    vel = {};
}

void SlewLimiter::setTarget (Vec3 p)
{
    target = p;
}

void SlewLimiter::tick (Vec3& outPos, Vec3& outVel)
{
    assert (dt > 0.0);   // prepare() muss vor dem ersten Tick gelaufen sein

    // Zielgeschwindigkeit: mit vollem Betrag v_max Richtung target. Nahe am
    // Ziel (dist ~ 0) ist die Richtung nicht mehr sinnvoll bestimmbar, dann
    // bleibt die Zielgeschwindigkeit 0 - der Limiter bremst.
    const Vec3 toTarget = target - pos;
    const double dist = toTarget.length();

    Vec3 desiredVel;
    if (dist > 1.0e-9)
        desiredVel = toTarget.normalised() * vMax;

    // a auf a_max begrenzt: erst den ungebremsten Beschleunigungswunsch
    // bilden, dann dessen LÄNGE klemmen statt einzelne Achsen einzeln zu
    // klemmen - sonst würde eine diagonale Bewegung stärker eingebremst als
    // eine achsenparallele mit derselben Gesamtbeschleunigung.
    Vec3 desiredAccel = (desiredVel - vel) * (1.0 / dt);
    const double accelLen = desiredAccel.length();
    if (accelLen > aMax)
        desiredAccel = desiredAccel * (aMax / accelLen);

    vel += desiredAccel * dt;

    // v zusätzlich hart auf v_max begrenzt, weil die a_max-begrenzte
    // Integration knapp darüber hinausschießen kann (letzter Schritt vor
    // Erreichen von v_max).
    const double velLen = vel.length();
    if (velLen > vMax)
        vel = vel * (vMax / velLen);

    pos += vel * dt;

    outPos = pos;
    outVel = vel;
}
