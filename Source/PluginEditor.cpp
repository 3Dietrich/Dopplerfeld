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
    motionPanel.onPlayClicked   = [this] { dopplerfeldProcessor.triggerPlayback(); };

    sourceButton.onClick = [this]
    {
        dopplerfeldProcessor.selectSampleSource (! dopplerfeldProcessor.isUsingSampleSource());
    };
    addAndMakeVisible (sourceButton);

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

    // Statuszeile neu zeichnen, nicht das ganze Fenster - die Panels darüber
    // ändern sich nur bei Bedienung.
    repaint (margin, getHeight() - statusHeight, fieldWidth, statusHeight);
}

juce::String DopplerfeldEditor::statusText() const
{
    juce::String text;

    text << "M  x " << juce::String (snapshot.sourcePos.x, 1)
         << " m   y " << juce::String (snapshot.sourcePos.y, 1) << " m";

    for (int i = 0; i < snapshot.pathCount; ++i)
    {
        const auto& info = snapshot.paths[(size_t) i];

        text << "      " << (info.ear == 0 ? "L" : "R")
             << " " << juce::String (info.delaySeconds * 1000.0, 1) << " ms"
             << "  M_r " << juce::String (info.machRadial, 2)
             << "  Zweige " << info.activeBranches;
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

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (12.0f);
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
