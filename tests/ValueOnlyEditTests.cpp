// A value-only edit (a port's fallback value or a non-structural parameter)
// keeps the running node — no click, no lost state — and the AUDIO thread
// applies the new value at the next plan's first block
// (ExecutionPlan::pendingParameterUpdates). A structural edit still builds a
// fresh node.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using namespace bazalt::engine;

namespace
{
    NodeGraph sineAt (float frequency)
    {
        NodeGraph graph;
        graph.addNode ({ "osc", "osc.sine", {}, { { "osc.sine.frequency", frequency } }, {} });
        graph.setOutput ("osc", "out");
        return graph;
    }

    Node* nodeOf (CompileResult& result, const juce::String& id) { return result.plan.nodes[(size_t) result.plan.nodeIdToSlot.at (id)].get(); }

    float firstSample (CompileResult& result)
    {
        result.plan.process (1);
        return result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0)[0];
    }
}

TEST_CASE ("A value-only edit keeps the running node and applies the new value at the next block",
           "[engine][GraphCompiler][liveEdit]")
{
    auto factory = buildDefaultNodeFactory();
    auto first = GraphCompiler::compile (sineAt (100.0f), factory, { 48000.0, 64 }, 1);
    REQUIRE (first.success);
    first.plan.process (60); // phase = 60 * 100 / 48000 = 0.125 cycles

    auto second = GraphCompiler::compile (sineAt (200.0f), factory, { 48000.0, 64 }, 2, &first.plan);
    REQUIRE (second.success);
    CHECK (nodeOf (second, "osc") == nodeOf (first, "osc")); // same object: state carried forward
    CHECK_FALSE (second.plan.pendingParametersApplied.value.load());

    // Phase continued from 0.125 (a fresh node would read sin(0) = 0)...
    CHECK (firstSample (second) == Catch::Approx (std::sin (juce::MathConstants<double>::twoPi * 0.125)).margin (1.0e-5));
    CHECK (second.plan.pendingParametersApplied.value.load());
    // ...and now advances at the NEW frequency: 0.125 + 200/48000.
    CHECK (firstSample (second) == Catch::Approx (std::sin (juce::MathConstants<double>::twoPi * (0.125 + 200.0 / 48000.0))).margin (1.0e-5));
}

TEST_CASE ("A plan replaced before it ever played hands its pending values on",
           "[engine][GraphCompiler][liveEdit]")
{
    auto factory = buildDefaultNodeFactory();
    auto first = GraphCompiler::compile (sineAt (100.0f), factory, { 48000.0, 64 }, 1);
    REQUIRE (first.success);
    first.plan.process (8);

    auto never = GraphCompiler::compile (sineAt (300.0f), factory, { 48000.0, 64 }, 2, &first.plan);
    REQUIRE (never.success); // ...and never processed

    // Same parameters as `never` — nothing new to apply of its own, but the
    // 300 Hz edit `never` carried must not be lost.
    auto third = GraphCompiler::compile (sineAt (300.0f), factory, { 48000.0, 64 }, 3, &never.plan);
    REQUIRE (third.success);
    REQUIRE (third.plan.pendingParameterUpdates.size() == 1);
    CHECK (third.plan.pendingParameterUpdates[0].value == 300.0f);
}

TEST_CASE ("A structural edit still builds a fresh node", "[engine][GraphCompiler][liveEdit]")
{
    auto factory = buildDefaultNodeFactory();
    auto graphWithShape = [] (float shape)
    {
        NodeGraph graph;
        graph.addNode ({ "osc", "osc.analog", {}, { { "osc.analog.shape", shape } }, {} });
        graph.setOutput ("osc", "out");
        return graph;
    };

    auto first = GraphCompiler::compile (graphWithShape (1.0f), factory, { 48000.0, 64 }, 1);
    REQUIRE (first.success);
    auto second = GraphCompiler::compile (graphWithShape (2.0f), factory, { 48000.0, 64 }, 2, &first.plan);
    REQUIRE (second.success);
    CHECK (nodeOf (second, "osc") != nodeOf (first, "osc")); // osc.analog.shape is structural
    CHECK (second.plan.pendingParameterUpdates.empty());
}
