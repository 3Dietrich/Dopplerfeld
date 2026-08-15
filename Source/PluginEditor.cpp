#include "PluginEditor.h"

DopplerfeldEditor::DopplerfeldEditor (DopplerfeldProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (700, 400);
}

void DopplerfeldEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText ("dopplerfeld - Gerust (H1)", getLocalBounds(), juce::Justification::centred, 1);
}
