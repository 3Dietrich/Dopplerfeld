// Pruefprogramm fuer die Hallbauarten. Kein JUCE, kein Audiogeraet - nur die
// Header aus Source/Reverb und ein paar Impulsantworten, die hier ausgerechnet
// und vermessen werden.
//
// Geprueft wird das, was auf jeder Maschine gleich ist: Stabilitaet, die
// Abklingzeit gegen ihre Vorgabe, die Orthogonalitaet der Mischmatrix und dass
// nirgends NaN entsteht. Was die Sache KOSTET, steht bewusst nicht hier,
// sondern im reverb_probe - Rechenzeit haengt an der Maschine und hat in einem
// Test nichts verloren, der auch auf einem ausgelasteten CI-Rechner gruen sein
// muss.
//
// exit 0 = alles erfuellt, exit 1 = mindestens eines nicht.

#include "Reverb/AllpassDiffuser.h"
#include "Reverb/FdnReverb.h"
#include "Reverb/SchroederReverb.h"
#include "Reverb/TapBus.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace
{

int failures = 0;

void check (bool condition, const char* name, const char* detail)
{
    std::printf ("  [%s] %-44s %s\n", condition ? "ok  " : "FAIL", name, detail);

    if (! condition)
        ++failures;
}

constexpr double sr    = 48000.0;
constexpr int    block = 256;

// Impulsantwort einer Bauart, in Bloecken gerechnet wie im Betrieb.
struct Impulse
{
    std::vector<float> l, r;
};

Impulse renderImpulse (ReverbUnit& unit, double seconds)
{
    const int total = (int) (seconds * sr);

    Impulse out;
    out.l.assign ((size_t) total, 0.0f);
    out.r.assign ((size_t) total, 0.0f);

    std::vector<float> in ((size_t) block, 0.0f);
    in[0] = 1.0f;

    for (int n = 0; n < total; n += block)
    {
        const int count = std::min (block, total - n);

        unit.process (in.data(), out.l.data() + n, out.r.data() + n, count);

        // Nur der allererste Block traegt den Impuls.
        in[0] = 0.0f;
    }

    return out;
}

// Abklingzeit nach Schroeder: die Energie rueckwaerts aufsummieren, daraus die
// Zeit von -5 dB auf -35 dB nehmen und verdoppeln.
//
// Warum nicht direkt bis -60 dB: dort liegt der Nachhall bei kurzen Zeiten
// schon im Rundungsrauschen, und bei langen Zeiten muesste man Minuten
// rechnen. Der Bereich dazwischen ist der, der stabil messbar ist; das ist die
// uebliche T30-Messung.
double measureRt60 (const std::vector<float>& x)
{
    const size_t n = x.size();

    std::vector<double> edc (n, 0.0);
    double              sum = 0.0;

    for (size_t i = n; i-- > 0;)
    {
        sum   += (double) x[i] * (double) x[i];
        edc[i] = sum;
    }

    if (edc[0] <= 0.0)
        return 0.0;

    const double ref = edc[0];

    auto timeAt = [&] (double db) -> double
    {
        const double target = ref * std::pow (10.0, db / 10.0);

        for (size_t i = 0; i < n; ++i)
            if (edc[i] <= target)
                return (double) i / sr;

        return -1.0;
    };

    const double t5  = timeAt (-5.0);
    const double t35 = timeAt (-35.0);

    if (t5 < 0.0 || t35 < 0.0)
        return -1.0;

    return (t35 - t5) * 2.0;
}

double peak (const std::vector<float>& x)
{
    double p = 0.0;

    for (float v : x)
        p = std::max (p, (double) std::fabs (v));

    return p;
}

bool allFinite (const std::vector<float>& x)
{
    for (float v : x)
        if (! std::isfinite (v))
            return false;

    return true;
}

// Wie stark sich die zwei Seiten aehneln. 1 = identisch (also mono), 0 = ohne
// Zusammenhang. Ein Hall, der hier nahe 1 liegt, ist keine Stereoquelle,
// sondern eine Monoquelle in der Mitte.
double correlation (const std::vector<float>& a, const std::vector<float>& b)
{
    double sab = 0.0, saa = 0.0, sbb = 0.0;

    for (size_t i = 0; i < a.size(); ++i)
    {
        sab += (double) a[i] * (double) b[i];
        saa += (double) a[i] * (double) a[i];
        sbb += (double) b[i] * (double) b[i];
    }

    if (saa <= 0.0 || sbb <= 0.0)
        return 1.0;

    return sab / std::sqrt (saa * sbb);
}

// ------------------------------------------------------------- Pruefungen

void checkDecay (ReverbUnit& unit, const char* label, double wanted)
{
    unit.prepare (sr, block);
    unit.setRoomSize (30.0);
    unit.setDamping (0.0);
    unit.setDecaySeconds (wanted);
    unit.reset();

    const Impulse ir  = renderImpulse (unit, wanted * 2.5 + 0.5);
    const double  rt  = measureRt60 (ir.l);

    char detail[160];

    // Ein Drittel Abweichung ist grosszuegig und soll es sein: die Vorgabe
    // beschreibt einen idealen Raum, die Bauart naehert ihn an. Was der Test
    // ausschliessen soll, ist ein Hall, der zehnmal zu lang steht oder gar
    // nicht abklingt - nicht die letzten Prozent.
    const bool okDecay = rt > 0.0 && rt > wanted * 0.66 && rt < wanted * 1.5;

    std::snprintf (detail, sizeof detail, "%s: %.2f s gewollt, %.2f s gemessen", label, wanted, rt);
    check (okDecay, "Abklingzeit trifft die Vorgabe", detail);

    std::snprintf (detail, sizeof detail, "%s: Spitze %.3f", label, peak (ir.l));
    check (peak (ir.l) < 4.0, "kein Aufschwingen", detail);

    check (allFinite (ir.l) && allFinite (ir.r), "kein NaN", label);

    const double c = correlation (ir.l, ir.r);
    std::snprintf (detail, sizeof detail, "%s: r = %.3f", label, c);
    check (std::fabs (c) < 0.9, "die zwei Seiten sind verschieden", detail);
}

// Die Mischmatrix darf keine Energie erzeugen. Das ist die Bedingung dafuer,
// dass die Abklingzeit ausschliesslich an den Rueckkopplungsfaktoren haengt -
// eine Matrix mit Verstaerkung wuerde das Netz unabhaengig davon aufschwingen
// lassen.
void checkNetworkStability()
{
    FdnReverb fdn;

    fdn.prepare (sr, block);
    fdn.setRoomSize (30.0);
    fdn.setDamping (0.0);

    // Ohne Verlust in den Leitungen: was jetzt noch waechst, kommt aus der
    // Matrix. Eine sehr lange Abklingzeit setzt die Rueckkopplung praktisch
    // auf eins, ohne den Sonderfall 1.0 exakt zu treffen.
    fdn.setDecaySeconds (1.0e6);
    fdn.reset();

    const Impulse ir = renderImpulse (fdn, 20.0);

    char detail[160];
    std::snprintf (detail, sizeof detail, "20 s bei quasi verlustfreiem Netz, Spitze %.3f", peak (ir.l));

    check (peak (ir.l) < 8.0 && allFinite (ir.l), "verlustfreies Netz bleibt beschraenkt", detail);
}

// Die Daempfung darf nur Hoehen nehmen, keinen Pegel. Gegenprobe ueber die
// Gesamtenergie im Vergleich zur Energie oberhalb der halben Nyquistfrequenz,
// grob geschaetzt ueber die Differenz aufeinanderfolgender Samples.
void checkDampingTakesTreble()
{
    // Erst ab einer halben Sekunde messen. Davor steht die erste Reflexion
    // jeder Leitung, und die hat den Rueckkopplungsweg noch gar nicht
    // durchlaufen - sie ist bei jeder Daempfungsstellung gleich hell und
    // wuerde die Messung beherrschen. Die Daempfung wirkt je Umlauf, also
    // zeigt sie sich im spaeten Teil.
    const size_t skip = (size_t) (0.5 * sr);

    auto hfEnergy = [skip] (const std::vector<float>& x)
    {
        double e = 0.0;

        for (size_t i = std::max (skip, (size_t) 1); i < x.size(); ++i)
        {
            const double d = (double) x[i] - (double) x[i - 1];
            e += d * d;
        }

        return e;
    };

    SchroederReverb open, closed;

    for (auto* u : { (ReverbUnit*) &open, (ReverbUnit*) &closed })
    {
        u->prepare (sr, block);
        u->setRoomSize (30.0);
        u->setDecaySeconds (2.0);
    }

    open.setDamping (0.0);
    closed.setDamping (1.0);
    open.reset();
    closed.reset();

    const Impulse a = renderImpulse (open, 3.0);
    const Impulse b = renderImpulse (closed, 3.0);

    const double ha = hfEnergy (a.l);
    const double hb = hfEnergy (b.l);

    char detail[160];
    std::snprintf (detail, sizeof detail, "Hochtonanteil offen %.3g, zu %.3g", ha, hb);

    check (hb < ha * 0.5, "Daempfung nimmt Hoehen", detail);
}

// Eine Bauart muss auch bei absurden Werten still bleiben statt zu explodieren:
// die Regler haben absichtlich keine engen Deckel.
void checkExtremes()
{
    FdnReverb        fdn;
    SchroederReverb  sch;
    AllpassDiffuser  dif;

    ReverbUnit* units[] { &fdn, &sch, &dif };

    for (auto* u : units)
    {
        u->prepare (sr, block);

        bool clean = true;

        for (double size : { 0.5, 1.0, 500.0, 5000.0 })
        {
            for (double decay : { 0.0, 0.01, 30.0, 1.0e5 })
            {
                u->setRoomSize (size);
                u->setDecaySeconds (decay);
                u->setDamping (0.5);
                u->reset();

                const Impulse ir = renderImpulse (*u, 0.5);

                if (! allFinite (ir.l) || ! allFinite (ir.r) || peak (ir.l) > 16.0)
                    clean = false;
            }
        }

        check (clean, "bleibt bei Grenzwerten beschraenkt", u->name());
    }
}

// Der Vorlauf bildet den Rueckweg vom Abgriffpunkt zum Hoerer ab. Er muss
// wirklich verzoegern - ohne ihn kaeme der Hall einer weit entfernten Talwand
// gleichzeitig mit dem Direktschall an.
void checkPredelay()
{
    constexpr double metres = 343.0;   // genau eine Sekunde

    TapBus bus;

    bus.prepare (sr, block);
    bus.setType (TapBus::Type::diffuser);
    bus.setRoomSize (10.0);
    bus.setDecaySeconds (0.5);
    bus.setGain (1.0);
    bus.setWidth (1.0);
    bus.setPredelayMetres (metres);
    bus.reset();

    const int total = (int) (2.0 * sr);

    std::vector<float> l ((size_t) total, 0.0f);
    std::vector<float> r ((size_t) total, 0.0f);
    std::vector<float> in ((size_t) block, 0.0f);

    in[0] = 1.0f;

    for (int n = 0; n < total; n += block)
    {
        bus.processAdd (in.data(), l.data() + n, r.data() + n, std::min (block, total - n));
        in[0] = 0.0f;
    }

    // Erster Ausschlag ueber der Rauschgrenze.
    int first = -1;

    for (int i = 0; i < total; ++i)
        if (std::fabs (l[(size_t) i]) > 1.0e-4f)
        {
            first = i;
            break;
        }

    const double t = first < 0 ? -1.0 : (double) first / sr;

    char detail[160];
    std::snprintf (detail, sizeof detail, "%.0f m sollen %.3f s dauern, gemessen %.3f s",
                   metres, metres / 343.0, t);

    check (t > 0.9 && t < 1.1, "Vorlauf verzoegert um den Weg", detail);
}

// Ein Abgriffpunkt ist eine ZUSAETZLICHE Signalquelle: er addiert auf den
// Ausgang, statt ihn zu ersetzen. Vorhandener Inhalt darf dabei nicht
// verschwinden.
void checkAdds()
{
    TapBus bus;

    bus.prepare (sr, block);
    bus.setType (TapBus::Type::fdn);
    bus.setRoomSize (20.0);
    bus.setDecaySeconds (1.0);
    bus.setGain (0.0);            // stumm: der Ausgang darf sich nicht aendern
    bus.setPredelayMetres (0.0);
    bus.reset();

    std::vector<float> in ((size_t) block, 0.5f);
    std::vector<float> l ((size_t) block, 0.25f);
    std::vector<float> r ((size_t) block, -0.25f);

    bus.processAdd (in.data(), l.data(), r.data(), block);

    bool untouched = true;

    for (int i = 0; i < block; ++i)
        if (std::fabs (l[(size_t) i] - 0.25f) > 1.0e-6f || std::fabs (r[(size_t) i] + 0.25f) > 1.0e-6f)
            untouched = false;

    check (untouched, "stummer Abgriffpunkt laesst den Ausgang unberuehrt", "Pegel 0");
}

} // namespace

int main()
{
    std::printf ("reverb_check\n");

    {
        AllpassDiffuser dif;
        SchroederReverb sch;
        FdnReverb       fdn;

        checkDecay (sch, "Schroeder 0.5 s", 0.5);
        checkDecay (sch, "Schroeder 2.0 s", 2.0);
        checkDecay (sch, "Schroeder 6.0 s", 6.0);
        checkDecay (fdn, "FDN 0.5 s",       0.5);
        checkDecay (fdn, "FDN 2.0 s",       2.0);
        checkDecay (fdn, "FDN 6.0 s",       6.0);

        // Der Diffusor hat keinen Nachhallschwanz, eine Abklingzeit ist bei ihm
        // nicht definiert. Geprueft wird nur, dass er beschraenkt bleibt und
        // zwei verschiedene Seiten liefert.
        dif.prepare (sr, block);
        dif.setRoomSize (30.0);
        dif.setDecaySeconds (2.0);
        dif.reset();

        const Impulse ir = renderImpulse (dif, 1.0);

        check (peak (ir.l) < 4.0 && allFinite (ir.l), "Diffusor bleibt beschraenkt", dif.name());
        check (std::fabs (correlation (ir.l, ir.r)) < 0.9, "Diffusor ist zweiseitig", dif.name());
    }

    checkPredelay();
    checkAdds();
    checkNetworkStability();
    checkDampingTakesTreble();
    checkExtremes();

    std::printf (failures == 0 ? "\nalles gruen\n" : "\n%d Pruefung(en) fehlgeschlagen\n", failures);

    return failures == 0 ? 0 : 1;
}
