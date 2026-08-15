#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// H1: leeres Gerüst, gibt Stille aus. Physik-Kern (Retarded-Time-Löser,
// PropagationPath, Crossfade-Engine, Motor-Generator) kommt in den
// folgenden Häppchen dazu - siehe dopplerfeld-plan.md.
class DopplerfeldProcessor : public juce::AudioProcessor
{
public:
    DopplerfeldProcessor();
    ~DopplerfeldProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dopplerfeld"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    // Platzhalter-Layout für H1. Wird durch Params::createParameterLayout()
    // aus Source/Params.cpp ersetzt, sobald H2 steht.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopplerfeldProcessor)
};
