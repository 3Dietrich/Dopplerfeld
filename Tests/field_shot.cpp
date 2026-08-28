// Rendert FieldComponent headless in PNG-Dateien: die Draufsicht mit M im
// Feld und mit M weit ausserhalb (Randmarke, s. topDownSourceMarker()) sowie
// die Perspektive mit dem Hoerersymbol in mehreren Blickrichtungen und
// Ohrhoehen (s. drawPerspectiveListener()).
//
// Zweck: Layout/Randmarken-Position lassen sich nicht am Quelltext pruefen.
// Hier entstehen Bilder, ohne dass ein Fenster auf dem Bildschirm aufgeht -
// kein Fokuswechsel, kein Popup waehrend der Arbeit (Vorschrift, s.
// Tests/panel_shot.cpp).
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build-ui --target field_shot && build-ui/field_shot

#include "UI/FieldComponent.h"

#include <cmath>
#include <cstdio>

namespace
{
// Wie im Editor (DopplerfeldEditor::fieldWidth/fieldHeight).
constexpr int fieldWidth  = 700;
constexpr int fieldHeight = 400;

void shoot (FieldComponent& field, const juce::String& name)
{
    juce::Image image (juce::Image::ARGB, fieldWidth, fieldHeight, true);

    {
        juce::Graphics g (image);
        field.paintEntireComponent (g, true);
    }

    const auto file = juce::File (DOPPLERFELD_SOURCE_DIR)
                          .getChildFile ("build-ui")
                          .getChildFile (name + ".png");

    file.deleteFile();

    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

    if (stream != nullptr && png.writeImageToStream (image, *stream))
        std::printf ("  %s\n", file.getFullPathName().toRawUTF8());
    else
        std::printf ("  FEHLER beim Schreiben von %s\n", file.getFullPathName().toRawUTF8());
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    FieldComponent field;
    field.setSize (fieldWidth, fieldHeight);
    field.setFieldMetres (100.0); // Default, wie in PluginEditor

    FieldSnapshot snap;

    // 1) M mittig im Feld - normaler Punkt, keine Randmarke erwartet.
    snap.sourcePos = { 50.0, 25.0, 0.0 };
    field.setSnapshot (snap);
    shoot (field, "field_topdown_inside");

    // 2) M weit ausserhalb (Feld ist 100 x ~57 m) - Randmarke am rechten
    // oberen Rand erwartet, kein normaler Punkt mehr sichtbar.
    snap.sourcePos = { 400.0, 300.0, 0.0 };
    field.setSnapshot (snap);
    shoot (field, "field_topdown_outside");

    // 3) M nur in x ausserhalb, y bequem in der Mitte - Randmarke am
    // rechten Rand auf halber Hoehe, weit weg von der Tempoanzeige oben.
    snap.sourcePos = { 400.0, 25.0, 0.0 };
    field.setSnapshot (snap);
    shoot (field, "field_topdown_outside_right");

    // 4) M nur in y ausserhalb (weit im Norden), x mittig im Feld - prueft,
    // ob die obere Randmarke mit der Tempoanzeige (oben rechts) oder der
    // Entfernungsanzeige (oben links) kollidiert.
    snap.sourcePos = { 50.0, 300.0, 0.0 };
    field.setSnapshot (snap);
    shoot (field, "field_topdown_outside_north");

    // 5) Perspektive: der Hoerer liegt flach in seiner Ohrhoehe und ist
    // perspektivisch verzerrt, mit Lotlinie auf den Boden. Drei Blick-
    // richtungen, weil die Verzerrung genau daran ablesbar ist - von der
    // Kamera weg, quer und zur Kamera hin.
    field.setViewMode (FieldComponent::ViewMode::Perspective);

    snap.sourcePos      = { 50.0, 60.0, 12.0 };
    snap.listener.head  = { 50.0, 28.5, 1.7 };
    snap.listener.yaw   = 0.0;                 // Nase in +y, von der Kamera weg
    field.setSnapshot (snap);
    shoot (field, "field_persp_listener_away");

    snap.listener.yaw = juce::MathConstants<double>::halfPi;   // Nase in +x, quer
    field.setSnapshot (snap);
    shoot (field, "field_persp_listener_side");

    snap.listener.yaw = juce::MathConstants<double>::pi;       // Nase zur Kamera
    field.setSnapshot (snap);
    shoot (field, "field_persp_listener_towards");

    // 6) Hoerer deutlich ueber dem Boden: der senkrechte Strich zu z = 0 wird
    // lang, das Symbol bleibt in seiner Hoehe liegen. Viel hoeher darf er
    // hier nicht stehen - die Kamera haengt an seiner Grundflaeche, nicht an
    // seiner Hoehe (s. cameraPosition()), und schiebt ihn sonst aus dem Bild.
    snap.listener.head = { 50.0, 28.5, 6.0 };
    snap.listener.yaw  = 0.0;
    field.setSnapshot (snap);
    shoot (field, "field_persp_listener_high");

    // 7) Ueberschall-Frontlinien (drawMachFronts/drawPerspectiveMachFronts):
    // eine Quelle in Flughoehe, geradeaus in +x, mit Spur und Wellenfronten
    // wie sie DopplerEngine::publishSnapshot() liefert (juengste zuerst).
    // Geprueft wird, dass die Front bei Mach 2 als Hyperbel-Schar auftaucht,
    // die hoeheren Schnitte anders liegen als die Bodenspur, und dass bei
    // Mach 0,5 nichts stehenbleibt.
    {
        // Grosses Feld: bei 400 m Flughoehe liegt die Bodenspur weit hinter
        // der Quelle, auf 100 m Feldbreite waere davon nichts zu sehen.
        field.setFieldMetres (2000.0);

        FieldSnapshot mach;

        mach.speedOfSound  = 343.0;
        mach.now           = 20.0;
        mach.listener.head = { 700.0, 400.0, 1.7 };

        const double speed = 686.0;   // Mach 2
        const Vec3   apex { 1200.0, 600.0, 400.0 };

        mach.sourcePos   = apex;
        mach.sourceSpeed = speed;

        // Spur: gerade Bahn in +x, die an der Quelle endet - daraus liest
        // machGeometry() die Flugrichtung.
        mach.trailCount = 32;
        for (int i = 0; i < mach.trailCount; ++i)
        {
            const double back = (double) (mach.trailCount - 1 - i) * 30.0;
            mach.trail[(size_t) i] = { apex.x - back, apex.y, apex.z };
        }

        // Wellenfronten: feste Schrittweite, juengste zuerst, Mittelpunkt
        // jeweils an der Quellposition zur Emissionszeit.
        const double spacing = 0.486;   // wie publishSnapshot() bei n = 2000 m
        mach.wavefrontCount = FieldSnapshot::maxWavefronts;
        for (int i = 0; i < mach.wavefrontCount; ++i)
        {
            const double age = (double) (i + 1) * spacing;
            mach.wavefrontEmitTimes[(size_t) i] = mach.now - age;
            mach.wavefrontPositions[(size_t) i] = { apex.x - speed * age, apex.y, apex.z };
        }

        field.setViewMode (FieldComponent::ViewMode::TopDown);
        field.setSnapshot (mach);
        shoot (field, "field_topdown_mach2");

        field.setViewMode (FieldComponent::ViewMode::Perspective);
        field.setSnapshot (mach);
        shoot (field, "field_persp_mach2");

        // Unterschall: keine Front, auch kein Rest davon.
        mach.sourceSpeed = 171.5;   // Mach 0,5
        field.setViewMode (FieldComponent::ViewMode::TopDown);
        field.setSnapshot (mach);
        shoot (field, "field_topdown_subsonic");

        // 8) Kurvenflug am Boden (@dpa 20260828, "hier?? niemals!"): die Front
        // darf auch bei krummer Bahn nirgends VOR der Quelle liegen und nicht
        // ueber den aeussersten Kreis hinausreichen. Die Bahn ist ein
        // Kreisbogen, die Wellenfronten sitzen an den Positionen, an denen die
        // Quelle jeweils war.
        FieldSnapshot turn;

        turn.speedOfSound  = 343.0;
        turn.now           = 20.0;
        turn.listener.head = { 700.0, 400.0, 1.7 };
        turn.sourceSpeed   = 778.0;   // Mach 2,27 wie im Screenshot

        // Sanfte Rechtskurve: Bogen mit 4 km Radius, die Quelle steht bei
        // turn.now auf 90 Grad (also mit Flugrichtung +x) im Bild.
        const double turnRadius = 4000.0;
        const Vec3   turnCentre { 1200.0, 600.0 - turnRadius, 0.0 };
        const double omega = turn.sourceSpeed / turnRadius;

        auto onArc = [&] (double t)
        {
            const double a = juce::MathConstants<double>::halfPi + omega * (t - turn.now);
            return Vec3 { turnCentre.x + turnRadius * std::cos (a),
                          turnCentre.y + turnRadius * std::sin (a),
                          0.0 };
        };

        turn.sourcePos = onArc (turn.now);

        turn.trailCount = 64;
        for (int i = 0; i < turn.trailCount; ++i)
            turn.trail[(size_t) i] = onArc (turn.now - 4.0 * (double) (turn.trailCount - 1 - i)
                                                            / (double) (turn.trailCount - 1));

        turn.wavefrontCount = FieldSnapshot::maxWavefronts;
        for (int i = 0; i < turn.wavefrontCount; ++i)
        {
            const double age = (double) (i + 1) * 0.486;
            turn.wavefrontEmitTimes[(size_t) i] = turn.now - age;
            turn.wavefrontPositions[(size_t) i] = onArc (turn.now - age);
        }

        field.setSnapshot (turn);
        shoot (field, "field_topdown_mach_turn");
    }

    return 0;
}
