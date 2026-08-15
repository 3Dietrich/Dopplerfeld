#pragma once

#include <cmath>

// Reiner Wertetyp für Positionen, Geschwindigkeiten, Verschiebungen - Meter
// als Einheit überall. Bewusst ohne JUCE-Abhängigkeit (siehe Plan 3.1), damit
// die gesamte Physik offline und ohne Audiothread testbar bleibt.
//
// Weltachsen (Plan 2.1): x nach rechts, y nach vorn, z nach oben. z ist in
// Phase 1 überall 0, wird aber mitgeführt - kein Code hier darf annehmen,
// dass z konstant bleibt.
struct Vec3
{
    double x = 0.0, y = 0.0, z = 0.0;

    constexpr Vec3() = default;
    constexpr Vec3 (double xIn, double yIn, double zIn) : x (xIn), y (yIn), z (zIn) {}

    Vec3& operator+= (const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-= (const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*= (double s)      { x *= s;   y *= s;   z *= s;   return *this; }

    double dot (const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    double lengthSquared() const { return x * x + y * y + z * z; }
    double length() const { return std::sqrt (lengthSquared()); }

    // Bei Nulllänge bleibt der Vektor {0,0,0} statt NaN zu erzeugen - ein
    // ruhender Punkt hat keine Richtung, das ist kein Fehlerfall.
    Vec3 normalised() const
    {
        const double len = length();
        if (len <= 0.0)
            return {};
        return { x / len, y / len, z / len };
    }
};

inline Vec3 operator+ (Vec3 a, const Vec3& b) { a += b; return a; }
inline Vec3 operator- (Vec3 a, const Vec3& b) { a -= b; return a; }
inline Vec3 operator- (const Vec3& a) { return { -a.x, -a.y, -a.z }; }
inline Vec3 operator* (Vec3 a, double s) { a *= s; return a; }
inline Vec3 operator* (double s, Vec3 a) { a *= s; return a; }

// Freie Variante zusätzlich zur Member-Funktion, falls sich das an anderer
// Stelle lesbarer schreibt (dot(a, b) statt a.dot(b)).
inline double dot (const Vec3& a, const Vec3& b) { return a.dot (b); }
