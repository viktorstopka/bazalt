// M20 (B1): the dynamic tap-push mechanism — GraphCompiler resolves every
// output port to a buffer index (ExecutionPlan::outputBufferIndexByNodeAndPort),
// and ExecutionPlan::setTapForBufferIndex()/process() push each block's real
// values into whichever Tap is currently registered, entirely without a
// recompile. This is the plan's own stated verification: subscribe, drive a
// block, assert the tap received the right values, with no recompile in
// between.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/telemetry/Tap.h"

using namespace bazalt::engine;

namespace
{
    NodeGraph buildConstantThroughGainGraph()
    {
        // ConstantNode's "out" is Control-typed, so it feeds mix.gain's
        // Control-typed "gain" input (not its Audio-typed "audio" input,
        // which canConnect() would reject as an incompatible signal type).
        NodeGraph graph;
        graph.addNode ({ "src", "util.constant", {}, { { "util.constant.value", 0.25f } }, {} });
        graph.addNode ({ "amp", "mix.gain", {}, {}, {} });
        graph.addConnection ({ "src", "out", "amp", "gain" });
        graph.setOutput ("amp", "out");
        return graph;
    }
}

TEST_CASE ("ExecutionPlan resolves (nodeId, portId) to a buffer index for every real output",
           "[engine][ExecutionPlan][M20]")
{
    auto factory = buildDefaultNodeFactory();
    const auto graph = buildConstantThroughGainGraph();
    auto compileResult = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (compileResult.success);

    const auto& plan = compileResult.plan;
    const auto srcIt = plan.outputBufferIndexByNodeAndPort.find ("src");
    REQUIRE (srcIt != plan.outputBufferIndexByNodeAndPort.end());
    const auto srcOutIt = srcIt->second.find ("out");
    REQUIRE (srcOutIt != srcIt->second.end());
    CHECK (srcOutIt->second >= 0);

    // No entry at all for a node/port that doesn't exist.
    CHECK (plan.outputBufferIndexByNodeAndPort.find ("no-such-node") == plan.outputBufferIndexByNodeAndPort.end());
}

TEST_CASE ("setTapForBufferIndex + process() pushes real block values with no recompile",
           "[engine][ExecutionPlan][M20]")
{
    auto factory = buildDefaultNodeFactory();
    const auto graph = buildConstantThroughGainGraph();
    auto compileResult = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (compileResult.success);
    auto& plan = compileResult.plan;

    const auto bufferIndex = plan.outputBufferIndexByNodeAndPort.at ("src").at ("out");

    Tap tap;
    tap.prepare (128); // power of two

    // Subscribing is exactly one atomic pointer store — no recompile, no
    // rebuild of the plan, the plan already running is mutated in place.
    plan.setTapForBufferIndex (bufferIndex, &tap);

    constexpr int numSamples = 32;
    plan.process (numSamples);

    float readBack[numSamples] {};
    const auto available = tap.readLatest (readBack, numSamples);
    REQUIRE (available == numSamples);
    for (int i = 0; i < numSamples; ++i)
        CHECK (readBack[i] == 0.25f); // util.constant's own value, unmodified by mix.gain's default gain

    // Unsubscribing is the same mechanism in reverse — no recompile either.
    plan.setTapForBufferIndex (bufferIndex, nullptr);
    plan.process (numSamples);
    // No crash, no further pushes — readLatest() still returns whatever was
    // already in the ring from the first process() call above (overwrite-
    // oldest semantics, tap wasn't advanced by the second process()).
    const auto stillAvailable = tap.readLatest (readBack, numSamples);
    CHECK (stillAvailable == numSamples);
}

TEST_CASE ("A tap subscribed to an out-of-range buffer index is safely ignored",
           "[engine][ExecutionPlan][M20]")
{
    auto factory = buildDefaultNodeFactory();
    const auto graph = buildConstantThroughGainGraph();
    auto compileResult = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (compileResult.success);
    auto& plan = compileResult.plan;

    Tap tap;
    tap.prepare (128);
    plan.setTapForBufferIndex (-1, &tap);
    plan.setTapForBufferIndex ((int) plan.blockBuffers.size() + 100, &tap);

    // Must not crash — both calls are silently bounds-checked no-ops.
    plan.process (16);
    SUCCEED();
}
