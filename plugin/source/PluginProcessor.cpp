#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace bazalt
{
    BazaltAudioProcessor::BazaltAudioProcessor()
        : juce::AudioProcessor (BusesProperties()
                                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    void BazaltAudioProcessor::prepareToPlay (double, int)
    {
    }

    void BazaltAudioProcessor::releaseResources()
    {
    }

    void BazaltAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
    }

    juce::AudioProcessorEditor* BazaltAudioProcessor::createEditor()
    {
        return new BazaltAudioProcessorEditor (*this);
    }

    const juce::String BazaltAudioProcessor::getName() const
    {
        return JucePlugin_Name;
    }

    void BazaltAudioProcessor::getStateInformation (juce::MemoryBlock&)
    {
        // Patch save/load lands in M3 (ARCHITECTURE.md §4.4) — plugin state
        // IS the patch, no separate serialization format.
    }

    void BazaltAudioProcessor::setStateInformation (const void*, int)
    {
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bazalt::BazaltAudioProcessor();
}
