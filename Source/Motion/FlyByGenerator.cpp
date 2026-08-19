#include "FlyByGenerator.h"

#include <algorithm>
#include <cmath>

void FlyByGenerator::configure (Kind kindIn, Start startIn, double distanceMetres, double approachMetres,
                                Vec3 listenerPos, double heightMetres)
{
    kind         = kindIn;
    startVariant = startIn;
    distance     = distanceMetres;
    approach     = approachMetres;
    listener     = listenerPos;
    height       = heightMetres;
}

Vec3 FlyByGenerator::direction() const
{
    // Die Richtung wird beim Start festgelegt und bleibt es. Sie am
    // Vorzeichen der laufenden Geschwindigkeit hängen zu lassen wäre
    // verlockend, würde die Bahn beim Umschalten aber nicht umdrehen, sondern
    // die Quelle auf die gespiegelte Position setzen - ein Sprung mitten im
    // Flug, und zwar einer, den die schon geschriebene Vorgeschichte nicht
    // erklärt.
    return kind == Kind::ThroughScreen ? Vec3 { 0.0, directionSign, 0.0 }
                                       : Vec3 { directionSign, 0.0, 0.0 };
}

Vec3 FlyByGenerator::offset() const
{
    // Senkrecht zur Flugrichtung, in der Waagerechten: die Bahn "durch den
    // Bildschirm" wird seitlich versetzt, die querende in die Tiefe.
    return kind == Kind::ThroughScreen ? Vec3 { distance, 0.0, 0.0 }
                                       : Vec3 { 0.0, distance, 0.0 };
}

double FlyByGenerator::halfLength() const
{
    // Eigenständiger Regler (Params::flyApproach), keine Ableitung mehr aus
    // distance - die beiden steuerten vorher ungewollt gemeinsam Bahnlänge
    // UND seitlichen Abstand. Untergrenze nur als Schutz vor einer entarteten
    // (quasi punktförmigen) Bahn.
    return std::max (10.0, approach);
}

Vec3 FlyByGenerator::positionAt (double distanceAlongPath) const
{
    // Der nächste Punkt liegt in der Mitte der Strecke, auf Höhe des Hörers
    // in Flugrichtung. Die Flughöhe kommt vom Source-Z-Regler und ist
    // unabhängig von der Hörerhöhe.
    const Vec3 nearest { listener.x + offset().x,
                         listener.y + offset().y,
                         height };

    return nearest + direction() * (distanceAlongPath - halfLength());
}

Vec3 FlyByGenerator::startPosition() const
{
    return positionAt (0.0);
}

Vec3 FlyByGenerator::startVelocity() const
{
    return direction() * std::abs (speed);
}

void FlyByGenerator::start()
{
    directionSign = (speed < 0.0) ? -1.0 : 1.0;
    travelled     = 0.0;
    targetLead    = 0.0;
    running       = true;
}

void FlyByGenerator::setTargetLead (double metres)
{
    targetLead = std::max (0.0, metres);
}

Vec3 FlyByGenerator::tick (double dt)
{
    if (! running)
        return currentPosition();

    // Betrag: die Richtung steht seit dem Start fest (siehe direction()), nur
    // das Tempo folgt dem Regler.
    travelled += std::abs (speed) * dt;

    if (travelled >= 2.0 * halfLength())
    {
        travelled = 2.0 * halfLength();
        running   = false;
    }

    return currentPosition();
}
