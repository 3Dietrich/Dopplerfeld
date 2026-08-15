#include "PluginProcessor.h"
#include "PluginEditor.h"

DopplerfeldProcessor::DopplerfeldProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DopplerfeldProcessor::createParameterLayout()
{
    // H1: bewusst leer. Das vollständige Layout (Feld/Quelle/Hörer/Motor/
    // Sample/Bewegung/Physik/Crossfade/Ausgang, siehe Plan Abschnitt 3.11)
    // kommt mit H2 in eine eigene Params.cpp.
    return {};
}

void DopplerfeldProcessor::prepareToPlay (double, int)
{
}

bool DopplerfeldProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DopplerfeldProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
}

juce::AudioProcessorEditor* DopplerfeldProcessor::createEditor()
{
    return new DopplerfeldEditor (*this);
}

void DopplerfeldProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void DopplerfeldProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// Diese Fabrikfunktion verlangt JUCE von jedem Plugin-Projekt.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DopplerfeldProcessor();
}
