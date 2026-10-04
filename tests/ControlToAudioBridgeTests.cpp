// Control -> Audio Bridge (wiki/plans/ControlToAudioBridge.md) - the reverse of
// the Audio -> Control Bridge (AudioControlBridgeTests.cpp). ControlToAudioNode's
// own unit behaviour, plus a real compiled-graph proof that a modulation source
// genuinely reaches the output as audio, not just that the graph compiles.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/ControlToAudioNode.h"
#include "bazalt/engine/nodes/RandomSteppedNode.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include <cmath>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    float processOnce (ControlToAudioNode& node, float in)
    {
        float output = 0.0f;
        node.processSample (&in, &output);
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

TEST_CASE ("ControlToAudioNode's 'in' is polymorphic on quantity: Unipolar by default, Bipolar once resolved",
           "[engine][nodes][ControlToAudioNode][ControlToAudioBridge]")
{
    ControlToAudioNode node;
    CHECK (node.hasPolymorphicPorts());
    REQUIRE (node.getInputPorts()[0].quantity == Quantity::Unipolar);

    PortDescriptor bipolarSource { "out", SignalType::Control };
    bipolarSource.quantity = Quantity::Bipolar;
    node.resolveIncomingPort ("in", bipolarSource);
    REQUIRE (node.getInputPorts()[0].quantity == Quantity::Bipolar);

    // A source of any other SignalType (never actually reachable through a
    // real compiled graph - canConnect only lets Control feed this port at
    // all - but resolveIncomingPort must still ignore it defensively) leaves
    // the already-resolved quantity untouched.
    PortDescriptor audioSource { "out", SignalType::Audio };
    node.resolveIncomingPort ("in", audioSource);
    CHECK (node.getInputPorts()[0].quantity == Quantity::Bipolar);
}

TEST_CASE ("ControlToAudioNode passes a Bipolar source straight through, clamped",
           "[engine][nodes][ControlToAudioNode][ControlToAudioBridge]")
{
    ControlToAudioNode node;
    PortDescriptor bipolarSource { "out", SignalType::Control };
    bipolarSource.quantity = Quantity::Bipolar;
    node.resolveIncomingPort ("in", bipolarSource);

    CHECK (processOnce (node, 0.5f) == Catch::Approx (0.5f));
    CHECK (processOnce (node, -0.5f) == Catch::Approx (-0.5f));
    CHECK (processOnce (node, 1.5f) == Catch::Approx (1.0f)); // clamped
    CHECK (processOnce (node, -1.5f) == Catch::Approx (-1.0f)); // clamped
}

TEST_CASE ("ControlToAudioNode expands an unresolved/Unipolar source to the full audio swing (*2-1)",
           "[engine][nodes][ControlToAudioNode][ControlToAudioBridge]")
{
    // Default (nothing has resolved it yet): Unipolar, same as MapNode's own
    // default - a real compiled graph always resolves this via canConnect
    // before anything reaches processSample(), but the node must still
    // behave sensibly un-resolved.
    ControlToAudioNode node;
    CHECK (processOnce (node, 0.0f) == Catch::Approx (-1.0f)); // 0..1's 0 -> full negative swing
    CHECK (processOnce (node, 0.5f) == Catch::Approx (0.0f));  // 0..1's midpoint -> audio's own centre
    CHECK (processOnce (node, 1.0f) == Catch::Approx (1.0f));  // 0..1's 1 -> full positive swing

    // Explicitly resolved Unipolar behaves identically to the default.
    PortDescriptor unipolarSource { "out", SignalType::Control };
    unipolarSource.quantity = Quantity::Unipolar;
    node.resolveIncomingPort ("in", unipolarSource);
    CHECK (processOnce (node, 0.5f) == Catch::Approx (0.0f));
    CHECK (processOnce (node, 1.5f) == Catch::Approx (1.0f)); // clamped after expansion (1.5*2-1=2 -> 1)
}

TEST_CASE ("A Bipolar modulation source reaches Master Out as real, measurable audio through To Audio",
           "[engine][ControlToAudioBridge][integration]")
{
    // random.stepped's own "out" is Bipolar by default (RandomSteppedNode.h) -
    // a real, already-registered Control source, same convention
    // AudioControlBridgeTests.cpp's own integration test uses an existing
    // osc.sine rather than a synthetic test-only node.
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    NodeGraph graph;
    graph.addNode ({ "random", "random.stepped", {}, { { "random.stepped.rate", 5000.0f } }, {} });
    graph.addNode ({ "bridge", "adapt.controlToAudio", {}, {}, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "random", "out", "bridge", "in" });
    graph.addConnection ({ "bridge", "out", "out", "in" });
    graph.setOutput ("out", "out");

    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);
    REQUIRE (result.success);
    auto& plan = result.plan;

    const auto* outputPtr = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    plan.process (blockSize);
    const std::vector<float> rendered (outputPtr, outputPtr + blockSize);

    for (auto s : rendered)
    {
        REQUIRE (std::isfinite (s));
        CHECK (s >= -1.0f);
        CHECK (s <= 1.0f);
    }

    // A real, fast-stepping random source produces genuine signal energy at
    // the output, not silence - proof the bridge actually carries the
    // modulation value through as audio, end to end through a real compiled
    // graph (GraphCompiler + ExecutionPlan), not just that it compiles.
    CHECK (rmsOf (rendered) > 0.05f);
}

TEST_CASE ("A real-quantity modulation source reaches Master Out through Normalise, then To Audio",
           "[engine][ControlToAudioBridge][integration]")
{
    // random.stepped's own "out" defaults to Bipolar - force it toward a
    // REAL quantity source instead by routing it through adapt.map first
    // (seeded to a real-unit range), so this test exercises the genuine
    // 2-step canConnect chain (adapt.normalise -> adapt.controlToAudio), not
    // the 1-step case the test above already covers.
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    NodeGraph graph;
    graph.addNode ({ "random", "random.stepped", {}, { { "random.stepped.rate", 5000.0f } }, {} });
    graph.addNode ({ "toFrequency", "adapt.map", {}, { { "adapt.map.inMin", -1.0f }, { "adapt.map.inMax", 1.0f }, { "adapt.map.outMin", 100.0f }, { "adapt.map.outMax", 2000.0f } }, {} });
    graph.addNode ({ "normalise", "adapt.normalise", {}, { { "adapt.normalise.min", 100.0f }, { "adapt.normalise.max", 2000.0f } }, {} });
    graph.addNode ({ "bridge", "adapt.controlToAudio", {}, {}, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "random", "out", "toFrequency", "in" });
    graph.addConnection ({ "toFrequency", "out", "normalise", "in" });
    graph.addConnection ({ "normalise", "out", "bridge", "in" });
    graph.addConnection ({ "bridge", "out", "out", "in" });
    graph.setOutput ("out", "out");

    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);
    REQUIRE (result.success);
    auto& plan = result.plan;

    const auto* outputPtr = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    plan.process (blockSize);
    const std::vector<float> rendered (outputPtr, outputPtr + blockSize);

    for (auto s : rendered)
    {
        REQUIRE (std::isfinite (s));
        CHECK (s >= -1.0f);
        CHECK (s <= 1.0f);
    }
    CHECK (rmsOf (rendered) > 0.05f);
}
