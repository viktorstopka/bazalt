// Domain Extensions batch — instance.allocate.swarmTransient's real
// plugin-level behavior: the first non-Voice origin to exercise
// PluginProcessor's internal-trigger-relay mechanism through the real
// InstanceOriginNode generalization (findAllocatorNode/renderOriginVoiceRange,
// 09-28-InstanceAllocator's own DomainRedesign.md Batch 1b machinery) rather
// than a hand-poked node. Unlike instance.allocate.swarmPopulation (always
// live from block 0), this origin's voice-pool slots stay genuinely Idle
// (silent, skipped by renderOriginVoiceRange's own loop) until something
// actually fires its "spawn" Event input.
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "GraphEditController.h"
#include <cmath>

using namespace bazalt;

namespace
{
    float rms (const juce::AudioBuffer<float>& buffer, int channel)
    {
        double sumSquares = 0.0;
        const auto* data = buffer.getReadPointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sumSquares += (double) data[i] * (double) data[i];
        return (float) std::sqrt (sumSquares / buffer.getNumSamples());
    }

    // clock.pulse (default 2Hz, free-running, nothing wired into its own
    // "spawn"-relevant inputs) -> swarm's "spawn" Event input -> random1
    // (adapted) drives osc's pitch -> instance.sum -> the processor's own
    // default Master Out. No io.noteIn, no envelope - deliberately minimal,
    // same spirit as InstanceSwarmPopulationTests.cpp's own test graph.
    bool buildSwarmTransientGraph (GraphEditController& controller, int maxInstances)
    {
        if (! controller.addNode ("instance.allocate.swarmTransient", "swarm", 0.0f, 0.0f).success)
            return false;
        if (! controller.setParameterValue (
                "swarm", "instance.allocate.swarmTransient.maxInstances", (float) maxInstances).success)
            return false;
        if (! controller.addNode ("clock.pulse", "clock", -200.0f, 0.0f).success)
            return false;
        if (! controller.connect ("clock", "tick", "swarm", "spawn").success) // Event -> Event, no adapter needed
            return false;
        if (! controller.addNode ("osc.analog", "osc", 200.0f, 0.0f).success)
            return false;
        if (! controller.addNode ("instance.sum", "sum", 400.0f, 0.0f).success)
            return false;
        if (! controller.connectWithAutoAdapt ("swarm", "random1", "osc", "pitch").success)
            return false;
        if (! controller.connect ("osc", "out", "sum", "in").success)
            return false;
        return controller.connect ("sum", "out", "masterOut", "in").success;
    }
}

TEST_CASE ("instance.allocate.swarmTransient's own spawn Event dispatches through the real "
           "InstanceOriginNode-generalized relay - a genuine VoiceManager voice gets allocated "
           "(not just the node's own internal gate flag), and real audio reaches the output",
           "[plugin][InstanceSwarmTransient]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (buildSwarmTransientGraph (controller, 8));

    const auto indices = controller.getOriginBundleIndices();
    const auto it = indices.find ("swarm");
    REQUIRE (it != indices.end());

    // ClockPulseNode's own tick-0 fires essentially immediately
    // (thresholdFor(0,...) == 0.0 - the very first sample already satisfies
    // it), not after a full period - but renderOriginVoiceRange's own
    // relay has a documented one-block latency by design (it reads slot 0's
    // consumeSpawnEventsThisBlock() for what happened on the PREVIOUS block,
    // checked BEFORE this block's own slot-0 render runs) - so the very
    // first block observes nothing yet, and the second one does.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer noMidi; // deliberately empty - this origin needs none
    buffer.clear();
    processor.processBlock (buffer, noMidi);
    buffer.clear();
    processor.processBlock (buffer, noMidi);

    // The real dispatch chain: findAllocatorNode (now InstanceOriginNode*) ->
    // consumeSpawnEventsThisBlock() -> getGate() -> VoiceManager::noteOn() -
    // proven by a REAL voice now being active, not read off the node's own
    // internal state directly.
    CHECK (processor.getOriginActiveVoiceCount (it->second) > 0);
    CHECK (rms (buffer, 0) > 0.001f);
}

TEST_CASE ("instance.allocate.swarmTransient's instance-count badge reports VoiceManager's own real "
           "active count (0 before any spawn) and the real, configured maxInstances ceiling",
           "[plugin][InstanceSwarmTransient]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (buildSwarmTransientGraph (controller, 3));

    const auto indices = controller.getOriginBundleIndices();
    const auto it = indices.find ("swarm");
    REQUIRE (it != indices.end());

    // Nothing has spawned yet - a real VoiceManager-driven count, not
    // swarmPopulation's own "always populationSize" badge bypass.
    CHECK (processor.getOriginActiveVoiceCount (it->second) == 0);
    CHECK (processor.getOriginMaxVoices (it->second) == 3);
}
