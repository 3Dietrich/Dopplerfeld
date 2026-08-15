#pragma once

#include "MotionSmoother.h"

// Slew-Limiter mit getrennten Grenzen für Geschwindigkeit und Beschleunigung
// (Plan 3.8) - entspricht am ehesten einer bewegten Maschine mit Trägheit:
// sie kann nicht beliebig schnell anfahren und nicht beliebig schnell
// bremsen. Anders als CriticallyDampedSpring plant der Limiter keine
// Bremsstrecke voraus, er fährt schlicht mit v_max auf das Ziel zu und
// bremst erst, sobald es fast erreicht ist - dabei kann er kurz übers Ziel
// hinauslaufen, wenn a_max das Abbremsen nicht rechtzeitig zulässt. Das ist
// gewollt (reales Trägheitsverhalten) und gut geeignet, wenn v_max gezielt
// Überschall erreichen soll.
//
// Defaults hier sind bewusst identisch zu den Parameter-Defaults von
// Params::slewVmax/slewAmax (Source/Params.cpp), damit ein frisch angelegter
// Smoother auch ohne Parameter-Sync schon plausibel klingt.
class SlewLimiter : public MotionSmoother
{
public:
    SlewLimiter (double vMaxIn = 50.0, double aMaxIn = 200.0);

    void setVMax (double vMaxIn);
    void setAMax (double aMaxIn);

    void prepare (double tickRateHz) override;
    void reset (Vec3 pos) override;
    void setTarget (Vec3 pos) override;
    void tick (Vec3& outPos, Vec3& outVel) override;

    // Zeit, um aus dem Stand mit a_max auf v_max zu beschleunigen - kein
    // exaktes τ wie bei den ersten beiden Implementierungen, aber dieselbe
    // Größenordnung "Sekunden bis eine Sprungantwort weitgehend abgeklungen
    // ist".
    double naturalTauSeconds() const override { return vMax / aMax; }

private:
    double vMax = 50.0;
    double aMax = 200.0;
    double dt   = 0.0;

    Vec3 target;
    Vec3 pos;
    Vec3 vel;
};
