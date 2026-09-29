#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/MultiplicityResolver.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"

// Replaces DomainSplitterTests.cpp (wiki/plans/DomainRedesign.md §10.2).
// Several of the old suite's cases tested the OLD model's specific,
// order-dependent brittleness — the exact thing this redesign exists to
// remove (§0's motivating repro). Those are called out explicitly below,
// re-purposed into demonstrations that the new behavior is now correct,
// rather than silently dropped.

using namespace bazalt::engine;

namespace
{
    // osc -> svf -> instancesum -> masterout, WITH a real allocator feeding
    // osc's pitch — unlike the old buildSplitTestGraph(), which wired
    // instance.mix straight off osc/svf with no allocator anywhere upstream
    // at all. That shape compiled under DomainSplitter's purely reachability-
    // based split; under this resolver it's a real, deliberate compile error
    // (see the instance.sum-with-Scalar-input test below) — instance.sum's
    // whole job is reducing an ACTUAL Poly signal, so a minimal real origin
    // is now part of the minimal valid graph, not an optional extra.
    NodeGraph buildSplitTestGraph()
    {
        NodeGraph graph;
        graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
        graph.addNode ({ "allocator", "instance.allocate.voice", {}, {}, {} });
        graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
        graph.addNode ({ "svf", "filter.svf", {}, {}, {} });
        graph.addNode ({ "instancesum", "instance.mix", {}, {}, {} }); // batch 1b renames this type id to "instance.sum"
        graph.addNode ({ "masterout", "io.output", {}, {}, {} });

        graph.addConnection ({ "noteIn", "notes", "allocator", "spawn" });
        graph.addConnection ({ "allocator", "pitch", "osc", "pitch" });
        graph.addConnection ({ "osc", "out", "svf", "in" });
        graph.addConnection ({ "svf", "out", "instancesum", "in" });
        graph.addConnection ({ "instancesum", "out", "masterout", "in" });

        graph.setOutput ("masterout", "out");
        return graph;
    }

    bool containsNode (const NodeGraph& graph, const juce::String& id)
    {
        return graph.findNode (id) != nullptr;
    }

    const MultiplicityOrigin* findOrigin (const MultiplicityResult& result, const juce::String& id)
    {
        for (const auto& origin : result.origins)
            if (origin.originId == id)
                return &origin;
        return nullptr;
    }
}

TEST_CASE ("A graph with no instance.allocate.voice and no instance.mix is a mono graph",
           "[engine][MultiplicityResolver]")
{
    NodeGraph effect;
    effect.addNode ({ "in", "io.audioIn", {}, {}, {} });
    effect.addNode ({ "out", "io.output", {}, {}, {} });
    effect.addConnection ({ "in", "channel.0", "out", "in" });
    effect.setOutput ("out", "out");

    const auto result = MultiplicityResolver::split (effect);
    REQUIRE (result.success);
    CHECK (result.monoOnly);
    CHECK_FALSE (result.hasGlobalDomain);
    CHECK (result.origins.empty());
    CHECK (result.globalGraph.getNodes().size() == 2); // the unchanged graph, for anything keyed on it
}

TEST_CASE ("MultiplicityResolver treats a graph with no instance.sum as entirely one origin's own voice region",
           "[engine][MultiplicityResolver]")
{
    auto graph = buildVoiceProofGraph(); // M2's proof graph — no instance.mix node, output IS the voice signal
    const auto result = MultiplicityResolver::split (graph);

    REQUIRE (result.success);
    CHECK_FALSE (result.monoOnly);
    CHECK_FALSE (result.hasGlobalDomain);
    CHECK (result.outputOriginId == "allocator");
    REQUIRE (result.origins.size() == 1);
    CHECK (result.origins[0].originId == "allocator");
    CHECK (result.origins[0].instanceSumNodeId.isEmpty());
    // Every node in this graph traces back to the allocator (noteIn feeds
    // it directly; osc/env/svf/amp all read its outputs) — the whole thing
    // is one origin's voice region, exactly DomainSplitter's own equivalent
    // assertion.
    CHECK (result.origins[0].voiceGraph.getNodes().size() == graph.getNodes().size());
    CHECK (result.globalGraph.getNodes().empty());
}

TEST_CASE ("MultiplicityResolver correctly partitions a graph with one instance.sum node",
           "[engine][MultiplicityResolver]")
{
    const auto graph = buildSplitTestGraph();
    const auto result = MultiplicityResolver::split (graph);

    REQUIRE (result.success);
    REQUIRE (result.errorMessage.isEmpty());
    REQUIRE (result.hasGlobalDomain);
    CHECK (result.outputOriginId.isEmpty());

    REQUIRE (result.origins.size() == 1);
    const auto& origin = result.origins[0];
    CHECK (origin.originId == "allocator");
    CHECK (origin.instanceSumNodeId == "instancesum");

    // Voice region: noteIn, allocator, osc, svf — output retargeted to
    // whatever fed instancesum.in (svf.out).
    REQUIRE (origin.voiceGraph.getNodes().size() == 4);
    CHECK (origin.voiceGraph.getOutputNodeId() == "svf");
    CHECK (origin.voiceGraph.getOutputPortId() == "out");
    for (const auto& node : origin.voiceGraph.getNodes())
        CHECK ((node.id == "noteIn" || node.id == "allocator" || node.id == "osc" || node.id == "svf"));
    for (const auto& connection : origin.voiceGraph.getConnections())
        CHECK (connection.toNodeId != "instancesum"); // the boundary edge must not appear in either subgraph verbatim

    // Global region: instancesum, masterout.
    REQUIRE (result.globalGraph.getNodes().size() == 2);
    CHECK (result.globalGraph.getOutputNodeId() == "masterout");
    CHECK (result.globalGraph.getOutputPortId() == "out");
    for (const auto& node : result.globalGraph.getNodes())
        CHECK ((node.id == "instancesum" || node.id == "masterout"));

    REQUIRE (result.globalGraph.getConnections().size() == 1);
    CHECK (result.globalGraph.getConnections()[0].fromNodeId == "instancesum");
    CHECK (result.globalGraph.getConnections()[0].toNodeId == "masterout");
}

TEST_CASE ("Both an origin's voiceGraph and globalGraph compile independently through the unmodified GraphCompiler",
           "[engine][MultiplicityResolver]")
{
    const auto graph = buildSplitTestGraph();
    const auto split = MultiplicityResolver::split (graph);
    REQUIRE (split.success);

    auto factory = buildDefaultNodeFactory();

    auto voiceResult = GraphCompiler::compile (split.origins[0].voiceGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (voiceResult.success);
    CHECK (voiceResult.plan.finalOutputBufferIndex >= 0);

    auto globalResult = GraphCompiler::compile (split.globalGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (globalResult.success);
    CHECK (globalResult.plan.finalOutputBufferIndex >= 0);
}

TEST_CASE ("The global plan's designated output equals whatever setExternalBlock() fed instance.sum",
           "[engine][MultiplicityResolver]")
{
    // Proves the full seam end-to-end: a value that would come from summing
    // (or averaging) numVoices voice plans (here, hand-supplied) arrives at
    // the global plan's final output, through instance.sum -> io.output.
    const auto graph = buildSplitTestGraph();
    const auto split = MultiplicityResolver::split (graph);
    REQUIRE (split.success);

    auto factory = buildDefaultNodeFactory();
    auto globalResult = GraphCompiler::compile (split.globalGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (globalResult.success);

    auto* instanceSumNode = dynamic_cast<nodes::InstanceMixNode*> (globalResult.plan.getNodeById ("instancesum"));
    REQUIRE (instanceSumNode != nullptr);

    constexpr int numSamples = 8;
    float summedVoices[numSamples];
    for (int i = 0; i < numSamples; ++i)
        summedVoices[i] = 0.1f * (float) i;

    instanceSumNode->setExternalBlock (summedVoices, numSamples);
    globalResult.plan.process (numSamples);

    const auto* output = globalResult.plan.blockBuffers[(size_t) globalResult.plan.finalOutputBufferIndex]
                              .getBlock()
                              .getChannelPointer (0);

    for (int i = 0; i < numSamples; ++i)
        CHECK (output[i] == summedVoices[i]);
}

TEST_CASE ("MultiplicityResolver rejects more than maxOrigins instance.allocate.voice nodes",
           "[engine][MultiplicityResolver]")
{
    NodeGraph graph;
    for (int i = 0; i < MultiplicityResolver::maxOrigins + 1; ++i)
        graph.addNode ({ "alloc" + juce::String (i), "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.setOutput ("masterout", "out");

    const auto result = MultiplicityResolver::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains (juce::String (MultiplicityResolver::maxOrigins)));
}

TEST_CASE ("Two independent origin/instance.sum pairs partition independently - DomainRedesign.md section 4",
           "[engine][MultiplicityResolver][DomainRedesign]")
{
    // The concrete feature this redesign buys: a seq-driven main voice and a
    // separately-triggered sub-oscillator, each with its own sequencing —
    // two ordinary, unremarkable allocator/sum pairs, no special multi-
    // region bookkeeping. Structurally minimal here (no real note source),
    // since this test is about partition mechanics, not audio behaviour.
    NodeGraph graph;
    graph.addNode ({ "allocA", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "oscA", "osc.analog", {}, {}, {} });
    graph.addNode ({ "sumA", "instance.mix", {}, {}, {} });

    graph.addNode ({ "allocB", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "oscB", "osc.analog", {}, {}, {} });
    graph.addNode ({ "sumB", "instance.mix", {}, {}, {} });

    graph.addNode ({ "mixdown", "mix.sum", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });

    graph.addConnection ({ "allocA", "pitch", "oscA", "pitch" });
    graph.addConnection ({ "oscA", "out", "sumA", "in" });
    graph.addConnection ({ "allocB", "pitch", "oscB", "pitch" });
    graph.addConnection ({ "oscB", "out", "sumB", "in" });
    graph.addConnection ({ "sumA", "out", "mixdown", "in.0" });
    graph.addConnection ({ "sumB", "out", "mixdown", "in.1" });
    graph.addConnection ({ "mixdown", "out", "masterout", "in" });

    graph.setOutput ("masterout", "out");

    const auto result = MultiplicityResolver::split (graph);
    REQUIRE (result.success);
    REQUIRE (result.hasGlobalDomain);
    REQUIRE (result.origins.size() == 2);

    const auto* originA = findOrigin (result, "allocA");
    const auto* originB = findOrigin (result, "allocB");
    REQUIRE (originA != nullptr);
    REQUIRE (originB != nullptr);
    CHECK (originA->instanceSumNodeId == "sumA");
    CHECK (originB->instanceSumNodeId == "sumB");
    CHECK (containsNode (originA->voiceGraph, "oscA"));
    CHECK_FALSE (containsNode (originA->voiceGraph, "oscB"));
    CHECK (containsNode (originB->voiceGraph, "oscB"));
    CHECK_FALSE (containsNode (originB->voiceGraph, "oscA"));

    CHECK (containsNode (result.globalGraph, "sumA"));
    CHECK (containsNode (result.globalGraph, "sumB"));
    CHECK (containsNode (result.globalGraph, "mixdown"));
    CHECK (containsNode (result.globalGraph, "masterout"));
}

TEST_CASE ("Two instance.sum nodes reducing the SAME origin is rejected", "[engine][MultiplicityResolver]")
{
    NodeGraph graph;
    graph.addNode ({ "alloc", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "sum1", "instance.mix", {}, {}, {} });
    graph.addNode ({ "sum2", "instance.mix", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });

    graph.addConnection ({ "alloc", "pitch", "osc", "pitch" });
    graph.addConnection ({ "osc", "out", "sum1", "in" });
    graph.addConnection ({ "osc", "out", "sum2", "in" });
    graph.addConnection ({ "sum1", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = MultiplicityResolver::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("one instance.sum is supported per origin"));
}

TEST_CASE ("instance.sum's input must resolve Poly - Scalar (nothing to reduce) is rejected",
           "[engine][MultiplicityResolver][DomainRedesign]")
{
    // §2.4's settled question: instance.sum's input is the one port in the
    // catalog required to be Poly. A constant feeding it directly (no
    // allocator anywhere in the graph) is exactly "nothing to reduce".
    NodeGraph graph;
    graph.addNode ({ "k", "util.constant", {}, {}, {} });
    graph.addNode ({ "sum", "instance.mix", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "k", "out", "sum", "in" });
    graph.addConnection ({ "sum", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = MultiplicityResolver::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("nothing to reduce"));
}

TEST_CASE ("An unwired instance.sum is not a domain boundary yet, not an error - even with no allocator "
           "anywhere in the graph",
           "[engine][MultiplicityResolver][M19]")
{
    // A freshly-placed instance.sum (before the user has wired anything into
    // it, or even placed an allocator yet) must not fail to compile — the
    // editor places a node, then wires it, as separate commands. Corrected
    // from DomainSplitterTests.cpp's own equivalent case: that suite
    // expected `hasGlobalDomain == false` here (an artefact of the OLD
    // model's fallback, which treated "no WIRED instance.mix" as "the whole
    // graph is voiceGraph" regardless of whether any allocator existed to
    // make that framing meaningful). Under this model, with no allocator at
    // all, nothing is ever Poly — `origins` is empty and the whole
    // (currently inert) graph is ordinary global content, which is the
    // more coherent answer, not a regression.
    NodeGraph graph;
    graph.addNode ({ "sum", "instance.mix", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "sum", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = MultiplicityResolver::split (graph);
    REQUIRE (result.success);
    CHECK (result.hasGlobalDomain);
    CHECK (result.origins.empty());
    CHECK (result.globalGraph.getNodes().size() == 2);
}

TEST_CASE ("instance.sum rejects more than one connection into its input port", "[engine][MultiplicityResolver][M19]")
{
    NodeGraph graph;
    graph.addNode ({ "sum", "instance.mix", {}, {}, {} });
    graph.addNode ({ "src1", "util.constant", {}, {}, {} });
    graph.addNode ({ "src2", "util.constant", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "src1", "out", "sum", "in" });
    graph.addConnection ({ "src2", "out", "sum", "in" });
    graph.addConnection ({ "sum", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = MultiplicityResolver::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("at most one connection"));
}

TEST_CASE ("A node fed directly by two different origins' Poly outputs is rejected - DomainRedesign.md section 2.1",
           "[engine][MultiplicityResolver][DomainRedesign]")
{
    NodeGraph graph;
    graph.addNode ({ "allocA", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "allocB", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "ringmod", "math.multiply", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });

    graph.addConnection ({ "allocA", "pitch", "ringmod", "in.0" });
    graph.addConnection ({ "allocB", "pitch", "ringmod", "in.1" });
    graph.addConnection ({ "ringmod", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = MultiplicityResolver::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("ringmod"));
    CHECK (result.errorMessage.contains ("allocA"));
    CHECK (result.errorMessage.contains ("allocB"));
}

TEST_CASE ("The exact motivating repro (section 0): env.adsr -> mix.gain connects successfully regardless of "
           "whether mix.gain is already wired to the output - no more order-dependent rejection",
           "[engine][MultiplicityResolver][DomainRedesign]")
{
    // §0's own repro: instance.allocate.voice -> env.adsr (poly), separately
    // osc.analog -> mix.gain (both mono, unconnected to the voice region) —
    // wiring env.adsr -> mix.gain directly used to be REJECTED by
    // DomainSplitter until mix.gain was ALSO wired to io.output (which
    // happened to fold mix.gain into the poly region first). Under this
    // model, mix.gain is an ordinary node with no fixed multiplicity — it
    // just resolves Poly the moment ANY Poly signal reaches it, in either
    // order.
    auto buildBase = [] {
        NodeGraph graph;
        graph.addNode ({ "alloc", "instance.allocate.voice", {}, {}, {} });
        graph.addNode ({ "env", "env.adsr", {}, {}, {} });
        graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
        graph.addNode ({ "gain", "mix.gain", {}, {}, {} });
        graph.addNode ({ "masterout", "io.output", {}, {}, {} });

        graph.addConnection ({ "alloc", "gate", "env", "gate" });
        graph.addConnection ({ "osc", "out", "gain", "audio" });
        graph.setOutput ("masterout", "out");
        return graph;
    };

    // Order 1: wire env -> gain FIRST, gain -> masterout second.
    {
        auto graph = buildBase();
        graph.addConnection ({ "env", "out", "gain", "gain" });
        const auto beforeOutput = MultiplicityResolver::split (graph);
        REQUIRE (beforeOutput.success); // accepted even with nothing downstream of gain yet

        graph.addConnection ({ "gain", "out", "masterout", "in" });
        const auto afterOutput = MultiplicityResolver::split (graph);
        REQUIRE (afterOutput.success);
        CHECK (afterOutput.outputOriginId == "alloc");
        const auto* origin = findOrigin (afterOutput, "alloc");
        REQUIRE (origin != nullptr);
        CHECK (containsNode (origin->voiceGraph, "gain"));
        CHECK (containsNode (origin->voiceGraph, "osc")); // the mono source, duplicated in (DOMAINS.md §2)
    }

    // Order 2 (the old failure mode): gain -> masterout FIRST, env -> gain second.
    {
        auto graph = buildBase();
        graph.addConnection ({ "gain", "out", "masterout", "in" });
        const auto beforeEnv = MultiplicityResolver::split (graph);
        REQUIRE (beforeEnv.success);
        CHECK (beforeEnv.hasGlobalDomain); // no Poly reaches gain/masterout yet — ordinary global content

        graph.addConnection ({ "env", "out", "gain", "gain" });
        const auto afterEnv = MultiplicityResolver::split (graph);
        REQUIRE (afterEnv.success); // <- the exact edge the OLD model rejected in this order
        CHECK (afterEnv.outputOriginId == "alloc");
    }
}

TEST_CASE ("A mono source that feeds only the global domain joins the global plan instead of being rejected",
           "[engine][MultiplicityResolver][M21]")
{
    NodeGraph graph;
    graph.addNode ({ "alloc", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "sum", "instance.mix", {}, {}, {} });
    graph.addNode ({ "audioin", "io.audioIn", {}, {}, {} });
    graph.addNode ({ "mixdown", "mix.sum", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });

    graph.addConnection ({ "alloc", "pitch", "osc", "pitch" });
    graph.addConnection ({ "osc", "out", "sum", "in" });
    graph.addConnection ({ "sum", "out", "mixdown", "in.0" });
    graph.addConnection ({ "audioin", "channel.0", "mixdown", "in.1" });
    graph.addConnection ({ "mixdown", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = MultiplicityResolver::split (graph);
    REQUIRE (result.success);
    REQUIRE (result.hasGlobalDomain);

    CHECK (containsNode (result.globalGraph, "audioin"));
    CHECK (containsNode (result.globalGraph, "mixdown"));
    CHECK (containsNode (result.globalGraph, "masterout"));
    CHECK_FALSE (containsNode (result.origins[0].voiceGraph, "audioin")); // never needed there — it never reaches a Poly node

    const auto factory = buildDefaultNodeFactory();
    CHECK (GraphCompiler::compile (result.origins[0].voiceGraph, factory, { 44100.0, 64 }, 1).success);
    auto global = GraphCompiler::compile (result.globalGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (global.success);
    CHECK (global.plan.hostInputNodes.size() == 1);
}

TEST_CASE ("An instance.allocate.voice not reachable from the output still runs its own voice region, "
           "and unrelated global content plays regardless",
           "[engine][MultiplicityResolver][InstanceAllocator]")
{
    // The exact 09-28-InstanceAllocator.1 regression, re-verified under the
    // new resolver: an allocator that genuinely exists must always be
    // compiled into its own origin bundle, whether or not its own signal
    // currently reaches the designated output.
    NodeGraph effect;
    effect.addNode ({ "in", "io.audioIn", {}, {}, {} });
    effect.addNode ({ "out", "io.output", {}, {}, {} });
    effect.addConnection ({ "in", "channel.0", "out", "in" });
    effect.setOutput ("out", "out");

    auto withUnconnectedAllocator = effect;
    withUnconnectedAllocator.addNode ({ "alloc", "instance.allocate.voice", {}, {}, {} }); // no connections at all
    auto result = MultiplicityResolver::split (withUnconnectedAllocator);
    REQUIRE (result.success);
    REQUIRE (result.hasGlobalDomain);
    REQUIRE (result.origins.size() == 1);
    CHECK (result.origins[0].originId == "alloc");
    CHECK (result.origins[0].instanceSumNodeId.isEmpty());
    REQUIRE (result.origins[0].voiceGraph.getNodes().size() == 1);
    CHECK (result.origins[0].voiceGraph.getNodes()[0].id == "alloc");

    REQUIRE (result.globalGraph.getNodes().size() == 2);
    CHECK (result.globalGraph.getOutputNodeId() == "out");

    // Also covers the allocator's own trigger source (io.noteIn), backward-
    // pulled into its origin even though NEITHER it nor the allocator
    // reaches the designated output.
    auto withWiredButIrrelevantAllocator = effect;
    withWiredButIrrelevantAllocator.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
    withWiredButIrrelevantAllocator.addNode ({ "alloc", "instance.allocate.voice", {}, {}, {} });
    withWiredButIrrelevantAllocator.addConnection ({ "noteIn", "notes", "alloc", "spawn" });
    result = MultiplicityResolver::split (withWiredButIrrelevantAllocator);
    REQUIRE (result.success);
    REQUIRE (result.hasGlobalDomain);
    REQUIRE (result.origins.size() == 1);
    REQUIRE (result.origins[0].voiceGraph.getNodes().size() == 2);
    for (const auto& node : result.origins[0].voiceGraph.getNodes())
        CHECK ((node.id == "noteIn" || node.id == "alloc"));

    REQUIRE (result.globalGraph.getNodes().size() == 2);
    for (const auto& node : result.globalGraph.getNodes())
        CHECK ((node.id == "in" || node.id == "out"));
}

TEST_CASE ("09-28-InstanceAllocator.1 (part 4): an unconnected node is folded into the global domain "
           "even when a mono source is also accepted in the same graph",
           "[engine][MultiplicityResolver][InstanceAllocator]")
{
    NodeGraph graph;
    graph.addNode ({ "alloc", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "sum", "instance.mix", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addNode ({ "orphan", "excite.burst", {}, {}, {} }); // never connected to anything

    graph.addConnection ({ "alloc", "pitch", "osc", "pitch" });
    graph.addConnection ({ "osc", "out", "sum", "in" });
    graph.addConnection ({ "sum", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    const auto result = MultiplicityResolver::split (graph);
    REQUIRE (result.success);
    CHECK (containsNode (result.globalGraph, "orphan"));
    CHECK_FALSE (containsNode (result.origins[0].voiceGraph, "orphan"));
}

TEST_CASE ("An origin with no instance.sum still targets the GRAPH's real designated output when that "
           "output is already this origin's own - not the allocator's own gate",
           "[engine][MultiplicityResolver]")
{
    // Real, found-live bug (caught by real audio-behaviour tests one layer
    // up, VoiceRenderTests.cpp/GraphEditControllerTests.cpp, not by any
    // resolver-level structural check): an origin with no instance.sum
    // ALWAYS retargeted its own voiceGraph's output to (originId, "gate"),
    // even when the graph's REAL designated output was already a member of
    // this exact origin (buildVoiceProofGraph()'s own shape: no instance.sum
    // anywhere, "amp" IS the audible output). That silently swapped every
    // such plan's real audible signal for the allocator's own raw Boolean
    // gate value.
    auto graph = buildVoiceProofGraph();
    graph.removeConnection ("svf", "out", "amp", "audio"); // still a real edit; must not disturb the output designation

    const auto result = MultiplicityResolver::split (graph);
    REQUIRE (result.success);
    CHECK_FALSE (result.hasGlobalDomain);
    CHECK (result.outputOriginId == "allocator");
    REQUIRE (result.origins.size() == 1);
    CHECK (result.origins[0].voiceGraph.getOutputNodeId() == "amp");
    CHECK (result.origins[0].voiceGraph.getOutputPortId() == "out");

    bool sawDisconnectedEdge = false;
    for (const auto& connection : result.origins[0].voiceGraph.getConnections())
        if (connection.fromNodeId == "svf" && connection.toNodeId == "amp")
            sawDisconnectedEdge = true;
    CHECK_FALSE (sawDisconnectedEdge);
    CHECK (result.globalGraph.getNodes().empty());
}
