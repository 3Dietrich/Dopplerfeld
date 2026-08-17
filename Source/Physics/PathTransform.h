#pragma once

#include "Vec3.h"

#include <cmath>

// Abbildung eines Empfangspunkts in das Koordinatensystem eines Ausbreitungs-
// pfades (Plan 3.4). Für den Direktschall ist das die Identität; jede
// Reflexion ist die Spiegelung an einer Ebene und damit dieselbe Struktur mit
// anderer Belegung - Boden und Wände sind kein Sonderweg, sondern weitere
// PropagationPath-Instanzen.
//
// Gespeichert wird eine allgemeine affine Abbildung x' = A x + offset, A
// zeilenweise. Der Grund für die Allgemeinheit ist die Mehrfachreflexion:
// eine einzelne Spiegelung ließe sich knapper als (Normale, Ebenenabstand)
// hinschreiben, aber die VERKETTUNG zweier Spiegelungen ist keine Spiegelung
// mehr, sondern eine Drehung (bei sich schneidenden Ebenen) bzw. eine
// Verschiebung (bei parallelen). Beides ist wieder eine Isometrie und damit
// zulässig - nur eben nicht mehr als Normale darstellbar.
//
// Bewusst JUCE-frei wie die restliche Physics-Schicht.
struct PathTransform
{
    // Zeilen der linearen Abbildung. Vorbelegt mit der Einheitsmatrix, also
    // der Identität - das ist der Direktschall.
    Vec3 row0 { 1.0, 0.0, 0.0 };
    Vec3 row1 { 0.0, 1.0, 0.0 };
    Vec3 row2 { 0.0, 0.0, 1.0 };

    Vec3 offset { 0.0, 0.0, 0.0 };

    float gain = 1.0f;
};

// Spiegelung an einer beliebigen Ebene durch pointOnPlane mit der (nicht
// notwendig normierten) Normalen n:
//
//   x' = x - 2 (n·x - d) n   mit d = n·pointOnPlane
//      = (I - 2 n nᵀ) x + 2 d n
//
// Gespiegelt wird der EMPFÄNGER, nicht die Quelle - das Ergebnis ist dasselbe
// und geometrisch exakt gleichwertig: eine Spiegelung ist eine Isometrie, also
// gilt |σ(L) - M| = |L - σ(M)| für jeden Zeitpunkt, und weil σ zeitunabhängig
// und linear ist, gilt dasselbe für die Zeitableitung - also auch für M_r und
// damit den Doppler. Der Empfänger ist der Parameter, den PropagationPath
// ohnehin pro Solver-Punkt hereinbekommt; die Quelle liegt in der geteilten
// Trajektorie, die alle Pfade gemeinsam lesen und die deshalb nicht pfadweise
// gespiegelt werden kann, ohne sie zu vervielfachen.
//
// gain bleibt 1: die Fläche reflektiert die Amplitude vollständig, der Verlust
// steckt in der Höhendämpfung (PropagationPath::setReflectionDamping).
inline PathTransform planeMirrorTransform (Vec3 n, Vec3 pointOnPlane)
{
    const Vec3   u = n.normalised();
    const double d = u.dot (pointOnPlane);

    PathTransform t;

    t.row0 = { 1.0 - 2.0 * u.x * u.x,      - 2.0 * u.x * u.y,       - 2.0 * u.x * u.z };
    t.row1 = {     - 2.0 * u.y * u.x,  1.0 - 2.0 * u.y * u.y,       - 2.0 * u.y * u.z };
    t.row2 = {     - 2.0 * u.z * u.x,      - 2.0 * u.z * u.y,   1.0 - 2.0 * u.z * u.z };

    t.offset = u * (2.0 * d);

    return t;
}

// Der Boden: Ebene z = 0, Normale nach oben.
inline PathTransform groundMirrorTransform()
{
    return planeMirrorTransform (Vec3 { 0.0, 0.0, 1.0 }, Vec3 { 0.0, 0.0, 0.0 });
}

// Eine Wand als unendliche Ebene, beschrieben so, wie man sie in der Draufsicht
// hinstellt:
//
//   anchor   - ein Punkt, durch den die Wand läuft (Fußpunkt am Boden)
//   azimuth  - Richtung der Wandlinie in der Draufsicht [rad]; 0 = die Wand
//              läuft entlang der x-Achse
//   tilt     - Neigung um genau diese Linie [rad]; 0 = senkrecht stehend,
//              π/2 = flach liegend (dann ist sie eine zweite Bodenebene)
//
// Herleitung der Normalen: die Wandlinie ist d = (cos a, sin a, 0). Senkrecht
// dazu stehen n0 = (-sin a, cos a, 0) und ẑ, und wegen d × n0 = ẑ bilden die
// beiden eine Orthonormalbasis der Ebene senkrecht zu d. Die Wand um d zu
// neigen heißt deshalb, n0 in dieser Basis zu drehen:
//
//   n = cos(tilt) n0 + sin(tilt) ẑ
//
// Bei tilt = π/2 fällt das auf ẑ zurück, also exakt auf den Bodenfall - die
// Formel ist damit an ihrem Grenzfall überprüfbar.
inline PathTransform wallMirrorTransform (Vec3 anchor, double azimuthRad, double tiltRad)
{
    const double sa = std::sin (azimuthRad);
    const double ca = std::cos (azimuthRad);
    const double st = std::sin (tiltRad);
    const double ct = std::cos (tiltRad);

    const Vec3 n { -sa * ct, ca * ct, st };

    return planeMirrorTransform (n, anchor);
}

// Verkettung: erst inner, dann outer, also x -> outer(inner(x)).
//
//   A = A_outer A_inner,  offset = A_outer offset_inner + offset_outer
//
// Damit wird aus zwei Reflexionen eine Abbildung, und die Mehrfachreflexion
// ist nur ein weiterer PropagationPath - kein Rückkopplungsweg, keine
// Rekursion, nichts, was aufschwingen könnte (siehe DopplerEngine).
inline PathTransform composeTransforms (const PathTransform& outer, const PathTransform& inner)
{
    // Spalten von A_inner, damit die Produkte als Skalarprodukte lesbar
    // bleiben.
    const Vec3 col0 { inner.row0.x, inner.row1.x, inner.row2.x };
    const Vec3 col1 { inner.row0.y, inner.row1.y, inner.row2.y };
    const Vec3 col2 { inner.row0.z, inner.row1.z, inner.row2.z };

    PathTransform t;

    t.row0 = { outer.row0.dot (col0), outer.row0.dot (col1), outer.row0.dot (col2) };
    t.row1 = { outer.row1.dot (col0), outer.row1.dot (col1), outer.row1.dot (col2) };
    t.row2 = { outer.row2.dot (col0), outer.row2.dot (col1), outer.row2.dot (col2) };

    t.offset = Vec3 { outer.row0.dot (inner.offset),
                      outer.row1.dot (inner.offset),
                      outer.row2.dot (inner.offset) } + outer.offset;

    t.gain = outer.gain * inner.gain;

    return t;
}

// Empfängerposition abbilden. Wird einmal je Teilblock und Pfad gerufen, nicht
// je Sample - die neun Multiplikationen fallen neben dem Löser nicht auf,
// deshalb gibt es hier keine Abkürzung für den Identitätsfall.
inline Vec3 applyPathTransform (const PathTransform& t, Vec3 receiverPos)
{
    return { t.row0.dot (receiverPos) + t.offset.x,
             t.row1.dot (receiverPos) + t.offset.y,
             t.row2.dot (receiverPos) + t.offset.z };
}

// Dieselbe Abbildung für eine Geschwindigkeit. Der offset fällt weg, weil er
// eine Konstante ist und beim Ableiten nach der Zeit verschwindet; der lineare
// Anteil wirkt dagegen sehr wohl - ein nach oben laufendes Ohr bewegt sich im
// Bodenbild nach unten. Ohne diese zweite Funktion wäre die Bewegung des
// Spiegelempfängers falsch und mit ihr der Doppler der Reflexion.
inline Vec3 applyPathTransformVelocity (const PathTransform& t, Vec3 receiverVel)
{
    return { t.row0.dot (receiverVel),
             t.row1.dot (receiverVel),
             t.row2.dot (receiverVel) };
}
