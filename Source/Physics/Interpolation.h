#pragma once

// Freie Interpolationsfunktionen, reines C++, keine JUCE-Abhängigkeit.
// Als Templates geschrieben, weil dieselbe Catmull-Rom- und
// Hermite-Mathematik sowohl auf Vec3 (Positionen/Geschwindigkeiten) als
// auch auf double (z.B. Verzögerungszeit τ) passt - Vec3 stellt dafür
// operator+, operator- und operator*(double) bereit (siehe Vec3.h).

// Catmull-Rom-Spline durch p1..p2, mit p0/p3 als Tangentenstützen davor/
// danach. t in [0,1] läuft von p1 (t=0) nach p2 (t=1). Liefert eine C1-
// stetige Kurve zwischen den Stützstellen (Plan 2.10: "damit M(t)
// C1-stetig ist" - eine nur linear interpolierte Trajektorie hätte an
// jeder Stützstelle einen Geschwindigkeitssprung).
template <typename T>
T catmullRom (const T& p0, const T& p1, const T& p2, const T& p3, double t)
{
    const double t2 = t * t;
    const double t3 = t2 * t;

    return (p1 * 2.0
            + (p2 - p0) * t
            + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * t2
            + (p1 * 3.0 - p0 - p2 * 3.0 + p3) * t3) * 0.5;
}

// Lagrange-Interpolation 4. Ordnung (5 Stützstellen, Grad-4-Polynom) durch
// y0..y4 an den ganzzahligen Stellen -2..+2. frac in [0,1] liegt zwischen
// y2 (Stelle 0) und y3 (Stelle 1) - zwei Stützstellen vor und zwei danach,
// symmetrisch um das Zielintervall (Plan 3.3: "Lagrange 4. Ordnung" fürs
// Auslesen von SourceSignalBuffer). Linear wäre hier zu dumpf, siehe Plan
// 3.3.
template <typename T>
T lagrange4 (T y0, T y1, T y2, T y3, T y4, double frac)
{
    const T y[5] = { y0, y1, y2, y3, y4 };
    const double node[5] = { -2.0, -1.0, 0.0, 1.0, 2.0 };

    T result{};

    for (int i = 0; i < 5; ++i)
    {
        double basis = 1.0;

        for (int j = 0; j < 5; ++j)
        {
            if (j == i)
                continue;

            basis *= (frac - node[j]) / (node[i] - node[j]);
        }

        result = result + T (y[i] * basis);
    }

    return result;
}

// Kubische Hermite-Interpolation zwischen p0 (t=0) und p1 (t=1) mit
// expliziten Endableitungen m0, m1 (bereits auf das Intervall skaliert,
// d.h. m = d/dt, nicht d/dx). Für die Verzögerungsinterpolation aus Plan
// 2.11: der Löser liefert pro Solver-Punkt nicht nur τ, sondern auch
// dτ/dt_h, damit der interpolierte Verzögerungsverlauf C1-stetig ist und
// der Doppler zwischen zwei Solver-Punkten nicht springt.
template <typename T>
T hermite (const T& p0, const T& m0, const T& p1, const T& m1, double t)
{
    const double t2 = t * t;
    const double t3 = t2 * t;

    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;

    return p0 * h00 + m0 * h10 + p1 * h01 + m1 * h11;
}
