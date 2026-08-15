#pragma once

#include "../Physics/Vec3.h"

// Gemeinsame Schnittstelle aller Bewegungsglätter (Plan 3.8). Der Smoother
// sitzt zwischen dem rohen Ziel (Maus, Hostautomation oder Wiedergabepfad)
// und der SourceTrajectory: er bekommt nur ein Ziel gesetzt und tickt auf
// der Trajektorienrate im Audiothread, egal woher das Ziel kam - dieselbe
// Kette glättet also alle drei Quellen identisch, statt jede eigens zu
// behandeln.
//
// Reines C++, keine JUCE-Abhängigkeit (gleiches Prinzip wie Source/Physics),
// damit jede Implementierung einzeln und offline testbar bleibt.
class MotionSmoother
{
public:
    virtual ~MotionSmoother() = default;

    // Setzt die Tickrate (i.d.R. die Trajektorienrate). Muss vor dem ersten
    // tick() aufgerufen werden, darf danach erneut aufgerufen werden, z.B.
    // bei einem Sample-Rate-Wechsel.
    virtual void prepare (double tickRateHz) = 0;

    // Springt sofort auf pos, ohne Anlaufzeit - interne Geschwindigkeit wird
    // auf 0 zurückgesetzt. Für Positionssprünge (Plan 3.2/3.7), nicht für
    // normale Zielverfolgung, dafür gibt es setTarget().
    virtual void reset (Vec3 pos) = 0;

    // Setzt das Ziel, dem sich der Smoother ab jetzt annähert. Darf pro
    // Tick beliebig oft neu gesetzt werden, nur der letzte Aufruf vor dem
    // nächsten tick() zählt.
    virtual void setTarget (Vec3 pos) = 0;

    // Ein Tick auf der in prepare() gesetzten Rate. Schreibt die neue
    // geglättete Position und die zugehörige Geschwindigkeit.
    virtual void tick (Vec3& outPos, Vec3& outVel) = 0;

    // Charakteristische Zeitkonstante der aktuellen Glättung, für die
    // Fade-Policy aus Plan 3.7 (computeFadeSamples). Bedeutung je nach
    // Implementierung leicht unterschiedlich (siehe dortige Kommentare),
    // aber immer "Sekunden, bis eine Sprungantwort weitgehend abgeklungen
    // ist".
    virtual double naturalTauSeconds() const = 0;
};
