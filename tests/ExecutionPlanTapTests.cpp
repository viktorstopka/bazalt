// M20 (B1): the dynamic tap-push mechanism — GraphCompiler resolves every
// output port to a buffer index (ExecutionPlan::outputBufferIndexByNodeAndPort),
// and ExecutionPlan::addTapForBufferIndex()/removeTap()/process() push each block's real
// values into whichever Tap is currently registered, entirely without a
// recompile. This is the plan's own stated verification: subscribe, drive a
// block, assert the tap received the right values, with no recompile in
// between.
#include <catch2/catch_test_macros.hpp>
#include <set>
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

TEST_CASE ("addTapForBufferIndex + process() pushes real block values with no recompile",
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
    REQUIRE (plan.addTapForBufferIndex (bufferIndex, &tap));

    constexpr int numSamples = 32;
    plan.process (numSamples);

    float readBack[numSamples] {};
    const auto available = tap.readLatest (readBack, numSamples);
    REQUIRE (available == numSamples);
    for (int i = 0; i < numSamples; ++i)
        CHECK (readBack[i] == 0.25f); // util.constant's own value, unmodified by mix.gain's default gain

    // Unsubscribing is the same mechanism in reverse — no recompile either.
    plan.removeTap (&tap);
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
    CHECK_FALSE (plan.addTapForBufferIndex (-1, &tap));
    CHECK_FALSE (plan.addTapForBufferIndex ((int) plan.blockBuffers.size() + 100, &tap));

    // Must not crash — both calls are refused, bounds-checked no-ops.
    plan.process (16);
    SUCCEED();
}

// ---- ADR-0029: several taps per buffer, and taps on INPUT ports ----

TEST_CASE ("A buffer carries several taps at once, each receiving every block",
           "[engine][ExecutionPlan][ADR-0029]")
{
    // A node's own preview and a view.scope on the same cable share one buffer;
    // with a single slot the second attach silently displaced the first.
    auto factory = buildDefaultNodeFactory();
    const auto graph = buildConstantThroughGainGraph();
    auto compileResult = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (compileResult.success);
    auto& plan = compileResult.plan;

    const auto bufferIndex = plan.outputBufferIndexByNodeAndPort.at ("src").at ("out");

    Tap taps[ExecutionPlan::maxTapsPerBuffer];
    for (auto& tap : taps)
    {
        tap.prepare (128);
        REQUIRE (plan.addTapForBufferIndex (bufferIndex, &tap));
    }

    Tap oneTooMany;
    oneTooMany.prepare (128);
    CHECK_FALSE (plan.addTapForBufferIndex (bufferIndex, &oneTooMany)); // full, and says so
    CHECK (plan.addTapForBufferIndex (bufferIndex, &taps[0]));           // already present: fine

    plan.process (32);
    for (auto& tap : taps)
        CHECK (tap.getTotalPushed() == 32);
    CHECK (oneTooMany.getTotalPushed() == 0);

    // Removing one leaves the others running and frees its place.
    plan.removeTap (&taps[1]);
    plan.process (32);
    CHECK (taps[1].getTotalPushed() == 32);
    CHECK (taps[0].getTotalPushed() == 64);
    CHECK (plan.addTapForBufferIndex (bufferIndex, &oneTooMany));
}

TEST_CASE ("A plan resolves an INPUT port to the buffer wired into it",
           "[engine][ExecutionPlan][ADR-0029]")
{
    auto factory = buildDefaultNodeFactory();
    const auto graph = buildConstantThroughGainGraph();
    auto compileResult = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (compileResult.success);
    const auto& plan = compileResult.plan;

    const auto srcOut = plan.outputBufferIndexByNodeAndPort.at ("src").at ("out");

    // amp.gain is fed by src.out, so tapping amp's "gain" input is tapping src's buffer.
    CHECK (plan.inputSourceBufferIndexByNodeAndPort.at ("amp").at ("gain") == srcOut);
    CHECK (plan.findTappableBufferIndex ("amp", "gain") == srcOut);

    // An output still resolves to its own buffer, an unwired input to nothing.
    CHECK (plan.findTappableBufferIndex ("src", "out") == srcOut);
    CHECK (plan.findTappableBufferIndex ("amp", "audio") == -1);
    CHECK (plan.findTappableBufferIndex ("amp", "no-such-port") == -1);
    CHECK (plan.findTappableBufferIndex ("no-such-node", "out") == -1);
}

TEST_CASE ("No node type reuses a port id across its inputs and outputs",
           "[engine][NodeFactory][ADR-0029]")
{
    // findTappableBufferIndex() and the tap name ("node:<id>:<port>") both
    // identify a port by id alone, which is only unambiguous while an input
    // and an output of one node never share an id.
    auto factory = buildDefaultNodeFactory();

    for (const auto& descriptor : factory.describeAll())
    {
        std::set<juce::String> seen;
        for (const auto& port : descriptor.inputs)
            seen.insert (port.id);

        for (const auto& port : descriptor.outputs)
        {
            INFO (descriptor.typeId << " declares both an input and an output '" << port.id << "'");
            CHECK (seen.count (port.id) == 0);
        }
    }
}
