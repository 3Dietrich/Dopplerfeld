#include "RetardedTimeSolver.h"
#include "StraightLineRetardedTime.h"

#include <algorithm>
#include <cmath>

// Brent-Bookkeeping vergleicht unten mehrfach bewusst exakt (z.B. "wurde t
// durchs Clamping verändert", "kam fs von a oder von b") - das sind
// Identitätsprüfungen auf direkt zugewiesene Werte, keine numerischen
// Toleranzvergleiche, also fachlich korrekt trotz -Wfloat-equal.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif

namespace
{

// Auswertung des Residuums F(t_e) = c*(t_h - t_e) - |L(t_h) - M(t_e)|
// (Plan 2.3). Ausserhalb des Puffers ruht die Quelle an ihrer ältesten
// bekannten Position (Plan 2.6). Das ist die Randbedingung, die garantiert,
// dass F am linken Fensterrand nicht negativ ist und immer mindestens eine
// Wurzel existiert - der Überschallknall entsteht dann als hinzukommendes
// Wurzelpaar, nicht als Sprung aus der Stille.
struct Residual
{
    const SourceTrajectory& traj;
    Vec3   listener;
    double t_h;
    double c;
    double tOldest;
    double tNewest;

    // Lastzähler des Lösers (siehe RetardedTimeSolver::residualEvaluations).
    // Ein Inkrement neben einer Catmull-Rom-Interpolation samt Wurzel fällt
    // nicht ins Gewicht, liefert aber ein Maß, das auf jeder Maschine gleich
    // ausfällt.
    std::uint64_t& evals;

    void sample (double t, Vec3& p, Vec3& v) const
    {
        const double tc = std::min (std::max (t, tOldest), tNewest);

        p = Vec3{};
        v = Vec3{};
        traj.sampleAt (tc, p, v);

        // Im eingefrorenen Randbereich ist die Quelle in Ruhe; die vom
        // Puffer gelieferte Randgeschwindigkeit gilt dort nicht mehr.
        if (tc != t)
            v = Vec3{};
    }

    // Die Geschwindigkeit geht in F nicht ein, deshalb hier die
    // positionsonly-Abtastung: das ist der heißeste Pfad des ganzen Lösers.
    double operator() (double t) const
    {
        ++evals;

        const double tc = std::min (std::max (t, tOldest), tNewest);

        Vec3 p {};
        traj.samplePositionAt (tc, p);

        return c * (t_h - t) - (listener - p).length();
    }
};

// Vorzeichenvergleich mit 0 als "nicht positiv" - reicht, weil exakte
// Nullen vor dem Aufruf gesondert behandelt werden.
inline bool oppositeSign (double a, double b)
{
    return (a > 0.0) != (b > 0.0);
}

// Brent: Bisektion mit inverser quadratischer Interpolation. Bewusst NICHT
// Newton (Plan 2.10): an der Mach-Front geht F' = -c*(1 - M_r) gegen 0,
// Newton springt dort unkontrolliert weg. Brent behält das Bracket und kann
// nicht divergieren.
double brent (const Residual& F, double a, double b, double fa, double fb,
              double tol, int maxIter)
{
    if (fa == 0.0) return a;
    if (fb == 0.0) return b;

    if (std::abs (fa) < std::abs (fb))
    {
        std::swap (a, b);
        std::swap (fa, fb);
    }

    double c = a, fc = fa;
    double d = b - a;
    bool   mflag = true;

    for (int i = 0; i < maxIter; ++i)
    {
        if (std::abs (b - a) < tol || fb == 0.0)
            return b;

        double s;

        if (fa != fc && fb != fc)
        {
            // Inverse quadratische Interpolation durch (fa,a),(fb,b),(fc,c).
            s =   a * fb * fc / ((fa - fb) * (fa - fc))
                + b * fa * fc / ((fb - fa) * (fb - fc))
                + c * fa * fb / ((fc - fa) * (fc - fb));
        }
        else
        {
            s = b - fb * (b - a) / (fb - fa);   // Sekante
        }

        const double lo = std::min ((3.0 * a + b) * 0.25, b);
        const double hi = std::max ((3.0 * a + b) * 0.25, b);

        const bool useBisect = (s < lo || s > hi)
                            || (mflag  && std::abs (s - b) >= std::abs (b - c) * 0.5)
                            || (! mflag && std::abs (s - b) >= std::abs (c - d) * 0.5)
                            || (mflag  && std::abs (b - c) < tol)
                            || (! mflag && std::abs (c - d) < tol);

        if (useBisect)
        {
            s = (a + b) * 0.5;
            mflag = true;
        }
        else
        {
            mflag = false;
        }

        const double fs = F (s);

        d = c;
        c = b;
        fc = fb;

        if (oppositeSign (fa, fs))
        {
            b = s;
            fb = fs;
        }
        else
        {
            a = s;
            fa = fs;
        }

        if (std::abs (fa) < std::abs (fb))
        {
            std::swap (a, b);
            std::swap (fa, fb);
        }
    }

    return b;
}

// Sucht ausgehend von start ein Bracket. Die Schrittweite ist wieder die
// Lipschitz-Schrittweite |F|/Lip: näher als das kann keine Wurzel liegen,
// also wird nichts übersprungen und weit weg wird grob gesprungen.
// dirHint zeigt in die Richtung, in der die Wurzel nach der linearisierten
// Ableitung F' = -c*(1 - M_r) liegen muss; die Gegenrichtung wird nur
// versucht, wenn das nicht aufgeht.
bool findBracket (const Residual& F, double start, double dirHint,
                  double lo, double hi, double lip, double minStep, double budget,
                  double& outA, double& outB, double& outFa, double& outFb)
{
    const double f0 = F (start);

    if (f0 == 0.0)
    {
        outA = outB = start;
        outFa = outFb = 0.0;
        return true;
    }

    const double dirs[2] = { dirHint >= 0.0 ? 1.0 : -1.0,
                             dirHint >= 0.0 ? -1.0 : 1.0 };

    for (int k = 0; k < 2; ++k)
    {
        const double dir = dirs[k];

        double t = start, ft = f0, travelled = 0.0;

        while (travelled < budget)
        {
            const double step = std::max (std::abs (ft) / lip, minStep);
            double tNext = t + dir * step;
            tNext = std::min (std::max (tNext, lo), hi);

            if (tNext == t)
                break;

            const double fNext = F (tNext);

            if (fNext == 0.0)
            {
                outA = outB = tNext;
                outFa = outFb = 0.0;
                return true;
            }

            if (oppositeSign (ft, fNext))
            {
                outA  = std::min (t, tNext);
                outB  = std::max (t, tNext);
                outFa = (outA == t) ? ft : fNext;
                outFb = (outB == t) ? ft : fNext;
                return true;
            }

            travelled += std::abs (tNext - t);
            t  = tNext;
            ft = fNext;
        }
    }

    return false;
}

} // namespace

void RetardedTimeSolver::reset()
{
    branchCount      = 0;
    nextId           = 0;
    droppedRootCount = 0;

    // evalCount bleibt stehen: er misst die geleistete Arbeit über einen
    // Messlauf hinweg, und ein Positionssprung mitten im Lauf ist Teil dieser
    // Arbeit. Genullt wird ausdrücklich per clearResidualEvaluations().
}

void RetardedTimeSolver::setMinScanStep (double seconds)
{
    minStep   = std::max (1.0e-6, seconds);
    trackStep = std::max (1.0e-7, minStep / 64.0);
}

int RetardedTimeSolver::solve (const SourceTrajectory& traj,
                               const MediumState& medium,
                               Vec3 receiverPos,
                               double t_h,
                               Root* outRoots,
                               int maxRoots,
                               bool allowFullScan)
{
    if (outRoots == nullptr || maxRoots <= 0)
        return 0;

    const double c       = medium.speedOfSound();
    const double tOldest = traj.oldestTime();
    const double tNewest = traj.newestTime();

    {
        // Noch nicht per reset()/jumpTo() befüllt -> keine Aussage möglich.
        Vec3 p, v;
        if (! traj.sampleAt (tNewest, p, v))
        {
            branchCount = 0;
            return 0;
        }
    }

    if (t_h < tOldest)
    {
        branchCount = 0;
        return 0;
    }

    const Residual F { traj, receiverPos, t_h, c, tOldest, tNewest, evalCount };

    // Fenster: oben durch t_h begrenzt - wegen |·| >= 0 liegt jede Wurzel bei
    // t_e <= t_h, Kausalität ist eingebaut.
    //
    // Nach unten reicht die längste überhaupt mögliche Laufzeit. Jede Wurzel
    // erfüllt c*(t_h - t_e) = R(t_e), und R ist durch
    //   R(t) = |L - M(t)| <= |L| + max |M(t)|
    // nach oben beschränkt (Dreiecksungleichung, max über das Fenster aus der
    // Deque der Trajektorie). Für t_e < t_h - R_max/c ist deshalb
    // F(t_e) = c*(t_h - t_e) - R(t_e) > 0, dort kann keine Wurzel liegen.
    //
    // Das ist keine Heuristik, sondern dieselbe Kausalität wie oben, nur von
    // der anderen Seite: der Puffer ist nach der GRÖSSTEN Feldgröße bemessen
    // (Plan 2.12, bei n = 10000 rund 42 s), gehört wird aber meist auf einem
    // kleinen Feld, wo die längste Laufzeit unter einer Sekunde liegt. Ohne
    // die Eingrenzung liefe der Scan trotzdem jedes Mal über die vollen 42 s.
    //
    // Der physikalisch echte Fall "verspäteter Boom aus großer Distanz" bleibt
    // dabei vollständig drin: ein Boom, der erst jetzt eintrifft, hat genau
    // t_e = t_h - R(t_e)/c >= t_h - R_max/c, liegt also im Fenster. Nur der
    // Bereich, in dem der Schall längst vorbeigezogen ist, fällt weg.
    //
    // Nebeneffekt: F(windowStart) >= 0 ist damit garantiert, solange der
    // Puffer lang genug ist - genau die Randbedingung aus Plan 2.6.
    //
    // Die Schranke darf danach mit dem bereits verkleinerten Fenster noch
    // einmal gebildet werden, und das bleibt exakt: liegt eine Wurzel in
    // [windowStart, t_h], dann ist R(t_e) <= |L| + max{|M(t)| : t in genau
    // diesem Fenster}, also t_e >= t_h - dieser neuen Schranke / c. Bei einer
    // Quelle, die vor einer Weile weit weg war und jetzt nah ist, schrumpft
    // das Fenster dadurch deutlich. Zwei Durchgänge reichen; jeder kostet nur
    // eine Binärsuche in der Deque.
    double windowStart = tOldest;

    for (int refine = 0; refine < 3; ++refine)
    {
        const double rMax  = receiverPos.length() + traj.maxDistanceSince (windowStart);
        const double next  = std::max (tOldest, t_h - rMax / c);

        if (next <= windowStart)
            break;

        windowStart = next;
    }

    const double windowEnd = t_h;

    // Schritt 1, Schnelltest (Plan 2.10): Binärsuche über die monotone Deque.
    // Die ist am jüngsten Schreibzeitpunkt verankert, kann bei einem t_h aus
    // der Vergangenheit also auch spätere Geschwindigkeiten mitzählen. Der
    // Fehler geht in die harmlose Richtung: lieber ein Vollscan zu viel als ein
    // übersehenes Wurzelpaar.
    //
    // Gefragt wird über das SUCHFENSTER, nicht über die ganze Historie. F wird
    // ausschließlich in [windowStart, t_h] ausgewertet (der Rückwärtsscan läuft
    // dort, findBracket klemmt dorthin), also gilt dort auch |F'| <= c + |v_M|
    // mit dem Maximum aus genau diesem Bereich. Und ist die Quelle im Fenster
    // durchweg langsamer als der Schall, fällt F dort streng monoton - dann
    // gibt es genau eine Wurzel, ganz gleich, wie schnell die Quelle vor zehn
    // Sekunden war.
    //
    // Das war die zweite Hälfte des "Hangover"-Problems: der Stride-Fix hat den
    // Löser nach einem Überschall-Moment wieder seltener gerufen, aber jeder
    // einzelne Aufruf lief bis zu 40 s lang weiter im teuren Vollscan-Modus,
    // mit einer aus derselben alten Spitze aufgeblähten Lipschitz-Schranke und
    // dadurch unnötig feinen Schritten. Der Puffer ist nach der GRÖSSTEN
    // Feldgröße bemessen (rund 42 s), gehört wird meist auf einem kleinen Feld
    // mit einem Fenster unter einer Sekunde.
    const double maxSpeed           = traj.maxSpeedSince (windowStart);
    const bool   supersonicPossible = (maxSpeed > c);
    const double lip                = c + maxSpeed;   // |F'| <= c + |v_M|

    // Deutlich unter einem Sample (1/96000 s = 1,04e-5 s), damit die
    // Verzögerung selbst bei 96 kHz nicht sichtbar quantisiert.
    const double brentTol = 1.0e-10;

    // Zwei Wurzeln, die näher beieinander liegen, sind die Doppelwurzel auf
    // der Mach-Front; sie zusammenzufassen ist harmlos, weil die Amplitude
    // dort ohnehin über eps regularisiert wird (Plan 2.7).
    const double dedupeTol = 1.0e-7;

    struct Candidate { double t_e; int id; };
    Candidate cand[scanCapacity];
    int nCand = 0;

    auto addCandidate = [&] (double t, int id)
    {
        for (int i = 0; i < nCand; ++i)
        {
            if (std::abs (cand[i].t_e - t) < dedupeTol)
            {
                if (cand[i].id < 0)
                    cand[i].id = id;   // die verfolgte Identität gewinnt
                else if (id >= 0 && cand[i].id != id)
                    ++collapsedTracks; // zwei Zweige auf derselben Wurzel

                return;
            }
        }

        if (nCand < scanCapacity)
            cand[nCand++] = { t, id };
        else
            ++droppedRootCount;
    };

    // Schritt 2 (Plan 2.10): bekannte Zweige nachführen. Immer, weil billig -
    // und weil nur so die ids über Aufrufe hinweg stabil bleiben.
    //
    // BEFUND zum Flackern im Überschall (@dpa 20260819), damit niemand die
    // beiden schon geprüften Wege noch einmal geht:
    //
    // Gemessen landen zwei nachgeführte Zweige 291 Mal auf DERSELBEN Wurzel.
    // Das Zusammenfassen wirft dann eine der beiden Identitäten weg, aus drei
    // Hörwegen wird einer, und der nächste Vollscan lässt das Paar mit NEUEN
    // Identitäten wiederauferstehen - mit frischer Hüllkurve und frischem
    // Filter. Dazu passen 295 neue Identitäten und 342 Wechsel der Wurzelzahl,
    // wo ein sauberer Überflug mit zwei auskommt.
    //
    // Zwei naheliegende Abhilfen wurden gebaut, gemessen und wieder verworfen:
    //
    //   1. Suchfenster nach unten auf die Wurzel des vorigen Zweigs begrenzen.
    //      Die Kollisionen gingen damit auf null - aber die Suche fand die
    //      eigene Wurzel dann gar nicht mehr (verlorene Nachführungen 4 -> 293),
    //      und die harten Abbrüche stiegen von 41,4 auf 48,3 %.
    //   2. Den Deckel auf |1 - M_r| von 0,05 auf 0,005 verfeinern, damit
    //      Startwert und Suchbudget der echten Wanderung folgen. Ohne jede
    //      messbare Wirkung.
    //
    // Beides zusammen heisst: der Startwert ist nicht bloss zu kurz, die Wurzel
    // liegt am Ende eines Solver-Schritts schlicht woanders, als eine
    // Fortschreibung aus dem letzten Zustand hergibt. Der nächste Ansatz muss
    // deshalb an der Schrittweite ansetzen (im Überschall häufiger lösen, damit
    // die Wurzel je Schritt weniger weit wandert), nicht an der Suche.

    for (int i = 0; i < branchCount; ++i)
    {
        const Branch& b = branches[i];

        const double dt_h = t_h - b.lastT_h;

        // dt_e/dt_h = 1/(1 - M_r) (Plan 2.11). Direkt an der Mach-Front geht
        // der Nenner gegen null und die Wurzel wandert entsprechend weit.
        //
        // Ein Deckel von 0,05 begrenzte die vorhergesagte Wanderung auf das
        // 20-fache eines Solver-Schritts. In Wirklichkeit sind an der Kaustik
        // einige hundert drin (für Mach 2 in 150 m Abstand rund 281). Der
        // Startwert läge damit systematisch zu kurz, und weil das Suchbudget
        // aus derselben Grösse gebildet wird (siehe budget unten), reichte auch
        // die Suche nicht bis zur echten Wurzel. Sie fand dann entweder die des
        // Nachbarn oder gar keine - beides ein Zweigtod bei vollem Pegel.
        //
        // Ihn auf 0,005 zu verfeinern (Wanderung bis zum 200-fachen, Budget
        // skaliert mit) wurde gemessen und blieb ohne jede Wirkung - siehe
        // Befund oben. Er steht deshalb weiter auf 0,05.
        double denom = 1.0 - b.machRadial;
        if (std::abs (denom) < denomFloor)
            denom = (denom < 0.0 ? -denomFloor : denomFloor);

        double guess = b.t_e + dt_h / denom;
        guess = std::min (std::max (guess, windowStart), windowEnd);

        // F' = -c*(1 - M_r): bei F > 0 und fallendem F liegt die Wurzel
        // rechts, bei steigendem F links - also dir = sign(F)*sign(1 - M_r).
        const double fGuess  = F (guess);
        const double dirHint = (fGuess >= 0.0 ? 1.0 : -1.0) * (denom >= 0.0 ? 1.0 : -1.0);

        const double budget = std::max (32.0 * minStep, 8.0 * std::abs (dt_h / denom));

        double a, bb, fa, fb;

        if (findBracket (F, guess, dirHint, windowStart, windowEnd, lip, trackStep, budget,
                         a, bb, fa, fb))
            addCandidate (brent (F, a, bb, fa, fb, brentTol, 80), b.id);
        else
            ++trackLost;   // Zweig verschwindet, er wird nicht mehr gemeldet
    }

    if (! supersonicPossible)
    {
        // Schritt 3 (Plan 2.10): kein Überschall im Fenster und genau ein
        // lebender Zweig -> fertig, Eindeutigkeit ist in Plan 2.4 bewiesen
        // (F' = -c*(1 - M_r) < 0 für |v_M| < c, F fällt streng monoton).
        if (nCand != 1)
        {
            // Kein oder mehr als ein nachgeführter Zweig (erster Aufruf,
            // Sprung, oder Reste aus einer Überschallphase): dieselbe
            // Monotonie erlaubt ein einziges Bracket über das ganze Fenster.
            nCand = 0;

            const double fLo = F (windowStart);
            const double fHi = F (windowEnd);   // = -R <= 0

            if (fLo >= 0.0 && fHi <= 0.0)
                addCandidate (brent (F, windowStart, windowEnd, fLo, fHi, brentTol, 200), -1);
            // fLo < 0 hiesse: Puffer kürzer als die Laufzeit. Dann gibt es
            // im Fenster keine Wurzel; die Stelle ist bewusst still statt
            // falsch (Plan 2.6 dimensioniert T_max so, dass das nicht auftritt).
        }
    }
    else if (allowFullScan || nCand == 0)
    {
        // Schritt 4a: geschlossene Lösung, wenn das ganze Suchfenster in einer
        // geradlinig gleichförmigen Phase der Bahn liegt.
        //
        // Genau das ist der Vorbeiflug, und genau der ist der teuerste Fall:
        // an der Mach-Front wird das Residuum flach, die Lipschitz-Schritte
        // fallen auf die Mindestweite, und der Vollscan tastet ein mehrere
        // Sekunden langes Fenster im Millisekundenraster ab. Gemessen kostete
        // ein einzelner Block dort 13211 us bei 10667 us Budget - der Ton setzt
        // ausgerechnet dort aus, wo der Mach-Kegel eintrifft.
        //
        // Hier gibt es nichts zu suchen: die Gleichung ist eine quadratische
        // (siehe StraightLineRetardedTime.h). Nachgeprüft wird trotzdem, und
        // zwar am echten Residuum gegen die tatsächlich geschriebene Bahn -
        // hält eine Wurzel dem nicht stand, übernimmt der Vollscan.
        bool solvedInClosedForm = false;

        if (traj.linearSince() <= windowStart)
        {
            Vec3 linePoint, lineVelocity;
            traj.linearMotion (linePoint, lineVelocity);

            const straightline::Roots exact =
                straightline::solve (linePoint, lineVelocity, receiverPos, c, t_h);

            // Die Formel gilt fuer die IDEALE Gerade, geschrieben wird die
            // geglaettete Bahn. Jede Wurzel wird deshalb gegen das echte
            // Residuum nachgezogen: ein Bracket um den Startwert herum
            // aufziehen und darin genau lösen. Weil der Startwert schon nahe
            // liegt, sind das eine Handvoll Auswertungen statt eines Vollscans
            // ueber mehrere Sekunden.
            //
            // Findet auch nur eine Wurzel kein Bracket, gilt der ganze Versuch
            // als gescheitert und der Vollscan uebernimmt. Lieber einmal zu oft
            // suchen als einen Hoerweg verlieren.
            constexpr double residualTolMetres = 1.0e-6;

            solvedInClosedForm = exact.count > 0;

            double refined[2] {};
            int    nRefined = 0;

            for (int i = 0; i < exact.count && solvedInClosedForm; ++i)
            {
                const double guess  = std::min (windowEnd, std::max (windowStart, exact.t_e[i]));
                const double fGuess = F (guess);

                if (std::abs (fGuess) <= residualTolMetres)
                {
                    refined[nRefined++] = guess;
                    continue;
                }

                // Schrittweite aus derselben Lipschitz-Schranke wie im
                // Vollscan: naeher als |F|/lip kann die Wurzel nicht liegen.
                const double step = std::max (std::abs (fGuess) / lip, minStep);

                bool bracketed = false;

                for (int k = 1; k <= 8 && ! bracketed; ++k)
                {
                    const double lo = std::max (windowStart, guess - (double) k * step);
                    const double hi = std::min (windowEnd,   guess + (double) k * step);

                    const double fLo = F (lo);
                    const double fHi = F (hi);

                    if (oppositeSign (fLo, fGuess))
                    {
                        refined[nRefined++] = brent (F, lo, guess, fLo, fGuess, brentTol, 60);
                        bracketed = true;
                    }
                    else if (oppositeSign (fGuess, fHi))
                    {
                        refined[nRefined++] = brent (F, guess, hi, fGuess, fHi, brentTol, 60);
                        bracketed = true;
                    }
                }

                if (! bracketed)
                    solvedInClosedForm = false;
            }

            if (solvedInClosedForm)
            {
                for (int i = 0; i < nRefined; ++i)
                    addCandidate (refined[i], -1);
            }
        }

        if (! solvedInClosedForm)
        {
            // Schritt 4b (Plan 2.10): Vollscan mit Lipschitz-Sprüngen. F ist
            // Lipschitz-stetig mit Lip = c + vmax; ist |F(t)| > Lip*step, kann
            // im Intervall keine Nullstelle liegen. Weit weg grosse Sprünge,
            // nahe an der Wurzel automatisch fein.
            double t     = windowEnd;
            double fPrev = F (t);

            if (fPrev == 0.0)
                addCandidate (t, -1);

            while (t > windowStart)
            {
                const double step = std::max (std::abs (fPrev) / lip, minStep);

                double tNext = t - step;
                if (tNext < windowStart)
                    tNext = windowStart;

                const double fNext = F (tNext);

                if (fNext == 0.0)
                    addCandidate (tNext, -1);
                else if (oppositeSign (fPrev, fNext))
                    addCandidate (brent (F, tNext, t, fNext, fPrev, brentTol, 80), -1);

                fPrev = fNext;
                t     = tNext;
            }
        }
    }

    if (nCand == 0)
    {
        branchCount = 0;
        return 0;
    }

    std::sort (cand, cand + nCand,
               [] (const Candidate& x, const Candidate& y) { return x.t_e < y.t_e; });

    // Messung, siehe rootCountBucket()/tightPairCount().
    ++rootHistogram[nCand < 8 ? nCand : 7];

    if (lastCandCount >= 0 && nCand != lastCandCount)
        ++countFlips;

    lastCandCount = nCand;

    for (int i = 1; i < nCand; ++i)
    {
        ++adjacentPairs;

        if (cand[i].t_e - cand[i - 1].t_e < 1.0e-3)
            ++tightPairs;
    }

    // Überlauf über K verwerfen: die ältesten Emissionen fliegen raus, weil
    // die jüngeren den aktuellen Klang tragen.
    if (nCand > maxBranches)
    {
        const int drop = nCand - maxBranches;
        droppedRootCount += drop;

        for (int i = 0; i < maxBranches; ++i)
            cand[i] = cand[i + drop];

        nCand = maxBranches;
    }

    Root roots[maxBranches];

    for (int i = 0; i < nCand; ++i)
    {
        Vec3 p, v;
        F.sample (cand[i].t_e, p, v);

        const Vec3   toReceiver = receiverPos - p;
        const double R          = toReceiver.length();
        const Vec3   uHat       = toReceiver.normalised();   // û, Plan 2.4

        roots[i].t_e        = cand[i].t_e;
        roots[i].R          = R;
        roots[i].machRadial = uHat.dot (v) / c;   // M_r = û·v_M / c
        roots[i].id         = cand[i].id;
    }

    // Zweige abgleichen (Plan 2.10 Schritt 5). Wurzeln aus dem Nachführen
    // tragen ihre id schon; für die aus dem Vollscan wird die Fortschreibung
    // b.t_e + dt_h/(1 - M_r) als Zuordnungsmass genommen, damit ein Zweig
    // seine Identität behält, auch wenn ihn diesmal nur der Scan gefunden hat.
    bool branchTaken[maxBranches] = {};

    for (int i = 0; i < nCand; ++i)
    {
        if (roots[i].id < 0)
            continue;

        for (int j = 0; j < branchCount; ++j)
            if (branches[j].id == roots[i].id)
                branchTaken[j] = true;
    }

    double nearestRatio[maxBranches];

    for (int i = 0; i < maxBranches; ++i)
        nearestRatio[i] = 1.0e30;

    for (int i = 0; i < nCand; ++i)
    {
        if (roots[i].id >= 0)
            continue;

        int    best     = -1;
        double bestDist = 0.0;

        for (int j = 0; j < branchCount; ++j)
        {
            if (branchTaken[j])
                continue;

            const double dt_h = t_h - branches[j].lastT_h;

            double denom = 1.0 - branches[j].machRadial;
            if (std::abs (denom) < 0.05)
                denom = (denom < 0.0 ? -0.05 : 0.05);

            const double predicted = branches[j].t_e + dt_h / denom;
            const double dist      = std::abs (roots[i].t_e - predicted);

            // Grosszügig: lieber eine Identität halten als einen Zweig
            // grundlos neu starten zu lassen (das hörte man als Aussetzer).
            const double tol = std::max (8.0 * minStep, 8.0 * std::abs (dt_h / denom));

            // Auch wenn die Zuordnung scheitert: wie knapp war es? Siehe
            // newIdNearCount().
            if (i < maxBranches && tol > 0.0)
                nearestRatio[i] = std::min (nearestRatio[i], dist / tol);

            if (dist < tol && (best < 0 || dist < bestDist))
            {
                best     = j;
                bestDist = dist;
            }
        }

        if (best >= 0)
        {
            roots[i].id        = branches[best].id;
            branchTaken[best]  = true;
        }
    }

    // Nicht gangbar: Zuordnung über die REIHENFOLGE beider Listen
    // (beide sind nach t_e sortiert, zwei Wurzeln können einander nicht
    // überholen ohne vorher zu verschmelzen). Die Idee scheitert an der
    // Bedingung, unter der sie gelten würde - gleich viele Wurzeln wie zuletzt
    // bekannte Zweige. Genau dann gibt es aber gar keine unzugeordnete Wurzel:
    // das Nachführen liefert für jeden bekannten Zweig einen Kandidaten, eine
    // identitätslose Wurzel kommt also immer OBEN DRAUF, und nCand ist dann
    // zwangsläufig grösser als branchCount. Gemessen hat die Zuordnung in
    // keinem einzigen Fall gegriffen.
    //
    // Der Befund dahinter steht: 97,6 % der neuen Identitäten liegen NICHT
    // knapp neben der Vorhersage eines bekannten Zweigs, sondern weit weg. An
    // der Toleranz zu drehen bringt also nichts. Der Löser findet im Überschall
    // tatsächlich rund 60 neue Wurzeln je Sekunde - ob die echt sind, ist die
    // offene Frage.

    // Unterschall hat genau eine Wurzel (Plan 2.4). Ist genau ein Zweig
    // bekannt, ist es zwingend derselbe - unabhängig davon, wie weit die
    // Fortschreibung danebenlag.
    if (! supersonicPossible && nCand == 1 && branchCount == 1 && roots[0].id < 0)
        roots[0].id = branches[0].id;

    for (int i = 0; i < nCand; ++i)
        if (roots[i].id < 0)
        {
            roots[i].id = nextId++;
            ++newIdGiven;

            if (i < maxBranches && nearestRatio[i] < 2.0)
                ++newIdNear;
        }

    branchCount = nCand;

    for (int i = 0; i < nCand; ++i)
    {
        branches[i].id         = roots[i].id;
        branches[i].t_e        = roots[i].t_e;
        branches[i].machRadial = roots[i].machRadial;
        branches[i].R          = roots[i].R;
        branches[i].lastT_h    = t_h;
    }

    int written = nCand;

    if (written > maxRoots)
    {
        droppedRootCount += written - maxRoots;
        written = maxRoots;
    }

    // Bei zu kleinem Ausgabepuffer wieder die jüngsten Emissionen behalten.
    const int offset = nCand - written;

    for (int i = 0; i < written; ++i)
        outRoots[i] = roots[i + offset];

    return written;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
