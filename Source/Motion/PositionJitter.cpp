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

Vec3 PositionJitter::pickAxisFactors()
{
    // Je Achse unabhaengig im Bereich [0.5, 2.0] gewuerfelt - keine
    // synchronen Achsen, sonst entstuende auf Dauer ein hoerbar periodisches
    // Lissajous-Muster statt eines unregelmaessigen Wackelns.
    //
    // Das sind seit dem Umbau auf Ausschlag/Tempo VERHAELTNISSE, keine
    // Frequenzen. Wie schnell die Bewegung insgesamt ablaeuft, sagt allein
    // das Tempo; hier wird nur ausgewuerfelt, welche der drei Achsen gerade
    // die fuehrende ist.
    const double gx = 0.5 + 1.5 * (double) nextRandom01 (rngState);
    const double gy = 0.5 + 1.5 * (double) nextRandom01 (rngState);
    const double gz = 0.5 + 1.5 * (double) nextRandom01 (rngState);

    return { gx, gy, gz };
}

void PositionJitter::prepare (double tickRateHz)
{
    tickRate = tickRateHz > 0.0 ? tickRateHz : 1000.0;

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

    freqSmoother.reset (pickAxisFactors());
    retargetTimer = 0.0;

    // Kein Anfahren aus dem Nichts: nach einem reset() steht der Wackler
    // sofort auf seinem eingestellten Ausschlag, sonst faehre er nach jedem
    // Neuanlassen erst wieder hoch.
    amount  = amountTarget;
    speed   = speedTarget;
    zFactor = zTarget;
}

void PositionJitter::setAmount (double metres)
{
    // Nur das ZIEL setzen - angefahren wird es in tick(), siehe Header.
    amountTarget = std::max (0.0, metres);
}

void PositionJitter::setSpeed (double metresPerSecond)
{
    speedTarget = std::max (0.0, metresPerSecond);
}

void PositionJitter::setZFactor (double factor01)
{
    // Wie der Ausschlag ein Ziel, kein Sprung: eine Reglerbewegung hier ist
    // eine Ortsveraenderung in z und muesste sonst genauso geflogen werden.
    zTarget = std::clamp (factor01, 0.0, 1.0);
}

Vec3 PositionJitter::tick (double dt)
{
    // Ausschlag, Tempo und Hoehenanteil an ihre Ziele heranfahren (siehe
    // Header). Der Ein-Pol formt die Bewegung, das eingestellte TEMPO
    // begrenzt sie: eine Aenderung des Ausschlags ist eine echte Strecke, und
    // Strecken legt der Wackler mit seiner Bahngeschwindigkeit zurueck.
    // Beim Tempo selbst gilt kein Deckel - das ist keine Strecke.
    {
        const double coeff = 1.0 - std::exp (-dt / amountGlideSeconds);

        speed += (speedTarget - speed) * coeff;

        // Gefahren wird mit dem ZIEL-Tempo, nicht mit dem gerade
        // angefahrenen: sonst blieben Ausschlag und Tempo aneinander haengen,
        // wenn beide gleichzeitig aus dem Stand hochgezogen werden - das Tempo
        // waere noch fast null und wuerde den Ausschlag festhalten.
        const double glideSpeed = std::max (speed, speedTarget);

        double delta = (amountTarget - amount) * coeff;

        if (glideSpeed > 0.0)
        {
            const double maxStep = glideSpeed * dt;
            delta = std::clamp (delta, -maxStep, maxStep);
        }
        else
        {
            // Tempo null heisst: der Wackler bewegt sich nicht. Auch nicht,
            // um einen neuen Ausschlag einzunehmen - das waere eine Bewegung,
            // und zwar eine ungebremste.
            delta = 0.0;
        }

        amount += delta;

        double zDelta = (zTarget - zFactor) * coeff;

        if (amount > 1.0e-9)
        {
            // Der z-Anteil bewegt die Quelle um amount * dz - dieselbe
            // Begrenzung wie beim Ausschlag, nur auf die Strecke umgerechnet,
            // die diese Aenderung tatsaechlich zuruecklegt.
            const double maxZStep = glideSpeed * dt / amount;
            zDelta = std::clamp (zDelta, -maxZStep, maxZStep);
        }

        zFactor += zDelta;
    }

    // --- Frequenz aus Ausschlag und Tempo ---
    //
    // Das ist der Kern des Umbaus. Ein Sinus der Amplitude A und der Frequenz
    // f hat die Bahngeschwindigkeit A*2pi*f*cos(phase). Aus dem gewuenschten
    // Tempo v folgt damit rueckwaerts die Frequenz - sie ist Ergebnis, nicht
    // Eingabe.
    //
    // Bezugsgroesse ist die SPITZE, nicht der Mittelwert: "Tempo" soll die
    // Geschwindigkeit sein, die der Wackler erreicht, und nicht eine, um die
    // herum er auch deutlich schneller werden darf. Nur so laesst sich die
    // Zahl mit der Schallgeschwindigkeit vergleichen - und genau darum geht
    // es, denn ein Wackler ueber Mach 1 loest fortwaehrend Stossfronten aus.
    // Auf den Mittelwert bezogen lag die Spitze beim Doppelten, und 340 m/s
    // im Regler waeren in Wahrheit Mach 2 gewesen.
    //
    // Die Spitze liegt vor, wenn alle drei Achsenfaktoren gleichzeitig an
    // ihrem oberen Anschlag (2) stehen und alle drei Kosinus gleichzeitig 1
    // sind:
    //     v_peak = A * 2pi * fBase * 2*sqrt(2 + z^2).
    // Fuer z = 1 ist das der Faktor 2*sqrt(3), also derselbe, mit dem der
    // frueher davorgeschaltete Tempo-Deckel gerechnet hat. Ein alter Zustand
    // mit "Jit Max" bewegt sich nach der Umrechnung deshalb genauso schnell
    // wie zuvor.
    //
    // Dass die Bewegung damit die meiste Zeit LANGSAMER laeuft als die Zahl
    // sagt, ist gewollt und keine versteckte Bremse: der ungueenstigste Fall
    // tritt selten ein, und dazwischen soll das Wackeln atmen. Ein Wackler,
    // der immer exakt gleich schnell ist, waere ein Kreisel.
    const double zNow = zFactor;

    const double peakFactor = 2.0 * std::sqrt (2.0 + zNow * zNow);

    // Der Ausschlag steht im Nenner: bei winzigem Ausschlag muesste die
    // Bewegung sehr schnell schwingen, um ueberhaupt auf das eingestellte
    // Tempo zu kommen. Nach oben begrenzt das allein die Darstellbarkeit auf
    // dem Tick-Raster (siehe tickRate im Header) - kein Bedienlimit, sondern
    // dieselbe Grenze, die ein Sample-Raster jeder Wellenform setzt.
    const double baseFreq = (amount > 1.0e-9 && peakFactor > 0.0)
                          ? std::min (speed / (kTwoPi * amount * peakFactor), tickRate * 0.5)
                          : 0.0;

    // Zeitkonstante der Achsendrift an die Frequenz gekoppelt: eine schnelle
    // Bewegung wuerfelt schneller neu und driftet schneller dorthin, eine
    // traege bleibt traege.
    const double driftHz = std::max (0.001, baseFreq);

    freqSmoother.setTau (1.0 / (2.0 * driftHz));

    retargetTimer -= dt;

    if (retargetTimer <= 0.0)
    {
        freqSmoother.setTarget (pickAxisFactors());

        // Zwei Zeitkonstanten, bis wieder neu gewuerfelt wird - sonst driftet
        // das Achsenverhaeltnis irgendwann exakt in den letzten Zufallswert
        // und bleibt dort stehen; die Unregelmaessigkeit soll aber andauern,
        // nicht abklingen.
        retargetTimer = 2.0 / driftHz;
    }

    Vec3 axisNow, axisVel;
    freqSmoother.tick (axisNow, axisVel);

    // std::abs, weil ein negativ driftender Faktor sonst die Phase rueckwaerts
    // liefe - als Wert ohne Bedeutung, nur sein Betrag zaehlt.
    phase[0] += kTwoPi * baseFreq * std::abs (axisNow.x) * dt;
    phase[1] += kTwoPi * baseFreq * std::abs (axisNow.y) * dt;
    phase[2] += kTwoPi * baseFreq * std::abs (axisNow.z) * dt;

    for (auto& p : phase)
        if (p > kTwoPi)
            p = std::fmod (p, kTwoPi);

    // Alle drei Achsen mit eigenem Verhaeltnis und ohne bevorzugte Ebene; die
    // Hoehe bekommt zusaetzlich ihren Anteil (siehe setZFactor).
    return { amount * std::sin (phase[0]),
             amount * std::sin (phase[1]),
             amount * zFactor * std::sin (phase[2]) };
}
