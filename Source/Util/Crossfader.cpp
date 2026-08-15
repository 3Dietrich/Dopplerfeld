#include "Crossfader.h"

namespace
{

// "Typische Modellgeschwindigkeit" für den Positionssprung (Plan 3.7).
// 30 m/s ist grob ein zügig fahrendes Auto - der Maßstab, an dem ein Sprung
// gemessen wird: die Zeit, die eine echte Bewegung für diese Strecke gebraucht
// hätte.
constexpr double positionRefSpeed = 30.0;   // m/s

// Anzahl Perioden der Grundfrequenz, über die ein Klangwechsel läuft.
constexpr double timbrePeriods = 3.0;

constexpr double positionMinSeconds = 0.005;
constexpr double positionMaxSeconds = 0.500;

constexpr double timbreMinSeconds = 0.010;
constexpr double timbreMaxSeconds = 0.300;

constexpr double fieldSizeSeconds  = 0.060;
constexpr double mediumSeconds     = 0.080;

double clampSeconds (double t, double lo, double hi)
{
    return t < lo ? lo : (t > hi ? hi : t);
}

}

int computeFadeSamples (const FadeContext& ctx)
{
    const double sr = ctx.sampleRate > 0.0 ? ctx.sampleRate : 48000.0;

    double seconds = 0.0;

    if (ctx.useManual || ctx.reason == FadeReason::Manual)
    {
        // Der manuelle Zeitregler überschreibt alles (Plan 3.11: fadeAuto +
        // fadeManualMs). Nach oben nur gegen Unsinn begrenzt, nicht gegen
        // "zu lang" - lange Fades sind hier ein legitimer Effekt.
        seconds = clampSeconds (ctx.manualSeconds, 0.0, 10.0);
    }
    else
    {
        switch (ctx.reason)
        {
            case FadeReason::SourcePosition:
            {
                // t = Δs / v_ref: kleine Sprünge schnell, große langsamer.
                // Ein Sprung über die halbe Feldbreite soll nicht wie ein
                // Schnitt klingen, ein Zentimeter-Ruckler aber auch nicht
                // wie eine Blende.
                const double delta = ctx.positionDeltaMetres > 0.0 ? ctx.positionDeltaMetres : 0.0;
                seconds = clampSeconds (delta / positionRefSpeed,
                                        positionMinSeconds, positionMaxSeconds);
                break;
            }

            case FadeReason::SourceTimbre:
            {
                // t = k / f_base: ein langsam laufender Motor braucht länger,
                // um seinen Klang zu wechseln. Unter einer Periode wäre der
                // Fade kürzer als die Wellenform selbst und würde als Klick
                // hörbar. f <= 0 (unbekannte Grundfrequenz) landet über die
                // Klemme automatisch beim langsamsten Fall.
                const double f = ctx.baseFrequencyHz;
                const double t = f > 0.0 ? timbrePeriods / f : timbreMaxSeconds;
                seconds = clampSeconds (t, timbreMinSeconds, timbreMaxSeconds);
                break;
            }

            case FadeReason::FieldSize:
                // Kein Echtzeit-Äquivalent: eine Feldgrößenänderung ist nichts,
                // was physikalisch passieren kann. Fester Wert.
                seconds = fieldSizeSeconds;
                break;

            case FadeReason::MediumChange:
                // Ebenso, Phase 2 (Temperatur).
                seconds = mediumSeconds;
                break;

            case FadeReason::Manual:
            default:
                seconds = clampSeconds (ctx.manualSeconds, 0.0, 10.0);
                break;
        }
    }

    // smootherTauSeconds bleibt hier bewusst ungenutzt: Plan 3.7 misst den
    // Positionssprung an der Strecke, nicht an der Glättung. Das Feld gehört
    // trotzdem in den FadeContext, weil die Fade-Policy in H13 (Automatik vs.
    // Handbetrieb) die Zeitkonstante des aktiven Smoothers kennen muss, um
    // Sprung und laufende Glättung nicht gegeneinander laufen zu lassen.

    const int samples = (int) (seconds * sr + 0.5);

    // Mindestens ein Sample: ein Fade der Länge 0 wäre eine Division durch
    // null in DualPathCrossfader.
    return samples < 1 ? 1 : samples;
}
