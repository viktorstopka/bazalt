// Audio -> Control Bridge (wiki/plans/AudioControlBridge.md). AudioToControlNode's
// own unit behaviour, plus a real compiled-graph proof that routing a raw
// waveform through it into another oscillator's phaseMod genuinely produces
// audio-rate phase modulation - not just that the graph compiles.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/AudioToControlNode.h"
#include "bazalt/engine/nodes/SineOscillatorNode.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include <cmath>
#include <limits>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    float processOnce (AudioToControlNode& node, float in, float depth = kNaN)
    {
        const float inputs[2] = { in, depth };
        float output = 0.0f;
        node.processSample (inputs, &output);
        return output;
    }

    float rmsOf (const std::vector<float>& samples)
    {
        double sumSquares = 0.0;
        for (auto s : samples)
            sumSquares += (double) s * (double) s;
        return (float) std::sqrt (sumSquares / (double) samples.size());
    }
}

TEST_CASE ("AudioToControlNode scales by depth and passes the waveform through unclamped in range",
           "[engine][nodes][AudioToControlNode][AudioControlBridge]")
{
    AudioToControlNode node;
    CHECK (processOnce (node, 0.5f, 1.0f) == Catch::Approx (0.5f));
    CHECK (processOnce (node, 0.5f, 0.5f) == Catch::Approx (0.25f));
    CHECK (processOnce (node, -0.5f, 1.0f) == Catch::Approx (-0.5f));
}

TEST_CASE ("AudioToControlNode's depth defaults to 1.0 (full-strength) when unconnected",
           "[engine][nodes][AudioToControlNode][AudioControlBridge]")
{
    AudioToControlNode node;
    // depth = NaN -> unconnected -> falls back to storedDepth (default 1.0),
    // same "unpatched is just as loud/present as before" contract mix.gain.gain
    // established (wiki/NODES_Gaps.md's modulation-only-port fix).
    CHECK (processOnce (node, 0.5f) == Catch::Approx (0.5f));

    node.setParameter ("depth", 0.25f);
    CHECK (processOnce (node, 0.8f) == Catch::Approx (0.2f));
}

TEST_CASE ("AudioToControlNode clamps to the Bipolar contract even when the source or depth overshoots",
           "[engine][nodes][AudioToControlNode][AudioControlBridge]")
{
    AudioToControlNode node;
    CHECK (processOnce (node, 1.5f, 1.0f) == Catch::Approx (1.0f));
    CHECK (processOnce (node, -1.5f, 1.0f) == Catch::Approx (-1.0f));

    node.setParameter ("depth", 2.0f); // depth itself isn't clamped by the node - the PRODUCT is
    CHECK (processOnce (node, 0.9f) == Catch::Approx (1.0f));
}

TEST_CASE ("Audio to Modulation into phaseMod produces real, measurable audio-rate phase modulation",
           "[engine][AudioControlBridge][integration]")
{
    // The plan's own motivating case (§3): a raw waveform's INSTANTANEOUS
    // value as the modulator, not a smoothed envelope. osc.sine's "phaseMod"
    // port is already Bipolar - exactly what adapt.audioToControl produces -
    // so this is the one-step chain, no adapt.map needed: modulator.out ->
    // bridge.in -> carrier.phaseMod.
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    NodeGraph graph;
    graph.addNode ({ "carrier", "osc.sine", {}, {}, {} });
    graph.addNode ({ "modulator", "osc.sine", {}, { { "osc.sine.frequency", 60.0f } }, {} });
    graph.addNode ({ "bridge", "adapt.audioToControl", {}, {}, {} });
    graph.addConnection ({ "modulator", "out", "bridge", "in" });
    graph.addConnection ({ "bridge", "out", "carrier", "phaseMod" });
    graph.setOutput ("carrier", "out");

    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);
    REQUIRE (result.success);
    auto& plan = result.plan;

    const auto* outputPtr = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    plan.process (blockSize);
    const std::vector<float> modulated (outputPtr, outputPtr + blockSize);

    for (auto s : modulated)
        REQUIRE (std::isfinite (s));

    // Independently computed reference: the SAME carrier, un-modulated
    // (phaseMod pinned to 0 for every sample) - carrier.h's own algorithm,
    // reproduced here rather than re-run through a second compiled plan, so
    // this test doesn't silently depend on bridge.depth's default to "prove"
    // the un-modulated case.
    std::vector<float> reference (blockSize);
    double phase = 0.0;
    const double phaseIncrement = SineOscillatorNode::defaultFrequencyHz / sampleRate;
    for (int i = 0; i < blockSize; ++i)
    {
        phase += phaseIncrement;
        phase -= std::floor (phase);
        reference[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase);
    }

    // A real depth-1.0 phase-modulation index this large (the modulator's
    // own -1..1 output, unscaled) measurably changes the waveform - not a
    // rounding-level difference.
    double sumSquaredDiff = 0.0;
    for (int i = 0; i < blockSize; ++i)
    {
        const auto diff = modulated[(size_t) i] - reference[(size_t) i];
        sumSquaredDiff += (double) diff * (double) diff;
    }
    const auto diffRms = (float) std::sqrt (sumSquaredDiff / (double) blockSize);
    CHECK (diffRms > 0.2f);

    // Phase modulation conserves amplitude (it's still a pure sine read at a
    // shifted phase, never scaled) - the modulated signal stays a genuine
    // oscillation, not silence or a runaway.
    CHECK (rmsOf (modulated) > 0.5f);
    CHECK (rmsOf (modulated) < 0.8f);
}

TEST_CASE ("Audio to Modulation's depth genuinely scales the modulation's audible effect",
           "[engine][AudioControlBridge][integration]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    auto renderWithDepth = [&] (float depth)
    {
        NodeGraph graph;
        graph.addNode ({ "carrier", "osc.sine", {}, {}, {} });
        graph.addNode ({ "modulator", "osc.sine", {}, { { "osc.sine.frequency", 60.0f } }, {} });
        graph.addNode ({ "bridge", "adapt.audioToControl", {}, { { "depth", depth } }, {} });
        graph.addConnection ({ "modulator", "out", "bridge", "in" });
        graph.addConnection ({ "bridge", "out", "carrier", "phaseMod" });
        graph.setOutput ("carrier", "out");

        auto factory = buildDefaultNodeFactory();
        auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);
        REQUIRE (result.success);
        auto& plan = result.plan;
        const auto* outputPtr = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
        plan.process (blockSize);
        return std::vector<float> (outputPtr, outputPtr + blockSize);
    };

    const auto zeroDepth = renderWithDepth (0.0f);
    const auto fullDepth = renderWithDepth (1.0f);

    // depth=0 must be bit-identical to a plain unmodulated sine - proof the
    // "depth" port, not just the bridge's mere presence in the graph, is
    // what's controlling the effect.
    double phase = 0.0;
    const double phaseIncrement = SineOscillatorNode::defaultFrequencyHz / sampleRate;
    bool allMatchPlainSine = true;
    for (int i = 0; i < blockSize; ++i)
    {
        phase += phaseIncrement;
        phase -= std::floor (phase);
        const auto expected = (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase);
        if (std::fabs (zeroDepth[(size_t) i] - expected) > 1.0e-5f)
            allMatchPlainSine = false;
    }
    CHECK (allMatchPlainSine);

    double sumSquaredDiff = 0.0;
    for (int i = 0; i < blockSize; ++i)
    {
        const auto diff = fullDepth[(size_t) i] - zeroDepth[(size_t) i];
        sumSquaredDiff += (double) diff * (double) diff;
    }
    CHECK (std::sqrt (sumSquaredDiff / (double) blockSize) > 0.2f);
}
