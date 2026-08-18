#include "PositionJitter.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kTwoPi = 6.283185307179586;
}

float PositionJitter::nextRandom01 (std::uint32_t& state)
{
    // xorshift32 (Marsaglia) - deterministisch, ohne Bibliotheks-Zustand,
    // allokationsfrei und lock-frei: fuer den Audiothread geeignet, anders
    // als z.B. std::mt19937 mit seinem grossen internen Zustand.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;

    return (float) (state >> 8) / (float) (1u << 24);   // [0, 1)
}

Vec3 PositionJitter::pickFreqTargetHz()
{
    // Je Achse unabhaengig im Bereich [0.5, 2.0] * rateHz gewuerfelt - keine
    // synchronen Achsen, sonst entstuende auf Dauer ein hoerbar periodisches
    // Lissajous-Muster statt eines unregelmaessigen Wackelns.
    const double fx = rateHz * (0.5 + 1.5 * (double) nextRandom01 (rngState));
    const double fy = rateHz * (0.5 + 1.5 * (double) nextRandom01 (rngState));
    const double fz = rateHz * (0.5 + 1.5 * (double) nextRandom01 (rngState));

    return { fx, fy, fz };
}

void PositionJitter::prepare (double tickRateHz)
{
    freqSmoother.prepare (tickRateHz);
    reset();
}

void PositionJitter::reset()
{
    phase[0] = phase[1] = phase[2] = 0.0;

    freqSmoother.reset (pickFreqTargetHz());
    retargetTimer = 0.0;
}

void PositionJitter::setAmount (double metres)
{
    amount = std::max (0.0, metres);
}

void PositionJitter::setRate (double hektikHz)
{
    rateHz = std::max (0.001, hektikHz);

    // Zeitkonstante der Frequenzdrift an die Hektik gekoppelt: hohe Hektik
    // wuerfelt schneller neu und driftet schneller dorthin, niedrige Hektik
    // bleibt traege.
    freqSmoother.setTau (1.0 / (2.0 * rateHz));
}

Vec3 PositionJitter::tick (double dt)
{
    retargetTimer -= dt;

    if (retargetTimer <= 0.0)
    {
        freqSmoother.setTarget (pickFreqTargetHz());

        // Zwei Zeitkonstanten, bis wieder neu gewuerfelt wird - sonst
        // driftet freq(t) irgendwann exakt in den letzten Zufallswert und
        // bleibt dort stehen; "Hektik" soll aber andauern, nicht abklingen.
        retargetTimer = 2.0 / rateHz;
    }

    Vec3 freqNow, freqVel;
    freqSmoother.tick (freqNow, freqVel);

    // std::abs, weil eine negativ driftende "Frequenz" sonst die Phase
    // rueckwaerts liefe - das ist als Wert ohne Bedeutung, nur ihr Betrag
    // zaehlt als Umlaufgeschwindigkeit.
    phase[0] += kTwoPi * std::abs (freqNow.x) * dt;
    phase[1] += kTwoPi * std::abs (freqNow.y) * dt;
    phase[2] += kTwoPi * std::abs (freqNow.z) * dt;

    for (auto& p : phase)
        if (p > kTwoPi)
            p = std::fmod (p, kTwoPi);

    return { amount * std::sin (phase[0]),
             amount * std::sin (phase[1]),
             amount * std::sin (phase[2]) };
}
