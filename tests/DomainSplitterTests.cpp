#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/DomainSplitter.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"

using namespace bazalt::engine;

namespace
{
    // osc -> svf -> instancemix -> masterout, with masterout as the graph's
    // designated output. Small enough to reason about by hand, exercises
    // every part of the split: a real voice-domain chain, the boundary
    // node itself, and a real global-domain node downstream of it.
    NodeGraph buildSplitTestGraph()
    {
        NodeGraph graph;
        graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
        graph.addNode ({ "svf", "filter.svf", {}, {}, {} });
        graph.addNode ({ "instancemix", "instance.mix", {}, {}, {} });
        graph.addNode ({ "masterout", "io.output", {}, {}, {} });

        graph.addConnection ({ "osc", "out", "svf", "in" });
        graph.addConnection ({ "svf", "out", "instancemix", "in" });
        graph.addConnection ({ "instancemix", "out", "masterout", "in" });

        graph.setOutput ("masterout", "out");
        return graph;
    }
}

TEST_CASE ("DomainSplitter treats a graph with no instance.mix as entirely voice-domain",
           "[engine][DomainSplitter][M17]")
{
    auto graph = buildVoiceProofGraph(); // M2's proof graph — no instance.mix node
    const auto result = DomainSplitter::split (graph);

    REQUIRE (result.success);
    CHECK_FALSE (result.hasGlobalDomain);
    CHECK (result.voiceGraph.getNodes().size() == graph.getNodes().size());
    CHECK (result.voiceGraph.getOutputNodeId() == graph.getOutputNodeId());
    CHECK (result.voiceGraph.getOutputPortId() == graph.getOutputPortId());
}

TEST_CASE ("DomainSplitter correctly partitions a graph with one instance.mix node",
           "[engine][DomainSplitter][M17]")
{
    const auto graph = buildSplitTestGraph();
    const auto result = DomainSplitter::split (graph);

    REQUIRE (result.success);
    REQUIRE (result.errorMessage.isEmpty());
    REQUIRE (result.hasGlobalDomain);
    CHECK (result.instanceMixNodeId == "instancemix");

    // Voice domain: osc, svf — output retargeted to whatever fed instancemix.in (svf.out).
    REQUIRE (result.voiceGraph.getNodes().size() == 2);
    CHECK (result.voiceGraph.getOutputNodeId() == "svf");
    CHECK (result.voiceGraph.getOutputPortId() == "out");

    for (const auto& node : result.voiceGraph.getNodes())
        CHECK ((node.id == "osc" || node.id == "svf"));

    for (const auto& connection : result.voiceGraph.getConnections())
        CHECK (connection.toNodeId != "instancemix"); // the boundary edge must not appear in either subgraph verbatim

    // Global domain: instancemix, masterout.
    REQUIRE (result.globalGraph.getNodes().size() == 2);
    CHECK (result.globalGraph.getOutputNodeId() == "masterout");
    CHECK (result.globalGraph.getOutputPortId() == "out");

    for (const auto& node : result.globalGraph.getNodes())
        CHECK ((node.id == "instancemix" || node.id == "masterout"));

    REQUIRE (result.globalGraph.getConnections().size() == 1);
    CHECK (result.globalGraph.getConnections()[0].fromNodeId == "instancemix");
    CHECK (result.globalGraph.getConnections()[0].toNodeId == "masterout");
}

TEST_CASE ("Both halves of a split graph compile independently through the unmodified GraphCompiler",
           "[engine][DomainSplitter][M17]")
{
    const auto graph = buildSplitTestGraph();
    const auto split = DomainSplitter::split (graph);
    REQUIRE (split.success);

    auto factory = buildDefaultNodeFactory();

    auto voiceResult = GraphCompiler::compile (split.voiceGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (voiceResult.success);
    CHECK (voiceResult.plan.finalOutputBufferIndex >= 0);

    auto globalResult = GraphCompiler::compile (split.globalGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (globalResult.success);
    CHECK (globalResult.plan.finalOutputBufferIndex >= 0);
}

TEST_CASE ("The global plan's designated output equals whatever setExternalBlock() fed instance.mix",
           "[engine][DomainSplitter][M17]")
{
    // Proves the full seam end-to-end: a value that would come from summing
    // (or averaging, per instance.mix.mode) 8 voice plans (here,
    // hand-supplied) arrives at the global plan's final output, through
    // instance.mix -> io.output, with no engine change beyond what
    // DomainSplitter + InstanceMixNode already provide.
    const auto graph = buildSplitTestGraph();
    const auto split = DomainSplitter::split (graph);
    REQUIRE (split.success);

    auto factory = buildDefaultNodeFactory();
    auto globalResult = GraphCompiler::compile (split.globalGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (globalResult.success);

    auto* instanceMixNode = dynamic_cast<nodes::InstanceMixNode*> (globalResult.plan.getNodeById ("instancemix"));
    REQUIRE (instanceMixNode != nullptr);

    constexpr int numSamples = 8;
    float summedVoices[numSamples];
    for (int i = 0; i < numSamples; ++i)
        summedVoices[i] = 0.1f * (float) i;

    instanceMixNode->setExternalBlock (summedVoices, numSamples);
    globalResult.plan.process (numSamples);

    const auto* output = globalResult.plan.blockBuffers[(size_t) globalResult.plan.finalOutputBufferIndex]
                              .getBlock()
                              .getChannelPointer (0);

    for (int i = 0; i < numSamples; ++i)
        CHECK (output[i] == summedVoices[i]);
}

TEST_CASE ("DomainSplitter rejects a graph with two instance.mix nodes", "[engine][DomainSplitter][M17]")
{
    NodeGraph graph;
    graph.addNode ({ "mix1", "instance.mix", {}, {}, {} });
    graph.addNode ({ "mix2", "instance.mix", {}, {}, {} });
    graph.setOutput ("mix1", "out");

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());
}

TEST_CASE ("DomainSplitter rejects a graph with two instance.allocator nodes", "[engine][DomainSplitter][M17]")
{
    NodeGraph graph;
    graph.addNode ({ "alloc1", "instance.allocator", {}, {}, {} });
    graph.addNode ({ "alloc2", "instance.allocator", {}, {}, {} });
    graph.addNode ({ "mix", "instance.mix", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "alloc1", "gate", "mix", "in" });
    graph.addConnection ({ "mix", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("instance.allocator"));
}

TEST_CASE ("DomainSplitter rejects instance.allocator placed downstream of instance.mix",
           "[engine][DomainSplitter][M17]")
{
    // A deliberately-wrong graph: the allocator ends up in the global
    // domain, which DOMAINS.md's model never intends — a light correctness
    // check (InstanceAllocatorNode.h's own comment explains why this can't
    // happen via M17's real reachability yet, but the check exists for
    // whenever a future graph could construct this by mistake).
    NodeGraph graph;
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "mix", "instance.mix", {}, {}, {} });
    graph.addNode ({ "alloc", "instance.allocator", {}, {}, {} });
    graph.addConnection ({ "osc", "out", "mix", "in" });
    graph.addConnection ({ "mix", "out", "alloc", "spawn" });
    graph.setOutput ("alloc", "gate");

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("voice domain"));
}

TEST_CASE ("DomainSplitter treats an unwired instance.mix as no domain boundary yet, not an error",
           "[engine][DomainSplitter][M17][M19]")
{
    // A freshly-placed instance.mix (before the user has wired anything
    // into it) must not make the whole graph fail to compile — the UI
    // places a node, then wires it, as two separate commands, so the
    // in-between state has to be valid. Found via real hands-on testing
    // (M19) after the original "reject unless already wired" behavior
    // made instance.mix impossible to ever place through the Add menu.
    NodeGraph graph;
    graph.addNode ({ "instancemix", "instance.mix", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "instancemix", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = DomainSplitter::split (graph);
    REQUIRE (result.success);
    CHECK_FALSE (result.hasGlobalDomain);
    CHECK (result.voiceGraph.getNodes().size() == 2);
}

TEST_CASE ("DomainSplitter rejects instance.mix with more than one connection into its input port",
           "[engine][DomainSplitter][M19]")
{
    NodeGraph graph;
    graph.addNode ({ "instancemix", "instance.mix", {}, {}, {} });
    graph.addNode ({ "src1", "util.constant", {}, {}, {} });
    graph.addNode ({ "src2", "util.constant", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    // NodeGraph itself doesn't prevent double-wiring an input (only
    // GraphCompiler/the UI do) — DomainSplitter must still catch it since
    // it inspects the raw connection list before GraphCompiler ever runs.
    graph.addConnection ({ "src1", "out", "instancemix", "in" });
    graph.addConnection ({ "src2", "out", "instancemix", "in" });
    graph.addConnection ({ "instancemix", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("at most one connection"));
}

TEST_CASE ("DomainSplitter rejects an orphaned node connected to neither domain", "[engine][DomainSplitter]")
{
    auto graph = buildSplitTestGraph();
    graph.addNode ({ "orphan", "excite.burst", {}, {}, {} }); // never connected to anything

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("orphan"));
}

TEST_CASE ("DomainSplitter rejects a graph output node left in the voice domain", "[engine][DomainSplitter]")
{
    NodeGraph graph;
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "instancemix", "instance.mix", {}, {}, {} });
    graph.addConnection ({ "osc", "out", "instancemix", "in" });
    graph.setOutput ("osc", "out"); // wrong: designated output is in the voice domain, not global

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("global domain"));
}
