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
    // Startphasen aus dem eigenen Zufallsgenerator, nicht alle bei null: sonst
    // stuenden saemtliche Klone im selben Punkt ihrer Bahn und wackelten
    // sichtbar wie ein einziger Koerper. Der Generator ist ueber setSeed je
    // Klon verschieden und trotzdem deterministisch.
    for (auto& p : phase)
        p = kTwoPi * (double) nextRandom01 (rngState);

    freqSmoother.reset (pickFreqTargetHz());
    retargetTimer = 0.0;

    // Kein Anfahren aus dem Nichts: nach einem reset() steht der Wackler
    // sofort auf seinem eingestellten Ausschlag, sonst faehre er nach jedem
    // Neuanlassen erst wieder hoch.
    amount  = amountTarget;
    rateHz  = rateTarget;
    zFactor = zTarget;
}

void PositionJitter::setAmount (double metres)
{
    // Nur das ZIEL setzen - angefahren wird es in tick(), siehe Header.
    amountTarget = std::max (0.0, metres);
}

void PositionJitter::setRate (double hektikHz)
{
    rateTarget = std::max (0.001, hektikHz);
}

void PositionJitter::setZFactor (double factor01)
{
    // Wie der Ausschlag ein Ziel, kein Sprung: eine Reglerbewegung hier ist
    // eine Ortsveraenderung in z und muesste sonst genauso geflogen werden.
    zTarget = std::clamp (factor01, 0.0, 1.0);
}

void PositionJitter::setMaxSpeed (double metresPerSecond)
{
    maxSpeed = metresPerSecond;
}

Vec3 PositionJitter::tick (double dt)
{
    // Ausschlag und Hektik an ihre Ziele heranfahren (siehe Header). Der
    // Ein-Pol formt die Bewegung, der Deckel begrenzt ihr Tempo - beim
    // Ausschlag, weil eine Aenderung dort eine echte Strecke ist, bei der
    // Hektik nicht, denn die ist keine Strecke.
    {
        const double coeff = 1.0 - std::exp (-dt / amountGlideSeconds);

        double delta = (amountTarget - amount) * coeff;

        if (maxSpeed > 0.0)
        {
            const double maxStep = maxSpeed * dt;
            delta = std::clamp (delta, -maxStep, maxStep);
        }

        amount += delta;
        rateHz += (rateTarget - rateHz) * coeff;

        double zDelta = (zTarget - zFactor) * coeff;

        if (maxSpeed > 0.0 && amount > 1.0e-9)
        {
            // Der z-Anteil bewegt die Quelle um amount * dz - derselbe Deckel
            // wie beim Ausschlag, nur auf die Strecke umgerechnet, die diese
            // Aenderung tatsaechlich zuruecklegt.
            const double maxZStep = maxSpeed * dt / amount;
            zDelta = std::clamp (zDelta, -maxZStep, maxZStep);
        }

        zFactor += zDelta;
    }

    // Zeitkonstante der Frequenzdrift an die Hektik gekoppelt: hohe Hektik
    // wuerfelt schneller neu und driftet schneller dorthin, niedrige Hektik
    // bleibt traege.
    freqSmoother.setTau (1.0 / (2.0 * std::max (0.001, rateHz)));

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

    // Tempogrenze: die Bahngeschwindigkeit ist amount * 2pi * f. Ueberschreitet
    // sie die Grenze, werden alle Frequenzen mit demselben Faktor gestreckt -
    // so bleibt das Bewegungsmuster erhalten und laeuft nur langsamer ab,
    // statt dass eine Achse gegen eine Kante faehrt.
    //
    // Der Bremsfaktor kommt aus den REGLERWERTEN, nicht aus der gerade
    // gewuerfelten Frequenz, und das ist der springende Punkt. Aus freqNow
    // gerechnet waere die Bremse ein Normierer: sie zoege jede Schwankung
    // exakt wieder auf die Grenze zurueck, und das Wackeln liefe mit
    // konstantem Tempo - die Unregelmaessigkeit, die "Hektik" ausmacht, waere
    // weg, sobald die Grenze ueberhaupt greift.
    //
    // Bezugsgroesse ist deshalb das SCHNELLSTMOEGLICHE, das der Wuerfel bei
    // den aktuellen Reglerwerten hergibt. Der Faktor ist damit ueber die Zeit
    // konstant, die relative Schwankung bleibt vollstaendig erhalten, und die
    // Grenze wird trotzdem nie ueberschritten.
    double slow = 1.0;

    if (maxSpeed > 0.0 && amount > 0.0)
    {
        // Je Achse reicht der Wuerfel bis zum Doppelten, und im unguenstigsten
        // Fall stehen alle drei gleichzeitig dort.
        const double peakFactor = 2.0 * std::sqrt (3.0);

        const double vPeak = amount * kTwoPi * rateHz * peakFactor;

        if (vPeak > maxSpeed)
            slow = maxSpeed / vPeak;
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

    // Alle drei Achsen mit eigener Frequenz und ohne bevorzugte Ebene; die
    // Hoehe bekommt zusaetzlich ihren Anteil (siehe setZFactor).
    return { amount * std::sin (phase[0]),
             amount * std::sin (phase[1]),
             amount * zFactor * std::sin (phase[2]) };
}
