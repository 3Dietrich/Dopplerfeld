#include "PositionJitter.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kPi    = 3.141592653589793;
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
    if (rotorMode)
    {
        // Rotor: EINE Umlaufgeschwindigkeit statt dreier Achsfrequenzen, in
        // alle drei Komponenten geschrieben, damit derselbe Glaetter beide
        // Betriebsarten tragen kann.
        //
        // Der Wuerfel sitzt multiplikativ um 1 herum (Faktor 1/4 bis 4,
        // gleichmaessig im Logarithmus - halb so schnell und doppelt so
        // schnell sind gleich wahrscheinlich). randomize blendet zwischen
        // "gar nicht" und "voll gewuerfelt": bei 0 kommt exakt rateHz heraus,
        // also ein sauberer Kreis mit konstantem Tempo.
        const double u      = 2.0 * (double) nextRandom01 (rngState) - 1.0;
        const double factor = std::exp (u * std::log (4.0));
        const double f      = rateHz * (1.0 + randomize * (factor - 1.0));

        return { f, f, f };
    }

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
    // Startphasen aus dem eigenen Zufallsgenerator, nicht alle bei null: sonst
    // stuenden im Rotoren-Modus saemtliche Klone im selben Punkt ihrer
    // Kreisbahn und drehten sichtbar wie ein einziger Koerper. Der Generator
    // ist ueber setSeed je Klon verschieden und trotzdem deterministisch.
    for (auto& p : phase)
        p = kTwoPi * (double) nextRandom01 (rngState);

    freqSmoother.reset (pickFreqTargetHz());
    retargetTimer = 0.0;

    lastOut   = { 0.0, 0.0, 0.0 };
    blendFrom = { 0.0, 0.0, 0.0 };
    blend     = 1.0;
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

void PositionJitter::setRotor (bool shouldRotate)
{
    if (shouldRotate == rotorMode)
        return;

    rotorMode = shouldRotate;

    // Der geglaettete Frequenzwert bedeutet in beiden Betriebsarten etwas
    // anderes (drei Achsfrequenzen gegen eine Umlaufgeschwindigkeit) - beim
    // Wechsel wird deshalb sofort neu gezielt statt den alten Wert
    // weiterzudriften.
    freqSmoother.setTarget (pickFreqTargetHz());
    retargetTimer = 2.0 / rateHz;

    // Die Formel wechselt, die Position darf es nicht: von hier aus wird
    // ueberblendet (siehe blendSeconds im Header).
    blendFrom = lastOut;
    blend     = 0.0;
}

void PositionJitter::setRandomize (double amount01)
{
    randomize = std::clamp (amount01, 0.0, 1.0);
}

void PositionJitter::setZJitter (double amount01)
{
    zJitter = std::clamp (amount01, 0.0, 1.0);
}

void PositionJitter::setMaxSpeed (double metresPerSecond)
{
    maxSpeed = metresPerSecond;
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

    // Tempogrenze: die Bahngeschwindigkeit ist amount * 2pi * f je Achse, im
    // ungünstigsten Fall stehen alle drei Achsen gleichzeitig auf ihrem
    // Maximum. Ueberschreitet das die Grenze, werden ALLE drei Frequenzen mit
    // demselben Faktor gestreckt - so bleibt das Bewegungsmuster erhalten und
    // laeuft nur langsamer ab, statt dass eine Achse gegen eine Kante faehrt.
    // Im Rotoren-Modus ist die Bahngeschwindigkeit exakt amount * 2pi * f
    // (Kreisbahn, unabhaengig von der Neigung der Ebene).
    double slow = 1.0;

    if (maxSpeed > 0.0 && amount > 0.0)
    {
        const double fMagnitude = rotorMode
                                ? std::abs (freqNow.x)
                                : std::sqrt (freqNow.x * freqNow.x
                                           + freqNow.y * freqNow.y
                                           + freqNow.z * freqNow.z);
        const double vMax       = amount * kTwoPi * fMagnitude;

        if (vMax > maxSpeed)
            slow = maxSpeed / vMax;
    }

    // std::abs, weil eine negativ driftende "Frequenz" sonst die Phase
    // rueckwaerts liefe - das ist als Wert ohne Bedeutung, nur ihr Betrag
    // zaehlt als Umlaufgeschwindigkeit.
    phase[0] += kTwoPi * std::abs (freqNow.x) * slow * dt;
    phase[1] += kTwoPi * std::abs (freqNow.y) * slow * dt;
    phase[2] += kTwoPi * std::abs (freqNow.z) * slow * dt;

    for (auto& p : phase)
        if (p > kTwoPi)
            p = std::fmod (p, kTwoPi);

    Vec3 out;

    if (rotorMode)
    {
        // Kreisbahn mit dem Radius amount, gefahren von EINER Phase - damit
        // ist der Abstand zum Mittelpunkt konstant und die Bahn wirklich rund,
        // anders als bei drei unabhaengigen Sinussen.
        //
        // Die Kreisebene kippt um die x-Achse: bei zJitter = 0 liegt sie flach
        // in xy, bei 1 steht sie senkrecht (90 Grad), der Rotor dreht sich
        // dann voll durch den z-Bereich. Dazwischen wandert der Anteil stetig
        // von y nach z, der Radius bleibt dabei gleich.
        const double tilt = zJitter * (0.5 * kPi);
        const double c    = std::cos (phase[0]);
        const double sn   = std::sin (phase[0]);

        out = { amount * c,
                amount * sn * std::cos (tilt),
                amount * sn * std::sin (tilt) };
    }
    else
    {
        out = { amount * std::sin (phase[0]),
                amount * std::sin (phase[1]),
                amount * std::sin (phase[2]) };
    }

    // Ueberblendung nach einem Moduswechsel: die Position laeuft vom zuletzt
    // ausgegebenen Punkt aus stetig in die neue Formel hinein, statt zu
    // springen.
    if (blend < 1.0)
    {
        blend = std::min (1.0, blend + dt / blendSeconds);
        out   = blendFrom + (out - blendFrom) * blend;
    }

    lastOut = out;

    return out;
}
