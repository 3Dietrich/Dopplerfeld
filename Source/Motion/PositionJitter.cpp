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

    // --- Tempo heisst Tempo: Parametrisierung nach Bogenlaenge ---
    //
    // Die Bahn ist ein Lissajous aus drei Sinus, x/y/z mit eigenen
    // Achsenfaktoren. Wie SCHNELL sie durchlaufen wird, ist davon unabhaengig -
    // und genau das wird hier laufend so eingestellt, dass die
    // Bahngeschwindigkeit exakt dem Regler entspricht.
    //
    // Der Vorgaenger rechnete stattdessen EINE feste Frequenz aus dem
    // unguenstigsten denkbaren Fall (alle drei Achsenfaktoren gleichzeitig am
    // oberen Anschlag UND alle drei Kosinus gleichzeitig eins). Der tritt
    // praktisch nie ein, und deshalb kam nur ein Bruchteil des eingestellten
    // Tempos an: @dpa 20260825 mass Mach 1,5, wo Mach 3 stand. Nachgemessen
    // im load_check-Abschnitt "Wackler-Tempo" waren es 60 bis 73 Prozent der
    // Spitze und 37 bis 39 Prozent im Effektivwert.
    //
    // Ableitung: mit P_i = A_i * sin(phi_i) ist
    //     dP_i/dt = A_i * cos(phi_i) * dphi_i/dt.
    // Sollen die Achsen ihr gewuerfeltes Verhaeltnis g_i behalten, ist
    // dphi_i/dt = omega * g_i, und aus |dP/dt| = v folgt
    //     omega = v / sqrt( SUM (A_i * g_i * cos phi_i)^2 ).
    // Die Wurzel ist der Momentanwert - deshalb wird omega jeden Tick neu
    // bestimmt, und deshalb stimmt das Tempo immer statt nur im Mittel.
    //
    // Klingt das nach einem Kreisel? Nein. Konstant ist nur der BETRAG der
    // Geschwindigkeit, die RICHTUNG wechselt weiter unregelmaessig - und fuer
    // den Doppler zaehlt allein die Komponente entlang der Sichtlinie zum
    // Hoerer. Die schwankt nach wie vor von +v bis -v durch null. Was wegfaellt,
    // ist das zufaellige Langsamsein, mit dem sich vorher kein Tempo einstellen
    // liess.

    // Zeitkonstante der Achsendrift und Takt des Neuwuerfelns haengen an einer
    // GROBEN Bezugsfrequenz aus den Reglerwerten, nicht am momentanen omega:
    // omega schwankt von Tick zu Tick (das ist ja der Zweck), und eine daran
    // haengende Zeitkonstante wuerde mitzappeln, statt eine zu sein. Die
    // Naeherung ist die Frequenz, die sich ergaebe, stuenden alle Achsen auf
    // eins - fuer "wie oft wuerfeln wir neu" genau genug.
    const double driftHz = std::max (0.001,
                                     amount > 1.0e-9
                                         ? speed / (kTwoPi * amount
                                                    * std::sqrt (2.0 + zFactor * zFactor))
                                         : 0.0);

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
    const double gx = std::abs (axisNow.x);
    const double gy = std::abs (axisNow.y);
    const double gz = std::abs (axisNow.z);

    // Die drei Beitraege zur Momentangeschwindigkeit, je Achse.
    const double cx = amount           * gx * std::cos (phase[0]);
    const double cy = amount           * gy * std::cos (phase[1]);
    const double cz = amount * zFactor * gz * std::cos (phase[2]);

    // Nur gegen die Division durch null: der Fall, dass alle drei Achsen
    // gleichzeitig an ihrem Umkehrpunkt stehen. Die eigentliche Absicherung
    // steht weiter unten und misst den zurueckgelegten Weg direkt.
    const double reach = std::max (std::sqrt (cx * cx + cy * cy + cz * cz),
                                   1.0e-9 * std::max (1.0, amount));

    const double omega = amount > 1.0e-9 ? speed / reach : 0.0;

    double step[3] { omega * gx * dt, omega * gy * dt, omega * gz * dt };

    // --- Der Schritt wird am tatsaechlich zurueckgelegten Weg nachgemessen ---
    //
    // omega kommt aus der ABLEITUNG an der aktuellen Stelle. Das stimmt nur,
    // solange der Schritt klein ist; nahe einem Umkehrpunkt ist die Ableitung
    // fast null, omega wird gross, und ein Euler-Schritt schiesst dann weit
    // ueber das Ziel hinaus. Nachgemessen waren das bei 50 m Ausschlag und
    // Mach 3 Spitzen vom Fuenffachen des eingestellten Tempos - also genau
    // wieder ein Regler, der nicht haelt, was er sagt, nur in die andere
    // Richtung.
    //
    // Deshalb wird die neue Stelle probeweise ausgerechnet und der Schritt
    // heruntergezogen, wenn der Weg dorthin laenger waere als v * dt. Ein
    // Durchgang genuegt: die Bahn ist glatt, und der Ueberschuss tritt nur in
    // der unmittelbaren Umgebung eines Umkehrpunkts auf.
    const Vec3 current { amount * std::sin (phase[0]),
                         amount * std::sin (phase[1]),
                         amount * zFactor * std::sin (phase[2]) };

    auto positionAfter = [&] (const double s[3])
    {
        return Vec3 { amount * std::sin (phase[0] + s[0]),
                      amount * std::sin (phase[1] + s[1]),
                      amount * zFactor * std::sin (phase[2] + s[2]) };
    };

    {
        const double allowed = speed * dt;
        const double moved   = (positionAfter (step) - current).length();

        if (moved > allowed && moved > 1.0e-12)
        {
            const double scale = allowed / moved;

            for (auto& v : step)
                v *= scale;
        }
    }

    phase[0] += step[0];
    phase[1] += step[1];
    phase[2] += step[2];

    for (auto& p : phase)
        if (p > kTwoPi)
            p = std::fmod (p, kTwoPi);

    // Alle drei Achsen mit eigenem Verhaeltnis und ohne bevorzugte Ebene; die
    // Hoehe bekommt zusaetzlich ihren Anteil (siehe setZFactor).
    return { amount * std::sin (phase[0]),
             amount * std::sin (phase[1]),
             amount * zFactor * std::sin (phase[2]) };
}
