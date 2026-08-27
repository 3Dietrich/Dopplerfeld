#include "PositionJitter.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kTwoPi = 6.283185307179586;

    // Vec3 hat kein Kreuzprodukt (das ist reine Physik-Geometrie, gehoert
    // nicht in den allgemeinen Wertetyp) - hier lokal, nur fuer die
    // Drehachsen-Bestimmung unten.
    Vec3 crossProduct (const Vec3& a, const Vec3& b)
    {
        return { a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x };
    }

    // Die Drehachse, die "from" auf kuerzestem Weg zu "to" dreht (beide
    // Einheitsvektoren). Normalfall: das Kreuzprodukt, senkrecht auf beiden.
    // Bei (fast) parallelen Vektoren ist die Achse egal (der Drehwinkel ist
    // ~0, sin(0)=0 macht sie wirkungslos). Bei (fast) entgegengesetzten
    // Vektoren verschwindet das Kreuzprodukt trotzdem (Winkel ~180 Grad hat
    // unendlich viele gueltige Achsen) - dann irgendeine zu "from" senkrechte
    // Achse ueber eine Hilfsrichtung konstruieren.
    Vec3 pickTurnAxis (const Vec3& from, const Vec3& to)
    {
        const Vec3   axis      = crossProduct (from, to);
        const double axisLenSq = axis.lengthSquared();

        if (axisLenSq > 1.0e-12)
            return axis * (1.0 / std::sqrt (axisLenSq));

        const Vec3 helper = std::fabs (from.z) < 0.9 ? Vec3 { 0.0, 0.0, 1.0 }
                                                      : Vec3 { 1.0, 0.0, 0.0 };
        const Vec3   fallback      = crossProduct (from, helper);
        const double fallbackLenSq = fallback.lengthSquared();

        return fallbackLenSq > 1.0e-18 ? fallback * (1.0 / std::sqrt (fallbackLenSq))
                                        : Vec3 { 0.0, 1.0, 0.0 };
    }
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

    const Vec3 toGo         = waypoint - offset;
    const Vec3 startHeading = toGo.lengthSquared() > 1.0e-18 ? toGo.normalised()
                                                             : Vec3 { 1.0, 0.0, 0.0 };

    // Direkt gesetzt statt gedreht: beim (Neu-)Start gibt es keine
    // Vorgaenger-Richtung, die einen Knick bilden koennte.
    legHeading = startHeading;
    heading    = startHeading;

    turnStartHeading = startHeading;
    turnAxis         = { 0.0, 0.0, 1.0 };
    turnTotalAngle   = 0.0;
    turnAngle        = 0.0;
    turnAngleVel     = 0.0;
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

    // Ein neues Bein beginnen: naechsten Wegpunkt wuerfeln, die feste
    // Flugrichtung fuer dieses Bein daraus ableiten, und eine neue Drehung
    // AB DER AKTUELLEN RICHTUNG (heading) mit Geschwindigkeit 0 ansetzen -
    // "kritisch gedaempft = kein Ueberschwingen" gilt nur aus der Ruhe
    // heraus, siehe unten bei den beiden Aufrufstellen.
    const auto beginNewLeg = [this]
    {
        waypoint = pickWaypoint();

        const Vec3 freshToGo = waypoint - offset;
        legHeading = freshToGo.lengthSquared() > 1.0e-18 ? freshToGo.normalised() : legHeading;

        turnStartHeading = heading;
        turnAxis         = pickTurnAxis (turnStartHeading, legHeading);
        turnTotalAngle   = std::acos (std::clamp (dot (turnStartHeading, legHeading), -1.0, 1.0));
        turnAngle        = 0.0;
        turnAngleVel     = 0.0;
    };

    // Ist der Zielpunkt erreicht (oder liegt er nach einer Reglerbewegung
    // ausserhalb des Bereichs), wird der naechste gewuerfelt. "Erreicht"
    // heisst hier: in Flugrichtung liegt der Wegpunkt nicht mehr vor uns
    // (Projektion auf legHeading <= 0) - ein Abstandsgrenzwert in Metern
    // waere je nach Tempo/Ausschlag falsch skaliert und wurde bei dieser
    // Kombination (grosser Ausschlag, hohes Tempo) nur selten unterschritten,
    // weil die Fliege den Punkt oft seitlich passiert statt ihn zu treffen.
    // Die Projektion erkennt das "Passiert" trotzdem zuverlaessig, unabhaengig
    // vom seitlichen Vorbeiflug-Abstand.
    const Vec3   toGo     = waypoint - offset;
    const double distance = toGo.length();

    if (dot (toGo, legHeading) <= 0.0 || waypoint.length() > amount * 1.5 || distance <= 1.0e-9)
        beginNewLeg();
    // Kein "sonst"-Zweig, der jeden Tick neu auf den (jetzt naeheren)
    // Wegpunkt nachzielt: das waere keine Gerade mehr, sondern eine
    // Verfolgungskurve, die sich - je nach Geometrie - beliebig eng um den
    // Zielpunkt herumziehen kann, bevor die Drehung unten ueberhaupt
    // hinterherkommt. Genau das erzeugte die Kreisbahn-artigen Ausreisser
    // (empirisch mit dem Testprogramm nachgewiesen: die Fliege blieb dabei
    // auf Dauer im Orbit um einen Punkt haengen, statt ihn zu erreichen).
    // Das Bein bleibt stattdessen fuer seine gesamte Dauer eine echte Gerade
    // in legHeading - "geradlinig angeflogen", wie im Klassenkommentar oben
    // beschrieben. Der Knick beim Wegpunktwechsel ist dann der EINZIGE Ort,
    // an dem sich die Richtung regulaer aendert.

    // Der Knick am Zielpunkt laeuft ueber eine kritisch gedaempfte DREHUNG,
    // nicht ueber geglaettete Vektor-Komponenten. Der Unterschied ist nicht
    // nur akademisch: zwei nahezu entgegengesetzte Einheitsvektoren
    // komponentenweise ueberblendet (egal ob per Ein-Pol oder per Feder)
    // durchlaufen zwangslaeufig einen Punkt nahe dem Nullvektor - und dort
    // ist die Richtung (der Vektor geteilt durch seine eigene, fast
    // verschwindende Laenge) numerisch hinfaellig. Das wurde mit dem
    // Testprogramm nachgewiesen: |heading| fiel bis auf ~0.001, und direkt
    // danach sprang ein einzelner Tick um mehrere zehn Zentimeter in eine
    // fast beliebige Richtung - das gemessene Krickseln.
    //
    // Hier wird stattdessen der DREHWINKEL selbst kritisch gedaempft (eine
    // Zahl von 0 bis turnTotalAngle, aus der Ruhe startend) und heading per
    // Rodrigues-Drehformel direkt aus turnStartHeading, turnAxis und diesem
    // Winkel rekonstruiert - dadurch bleibt heading fuer JEDEN Winkel
    // (auch exakt 180 Grad) exakt Einheitslaenge, ohne je durch Null zu
    // muessen. Ein Sprung in der Geschwindigkeit (harter Richtungswechsel)
    // waere ein Klick im Doppler; kritisch gedaempft aus der Ruhe gibt es
    // keinen Sprung in der Winkelgeschwindigkeit am Scheitel (anders als bei
    // einem Ein-Pol, dessen Steigung genau am Scheitel am groessten ist -
    // das erzeugte die "sehr spitzen Kurven", siehe Klassenkommentar oben).
    // Die Zeitkonstante haengt an der Zeit, die ein Weg quer durch den
    // Bereich dauert - bei gemaechlichem Wackeln darf der Bogen laenger sein.
    {
        const double crossingSeconds = amount / speed;
        const double tau   = std::clamp (0.15 * crossingSeconds, 4.0 / tickRate, 0.25);
        const double omega = 1.0 / tau;

        const double accel = omega * omega * (turnTotalAngle - turnAngle) - 2.0 * omega * turnAngleVel;
        turnAngleVel += accel * dt;
        turnAngle    += turnAngleVel * dt;
        turnAngle     = std::clamp (turnAngle, 0.0, turnTotalAngle);

        const double c = std::cos (turnAngle);
        const double s = std::sin (turnAngle);
        const Vec3   perp = crossProduct (turnAxis, turnStartHeading);

        heading = turnStartHeading * c + perp * s;
    }

    offset += heading * (speed * dt);

    // Sicherheitsnetz und zugleich der Weg, auf dem ein kleiner werdender
    // Ausschlag die Fliege hereinholt: der Versatz bleibt in der Ebene
    // innerhalb des Ausschlags und in der Hoehe innerhalb seines Anteils -
    // dieselbe Trennung wie bei den Zielpunkten. Weil der Ausschlag selbst nur
    // mit dem eingestellten Tempo schrumpft (siehe oben), ist auch dieses
    // Hereinholen nie schneller als die Bewegung selbst.
    //
    // Dass die Klemmung ueberhaupt greift, ist bei einem Bein nahe am Rand
    // normal (der Wegpunkt selbst darf bis zu 100% des Ausschlags liegen, ein
    // Kurvenscheitel direkt danach kann knapp darueber hinauswollen). Weich
    // statt hart: eine harte Klemmung (Position sofort auf amount
    // zurueckgesetzt) aendert die radiale Geschwindigkeit in EINEM Tick von
    // "wie eingestellt" auf 0 - genau das war der zweite gemessene
    // Krickseln-Mechanismus (Testprogramm: exakt an der Beruehrung ein
    // einzelner Ausreisser im Schrittwinkel).
    //
    // Die Weichfeder unten ist STETIG UND GLATT (C1) an der Grenze: innerhalb
    // von amount/zReach ist sie exakt die Identitaet (der Ausschlag bleibt
    // exakt der eingestellte, wie gefordert), und erst jenseits der Grenze
    // biegt sie glatt in eine Asymptote bei amount+knee bzw. zReach+knee ab -
    // Wert UND Steigung stimmen an der Grenze exakt mit der Identitaet
    // ueberein (Ableitung der Asymptote bei Ueberschuss=0 ist 1), es gibt
    // also keinen Tick, an dem sich die radiale Geschwindigkeit sprunghaft
    // aendert. knee ist grosszuegig (10% des Ausschlags, mindestens ein paar
    // Schrittlaengen): ein zu enger Knick wuerde die Bewegung im Ueberschuss-
    // Bereich fast einfrieren (die Ableitung der Asymptote faellt dort
    // exponentiell), das war beim Ausprobieren mit engerem Knick als erneutes
    // Krickseln sichtbar - grosszuegig bleibt die Ableitung nahe genug an 1,
    // dass sich der Versatz auch jenseits der Grenze noch spuerbar weiterdreht
    // (der Ausschlag selbst wird dadurch nur um bis zu ~1% ueberschritten,
    // in seltenen Randfaellen - kein verstecktes Limit, sondern das
    // ausdrueckliche Sicherheitsnetz aus dem Klassenkommentar).
    {
        const double knee = std::max (amount * 0.10, speed * dt * 4.0);

        const double flat = std::sqrt (offset.x * offset.x + offset.y * offset.y);

        if (flat > amount && flat > 1.0e-12)
        {
            const double excess = flat - amount;
            const double softened = amount + knee * (1.0 - std::exp (-excess / knee));
            const double scale = softened / flat;

            offset.x *= scale;
            offset.y *= scale;
        }

        const double zReach = amount * zFactor;

        // Die Aufweichung gehoert zu DER Grenze, die sie aufweicht, und darum
        // haengt sie hier an zReach und nicht an amount: an amount gekoppelt
        // stuende sie bei Hoehenanteil 0 als fester Betrag ueber der Hoehe
        // null, und "aus" waere nicht aus. Halber Anteil muss halbe Hoehe
        // heissen, ganz aus muss ganz aus heissen.
        const double zKnee = zReach * 0.10;

        // Ist die Hoehe so eng, dass ihre Aufweichung unter einer Schrittlaenge
        // laege, wird hart geklemmt. Der weiche Uebergang soll ein Rattern
        // vermeiden, das aus dem Anlaufen gegen die Grenze entsteht - auf
        // einer Hoehe, die kuerzer ist als ein einzelner Schritt, gibt es
        // dieses Anlaufen nicht.
        if (zKnee > speed * dt)
        {
            if (offset.z > zReach)
            {
                const double excess = offset.z - zReach;
                offset.z = zReach + zKnee * (1.0 - std::exp (-excess / zKnee));
            }
            else if (offset.z < -zReach)
            {
                const double excess = -zReach - offset.z;
                offset.z = -zReach - zKnee * (1.0 - std::exp (-excess / zKnee));
            }
        }
        else
        {
            offset.z = std::clamp (offset.z, -zReach, zReach);
        }
    }

    return offset;
}
