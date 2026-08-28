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

    return 0;
}
