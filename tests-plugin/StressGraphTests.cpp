#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "StressGraphGenerator.h"
#include <chrono>
#include <cmath>

using namespace bazalt;

namespace
{
    void deleteDefaultGraphNodes (GraphEditController& controller)
    {
        // Only safe to call *after* the output has already been
        // redirected elsewhere (e.g. by buildStressReductionGraph's own
        // setOutput call) — deleting "amp" while it's still the graph's
        // designated output would fail to compile (no output node found)
        // and roll back, silently leaving it in place.
        for (const auto& id : { "noteIn", "allocator", "osc", "svf", "env", "amp" })
            controller.deleteNode (id);
    }
}

TEST_CASE ("A 250-source (500-node) stress graph compiles and runs entirely through the M7 command path",
           "[plugin][GraphEditController][stress][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();

    constexpr int numSources = 250; // -> 499 nodes, 498 connections (StressGraphGenerator.h)
    const auto stats = buildStressReductionGraph (controller, numSources);
    deleteDefaultGraphNodes (controller);

    CHECK (stats.numNodes == 499);
    CHECK (stats.numConnections == 498);
    CHECK (controller.getGraph().getNodes().size() == (size_t) stats.numNodes);
    CHECK (controller.getGraph().getConnections().size() == (size_t) stats.numConnections);

    // The graph has no voice-triggering nodes at all (pure util.* nodes),
    // so it renders identically whether or not a note is active — just
    // process blocks and confirm the compiled plan produces real, finite,
    // non-silent output (the reduction tree sums numSources distinct
    // constant values, so it's provably not just leftover silence).
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    for (int block = 0; block < 5; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    bool anyNonZero = false;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto sample = buffer.getSample (ch, i);
            REQUIRE (std::isfinite (sample));
            if (sample != 0.0f)
                anyNonZero = true;
        }
    }

    CHECK (anyNonZero);
}

TEST_CASE ("Building the 500-node stress graph via applyBatch completes in a fraction of a second",
           "[plugin][GraphEditController][stress][NODE_EDITOR]")
{
    // Not a hard perf gate (CI hardware varies) — a sanity ceiling proving
    // one applyBatch call building ~1000 graph-model mutations (250
    // constants + 249 reduction adds, 498 connections) and compiling once
    // stays fast. Worth remembering why this is a
    // batch and not individual addNode/connect commands: the first version
    // of this test issued ~750 *individual* commands (each its own full
    // 8-voice recompile+publish) and measured in the tens of seconds —
    // exactly the "composite operations need to be one recompile, not N"
    // problem NODE_EDITOR.md §6 already flagged for future UI gestures
    // (splice, Unwrap, ...), just discovered here first via this
    // generator. See GraphEditController::applyBatch and ADR-0009.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();

    const auto start = std::chrono::steady_clock::now();
    buildStressReductionGraph (controller, 250);
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    INFO ("Stress graph build (single applyBatch, 500 nodes) took " << elapsed << "s");
    CHECK (elapsed < 5.0);
}
