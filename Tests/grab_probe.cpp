// Messprogramm zum Anfassen der Quelle M (@dpa 20260826: "wenn man den M
// anfasst springt er meist ... es soll sich durchs click 0 bewegen. Erst
// dragging zaehlt dann von der Klickposition aus.. ohne sprung, einfach ein
// mausversatz").
//
// Simuliert Mausereignisse auf einer FieldComponent und misst, was sie nach
// aussen meldet: erst ein Klick neben die Mitte des gewackelten Symbols, dann
// ein Zug um eine bekannte Strecke. Geprueft wird das, was der Processor
// spaeter zu sehen bekaeme (onSourceDragged), nicht das gezeichnete Bild.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build-ui --target grab_probe && build-ui/grab_probe

#include "UI/FieldComponent.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr int    fieldWidth  = 700;
constexpr int    fieldHeight = 400;
constexpr double fieldMetres = 100.0;

// Pixel je Meter, wie FieldComponent::worldToScreen() sie benutzt.
constexpr double pxPerMetre = (double) fieldWidth / fieldMetres;

juce::MouseEvent makeEvent (juce::Component& c, juce::Point<float> pos,
                            juce::Point<float> downPos, bool wasDragged)
{
    return { juce::Desktop::getInstance().getMainMouseSource(),
             pos,
             juce::ModifierKeys::currentModifiers,
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &c, &c,
             juce::Time::getCurrentTime(),
             downPos,
             juce::Time::getCurrentTime(),
             1, wasDragged };
}

int failures = 0;

void check (const char* label, double got, double want, double tolerance)
{
    const bool ok = std::abs (got - want) <= tolerance;

    if (! ok)
        ++failures;

    std::printf ("  %-46s %10.5f  (erwartet %8.5f)  %s\n",
                 label, got, want, ok ? "ok" : "FEHLER");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    FieldComponent field;
    field.setSize (fieldWidth, fieldHeight);
    field.setFieldMetres (fieldMetres);

    // Ereignisse sofort melden statt auf dem Bildtakt - der Timer laeuft in
    // diesem Programm nicht (keine Nachrichtenschleife).
    field.setMouseFrameSmoothing (false);

    int    reports = 0;
    double lastNormX = 0.0;
    double lastNormY = 0.0;

    field.onSourceDragged = [&] (double normX, double normY)
    {
        ++reports;
        lastNormX = normX;
        lastNormY = normY;
    };

    // Ruhelage: mehrere Snapshots, damit der jitterfreie Anker darauf steht.
    FieldSnapshot snap;
    snap.sourcePos = { 50.0, 28.5, 0.0 };

    for (int i = 0; i < 20; ++i)
    {
        snap.now = 0.03 * (double) i;
        field.setSnapshot (snap);
    }

    // Ein gewackelter Snapshot: M wird 3 m weiter rechts und 1,5 m weiter
    // vorn gezeichnet. Nur 5 ms nach dem vorigen, damit die Glaettung des
    // Ankers ihm in dieser einen Runde kaum folgt (Zeitkonstante 0,25 s) -
    // seine Ruhelage bleibt damit praktisch stehen, und genau von ihr aus
    // muss der Zug rechnen.
    snap.now       = 0.03 * 19.0 + 0.005;
    snap.sourcePos = { 53.0, 30.0, 0.0 };
    field.setSnapshot (snap);

    // Klick zwischen Ruhelage und gewackeltem Punkt - innerhalb des
    // Fangradius, aber auf keinem von beiden.
    const juce::Point<float> down { (float) (52.0 * pxPerMetre),
                                    (float) (fieldHeight - 29.5 * pxPerMetre) };

    field.mouseDown (makeEvent (field, down, down, false));

    std::printf ("Klick auf M (2 m neben seiner Ruhelage):\n");
    check ("Meldungen an den Processor", (double) reports, 0.0, 0.0);

    // Zug um 70 px nach rechts = 10 m. Erwartet wird die RUHELAGE plus diese
    // 10 m, nicht die Mausposition.
    const juce::Point<float> dragged = down + juce::Point<float> (70.0f, 0.0f);

    field.mouseDrag (makeEvent (field, dragged, down, true));

    std::printf ("Zug um 70 px (10 m) nach rechts:\n");
    check ("Meldungen an den Processor", (double) reports, 1.0, 0.0);
    check ("gemeldetes x (Anteil der Feldbreite)", lastNormX, 0.60, 0.002);
    check ("gemeldetes y (unveraendert)",          lastNormY, 28.5 / (fieldHeight / pxPerMetre), 0.002);

    field.mouseUp (makeEvent (field, dragged, down, true));

    std::printf ("\n%s\n", failures == 0 ? "alles ok" : "FEHLER, siehe oben");
    return failures == 0 ? 0 : 1;
}
