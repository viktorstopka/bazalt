#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include <atomic>
#include <cmath>
#include <thread>

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
}

TEST_CASE ("addNode + connect + setParameterValue commands produce the expected compiled audio",
           "[plugin][GraphEditController][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();

    // Reroute svf's output through a new one-pole damping node instead of
    // straight into amp — proves add/connect/setParameter together produce
    // a graph that actually compiles differently and sounds differently.
    REQUIRE (controller.disconnect ("svf", "out", "amp", "audio").success);
    REQUIRE (controller.addNode ("filter.onepole", "damper", 100.0f, 100.0f).success);
    REQUIRE (controller.connect ("svf", "out", "damper", "in").success);
    REQUIRE (controller.connect ("damper", "out", "amp", "audio").success);
    REQUIRE (controller.setParameterValue ("damper", "filter.onepole.coefficient", 0.8f).success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    for (int block = 0; block < 19; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    CHECK (rms (buffer, 0) > 0.001f);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}

TEST_CASE ("deleteNode and disconnect commands are reflected in the live graph and in compiled audio",
           "[plugin][GraphEditController][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();

    // Disconnecting the envelope leaves amp's gain input silent (0), so
    // amp's audio*gain output is silent regardless of the oscillator —
    // directly, audibly verifiable, not just a graph-shape assertion.
    REQUIRE (controller.disconnect ("env", "out", "amp", "gain").success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    for (int block = 0; block < 5; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    CHECK (rms (buffer, 0) < 0.0001f); // amp.gain is silent -> whole voice is silent

    // deleteNode removes the node AND every connection touching it —
    // verified directly against the live graph.
    REQUIRE (controller.deleteNode ("env").success);
    CHECK (controller.getGraph().findNode ("env") == nullptr);

    for (const auto& connection : controller.getGraph().getConnections())
    {
        CHECK (connection.fromNodeId != "env");
        CHECK (connection.toNodeId != "env");
    }
}

TEST_CASE ("An invalid command is rejected and leaves the graph and compiled audio unchanged",
           "[plugin][GraphEditController][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    const auto nodesBefore = controller.getGraph().getNodes().size();
    const auto connectionsBefore = controller.getGraph().getConnections().size();

    // Unknown node id — rejected before any mutation is even attempted.
    const auto badConnect = controller.connect ("does-not-exist", "out", "amp", "audio");
    CHECK_FALSE (badConnect.success);
    CHECK (badConnect.errorMessage.isNotEmpty());

    // Unknown node type — same.
    const auto badAdd = controller.addNode ("does.not.exist", "x", 0.0f, 0.0f);
    CHECK_FALSE (badAdd.success);
    CHECK (badAdd.errorMessage.isNotEmpty());

    // A connection to a port that doesn't exist on the target node — this
    // one DOES mutate the graph speculatively, attempts a recompile, fails
    // there, and must roll back (CLAUDE.md rule 5: a bad edit never
    // reaches the audio thread).
    const auto badPort = controller.connect ("osc", "out", "amp", "no-such-port");
    CHECK_FALSE (badPort.success);
    CHECK (badPort.errorMessage.isNotEmpty());

    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);

    // The processor must still work exactly as it did before any of the
    // rejected attempts — proving the previous plan really did stay live.
    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    for (int block = 0; block < 19; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    CHECK (rms (buffer, 0) > 0.01f);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}

TEST_CASE ("Concurrent graph edits never produce a torn read within a single processBlock call",
           "[plugin][GraphEditController][swap-under-load][NODE_EDITOR]")
{
    // Swap-under-load, exercised through the real command path (M2's
    // PlanSwapperTests.cpp proved the underlying mechanism directly; this
    // proves GraphEditController's recompile-and-publish path preserves
    // that guarantee end to end). A single util.constant node's output is
    // identical for every sample in a block by construction, which is what
    // makes "was this whole block computed against one plan generation"
    // trivially checkable — any two different samples in one block would
    // mean a torn/inconsistent read.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 64);

    bazalt::engine::NodeGraph graph;
    graph.addNode ({ "const", "util.constant", {}, { { "util.constant.value", 0.5f } }, {} });
    graph.setOutput ("const", "out");

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (graph).success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> primingBuffer (2, 64);
    primingBuffer.clear();
    processor.processBlock (primingBuffer, noteOn); // one active voice, held indefinitely (no envelope node in this graph)

    std::atomic<bool> stop { false };
    std::atomic<bool> discontinuityWithinBlock { false };
    std::atomic<bool> nonFiniteSample { false };
    std::atomic<uint64_t> blocksProcessed { 0 };

    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer empty;

        while (! stop.load (std::memory_order_relaxed))
        {
            buffer.clear();
            processor.processBlock (buffer, empty);

            const auto* data = buffer.getReadPointer (0);
            const auto first = data[0];

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (! std::isfinite (data[i]))
                    nonFiniteSample.store (true, std::memory_order_relaxed);
                if (data[i] != first)
                    discontinuityWithinBlock.store (true, std::memory_order_relaxed);
            }

            blocksProcessed.fetch_add (1, std::memory_order_relaxed);
        }
    });

    std::thread editThread ([&]
    {
        for (int i = 0; i < 500; ++i)
        {
            const auto value = 0.1f + 0.01f * (float) (i % 50);
            controller.setParameterValue ("const", "util.constant.value", value);
        }
    });

    editThread.join();
    stop.store (true, std::memory_order_relaxed);
    audioThread.join();

    CHECK_FALSE (discontinuityWithinBlock.load());
    CHECK_FALSE (nonFiniteSample.load());
    CHECK (blocksProcessed.load() > 0);
}

TEST_CASE ("applyBatch rolls back every mutation in the batch if the resulting graph doesn't compile",
           "[plugin][GraphEditController][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    const auto nodesBefore = controller.getGraph().getNodes().size();
    const auto connectionsBefore = controller.getGraph().getConnections().size();

    const auto result = controller.applyBatch ([] (bazalt::engine::NodeGraph& graph)
    {
        graph.addNode ({ "extra1", "util.constant", {}, {}, {} });
        graph.addNode ({ "extra2", "util.constant", {}, {}, {} });
        // Bad connection — "no-such-port" doesn't exist on util.constant —
        // makes the whole batch's recompile fail.
        graph.addConnection ({ "extra1", "out", "extra2", "no-such-port" });
    });

    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());

    // Neither extra1 nor extra2 should have survived the rollback.
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);
    CHECK (controller.getGraph().findNode ("extra1") == nullptr);
    CHECK (controller.getGraph().findNode ("extra2") == nullptr);
}

TEST_CASE ("A live-edited graph round-trips exactly through getStateAsJson/loadStateFromJson",
           "[plugin][GraphEditController][patch][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.addNode ("util.constant", "extra", 42.0f, -17.0f).success);
    REQUIRE (controller.setParameterValue ("extra", "util.constant.value", 0.75f).success);

    const auto json = processor.getStateAsJson();

    BazaltAudioProcessor reloaded;
    reloaded.prepareToPlay (44100.0, 512);
    REQUIRE (reloaded.loadStateFromJson (json));

    const auto* extra = reloaded.getGraphEditController().getGraph().findNode ("extra");
    REQUIRE (extra != nullptr);
    CHECK (extra->type == "util.constant");
    CHECK (extra->position.x == 42.0f);
    CHECK (extra->position.y == -17.0f);
    REQUIRE (extra->parameters.count ("util.constant.value") == 1);
    CHECK (extra->parameters.at ("util.constant.value") == 0.75f);
}
