#pragma once

#include "Vec3.h"

#include <cmath>

// Kopfgeometrie des Hörers (Plan 3.5). JUCE-frei wie die restliche
// Physics-Schicht, damit die Ohrgeometrie offline nachrechenbar bleibt.
struct ListenerState
{
    Vec3   head { 0.0, 0.0, 0.0 };
    double yaw        = 0.0;    // Radiant, 0 = Nase in +y
    double earSpacing = 0.17;   // Meter
};

// Nase (Blickrichtung) und "rechts" in der xy-Ebene (Plan 3.5):
//
//   n = ( sin(yaw),  cos(yaw), 0)
//   r = ( cos(yaw), -sin(yaw), 0)
//
// r ist n um -90° gedreht, steht also senkrecht auf n und zeigt bei yaw = 0
// nach +x - das ist die Weltachse "rechts" aus Plan 2.1. Der Vorzeichenwechsel
// zwischen n und r darf nirgends anders auftauchen, sonst vertauschen sich
// links und rechts.
inline Vec3 listenerNose (const ListenerState& l)
{
    return { std::sin (l.yaw), std::cos (l.yaw), 0.0 };
}

inline Vec3 listenerRight (const ListenerState& l)
{
    return { std::cos (l.yaw), -std::sin (l.yaw), 0.0 };
}

// Ohr relativ zum Kopfmittelpunkt: rechtes Ohr +, linkes Ohr - (earSpacing/2).
inline Vec3 earOffset (const ListenerState& l, bool rightEar)
{
    const double half = 0.5 * l.earSpacing * (rightEar ? 1.0 : -1.0);
    return listenerRight (l) * half;
}

inline Vec3 earPosition (const ListenerState& l, bool rightEar)
{
    return l.head + earOffset (l, rightEar);
}

// Ohrgeschwindigkeit = Kopfgeschwindigkeit + Rotationsanteil ω × (Ohr − Kopf).
//
// Der Rotationsanteil gehört zwingend dazu: eine reine Kopfdrehung bewegt die
// Ohren tatsächlich durch den Raum und erzeugt damit echten Doppler, bei
// schnellem Drehen hörbar (Plan 3.5). Ohne ihn wäre ein sich drehender Kopf
// akustisch ein starrer Punkt.
//
// Herleitung des vereinfachten Kreuzprodukts: die Drehung läuft in der
// xy-Ebene, der Drehvektor zeigt also in z-Richtung, ω = (0, 0, ω_z). Mit
// d = Ohr − Kopf = (d_x, d_y, d_z) ist
//
//   ω × d = ( ω_y·d_z − ω_z·d_y ,  ω_z·d_x − ω_x·d_z ,  ω_x·d_y − ω_y·d_x )
//         = ( −ω_z·d_y ,  ω_z·d_x ,  0 )                  wegen ω_x = ω_y = 0.
//
// d_z fällt dabei von selbst heraus - die Formel gilt also auch dann noch,
// wenn z in Phase 2 ungleich 0 wird, solange nur um die z-Achse gedreht wird.
//
// Vorzeichen: ω_z > 0 ist eine Drehung von +x nach +y (mathematisch positiv).
// Die yaw-Definition oben dreht bei wachsendem yaw von +y nach +x, ist also
// mathematisch negativ - der Aufrufer muss headAngularVel entsprechend als
// -d(yaw)/dt übergeben, wenn er von yaw kommt.
inline Vec3 earVelocity (const ListenerState& l,
                         Vec3   headVel,
                         double headAngularVel,
                         bool   rightEar)
{
    const Vec3 d = earOffset (l, rightEar);

    const Vec3 rotational { -headAngularVel * d.y,
                             headAngularVel * d.x,
                             0.0 };

    return headVel + rotational;
}
