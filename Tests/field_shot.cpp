// Rendert die Draufsicht von FieldComponent headless in PNG-Dateien - einmal
// mit M im Feld, einmal mit M weit ausserhalb, um die neue Randmarke zu
// pruefen (s. topDownSourceMarker() in FieldComponent.cpp).
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

    return 0;
}
