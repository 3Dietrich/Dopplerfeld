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
#include "UI/CollapsiblePanel.h"
#include "UI/SwarmPanel.h"

#include <cstdio>
#include <memory>
#include <vector>

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

// Klappt jedes CollapsiblePanel unterhalb von `root` auf. Die Panels der
// rechten Spalte starten zugeklappt; zugeklappt sieht man aber nur die
// Kopfzeilen und nicht, wie die Flaechen darunter wirken.
void collectPanels (juce::Component& root, std::vector<CollapsiblePanel*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* panel = dynamic_cast<CollapsiblePanel*> (child))
            out.push_back (panel);
        else
            collectPanels (*child, out);
    }
}

// Klappt genau die Panels auf, deren Nummer in [first, last] liegt, und alle
// anderen zu. Die Spalte ist hoeher als das Fenster - offene Panels weiter
// unten waeren sonst nie im Bild.
void showPanelRange (juce::Component& root, int first, int last)
{
    std::vector<CollapsiblePanel*> panels;
    collectPanels (root, panels);

    for (int i = 0; i < (int) panels.size(); ++i)
        panels[(size_t) i]->setExpanded (i >= first && i <= last);
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

        // Die Spalte in zwei Haelften, jeweils offen - so wird sichtbar, wie
        // sich die Bereiche voneinander absetzen. Alles gleichzeitig offen
        // passt nicht ins Fenster.
        showPanelRange (*owned, 0, 2);
        shoot (*owned, "editor_panels_oben");

        showPanelRange (*owned, 3, 6);
        shoot (*owned, "editor_panels_unten");
    }

    // Das Schwarm-Panel einzeln, in seiner Groesse aus dem Editor
    // (panelColumnWidth abzueglich Scrollbalken, swarmContentHeight): darin
    // die Reglerreihe mit dem Z-Anteil.
    SwarmPanel swarm (proc.apvts);
    swarm.setSize (462, 171);
    shoot (swarm, "panel_swarm");

    return 0;
}
