#include "PropagationPath.h"
#include "Interpolation.h"

#include <algorithm>
#include <cmath>

void PropagationPath::prepare (double sampleRate, int maxBlockSize)
{
    sr              = sampleRate;
    maxBlockSamples = maxBlockSize;

    reset();
}

void PropagationPath::reset()
{
    solver.reset();

    for (auto& b : branches)
        b = Branch{};

    lastSolveTime = 0.0;
    seeded        = false;

    dispBranches.store (0);
    dispDelay.store (0.0);
    dispMach.store (0.0);
}

void PropagationPath::setBoomLimitDb (double dB)
{
    boomDb = dB;
    eps    = std::pow (10.0, -dB / 20.0);
}

void PropagationPath::setAirAbsorptionAmount (double amount01)
{
    airAmount = std::min (1.0, std::max (0.0, amount01));
}

void PropagationPath::setAirAbsorption (double fc0Hz, double refMetres, double exponent)
{
    airFc0      = std::max (20.0, fc0Hz);
    airRefM     = std::max (1.0e-3, refMetres);
    airExponent = exponent;
}

void PropagationPath::setSolverStride (int normalStride, int supersonicStrideIn)
{
    baseStride       = std::max (1, normalStride);
    supersonicStride = std::max (1, supersonicStrideIn);
}

void PropagationPath::setTrajectoryGridSeconds (double seconds)
{
    trajGridSeconds = std::max (1.0e-6, seconds);
}

void PropagationPath::setBranchRampSeconds (double seconds)
{
    rampSeconds = std::min (2.0e-3, std::max (0.5e-3, seconds));
}

double PropagationPath::nearFieldGain (double, double) const
{
    // Phase 2 (Plan 2.7 / Abschnitt 7): der 1/R²-Nahfeldterm ist bewusst nicht
    // implementiert. Der Aufruf existiert nur, damit später kein Aufrufer
    // geändert werden muss.
    return 0.0;
}

double PropagationPath::lowpassCoeff (double R) const
{
    // fc(R) = clamp(fc0 * (R_ref/R)^k, 200, 18000) - Plan 2.9. Näherung, keine
    // Normrechnung; ISO 9613-1 passt später in dieselbe Schnittstelle.
    const double fc = std::min (18000.0,
                                std::max (200.0,
                                          airFc0 * std::pow (airRefM / std::max (R, 1.0e-6),
                                                             airExponent)));

    // One-Pole: y += a*(x - y), a = 1 - exp(-2π·fc/fs). Gleichstromverstärkung
    // 1, damit die Dämpfung nur die Höhen kostet und nicht den Pegel.
    const double a = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * fc / sr);

    // airAmount blendet den Koeffizienten gegen 1 (= Bypass). Ohne das wäre
    // "Luftdämpfung aus" immer noch der 18-kHz-Tiefpass.
    return 1.0 - airAmount * (1.0 - std::min (1.0, std::max (0.0, a)));
}

int PropagationPath::findSlot (int id) const
{
    for (int i = 0; i < maxBranchSlots; ++i)
        if (branches[i].used && branches[i].id == id)
            return i;

    return -1;
}

int PropagationPath::freeSlot() const
{
    for (int i = 0; i < maxBranchSlots; ++i)
        if (! branches[i].used)
            return i;

    return -1;
}

void PropagationPath::evaluateRoot (const SourceTrajectory& traj,
                                    const Root& root,
                                    Vec3   recvPos,
                                    Vec3   recvVel,
                                    double c,
                                    Target& out) const
{
    // R nach unten klemmen, damit ein Ohr direkt auf der Quelle nicht in eine
    // Division durch Null läuft (Plan 2.7, R_min).
    const double R  = std::max (root.R, minRadius);
    const double mr = root.machRadial;

    // Regularisierte Divergenz: denom = sqrt((1 - M_r)² + eps²) statt |1 - M_r|
    // (Plan 2.7). Kantenlos, überall differenzierbar, entspricht einer Quelle
    // mit endlicher Ausdehnung statt eines mathematischen Punkts.
    const double dm    = 1.0 - mr;
    const double denom = std::sqrt (dm * dm + eps * eps);

    // A = A_geo * A_focus. A_focus = 1/|1 - M_r| entsteht beim fraktionalen
    // Auslesen der Delay-Line NICHT von allein und muss hier explizit gesetzt
    // werden (Plan 2.7) - das ist die Stelle, die erfahrungsgemäß fehlt.
    double amp = 1.0 / (R * denom);

    if (nearFieldOn)
        amp += nearFieldGain (R, dominantFreqHz);

    // dτ/dt_h. Für einen ruhenden Empfänger ist das -M_r/(1 - M_r) (Plan 2.11).
    // Der bewegte Empfänger ist der allgemeine Fall derselben Rechnung:
    //   F = c·(t_h - t_e) - |L(t_h) - M(t_e)| = 0
    //   c·(1 - t_e') = û·(v_L - v_M·t_e')  =>  t_e' = (1 - M_L)/(1 - M_r)
    //   τ' = 1 - t_e' = (M_L - M_r)/(1 - M_r)
    // mit M_L = û·v_L/c. Mit v_L = 0 fällt das exakt auf die Planformel
    // zurück. Ohne diesen Term stimmte die Verzögerungssteigung nicht, sobald
    // der Kopf bewegt oder gedreht wird - und genau das soll hörbar sein.
    double machListener = 0.0;

    if (recvVel.lengthSquared() > 0.0)
    {
        Vec3 sourcePos, sourceVel;

        if (traj.sampleAt (root.t_e, sourcePos, sourceVel))
        {
            const Vec3 uHat = (recvPos - sourcePos).normalised();
            machListener    = uHat.dot (recvVel) / c;
        }
    }

    // Denselben regularisierten Nenner mit Vorzeichen benutzen: an der
    // Mach-Front bleibt die Steigung damit endlich (|dτ/dt_h| <= ~1/eps),
    // ohne dass ein zweiter, anders gesetzter Grenzwert entsteht.
    const double signedDenom = (dm < 0.0) ? -denom : denom;

    out.present = true;
    out.tau     = 0.0;   // vom Aufrufer gesetzt, er kennt t_h
    out.dTau    = (machListener - mr) / signedDenom;
    out.amp     = amp;
    out.R       = R;
    out.mach    = mr;
    out.lpCoeff = lowpassCoeff (R);
}

void PropagationPath::seedAt (const SourceTrajectory& traj,
                              const MediumState&      medium,
                              Vec3   recvPos,
                              Vec3   recvVel,
                              double t_h)
{
    // Nach reset(), einem Positionssprung oder einer Lücke in der Zeitachse
    // gibt es keinen linken Stützpunkt für die Hermite-Strecke. Statt über die
    // Lücke zu interpolieren wird hier ein Solver-Punkt bei t_h gesetzt; alle
    // dabei gefundenen Zweige starten mit Envelope 0 und rampen sauber ein.
    for (auto& b : branches)
        b = Branch{};

    solver.reset();

    const double c = medium.speedOfSound();

    Root roots[maxBranchSlots];
    const int n = solver.solve (traj, medium, recvPos, t_h, roots, maxBranchSlots);

    for (int i = 0; i < n && i < maxBranchSlots; ++i)
    {
        Target tg;
        evaluateRoot (traj, roots[i], recvPos, recvVel, c, tg);

        Branch& b = branches[i];
        b.used    = true;
        b.id      = roots[i].id;
        b.tau     = t_h - roots[i].t_e;
        b.dTau    = tg.dTau;
        b.amp     = tg.amp;
        b.R       = tg.R;
        b.mach    = tg.mach;
        b.lpCoeff = tg.lpCoeff;
        b.lpZ     = 0.0;
        b.env     = 0.0;
    }

    lastSolveTime = t_h;
    seeded        = true;
}

void PropagationPath::process (const SourceTrajectory&   traj,
                               const SourceSignalBuffer& sig,
                               const MediumState&        medium,
                               Vec3   receiverPos,
                               Vec3   receiverVel,
                               double blockStartTime,
                               float* out,
                               int    numSamples)
{
    if (out == nullptr || numSamples <= 0 || sr <= 0.0)
        return;

    // Pflichtstelle für die Spiegelquellen aus Phase 2 (Plan 3.4): der
    // Empfänger wird zuerst in das Koordinatensystem dieses Pfades gespiegelt.
    // Für den Direktschall ist das die Identität, also ein No-op.
    const Vec3 recvPos0 = applyPathTransform (transform, receiverPos);
    const Vec3 recvVel  = applyPathTransformVelocity (transform, receiverVel);

    const double c = medium.speedOfSound();

    solver.setMinScanStep (trajGridSeconds);

    // Überschall JETZT (nicht irgendwann in den letzten bis zu 40s) ->
    // feineres Solver-Raster (Plan 2.11), weil M_r dort innerhalb weniger
    // Samples umschlägt. Bewusst recentMaxSpeed() statt maxSpeedInWindow():
    // Stride ist eine reine Zeitraster-Entscheidung - ob JETZT fein
    // aufgelöst werden muss, nicht ob es das je einmal musste. Ein
    // Überschall-Moment vor 30s braucht keine 8-fach so oft aufgerufene
    // Löserkette mehr, dessen M_r ändert sich nicht mehr schnell. Die
    // korrektheitskritische Vollfenster-Suche nach eventuell noch
    // eintreffenden alten Wurzeln (verspätete Booms aus großer Distanz,
    // Plan 2.6/2.10) bleibt im Löser selbst unverändert auf dem vollen
    // Fenster - hier geht es nur darum, wie oft er gerufen wird, nicht was
    // er dabei findet. (Ursache des von @dpa beobachteten "Aussetzer nach
    // schnellem Bewegen": stride blieb bis zu 40s lang auf 8 hängen, macht
    // pro Solver-Punkt ~8x mehr Aufrufe plus - über den Vollfenster-Löser
    // selbst - einen gröberen/teureren Scan durch den dadurch aufgeblähten
    // Lipschitz-Bound.)
    const double recentMaxSpeed = traj.recentMaxSpeed (2.0);
    const int    stride         = (recentMaxSpeed > c) ? supersonicStride : baseStride;

    // Zusammenhängende Zeitachse? Ein halbes Sample Toleranz reicht, weil der
    // Aufrufer die Blockzeit aus einem ganzzahligen Sample-Zähler bildet.
    if (! seeded || std::abs (blockStartTime - lastSolveTime) > 0.5 / sr)
        seedAt (traj, medium, recvPos0, recvVel, blockStartTime);

    const double envInc = 1.0 / std::max (1.0, rampSeconds * sr);
    const double gain   = (double) transform.gain;

    for (int n0 = 0; n0 < numSamples; n0 += stride)
    {
        const int    len    = std::min (stride, numSamples - n0);
        const double tStart = lastSolveTime;
        const double tEnd   = blockStartTime + (double) (n0 + len) / sr;
        const double h      = tEnd - tStart;

        if (h <= 0.0)
            break;

        // Ohrposition am Solver-Punkt: linear aus Position und Geschwindigkeit
        // extrapoliert. Damit wird die Ohrgeometrie pro Solver-Punkt und nicht
        // pro Block ausgewertet (Plan 3.5), ohne dass der Pfad den Hörer kennt.
        const Vec3 recvAtEnd = recvPos0 + recvVel * (tEnd - blockStartTime);

        Root roots[maxBranchSlots];
        const int nRoots = solver.solve (traj, medium, recvAtEnd, tEnd,
                                         roots, maxBranchSlots);

        Target targets[maxBranchSlots];

        for (int i = 0; i < nRoots; ++i)
        {
            Target tg;
            evaluateRoot (traj, roots[i], recvAtEnd, recvVel, c, tg);
            tg.tau = tEnd - roots[i].t_e;

            int slot = findSlot (roots[i].id);

            if (slot < 0)
            {
                // Neuer Zweig (Plan 2.10 Schritt 5): eigener Slot, Filter und
                // Envelope fangen bei Null an - der Zustand eines fremden
                // Zweigs wird ausdrücklich nicht geerbt (Plan 2.9).
                slot = freeSlot();

                if (slot < 0)
                    continue;   // mehr als K Zweige: der Löser hat bereits gezählt

                Branch& nb = branches[slot];
                nb         = Branch{};
                nb.used    = true;
                nb.id      = roots[i].id;
                nb.dTau    = tg.dTau;
                nb.amp     = tg.amp;
                nb.R       = tg.R;
                nb.mach    = tg.mach;
                nb.lpCoeff = tg.lpCoeff;

                // Linker Stützpunkt rückwärts aus der bekannten Steigung
                // extrapoliert, damit die Hermite-Strecke im Segment schon
                // stimmt und nicht erst ab dem nächsten Solver-Punkt.
                nb.tau = tg.tau - tg.dTau * h;
            }

            targets[slot] = tg;
        }

        for (int s = 0; s < maxBranchSlots; ++s)
        {
            Branch& b = branches[s];

            if (! b.used)
                continue;

            const Target& tg    = targets[s];
            const bool    alive = tg.present;

            // Verschwundener Zweig: mit der zuletzt bekannten Steigung
            // weiterlaufen lassen, während der Envelope auf 0 fährt. Ein
            // harter Abbruch wäre ein Sprung und damit Energie oberhalb
            // Nyquist (Plan 3.7).
            const double tau1   = alive ? tg.tau     : b.tau + b.dTau * h;
            const double dTau1  = alive ? tg.dTau    : b.dTau;
            const double amp1   = alive ? tg.amp     : b.amp;
            const double coeff  = alive ? tg.lpCoeff : b.lpCoeff;
            const double target = alive ? 1.0 : 0.0;

            // Hermite-Tangenten müssen auf den Parameter u in [0,1] skaliert
            // sein, deshalb dτ/dt_h mal Segmentlänge (siehe Interpolation.h).
            const double m0 = b.dTau * h;
            const double m1 = dTau1 * h;

            const double tau0 = b.tau;
            const double amp0 = b.amp;

            for (int i = 0; i < len; ++i)
            {
                const double u  = (double) i / (double) len;
                const double tH = tStart + (double) i / sr;

                // Kubisch nach Hermite (Plan 2.11). Linear wäre stückweise
                // konstanter Doppler und damit an jedem Solver-Punkt ein
                // Tonhöhensprung mit Stride-Wiederholrate.
                const double tau = hermite (tau0, m0, tau1, m1, u);

                // Leseposition darf rückwärts wandern - bei M_r > 1 wird der
                // Zweig zeitverkehrt gehört (Plan 2.8). Kein Sondercode, aber
                // auch keine Annahme über Monotonie.
                const double x = (double) sig.readAt (sig.timeToIndex (tH - tau));

                const double amp = amp0 + (amp1 - amp0) * u;

                b.lpZ += coeff * (x * amp - b.lpZ);

                // Ein einzelner nicht-endlicher Wert (Rand-/Sonderfall
                // irgendwo in der Kette) würde diesen One-Pole sonst für
                // immer vergiften - "+= coeff*(... - NaN)" bleibt NaN, egal
                // was als Nächstes hereinkommt. Die Ausgangsstufe filtert nur
                // noch das einzelne Sample, nicht diesen Zustand - ohne diese
                // Selbstheilung bliebe der Zweig bis zum Neuladen des Plugins
                // stumm (genau das beobachtete Verhalten: Ton weg, Neustart
                // hilft).
                if (! std::isfinite (b.lpZ))
                    b.lpZ = 0.0;

                // Envelope nach dem Filter: so ist der Zweig bei env = 0 exakt
                // still und kann sofort freigegeben werden, ohne dass ein
                // abgeschnittener Filterschwanz knackt.
                if (b.env < target)
                    b.env = std::min (target, b.env + envInc);
                else if (b.env > target)
                    b.env = std::max (target, b.env - envInc);

                out[n0 + i] += (float) (b.lpZ * b.env * gain);
            }

            b.tau     = tau1;
            b.dTau    = dTau1;
            b.amp     = amp1;
            b.lpCoeff = coeff;

            if (alive)
            {
                b.R    = tg.R;
                b.mach = tg.mach;
            }
            else if (b.env <= 0.0)
            {
                b.used = false;   // ausgelaufen, Slot frei für den nächsten Zweig
            }
        }

        lastSolveTime = tEnd;
    }

    // Anzeigewerte: der lauteste Zweig ist der, den man hört (Plan 3.12).
    int    active   = 0;
    double bestAmp  = -1.0;
    double bestTau  = 0.0;
    double bestMach = 0.0;

    for (const auto& b : branches)
    {
        if (! b.used)
            continue;

        ++active;

        if (b.amp > bestAmp)
        {
            bestAmp  = b.amp;
            bestTau  = b.tau;
            bestMach = b.mach;
        }
    }

    dispBranches.store (active);
    dispDelay.store (bestTau);
    dispMach.store (bestMach);
}
