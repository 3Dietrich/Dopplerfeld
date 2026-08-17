#include "PluginEditor.h"
#include "Params.h"

#include <cmath>

DopplerfeldEditor::DopplerfeldEditor (DopplerfeldProcessor& p)
    : AudioProcessorEditor (&p),
      dopplerfeldProcessor (p),
      enginePanel (p.apvts),
      samplePanel (p.apvts),
      motionPanel (p.apvts),
      fieldPanel  (p.apvts)
{
    addAndMakeVisible (field);

    // Ziehen im Feld schreibt auf dieselben Parameter, die auch der Host
    // automatisiert - der Weg in die Physik ist für Maus und Automation
    // derselbe (Plan 3.12: der GUI-Thread schreibt nur Ziele).
    field.onSourceDragged = [this] (double normX, double normY)
    {
        setParameter (Params::srcX, normX);
        setParameter (Params::srcY, normY);
    };

    field.onListenerDragged = [this] (double normX, double normY)
    {
        setParameter (Params::lisX, normX);
        setParameter (Params::lisY, normY);
    };

    field.onListenerRotated = [this] (double yawRadians)
    {
        setParameter (Params::lisYaw, juce::radiansToDegrees (yawRadians));
    };

    enginePanelBox.setContent (&enginePanel);
    samplePanelBox.setContent (&samplePanel);
    motionPanelBox.setContent (&motionPanel);
    fieldPanelBox.setContent (&fieldPanel);

    // Motor aufgeklappt (die Default-Quelle), der Rest zugeklappt - sonst
    // steht die Spalte beim Öffnen sofort voll.
    samplePanelBox.setExpanded (false);
    motionPanelBox.setExpanded (false);
    fieldPanelBox.setExpanded (true);

    for (auto* box : { &enginePanelBox, &samplePanelBox, &motionPanelBox, &fieldPanelBox })
    {
        box->onExpandedChanged = [this] { layoutPanels(); };
        panelHolder.addAndMakeVisible (box);
    }

    panelViewport.setViewedComponent (&panelHolder, false);
    panelViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (panelViewport);

    samplePanel.onFileSelected = [this] (const juce::File& file)
    {
        dopplerfeldProcessor.loadSampleFile (file);
    };

    motionPanel.onRecordClicked = [this] { dopplerfeldProcessor.toggleRecording(); };
    motionPanel.onPlayClicked   = [this]
    {
        if (dopplerfeldProcessor.isPlayingMotion())
            dopplerfeldProcessor.stopPlayback();
        else
            dopplerfeldProcessor.triggerPlayback();
    };

    sourceButton.setTooltip ("Klangquelle umschalten: Motor-Generator oder geladenes Sample.");
    sourceButton.onClick = [this]
    {
        dopplerfeldProcessor.selectSampleSource (! dopplerfeldProcessor.isUsingSampleSource());
    };
    addAndMakeVisible (sourceButton);

    field.setTooltip ("Ziehen an M verschiebt die Schallquelle. Ziehen am Kopf verschiebt "
                      "den Hoerer, Ziehen an der Nase dreht ihn.");

    // @dpa-Feedback: Hilfehinweise abschaltbar. Start an, weil neue Regler
    // ohne Erklaerung sonst raten heisst.
    tooltipsButton.setToggleState (true, juce::dontSendNotification);
    tooltipsButton.setTooltip ("Hilfehinweise beim Ueberfahren der Regler ein-/ausblenden.");
    tooltipsButton.onClick = [this] { tooltipWindow.enabled = tooltipsButton.getToggleState(); };
    addAndMakeVisible (tooltipsButton);

    setSize (margin * 2 + fieldWidth + margin + panelColumnWidth,
             margin * 2 + topBarHeight + 6 + fieldHeight + statusHeight);

    // 30 Hz: schnell genug, dass eine gezogene Quelle nicht ruckelt, und
    // langsam genug, dass die Wellenfronten nicht flimmern.
    startTimerHz (30);
}

void DopplerfeldEditor::setParameter (const char* paramID, double value)
{
    if (auto* parameter = dopplerfeldProcessor.apvts.getParameter (paramID))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
}

void DopplerfeldEditor::timerCallback()
{
    dopplerfeldProcessor.fillFieldSnapshot (snapshot);

    field.setFieldMetres ((double) *dopplerfeldProcessor.apvts.getRawParameterValue (Params::fieldMetres));
    field.setSnapshot (snapshot);

    sourceButton.setButtonText (dopplerfeldProcessor.isUsingSampleSource() ? "Quelle: Sample"
                                                                           : "Quelle: Motor");
    motionPanel.setPlaying (dopplerfeldProcessor.isPlayingMotion());

    // 30Hz-Timer = ~33ms zwischen zwei Aufrufen (siehe startTimerHz weiter
    // unten) - fest verdrahtet statt gemessen, das Levelmeter braucht nur
    // eine grobe Zeitbasis für Decay/Clip-Halt, keine exakte.
    fieldPanel.pushLevels (dopplerfeldProcessor.consumeOutputPeakL(),
                           dopplerfeldProcessor.consumeOutputPeakR(),
                           1000.0 / 30.0);

    // Statuszeile neu zeichnen, nicht das ganze Fenster - die Panels darüber
    // ändern sich nur bei Bedienung.
    repaint (margin, getHeight() - statusHeight, fieldWidth, statusHeight);
}

juce::String DopplerfeldEditor::statusText() const
{
    juce::String text;

    // Feste Breite pro Zahl (printf-Padding), zusammen mit dem Monospace-Font
    // in paint(): bei jedem Timer-Tick ändern sich diese Werte, ohne feste
    // Breite verschiebt eine kürzer werdende Zahl (z.B. "9.3" -> "-9.3")
    // allen nachfolgenden Text um ein wechselndes Stück - die ganze Zeile
    // "wackelt". Mit fester Zeichenbreite bleiben Spalten stehen.
    text << "M  x " << juce::String::formatted ("%7.1f", snapshot.sourcePos.x)
         << " m   y " << juce::String::formatted ("%7.1f", snapshot.sourcePos.y) << " m";

    // @dpa-Feedback: CPU-Echtzeit-Anzeige (Wanduhrzeit/Audiozeit, geglättet -
    // siehe cpuLoadPercent()). Über 100% färbt paint() die ganze Statuszeile
    // rot (siehe dort) - reiner Text reicht hier, kein eigener Meter nötig.
    const float cpu = dopplerfeldProcessor.cpuLoadPercent();
    text << "      CPU " << juce::String::formatted ("%4.0f", (double) cpu) << " %"
         << " (Physik " << juce::String::formatted ("%4.0f", (double) dopplerfeldProcessor.cpuLoadPhysicsPercent()) << "%"
         << " / Quelle " << juce::String::formatted ("%4.0f", (double) dopplerfeldProcessor.cpuLoadSourcePercent()) << "%)";

    for (int i = 0; i < snapshot.pathCount; ++i)
    {
        const auto& info = snapshot.paths[(size_t) i];

        // Spiegelpfade mit ' markiert (L' / R'), sonst stünden bei
        // eingeschalteter Bodenreflexion vier gleich aussehende Blöcke da.
        text << "      " << (info.ear == 0 ? "L" : "R") << (info.mirrored ? "'" : " ")
             << " " << juce::String::formatted ("%7.1f", info.delaySeconds * 1000.0) << " ms"
             << "  M_r " << juce::String::formatted ("%5.2f", info.machRadial)
             << "  Zweige " << juce::String::formatted ("%2d", info.activeBranches);
    }

    if (dopplerfeldProcessor.isRecording())
        text << "      Aufnahme " << dopplerfeldProcessor.recordedFrameCount() << " Frames";
    else if (dopplerfeldProcessor.isPlayingMotion())
        text << "      Wiedergabe";

    return text;
}

void DopplerfeldEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (13.0f);
    g.drawText ("dopplerfeld", margin, margin, 100, topBarHeight, juce::Justification::centredLeft);

    // CPU über 100% ist hörbar (Aussetzer) - die ganze Statuszeile färbt sich
    // dafür rot, statt nur die Zahl selbst hervorzuheben. Einfacher als ein
    // gemischtfarbiger Text und im Zweifel eher zu auffällig als übersehen.
    const bool overBudget = dopplerfeldProcessor.cpuLoadPercent() > 100.0f;
    g.setColour (overBudget ? juce::Colours::orangered.withAlpha (0.85f)
                            : juce::Colours::white.withAlpha (0.6f));
    // Monospace statt Proportionalschrift: nur bei fester Zeichenbreite pro
    // Glyphe hält das Zahlen-Padding in statusText() die Spalten auch
    // tatsaechlich stabil (siehe Kommentar dort).
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    g.drawFittedText (statusText(),
                      margin, getHeight() - statusHeight, fieldWidth, statusHeight,
                      juce::Justification::topLeft, 2);
}

void DopplerfeldEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    auto topBar = area.removeFromTop (topBarHeight);
    topBar.removeFromLeft (100);   // Platz für den Schriftzug links
    sourceButton.setBounds (topBar.removeFromLeft (130));
    topBar.removeFromLeft (8);
    tooltipsButton.setBounds (topBar.removeFromLeft (130));

    area.removeFromTop (6);

    auto fieldArea = area.removeFromLeft (fieldWidth);
    field.setBounds (fieldArea.removeFromTop (fieldHeight));

    area.removeFromLeft (margin);
    panelViewport.setBounds (area);

    layoutPanels();
}

void DopplerfeldEditor::layoutPanels()
{
    const int width = juce::jmax (100, panelViewport.getMaximumVisibleWidth());

    struct Entry { CollapsiblePanel* box; int contentHeight; };

    const Entry entries[] {
        { &enginePanelBox, engineContentHeight },
        { &samplePanelBox, sampleContentHeight },
        { &motionPanelBox, motionContentHeight },
        { &fieldPanelBox,  fieldContentHeight  }
    };

    int y = 0;

    for (const auto& entry : entries)
    {
        const int height = entry.box->isExpanded() ? CollapsiblePanel::headerHeight + entry.contentHeight
                                                    : CollapsiblePanel::headerHeight;

        entry.box->setBounds (0, y, width, height);
        y += height + 4;
    }

    panelHolder.setSize (width, y);
}
