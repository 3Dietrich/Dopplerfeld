#pragma once

#include "PluginProcessor.h"

// H1: reiner Platzhalter. Das 700x400-Feld (FieldComponent) mit M/L
// kommt mit H11, die einklappbaren Panels mit H12 - siehe dopplerfeld-plan.md.
class DopplerfeldEditor : public juce::AudioProcessorEditor
{
public:
    explicit DopplerfeldEditor (DopplerfeldProcessor&);
    ~DopplerfeldEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override {}

private:
    DopplerfeldProcessor& dopplerfeldProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopplerfeldEditor)
};
