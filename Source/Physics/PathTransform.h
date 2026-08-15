#pragma once

#include "Vec3.h"

// Abbildung eines Empfangspunkts in das Koordinatensystem eines Ausbreitungs-
// pfades (Plan 3.4). Für den Direktschall ist das die Identität; die Spiegel-
// quellen aus Phase 2 sind genau dieser Struktur wegen kein Umbau, sondern nur
// weitere PropagationPath-Instanzen mit anderer Belegung:
//
//   Boden (z = 0):      scale.z = -1
//   Wand bei x = w:     scale.x = -1, offset.x = 2w
//   Reflexionsgrad:     gain < 1
//
// Bewusst JUCE-frei wie die restliche Physics-Schicht.
struct PathTransform
{
    Vec3  scale  { 1.0, 1.0, 1.0 };
    Vec3  offset { 0.0, 0.0, 0.0 };
    float gain = 1.0f;
};

// Empfängerposition spiegeln/verschieben. Komponentenweise skalieren, dann
// offset addieren - die Reihenfolge ist Teil der Definition oben (die Wand bei
// x = w wird zu x -> 2w - x, nicht zu 2w + (-x) mit vorher verschobenem x).
inline Vec3 applyPathTransform (const PathTransform& t, Vec3 receiverPos)
{
    return { receiverPos.x * t.scale.x + t.offset.x,
             receiverPos.y * t.scale.y + t.offset.y,
             receiverPos.z * t.scale.z + t.offset.z };
}

// Dieselbe Abbildung für eine Geschwindigkeit. Der offset fällt weg, weil er
// eine konstante Verschiebung ist und beim Ableiten nach der Zeit verschwindet;
// die Spiegelung (scale) wirkt dagegen sehr wohl - ein nach oben laufendes Ohr
// bewegt sich im Bodenbild nach unten. Ohne diese zweite Funktion wäre die
// Bewegung des Spiegelempfängers in Phase 2 falsch.
inline Vec3 applyPathTransformVelocity (const PathTransform& t, Vec3 receiverVel)
{
    return { receiverVel.x * t.scale.x,
             receiverVel.y * t.scale.y,
             receiverVel.z * t.scale.z };
}
