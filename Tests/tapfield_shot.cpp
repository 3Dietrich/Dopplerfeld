#include "UI/FieldComponent.h"
#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    FieldComponent field;
    field.setSize (700, 400);
    field.setFieldMetres (100.0);

    FieldSnapshot snap;
    snap.sourcePos      = { 30.0, 20.0, 0.0 };
    snap.listener.head  = { 50.0, 40.0, 1.75 };

    // Vier Abgriffpunkte in verschiedenen Hoehen: der Ring waechst mit z.
    // Die Raumgroesse steht daneben als gestrichelter Kreis im Feldmassstab -
    // 8 m sind kaum groesser als der Ring, 120 m reichen ueber den Bildrand.
    snap.taps[0] = { true, Vec3 { 15.0, 12.0,  0.0 },   8.0 };
    snap.taps[1] = { true, Vec3 { 82.0, 15.0,  8.0 },  30.0 };
    snap.taps[2] = { true, Vec3 { 20.0, 48.0, 25.0 },  60.0 };
    snap.taps[3] = { true, Vec3 { 70.0, 45.0,  2.0 }, 120.0 };
    snap.taps[4] = { false, Vec3 { 50.0, 30.0, 0.0 },  40.0 };

    field.setSnapshot (snap);

    juce::Image image (juce::Image::ARGB, 700, 400, true);
    { juce::Graphics g (image); field.paintEntireComponent (g, true); }

    const auto f = juce::File (DOPPLERFELD_SOURCE_DIR).getChildFile ("build").getChildFile ("tapfield.png");
    f.deleteFile();
    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileOutputStream> st (f.createOutputStream());
    if (st != nullptr && png.writeImageToStream (image, *st))
        std::printf ("%s\n", f.getFullPathName().toRawUTF8());
    return 0;
}
