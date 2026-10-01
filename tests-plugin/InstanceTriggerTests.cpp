// Domain Extensions batch — instance.allocate.trigger's real plugin-level
// behavior: reuses the exact InstanceOriginNode-generalized relay Batch 3
// built, with maxInstances implicitly 1 (VoiceManager's own existing
// stealing policy, not any new node-level logic, is what makes "a second
// trigger re-triggers the same instance" true at the compiled-graph level).
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

    // clock.pulse (default 2Hz, free-running) -> the trigger's own "trigger"
    // Event input -> random1 (adapted) drives osc's pitch -> instance.sum ->
    // the processor's own default Master Out. Same minimal shape as
    // InstanceSwarmPopulationTests.cpp/InstanceSwarmTransientTests.cpp.
    bool buildTriggerGraph (GraphEditController& controller)
    {
        if (! controller.addNode ("instance.allocate.trigger", "trig", 0.0f, 0.0f).success)
            return false;
        if (! controller.addNode ("clock.pulse", "clock", -200.0f, 0.0f).success)
            return false;
        if (! controller.connect ("clock", "tick", "trig", "trigger").success) // Event -> Event, no adapter needed
            return false;
        if (! controller.addNode ("osc.analog", "osc", 200.0f, 0.0f).success)
            return false;
        if (! controller.addNode ("instance.sum", "sum", 400.0f, 0.0f).success)
            return false;
        if (! controller.connectWithAutoAdapt ("trig", "random1", "osc", "pitch").success)
            return false;
        if (! controller.connect ("osc", "out", "sum", "in").success)
            return false;
        return controller.connect ("sum", "out", "masterOut", "in").success;
    }
}

TEST_CASE ("instance.allocate.trigger's own trigger Event dispatches through the same generalized "
           "relay Batch 3 built - a real VoiceManager voice gets allocated and real audio reaches "
           "the output",
           "[plugin][InstanceTrigger]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (buildTriggerGraph (controller));

    const auto indices = controller.getOriginBundleIndices();
    const auto it = indices.find ("trig");
    REQUIRE (it != indices.end());

    // No spawn observed yet - the relay's own documented one-block latency
    // (InstanceSwarmTransientTests.cpp's own comment has the full reasoning).
    CHECK (processor.getOriginActiveVoiceCount (it->second) == 0);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer noMidi;
    buffer.clear();
    processor.processBlock (buffer, noMidi); // clock's own tick 0 fires inside this block
    buffer.clear();
    processor.processBlock (buffer, noMidi); // relay observes it, dispatches a real voice

    CHECK (processor.getOriginActiveVoiceCount (it->second) > 0);
    CHECK (rms (buffer, 0) > 0.001f);
}

TEST_CASE ("instance.allocate.trigger's instance-count badge always reports a ceiling of 1 - "
           "maxInstances is implicitly 1, never an editable parameter",
           "[plugin][InstanceTrigger]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (buildTriggerGraph (controller));

    const auto indices = controller.getOriginBundleIndices();
    const auto it = indices.find ("trig");
    REQUIRE (it != indices.end());

    CHECK (processor.getOriginMaxVoices (it->second) == 1);
}
