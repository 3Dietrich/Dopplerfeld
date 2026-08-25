// Rendert das Bewegungs-Panel headless in PNG-Dateien, einmal je Reiter.
//
// Zweck: ein Layout laesst sich nicht am Quelltext pruefen. Hier entstehen
// Bilder, ohne dass ein Fenster auf dem Bildschirm aufgeht - kein Fokuswechsel,
// kein Popup waehrend der Arbeit.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target panel_shot && build/panel_shot

#include "PluginProcessor.h"
#include "UI/MotionPanel.h"

#include <cstdio>

namespace
{
// Breite wie im Editor (Panelspalte), Hoehe aus
// DopplerfeldEditor::motionContentHeight.
constexpr int panelWidth  = 462;
constexpr int panelHeight = 274;

void shoot (MotionPanel& panel, const juce::String& name)
{
    juce::Image image (juce::Image::ARGB, panelWidth, panelHeight, true);

    {
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff23262b));
        panel.paintEntireComponent (g, true);
    }

    const auto file = juce::File (DOPPLERFELD_SOURCE_DIR)
                          .getChildFile ("build")
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

    DopplerfeldProcessor proc;
    MotionPanel panel (proc.apvts);

    panel.setSize (panelWidth, panelHeight);

    // Reihenfolge wie die Reiter selbst: Live, Vorbeiflug, Record/Play.
    for (const auto& tab : { juce::String ("Live"), juce::String ("Vorbeiflug"),
                             juce::String ("Record/Play") })
    {
        panel.selectTabForTest (tab);
        panel.resized();
        shoot (panel, "panel_" + tab.replaceCharacter ('/', '_').toLowerCase());
    }

    return 0;
}
