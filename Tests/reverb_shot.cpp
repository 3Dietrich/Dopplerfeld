// Layout-Bild des Hall-Panels (Tests/reverb_shot.cpp), nach dem Muster von
// panel_shot. Kein Test - es prueft nichts, es zeigt nur, was herauskommt.
//
// Offscreen gerendert, ohne Fenster: das Bild entsteht in einem juce::Image,
// nichts davon erscheint auf dem Bildschirm.
//
//   cmake --build build --target reverb_shot && build/reverb_shot

#include "PluginProcessor.h"
#include "UI/ReverbPanel.h"

#include <cstdio>
#include <memory>

namespace
{
// Breite wie die Panelspalte im Editor, Hoehe aus
// DopplerfeldEditor::reverbContentHeight.
constexpr int panelWidth  = 462;
constexpr int panelHeight = 8 + 26 + 6 + 2 * Theme::knobHeight + 4 + 6 + 26 + 8;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    DopplerfeldProcessor proc;

    ReverbPanel panel (proc.apvts);
    panel.setSize (panelWidth, panelHeight);

    juce::Image image (juce::Image::ARGB, panelWidth, panelHeight, true);

    {
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff23262b));
        panel.paintEntireComponent (g, true);
    }

    const auto file = juce::File (DOPPLERFELD_SOURCE_DIR)
                          .getChildFile ("build")
                          .getChildFile ("reverb_panel.png");

    file.deleteFile();

    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

    if (stream != nullptr && png.writeImageToStream (image, *stream))
        std::printf ("  %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), panelWidth, panelHeight);
    else
        std::printf ("  FEHLER beim Schreiben\n");

    return 0;
}
