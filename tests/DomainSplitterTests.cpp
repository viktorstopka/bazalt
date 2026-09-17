#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/DomainSplitter.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/VoiceSumNode.h"

using namespace bazalt::engine;

namespace
{
    // osc -> svf -> voicesum -> masterout, with masterout as the graph's
    // designated output. Small enough to reason about by hand, exercises
    // every part of the split: a real voice-domain chain, the boundary
    // node itself, and a real global-domain node downstream of it.
    NodeGraph buildSplitTestGraph()
    {
        NodeGraph graph;
        graph.addNode ({ "osc", "osc.basic", {}, {}, {} });
        graph.addNode ({ "svf", "filter.svf", {}, {}, {} });
        graph.addNode ({ "voicesum", "util.voiceSum", {}, {}, {} });
        graph.addNode ({ "masterout", "util.output", {}, {}, {} });

        graph.addConnection ({ "osc", "out", "svf", "in" });
        graph.addConnection ({ "svf", "out", "voicesum", "in" });
        graph.addConnection ({ "voicesum", "out", "masterout", "in" });

        graph.setOutput ("masterout", "out");
        return graph;
    }
}

TEST_CASE ("DomainSplitter treats a graph with no util.voiceSum as entirely voice-domain",
           "[engine][DomainSplitter][NODE_EDITOR]")
{
    auto graph = buildVoiceProofGraph(); // M2's proof graph — no voiceSum node
    const auto result = DomainSplitter::split (graph);

    REQUIRE (result.success);
    CHECK_FALSE (result.hasGlobalDomain);
    CHECK (result.voiceGraph.getNodes().size() == graph.getNodes().size());
    CHECK (result.voiceGraph.getOutputNodeId() == graph.getOutputNodeId());
    CHECK (result.voiceGraph.getOutputPortId() == graph.getOutputPortId());
}

TEST_CASE ("DomainSplitter correctly partitions a graph with one util.voiceSum node",
           "[engine][DomainSplitter][NODE_EDITOR]")
{
    const auto graph = buildSplitTestGraph();
    const auto result = DomainSplitter::split (graph);

    REQUIRE (result.success);
    REQUIRE (result.errorMessage.isEmpty());
    REQUIRE (result.hasGlobalDomain);
    CHECK (result.voiceSumNodeId == "voicesum");

    // Voice domain: osc, svf — output retargeted to whatever fed voicesum.in (svf.out).
    REQUIRE (result.voiceGraph.getNodes().size() == 2);
    CHECK (result.voiceGraph.getOutputNodeId() == "svf");
    CHECK (result.voiceGraph.getOutputPortId() == "out");

    for (const auto& node : result.voiceGraph.getNodes())
        CHECK ((node.id == "osc" || node.id == "svf"));

    for (const auto& connection : result.voiceGraph.getConnections())
        CHECK (connection.toNodeId != "voicesum"); // the boundary edge must not appear in either subgraph verbatim

    // Global domain: voicesum, masterout.
    REQUIRE (result.globalGraph.getNodes().size() == 2);
    CHECK (result.globalGraph.getOutputNodeId() == "masterout");
    CHECK (result.globalGraph.getOutputPortId() == "out");

    for (const auto& node : result.globalGraph.getNodes())
        CHECK ((node.id == "voicesum" || node.id == "masterout"));

    REQUIRE (result.globalGraph.getConnections().size() == 1);
    CHECK (result.globalGraph.getConnections()[0].fromNodeId == "voicesum");
    CHECK (result.globalGraph.getConnections()[0].toNodeId == "masterout");
}

TEST_CASE ("Both halves of a split graph compile independently through the unmodified GraphCompiler",
           "[engine][DomainSplitter][NODE_EDITOR]")
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

TEST_CASE ("The global plan's designated output equals whatever setExternalBlock() fed util.voiceSum",
           "[engine][DomainSplitter][NODE_EDITOR]")
{
    // Proves the full seam end-to-end: a value that would come from summing
    // 8 voice plans (here, hand-supplied) arrives at the global plan's
    // final output, through util.voiceSum -> util.output, with no engine
    // change beyond what DomainSplitter + VoiceSumNode already provide.
    const auto graph = buildSplitTestGraph();
    const auto split = DomainSplitter::split (graph);
    REQUIRE (split.success);

    auto factory = buildDefaultNodeFactory();
    auto globalResult = GraphCompiler::compile (split.globalGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (globalResult.success);

    auto* voiceSumNode = dynamic_cast<nodes::VoiceSumNode*> (globalResult.plan.getNodeById ("voicesum"));
    REQUIRE (voiceSumNode != nullptr);

    constexpr int numSamples = 8;
    float summedVoices[numSamples];
    for (int i = 0; i < numSamples; ++i)
        summedVoices[i] = 0.1f * (float) i;

    voiceSumNode->setExternalBlock (summedVoices, numSamples);
    globalResult.plan.process (numSamples);

    const auto* output = globalResult.plan.blockBuffers[(size_t) globalResult.plan.finalOutputBufferIndex]
                              .getBlock()
                              .getChannelPointer (0);

    for (int i = 0; i < numSamples; ++i)
        CHECK (output[i] == summedVoices[i]);
}

TEST_CASE ("DomainSplitter rejects a graph with two util.voiceSum nodes", "[engine][DomainSplitter]")
{
    NodeGraph graph;
    graph.addNode ({ "voicesum1", "util.voiceSum", {}, {}, {} });
    graph.addNode ({ "voicesum2", "util.voiceSum", {}, {}, {} });
    graph.setOutput ("voicesum1", "out");

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());
}

TEST_CASE ("DomainSplitter rejects util.voiceSum with no connection into its input port",
           "[engine][DomainSplitter]")
{
    NodeGraph graph;
    graph.addNode ({ "voicesum", "util.voiceSum", {}, {}, {} });
    graph.addNode ({ "masterout", "util.output", {}, {}, {} });
    graph.addConnection ({ "voicesum", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("exactly one connection"));
}

TEST_CASE ("DomainSplitter rejects an orphaned node connected to neither domain", "[engine][DomainSplitter]")
{
    auto graph = buildSplitTestGraph();
    graph.addNode ({ "orphan", "noise.burst", {}, {}, {} }); // never connected to anything

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("orphan"));
}

TEST_CASE ("DomainSplitter rejects a graph output node left in the voice domain", "[engine][DomainSplitter]")
{
    NodeGraph graph;
    graph.addNode ({ "osc", "osc.basic", {}, {}, {} });
    graph.addNode ({ "voicesum", "util.voiceSum", {}, {}, {} });
    graph.addConnection ({ "osc", "out", "voicesum", "in" });
    graph.setOutput ("osc", "out"); // wrong: designated output is in the voice domain, not global

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("global domain"));
}
