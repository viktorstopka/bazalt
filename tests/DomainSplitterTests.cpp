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

// ---- M21: mono graphs, mono sources, and the voice->global bypass ----------

namespace
{
    // The split-test graph plus a global-domain mix.sum that sums the voice
    // output (in.0, via instance.mix) with an io.audioIn (in.1), and a
    // designated output after it:
    //   osc -> svf -> instancemix -> sum.in.0
    //   audioIn.channel.0 -----------> sum.in.1 -> masterout
    NodeGraph buildMonoSourceGraph()
    {
        NodeGraph graph;
        graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
        graph.addNode ({ "svf", "filter.svf", {}, {}, {} });
        graph.addNode ({ "instancemix", "instance.mix", {}, {}, {} });
        graph.addNode ({ "sum", "mix.sum", {}, {}, {} });
        graph.addNode ({ "audioin", "io.audioIn", {}, {}, {} });
        graph.addNode ({ "masterout", "io.output", {}, {}, {} });

        graph.addConnection ({ "osc", "out", "svf", "in" });
        graph.addConnection ({ "svf", "out", "instancemix", "in" });
        graph.addConnection ({ "instancemix", "out", "sum", "in.0" });
        graph.addConnection ({ "audioin", "channel.0", "sum", "in.1" });
        graph.addConnection ({ "sum", "out", "masterout", "in" });

        graph.setOutput ("masterout", "out");
        return graph;
    }

    bool containsNode (const NodeGraph& graph, const juce::String& id)
    {
        return graph.findNode (id) != nullptr;
    }
}

TEST_CASE ("A graph with no instance.allocator and no instance.mix is a mono graph",
           "[engine][DomainSplitter][M21]")
{
    // DOMAINS.md §7: the allocator's outputs are what make a region poly, so
    // without one nothing is per-instance — the whole graph is mono and runs
    // once, every block, rather than once per active voice.
    NodeGraph effect;
    effect.addNode ({ "in", "io.audioIn", {}, {}, {} });
    effect.addNode ({ "out", "io.output", {}, {}, {} });
    effect.addConnection ({ "in", "channel.0", "out", "in" });
    effect.setOutput ("out", "out");

    const auto result = DomainSplitter::split (effect);
    REQUIRE (result.success);
    CHECK (result.monoOnly);
    CHECK_FALSE (result.hasGlobalDomain);
    CHECK (result.voiceGraph.getNodes().size() == 2); // still the unchanged graph, for anything keyed on it
}

TEST_CASE ("09-28-InstanceAllocator.1: an instance.allocator not reachable from the output still runs real per-voice plans, and the unrelated output plays regardless",
           "[engine][DomainSplitter][InstanceAllocator]")
{
    // The exact regression this milestone fixes, in its FINAL corrected
    // shape (a first cut set monoOnly=true here instead — which fixed the
    // audible symptom but also silently disabled MIDI dispatch/voice
    // rendering entirely, breaking the ability to build and observe an
    // allocator chain, e.g. via view.glance, before it's wired to the
    // output. Found live, same session, real user testing). Two
    // requirements, genuinely in tension, both real:
    //  (a) content unrelated to the allocator must play regardless — an
    //      independent, unconditionally-running global region.
    //  (b) an allocator that genuinely exists must ALWAYS receive MIDI and
    //      run real per-voice plans, whether or not its own output
    //      currently reaches the designated output.
    NodeGraph effect;
    effect.addNode ({ "in", "io.audioIn", {}, {}, {} });
    effect.addNode ({ "out", "io.output", {}, {}, {} });
    effect.addConnection ({ "in", "channel.0", "out", "in" });
    effect.setOutput ("out", "out");

    NodeGraph withUnconnectedAllocator = effect;
    withUnconnectedAllocator.addNode ({ "alloc", "instance.allocator", {}, {}, {} }); // no connections at all
    auto result = DomainSplitter::split (withUnconnectedAllocator);
    REQUIRE (result.success);
    CHECK_FALSE (result.monoOnly); // a real allocator exists - voices always run
    REQUIRE (result.hasGlobalDomain); // an independent region carries the unrelated content
    CHECK (result.instanceMixNodeId.isEmpty()); // unbridged - no instance.mix involved

    REQUIRE (result.voiceGraph.getNodes().size() == 1);
    CHECK (result.voiceGraph.getNodes()[0].id == "alloc");

    REQUIRE (result.globalGraph.getNodes().size() == 2);
    CHECK (result.globalGraph.getOutputNodeId() == "out");
    CHECK (result.globalGraph.getOutputPortId() == "out");

    // Also covers the case where the allocator IS wired to something real
    // (its own trigger source, io.noteIn) — that source must land in
    // voiceGraph alongside the allocator (it's the allocator's own
    // predecessor, not unrelated content), even though NEITHER of them
    // reaches the designated output.
    NodeGraph withWiredButIrrelevantAllocator = effect;
    withWiredButIrrelevantAllocator.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
    withWiredButIrrelevantAllocator.addNode ({ "alloc", "instance.allocator", {}, {}, {} });
    withWiredButIrrelevantAllocator.addConnection ({ "noteIn", "notes", "alloc", "spawn" });
    // alloc's own outputs (pitch/gate/...) are never wired to anything further.
    result = DomainSplitter::split (withWiredButIrrelevantAllocator);
    REQUIRE (result.success);
    CHECK_FALSE (result.monoOnly);
    REQUIRE (result.hasGlobalDomain);

    REQUIRE (result.voiceGraph.getNodes().size() == 2);
    for (const auto& node : result.voiceGraph.getNodes())
        CHECK ((node.id == "noteIn" || node.id == "alloc"));

    REQUIRE (result.globalGraph.getNodes().size() == 2);
    for (const auto& node : result.globalGraph.getNodes())
        CHECK ((node.id == "in" || node.id == "out"));
}

TEST_CASE ("09-28-InstanceAllocator.1: an instance.allocator genuinely wired through to the output IS treated as per-voice, even with no instance.mix",
           "[engine][DomainSplitter][InstanceAllocator]")
{
    // The case that must keep working exactly as before: a plain voice
    // patch (no instance.mix at all — the "voice sum is the final output"
    // fallback handles delivery) with the allocator's own signal genuinely
    // reaching the designated output.
    NodeGraph synth;
    synth.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
    synth.addNode ({ "alloc", "instance.allocator", {}, {}, {} });
    synth.addNode ({ "osc", "osc.analog", {}, {}, {} });
    synth.addNode ({ "out", "io.output", {}, {}, {} });
    synth.addConnection ({ "noteIn", "notes", "alloc", "spawn" });
    synth.addConnection ({ "alloc", "pitch", "osc", "pitch" });
    synth.addConnection ({ "osc", "out", "out", "in" });
    synth.setOutput ("out", "out");

    const auto result = DomainSplitter::split (synth);
    REQUIRE (result.success);
    CHECK_FALSE (result.monoOnly);
    CHECK_FALSE (result.hasGlobalDomain); // no instance.mix — the pre-existing "voice sum is final output" path
    CHECK (result.voiceGraph.getNodes().size() == 4);
}

TEST_CASE ("A mono source that feeds only the global domain joins the global plan instead of being rejected",
           "[engine][DomainSplitter][M21]")
{
    // Before M21 io.audioIn here was "connected to neither domain": not
    // upstream of the mix (so not voice) and not downstream of it (so not
    // global). It is a mono source, and mono is free everywhere (DOMAINS.md §2).
    const auto result = DomainSplitter::split (buildMonoSourceGraph());
    REQUIRE (result.success);
    REQUIRE (result.hasGlobalDomain);
    CHECK_FALSE (result.monoOnly);

    CHECK (containsNode (result.globalGraph, "audioin"));
    CHECK (containsNode (result.globalGraph, "sum"));
    CHECK (containsNode (result.globalGraph, "masterout"));
    CHECK_FALSE (containsNode (result.voiceGraph, "audioin"));
    CHECK (containsNode (result.voiceGraph, "osc"));

    // The mono source's own wire into the global chain is kept.
    bool sawAudioInWire = false;
    for (const auto& connection : result.globalGraph.getConnections())
        if (connection.fromNodeId == "audioin" && connection.toNodeId == "sum")
            sawAudioInWire = true;
    CHECK (sawAudioInWire);

    // Both halves compile, and the global plan knows it has a host-input node.
    const auto factory = buildDefaultNodeFactory();
    CHECK (GraphCompiler::compile (result.voiceGraph, factory, { 44100.0, 64 }, 1).success);
    auto global = GraphCompiler::compile (result.globalGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (global.success);
    CHECK (global.plan.hostInputNodes.size() == 1);
}

TEST_CASE ("A voice-domain node feeding the global domain around instance.mix is an error, not a silently dropped cable",
           "[engine][DomainSplitter][M21]")
{
    // DOMAINS.md §2: "poly -> mono requires Voice Mix. There is no implicit
    // summing anywhere, ever." This edge used to be discarded when the global
    // graph was built, leaving in.1 reading silence with no explanation.
    auto graph = buildMonoSourceGraph();
    graph.removeConnection ("audioin", "channel.0", "sum", "in.1");
    graph.addConnection ({ "svf", "out", "sum", "in.1" }); // a voice node straight into the global chain

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("svf"));
    CHECK (result.errorMessage.contains ("instance.mix"));
}

TEST_CASE ("A mono source that feeds BOTH domains gets a clear error asking for one per domain",
           "[engine][DomainSplitter][M21]")
{
    auto graph = buildMonoSourceGraph();
    graph.addConnection ({ "audioin", "channel.1", "svf", "in" }); // now also upstream of the mix, i.e. voice domain

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("audioin"));
    CHECK (result.errorMessage.contains ("mono source"));
}

TEST_CASE ("An unconnected node is still rejected even when a mono source is accepted",
           "[engine][DomainSplitter][M21]")
{
    auto graph = buildMonoSourceGraph();
    graph.addNode ({ "orphan", "io.transport", {}, {}, {} }); // feeds nothing

    const auto result = DomainSplitter::split (graph);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("orphan"));
}
