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

Vec3 PositionJitter::pickWaypoint()
{
    // Der naechste Punkt, den die Fliege anfliegt.
    //
    // Ebene und Hoehe werden getrennt gewuerfelt, damit der Hoehenanteil
    // (setZFactor) nur die Hoehe aendert: eine gemeinsame Kugelrichtung
    // wuerde bei flachem Wackeln auch den Ausschlag in der Ebene
    // verkleinern, weil beide sich denselben Radius teilen.
    //
    // In der Ebene liegt der Punkt auf einem Kreis um den Anker, dessen
    // Radius zwischen 40 und 100 Prozent des Ausschlags schwankt - so fliegt
    // die Fliege mal quer durch die Mitte und mal nur ein kurzes Stueck.
    const double azi   = kTwoPi * (double) nextRandom01 (rngState);
    const double reach = amount * (0.4 + 0.6 * (double) nextRandom01 (rngState));

    const double high = amount * zFactor * (2.0 * (double) nextRandom01 (rngState) - 1.0);

    return { reach * std::cos (azi),
             reach * std::sin (azi),
             high };
}

void PositionJitter::prepare (double tickRateHz)
{
    tickRate = tickRateHz > 0.0 ? tickRateHz : 1000.0;

    headingSmoother.prepare (tickRateHz);
    reset();
}

void PositionJitter::reset()
{
    // Startpunkt und Startrichtung aus dem eigenen Zufallsgenerator, nicht
    // alle bei null: sonst stuenden saemtliche Klone im selben Punkt und
    // floegen dieselbe Bahn. Der Generator ist ueber setSeed je Klon
    // verschieden und trotzdem deterministisch.
    amount  = amountTarget;
    speed   = speedTarget;
    zFactor = zTarget;

    offset   = pickWaypoint();
    waypoint = pickWaypoint();

    const Vec3 toGo    = waypoint - offset;
    const Vec3 heading = toGo.lengthSquared() > 1.0e-18 ? toGo.normalised()
                                                        : Vec3 { 1.0, 0.0, 0.0 };

    headingSmoother.reset (heading);
    headingSmoother.setTarget (heading);
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

    // --- Wie eine Fliege, nicht wie ein Karussell ---
    //
    // Die Bewegung ist eine Folge angeflogener Zielpunkte: ein Punkt im
    // Wackelbereich wird geradlinig angesteuert, und sobald er erreicht ist,
    // knickt die Fliege zum naechsten ab (@dpa 20260825: "wie die Fliegen:
    // jeder einzeln ueber den Jitter bereich", nachdem die Klone sich sichtbar
    // "um das Original gedreht" hatten).
    //
    // Warum nicht Sinusse je Achse: drei Sinus mit festem Achsenverhaeltnis
    // ergeben eine geschlossene Lissajous-Figur, und laesst man sie mit
    // konstanter Bahngeschwindigkeit durchlaufen, ist genau das eine
    // Kreisbahn - der Karussell-Eindruck steckte in der Bahnform selbst, nicht
    // in ihrer Geschwindigkeit.
    //
    // Das Tempo bleibt dabei exakt der eingestellte Wert: der Schritt ist
    // heading (Einheitsvektor) mal speed mal dt, unabhaengig davon, wo die
    // Fliege gerade ist. Der Ausschlag bleibt exakt der eingestellte: die
    // Zielpunkte liegen im Bereich, und die Klemmung ganz unten faengt den
    // Rest ab.
    if (amount <= 1.0e-9 || speed <= 0.0)
    {
        // Kein Ausschlag oder kein Tempo heisst: keine Bewegung. Der Versatz
        // faellt nicht auf null zurueck, sondern wird unten mit dem
        // schrumpfenden Ausschlag hereingezogen - ein Sprung waere hier
        // formal Ueberschall.
        if (amount <= 1.0e-9)
            offset = {};

        return offset;
    }

    // Ist der Zielpunkt erreicht (oder liegt er nach einer Reglerbewegung
    // ausserhalb des Bereichs), wird der naechste gewuerfelt.
    const Vec3   toGo     = waypoint - offset;
    const double distance = toGo.length();

    if (distance <= speed * dt * 2.0 || waypoint.length() > amount * 1.5)
    {
        waypoint = pickWaypoint();
        headingSmoother.setTarget ((waypoint - offset).normalised());
    }
    else
    {
        headingSmoother.setTarget (toGo * (1.0 / distance));
    }

    // Der Knick am Zielpunkt laeuft ueber einen kurzen Ein-Pol, statt die
    // Richtung umzuschalten. Ein harter Richtungswechsel waere ein Sprung in
    // der Geschwindigkeit und damit ein Klick im Doppler; ueber ein paar
    // Millisekunden gezogen bleibt er sichtbar ein Knick und ist trotzdem
    // stetig. Die Zeitkonstante haengt an der Zeit, die ein Weg quer durch den
    // Bereich dauert - bei gemaechlichem Wackeln darf der Bogen laenger sein.
    const double crossingSeconds = amount / speed;

    headingSmoother.setTau (std::clamp (0.15 * crossingSeconds, 4.0 / tickRate, 0.25));

    Vec3 heading, headingVel;
    headingSmoother.tick (heading, headingVel);

    const double headingLength = heading.length();

    if (headingLength > 1.0e-12)
        offset += heading * (speed * dt / headingLength);

    // Sicherheitsnetz und zugleich der Weg, auf dem ein kleiner werdender
    // Ausschlag die Fliege hereinholt: der Versatz bleibt in der Ebene
    // innerhalb des Ausschlags und in der Hoehe innerhalb seines Anteils -
    // dieselbe Trennung wie bei den Zielpunkten. Weil der Ausschlag selbst nur
    // mit dem eingestellten Tempo schrumpft (siehe oben), ist auch dieses
    // Hereinholen nie schneller als die Bewegung selbst.
    {
        const double flat = std::sqrt (offset.x * offset.x + offset.y * offset.y);

        if (flat > amount && flat > 1.0e-12)
        {
            const double scale = amount / flat;

            offset.x *= scale;
            offset.y *= scale;
        }

        const double zReach = amount * zFactor;

        offset.z = std::clamp (offset.z, -zReach, zReach);
    }

    return offset;
}
