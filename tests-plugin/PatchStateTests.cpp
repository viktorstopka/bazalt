#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "MacroParameters.h"
#include <cmath>

using namespace bazalt;

TEST_CASE ("Plugin state round-trips macro values exactly through getStateInformation/setStateInformation",
           "[plugin][patch]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& parameters = processor.getParameters();
    REQUIRE (parameters.size() >= MacroParameters::numMacros);

    *dynamic_cast<juce::AudioParameterFloat*> (parameters[0]) = 0.35f;
    *dynamic_cast<juce::AudioParameterFloat*> (parameters[1]) = 0.812345f;
    *dynamic_cast<juce::AudioParameterFloat*> (parameters[2]) = 1.0f;

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    BazaltAudioProcessor reloaded;
    reloaded.prepareToPlay (44100.0, 512);
    reloaded.setStateInformation (state.getData(), (int) state.getSize());

    auto& reloadedParameters = reloaded.getParameters();

    for (int i = 0; i < MacroParameters::numMacros; ++i)
    {
        auto* original = dynamic_cast<juce::AudioParameterFloat*> (parameters[i]);
        auto* restored = dynamic_cast<juce::AudioParameterFloat*> (reloadedParameters[i]);

        REQUIRE (original != nullptr);
        REQUIRE (restored != nullptr);
        CHECK (restored->get() == original->get()); // bit-identical, not approximate
    }
}

TEST_CASE ("Plugin state's graph/macro content round-trips exactly (meta.modifiedAtMs deliberately excluded)",
           "[plugin][patch]")
{
    // meta.modifiedAtMs is stamped with juce::Time::getCurrentTime() on
    // every getStateAsJson() call by design (it reflects "when this was
    // serialized", not stored state) — comparing it across two separate
    // calls would spuriously differ by design, not by bug. The exit
    // criterion this test earns its keep against is specifically
    // "parameter/macro state", so that's what gets compared here.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    const auto json = processor.getStateAsJson();
    REQUIRE (json.isNotEmpty());

    BazaltAudioProcessor reloaded;
    reloaded.prepareToPlay (44100.0, 512);
    REQUIRE (reloaded.loadStateFromJson (json));

    const auto original = processor.getCurrentPatchDocument();
    const auto restored = reloaded.getCurrentPatchDocument();

    REQUIRE (restored.nodes.size() == original.nodes.size());
    for (size_t i = 0; i < original.nodes.size(); ++i)
    {
        CHECK (restored.nodes[i].id == original.nodes[i].id);
        CHECK (restored.nodes[i].type == original.nodes[i].type);

        for (const auto& [paramId, value] : original.nodes[i].parameters)
        {
            REQUIRE (restored.nodes[i].parameters.count (paramId) == 1);
            CHECK (restored.nodes[i].parameters.at (paramId) == value);
        }
    }

    REQUIRE (restored.connections.size() == original.connections.size());
    for (size_t i = 0; i < original.connections.size(); ++i)
    {
        CHECK (restored.connections[i].fromNodeId == original.connections[i].fromNodeId);
        CHECK (restored.connections[i].toNodeId == original.connections[i].toNodeId);
    }

    REQUIRE (restored.macroMappings.size() == original.macroMappings.size());
    REQUIRE (restored.macroValues.size() == original.macroValues.size());
    for (size_t i = 0; i < original.macroValues.size(); ++i)
        CHECK (restored.macroValues[i] == original.macroValues[i]);
}

TEST_CASE ("Loading malformed state data is rejected without crashing or corrupting the processor",
           "[plugin][patch]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    const char garbage[] = "not json at all { [ }";
    processor.setStateInformation (garbage, (int) sizeof (garbage));

    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    buffer.clear();
    processor.processBlock (buffer, midi);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}
