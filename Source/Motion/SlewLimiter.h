#pragma once

#include "MotionSmoother.h"

// Slew-Limiter mit getrennten Grenzen für Geschwindigkeit und Beschleunigung
// (Plan 3.8) - entspricht am ehesten einer bewegten Maschine mit Trägheit:
// sie kann nicht beliebig schnell anfahren und nicht beliebig schnell
// bremsen. Die Zielgeschwindigkeit ist Richtung target geklemmt auf das, was
// sich mit a_max noch rechtzeitig auf 0 abbremsen lässt (Bremskurve aus
// v² = 2*a*d) - ohne diese Vorausplanung würde der Limiter bis zuletzt mit
// vollem v_max aufs Ziel zufahren, zwangsläufig drüber hinausschießen und
// nie zur Ruhe kommen. Ein minimaler Überschwinger bleibt möglich (letzter
// Diskretisierungsschritt, sich bewegendes Ziel), aber kein Dauerschwingen.
// Gut geeignet, wenn v_max gezielt Überschall erreichen soll.
//
// Der Default fuer v_max ist bewusst identisch zu Params::slewVmax
// (Source/Params.cpp), damit ein frisch angelegter Smoother auch ohne
// Parameter-Sync schon plausibel klingt; a_max folgt daraus.
class SlewLimiter : public MotionSmoother
{
public:
    SlewLimiter (double vMaxIn = 50.0, double aMaxIn = 200.0);

    void setVMax (double vMaxIn);
    void setAMax (double aMaxIn);

    // Zeit, die der Limiter aus dem Stand auf v_max braucht.
    //
    // Eine eigene Beschleunigungsgrenze gibt es nicht mehr: sie ergibt sich
    // aus v_max geteilt durch diese Zeit (@dpa 20260825: "ich verstehe ja bis
    // heute nicht warum es zwei regler sind. Ich habe die besten Ergebnisse,
    // wenn ich sie gleich einstelle. Meinst Du nicht auch, dass die
    // Kombination der beiden Regler (Vmax=Amax) ausreichen?").
    //
    // Eine Sekunde ist genau seine Einstellung: a_max = v_max heisst
    // rechnerisch v_max / 1 s. Die beiden Zahlen gleich zu setzen sah nach
    // Willkuer aus, weil sie verschiedene Einheiten haben - dahinter steckt
    // aber eine handfeste Groesse, und das ist diese Anfahrzeit. Sie ist
    // zugleich das tau des Limiters (siehe naturalTauSeconds()).
    static constexpr double accelTimeSeconds = 1.0;

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
