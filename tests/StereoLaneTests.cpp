// wiki/plans/StereoChannels.md: a per-channel node (every Audio port
// Inherited) fed a stereo signal runs once per channel — a "lane" — each with
// its own state, so stereo survives filters, delays and feedback loops
// without any node knowing about stereo.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using namespace bazalt::engine;

namespace
{
    // A sine on the left channel only, through `chain` (each with an
    // "in" — "audio" for mix.gain — and an "out"), into io.output.
    NodeGraph leftOnlySineThrough (std::vector<NodeInstance> chain, float cutoff = 1000.0f)
    {
        NodeGraph graph;
        graph.addNode ({ "osc", "osc.sine", {}, { { "osc.sine.frequency", 220.0f } }, {} });
        graph.addNode ({ "combine", "stereo.combine", {}, {}, {} });
        graph.addConnection ({ "osc", "out", "combine", "left" });

        juce::String previous = "combine";
        for (auto& node : chain)
        {
            if (node.type == "filter.svf")
                node.parameters["filter.svf.cutoff"] = cutoff;
            const auto id = node.id;
            graph.addNode (node);
            graph.addConnection ({ previous, "out", id, node.type == "mix.gain" ? "audio" : "in" });
            previous = id;
        }

        graph.addNode ({ "master", "io.output", {}, {}, {} });
        graph.addConnection ({ previous, "out", "master", "in" });
        graph.setOutput ("master", "out");
        return graph;
    }

    struct Peaks
    {
        float left = 0.0f, right = 0.0f;
    };

    Peaks renderPeaks (ExecutionPlan& plan, int blocks = 20, int blockSize = 64)
    {
        Peaks peaks;
        REQUIRE (plan.finalOutputBufferIndexRight >= 0);
        for (int b = 0; b < blocks; ++b)
        {
            plan.process (blockSize);
            const auto* left = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
            const auto* right = plan.blockBuffers[(size_t) plan.finalOutputBufferIndexRight].getBlock().getChannelPointer (0);
            for (int i = 0; i < blockSize; ++i)
            {
                peaks.left = std::max (peaks.left, std::abs (left[i]));
                peaks.right = std::max (peaks.right, std::abs (right[i]));
            }
        }
        return peaks;
    }

    size_t lanesOf (const ExecutionPlan& plan, const juce::String& id)
    {
        return plan.laneSlotsBySlot[(size_t) plan.nodeIdToSlot.at (id)].size();
    }
}

TEST_CASE ("A per-channel filter fed stereo runs one lane per channel, each with its own state",
           "[engine][GraphCompiler][stereo]")
{
    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (leftOnlySineThrough ({ { "svf", "filter.svf", {}, {}, {} },
                                                                 { "amp", "mix.gain", {}, {}, {} } }),
                                          factory, { 48000.0, 64 }, 1);
    REQUIRE (result.success);
    CHECK (lanesOf (result.plan, "svf") == 2);
    CHECK (lanesOf (result.plan, "amp") == 2);

    const auto peaks = renderPeaks (result.plan);
    CHECK (peaks.left > 0.1f);
    CHECK (peaks.right == 0.0f); // the right lane never heard the left channel
}

TEST_CASE ("A mono chain keeps one lane per node", "[engine][GraphCompiler][stereo]")
{
    auto factory = buildDefaultNodeFactory();
    NodeGraph graph;
    graph.addNode ({ "osc", "osc.sine", {}, {}, {} });
    graph.addNode ({ "svf", "filter.svf", {}, {}, {} });
    graph.addNode ({ "master", "io.output", {}, {}, {} });
    graph.addConnection ({ "osc", "out", "svf", "in" });
    graph.addConnection ({ "svf", "out", "master", "in" });
    graph.setOutput ("master", "out");

    auto result = GraphCompiler::compile (graph, factory, { 48000.0, 64 }, 1);
    REQUIRE (result.success);
    CHECK (lanesOf (result.plan, "svf") == 1);
    CHECK (result.plan.nodes.size() == graph.getNodes().size()); // no extra lane instances

    const auto peaks = renderPeaks (result.plan);
    CHECK (peaks.left > 0.1f);
    CHECK (peaks.left == peaks.right); // mono broadcast into the stereo output
}

TEST_CASE ("A stereo feedback loop keeps its channels apart and publishes both", "[engine][GraphCompiler][stereo]")
{
    // combine -> add.in.0; add -> delay; delay -> add.in.1 (the loop) and -> master.
    auto factory = buildDefaultNodeFactory();
    NodeGraph graph;
    graph.addNode ({ "osc", "osc.sine", {}, { { "osc.sine.frequency", 220.0f } }, {} });
    graph.addNode ({ "combine", "stereo.combine", {}, {}, {} });
    graph.addNode ({ "add", "math.add", {}, {}, {} });
    graph.addNode ({ "delay", "delay.line", {}, { { "delay.line.samples", 10.0f } }, {} });
    graph.addNode ({ "master", "io.output", {}, {}, {} });
    graph.addConnection ({ "osc", "out", "combine", "left" });
    graph.addConnection ({ "combine", "out", "add", "in.0" });
    graph.addConnection ({ "add", "out", "delay", "in" });
    graph.addConnection ({ "delay", "out", "add", "in.1" });
    graph.addConnection ({ "delay", "out", "master", "in" });
    graph.setOutput ("master", "out");

    auto result = GraphCompiler::compile (graph, factory, { 48000.0, 64 }, 1);
    REQUIRE (result.success);
    CHECK (lanesOf (result.plan, "delay") == 2);
    CHECK (lanesOf (result.plan, "add") == 2);

    const auto peaks = renderPeaks (result.plan);
    CHECK (peaks.left > 0.1f);
    CHECK (peaks.right == 0.0f);
}

TEST_CASE ("A value-only edit keeps every lane and updates all of them", "[engine][GraphCompiler][stereo][liveEdit]")
{
    auto factory = buildDefaultNodeFactory();
    auto first = GraphCompiler::compile (leftOnlySineThrough ({ { "svf", "filter.svf", {}, {}, {} } }, 1000.0f),
                                         factory, { 48000.0, 64 }, 1);
    REQUIRE (first.success);
    first.plan.process (64);

    auto second = GraphCompiler::compile (leftOnlySineThrough ({ { "svf", "filter.svf", {}, {}, {} } }, 2000.0f),
                                          factory, { 48000.0, 64 }, 2, &first.plan);
    REQUIRE (second.success);

    const auto& firstLanes = first.plan.laneSlotsBySlot[(size_t) first.plan.nodeIdToSlot.at ("svf")];
    const auto& secondLanes = second.plan.laneSlotsBySlot[(size_t) second.plan.nodeIdToSlot.at ("svf")];
    REQUIRE (secondLanes.size() == 2);
    for (size_t lane = 0; lane < 2; ++lane)
    {
        const auto* node = second.plan.nodes[(size_t) secondLanes[lane]].get();
        CHECK (node == first.plan.nodes[(size_t) firstLanes[lane]].get());
        CHECK (std::any_of (second.plan.pendingParameterUpdates.begin(), second.plan.pendingParameterUpdates.end(),
                            [node] (const auto& update) { return update.node == node && update.parameterId == "filter.svf.cutoff"; }));
    }
}

TEST_CASE ("Bypassing a per-channel node passes both channels through", "[engine][GraphCompiler][stereo]")
{
    auto factory = buildDefaultNodeFactory();
    NodeInstance svf { "svf", "filter.svf", {}, {}, {} };
    svf.properties["bypassed"] = true;
    auto result = GraphCompiler::compile (leftOnlySineThrough ({ svf }), factory, { 48000.0, 64 }, 1);
    REQUIRE (result.success);

    const auto peaks = renderPeaks (result.plan);
    CHECK (peaks.left > 0.5f); // unfiltered sine
    CHECK (peaks.right == 0.0f);
}
