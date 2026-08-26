// Rendert das komplette Editor-Fenster und das Schwarm-Panel headless in
// PNG-Dateien.
//
// Zweck: Werkzeugleisten und Reglerreihen lassen sich nicht am Quelltext
// pruefen - hier entstehen Bilder, ohne dass ein Fenster auf dem Bildschirm
// aufgeht (kein Fokuswechsel, kein Popup waehrend der Arbeit, s.
// Tests/panel_shot.cpp).
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build-ui --target editor_shot && build-ui/editor_shot

#include "PluginProcessor.h"
#include "UI/SwarmPanel.h"

#include <cstdio>
#include <memory>

namespace
{
void shoot (juce::Component& c, const juce::String& name)
{
    juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);

    {
        juce::Graphics g (image);
        c.paintEntireComponent (g, true);
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

    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    // Das ganze Fenster: darin die Scope-Werkzeugleiste mit dem Play-Knopf.
    if (auto* editor = proc.createEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> owned (editor);
        shoot (*owned, "editor_full");
    }

    // Das Schwarm-Panel einzeln, in seiner Groesse aus dem Editor
    // (panelColumnWidth abzueglich Scrollbalken, swarmContentHeight): darin
    // die Reglerreihe mit dem Z-Anteil.
    SwarmPanel swarm (proc.apvts);
    swarm.setSize (462, 171);
    shoot (swarm, "panel_swarm");

    return 0;
}
