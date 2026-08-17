#pragma once

#include "Vec3.h"

#include <cmath>

// Abbildung eines Empfangspunkts in das Koordinatensystem eines Ausbreitungs-
// pfades (Plan 3.4). Für den Direktschall ist das die Identität; jede
// Reflexion ist die Spiegelung an einer Ebene und damit dieselbe Struktur mit
// anderer Belegung - Boden und Wände sind kein Sonderweg, sondern weitere
// PropagationPath-Instanzen.
//
// Die Ebene steht als { x : normal·x = planeOffset }. Gespiegelt wird nach
// Householder:
//
//   x' = x - 2 (normal·x - planeOffset) normal
//
// Das deckt jede Lage ab, nicht nur achsenparallele: eine schräg im Feld
// stehende Wand ist damit derselbe Aufwand wie der Boden. Eine frühere Fassung
// konnte nur komponentenweise skalieren und verschieben (scale/offset) - damit
// waren nur Ebenen senkrecht zu einer Koordinatenachse darstellbar.
//
// Bewusst JUCE-frei wie die restliche Physics-Schicht.
struct PathTransform
{
    // Einheitsnormale der Spiegelebene. Länge 0 heißt "keine Spiegelung",
    // die Abbildung ist dann die Identität - das ist der Direktschall.
    Vec3   normal { 0.0, 0.0, 0.0 };
    double planeOffset = 0.0;

    float  gain = 1.0f;

    bool mirrors() const { return normal.lengthSquared() > 0.0; }
};

// Spiegelung an einer beliebigen Ebene durch pointOnPlane mit der (nicht
// notwendig normierten) Normalen n.
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
    PathTransform t;

    t.normal      = n.normalised();
    t.planeOffset = t.normal.dot (pointOnPlane);

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

// Empfängerposition spiegeln. Ohne Normale (Direktschall) unverändert
// durchgereicht - das ist kein Sonderfall der Formel, sondern nur die
// Abkürzung dafür.
inline Vec3 applyPathTransform (const PathTransform& t, Vec3 receiverPos)
{
    if (! t.mirrors())
        return receiverPos;

    const double d = t.normal.dot (receiverPos) - t.planeOffset;

    return receiverPos - t.normal * (2.0 * d);
}

// Dieselbe Abbildung für eine Geschwindigkeit. Der Ebenenabstand fällt weg,
// weil er eine Konstante ist und beim Ableiten nach der Zeit verschwindet; der
// Spiegelanteil wirkt dagegen sehr wohl - ein nach oben laufendes Ohr bewegt
// sich im Bodenbild nach unten. Ohne diese zweite Funktion wäre die Bewegung
// des Spiegelempfängers falsch und mit ihr der Doppler der Reflexion.
inline Vec3 applyPathTransformVelocity (const PathTransform& t, Vec3 receiverVel)
{
    if (! t.mirrors())
        return receiverVel;

    return receiverVel - t.normal * (2.0 * t.normal.dot (receiverVel));
}
