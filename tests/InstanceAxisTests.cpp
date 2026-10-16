// wiki/plans/InstanceAxis.md: one plan with an instance axis instead of one
// plan per voice. The new plan must sound exactly like the per-voice plans
// summed (for patches whose once-only part has no state of its own), skip
// what doesn't run, and reset one instance without touching the others.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/InstanceExpansion.h"
#include "bazalt/engine/graph/MultiplicityResolver.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"
#include <vector>

using namespace bazalt::engine;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int block = 64;

    /** noteIn → Voice → pitch → sine (gated) → × 0.5 → Merge → out. */
    NodeGraph voicePatch (bool withLfo = false, bool stereo = false)
    {
        NodeGraph graph;
        graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
        graph.addNode ({ "voice", "life.population", {}, {}, {} });
        graph.addNode ({ "p2f", "math.pitchToFrequency", {}, {}, {} });
        graph.addNode ({ "osc", "source.sine", {}, {}, {} });
        graph.addNode ({ "amp", "math.multiply", {}, { { "in.1", 0.5f } }, {} });
        graph.addNode ({ "merge", "life.merge", {}, {}, {} });
        graph.addNode ({ "out", "io.output", {}, {}, {} });
        graph.addConnection ({ "noteIn", "notes", "voice", "notes" });
        graph.addConnection ({ "voice", "pitch", "p2f", "pitch" });
        graph.addConnection ({ "p2f", "frequency", "osc", "source.sine.frequency" });
        graph.addConnection ({ "voice", "gate", "osc", "source.sine.amplitude" });
        graph.addConnection ({ "osc", "out", "amp", "in.0" });
        if (stereo)
        {
            graph.addNode ({ "pan", "space.pan", {}, { { "space.pan.pan", 0.6f } }, {} });
            graph.addConnection ({ "amp", "out", "pan", "in" });
            graph.addConnection ({ "pan", "out", "merge", "in" });
        }
        else
        {
            graph.addConnection ({ "amp", "out", "merge", "in" });
        }
        graph.addConnection ({ "merge", "out", "out", "in" });
        if (withLfo)
        {
            graph.addNode ({ "lfo", "source.sine", {}, { { "source.sine.frequency", 3.0f } }, {} });
            graph.addConnection ({ "lfo", "out", "amp", "in.1" });
        }
        graph.setOutput ("out", "out");
        return graph;
    }

    InstanceLayout layoutOf (const NodeGraph& graph, int count)
    {
        const auto split = MultiplicityResolver::split (graph);
        REQUIRE (split.success);
        return computeInstanceLayout (split, [count] (const juce::String&) { return count; });
    }

    const float* buffer (const ExecutionPlan& plan, int index)
    {
        return plan.blockBuffers[(size_t) index].getBlock().getChannelPointer (0);
    }

    void playNote (ExecutionPlan& plan, int instance, float pitch)
    {
        plan.resetInstance (0, instance);
        auto& state = plan.instanceGroups[0]->instances[(size_t) instance];
        state.run = state.audible = true;
        auto* noteIn = dynamic_cast<nodes::IoNoteInNode*> (plan.instanceGroups[0]->noteInNodes[(size_t) instance]);
        REQUIRE (noteIn != nullptr);
        noteIn->injectNoteOn (pitch, 0.8f);
    }
}

TEST_CASE ("The layout runs the voice part per instance and everything else once", "[engine][instanceAxis]")
{
    const auto layout = layoutOf (voicePatch (true), 8);
    REQUIRE (layout.groups.size() == 1);
    CHECK (layout.groups[0].originId == "voice");
    for (const auto* id : { "noteIn", "voice", "p2f", "osc", "amp" })
        CHECK (layout.groupOf (id) == 0);
    for (const auto* id : { "lfo", "merge", "out" })
        CHECK (layout.groupOf (id) == -1);

    const auto factory = buildDefaultNodeFactory();
    auto result = compileWithInstances (voicePatch (true), layout, factory, { sampleRate, block }, 1);
    REQUIRE (result.success);
    CHECK (result.plan.copySlotsByBaseId.at ("osc").size() == 8);
    CHECK (result.plan.copySlotsByBaseId.at ("lfo").size() == 1); // one LFO, shared — not eight drifting copies
    CHECK (result.plan.instanceGroups.size() == 1);
    CHECK (result.plan.getNodeById ("osc") != nullptr); // an instanced node answers by its own id (instance 0)
}

TEST_CASE ("One instanced plan sounds exactly like the per-voice plans summed", "[engine][instanceAxis]")
{
    const auto graph = voicePatch();
    const auto factory = buildDefaultNodeFactory();

    // Today's engine: one plan per voice, summed outside.
    const auto split = MultiplicityResolver::split (graph);
    REQUIRE (split.success);
    std::vector<CompileResult> voices;
    for (int v = 0; v < 2; ++v)
    {
        voices.push_back (GraphCompiler::compile (split.origins[0].voiceGraph, factory, { sampleRate, block }, 1));
        REQUIRE (voices.back().success);
        auto& plan = voices.back().plan;
        plan.reset();
        dynamic_cast<nodes::IoNoteInNode*> (plan.getNodeById (plan.noteInNodeId))->injectNoteOn (v == 0 ? 60.0f : 67.0f, 0.8f);
    }

    // The instance axis: one plan, instances 0 and 2 playing.
    auto instanced = compileWithInstances (graph, layoutOf (graph, 4), factory, { sampleRate, block }, 1);
    REQUIRE (instanced.success);
    auto& plan = instanced.plan;
    playNote (plan, 0, 60.0f);
    playNote (plan, 2, 67.0f);

    size_t mismatches = 0;
    float loudest = 0.0f;
    for (int b = 0; b < 100; ++b)
    {
        for (auto& v : voices)
            v.plan.process (block);
        plan.process (block);
        const auto* a = buffer (voices[0].plan, voices[0].plan.finalOutputBufferIndex);
        const auto* c = buffer (voices[1].plan, voices[1].plan.finalOutputBufferIndex);
        const auto* got = buffer (plan, plan.finalOutputBufferIndex);
        for (int i = 0; i < block; ++i)
        {
            const auto expected = 0.0f + a[i] + c[i];
            mismatches += got[i] != expected;
            loudest = std::max (loudest, std::abs (got[i]));
        }
    }
    CHECK (mismatches == 0);
    CHECK (loudest > 0.3f);
}

TEST_CASE ("An instance that doesn't run doesn't advance; reset starts it fresh", "[engine][instanceAxis]")
{
    const auto graph = voicePatch();
    const auto factory = buildDefaultNodeFactory();
    auto result = compileWithInstances (graph, layoutOf (graph, 2), factory, { sampleRate, block }, 1);
    REQUIRE (result.success);
    auto& plan = result.plan;

    // Instance 0 plays for a while; instance 1 never ran.
    playNote (plan, 0, 60.0f);
    for (int b = 0; b < 50; ++b)
        plan.process (block);
    plan.instanceGroups[0]->instances[0] = {};

    // Instance 1 now plays the same note: identical to instance 0's first block.
    auto reference = compileWithInstances (graph, layoutOf (graph, 2), factory, { sampleRate, block }, 1);
    playNote (reference.plan, 0, 60.0f);
    reference.plan.process (block);
    playNote (plan, 1, 60.0f);
    plan.process (block);
    const auto* expected = buffer (reference.plan, reference.plan.finalOutputBufferIndex);
    const auto* got = buffer (plan, plan.finalOutputBufferIndex);
    for (int i = 0; i < block; ++i)
        REQUIRE (got[i] == expected[i]);

    // Playing instance 0 again after resetInstance also starts from scratch.
    plan.instanceGroups[0]->instances[1] = {};
    playNote (plan, 0, 60.0f);
    plan.process (block);
    for (int i = 0; i < block; ++i)
        REQUIRE (got[i] == expected[i]);
}

TEST_CASE ("A stereo voice reduces into a stereo sum", "[engine][instanceAxis][stereo]")
{
    const auto graph = voicePatch (false, true);
    const auto factory = buildDefaultNodeFactory();
    auto result = compileWithInstances (graph, layoutOf (graph, 4), factory, { sampleRate, block }, 1);
    REQUIRE (result.success);
    auto& plan = result.plan;
    REQUIRE (plan.finalOutputBufferIndexRight >= 0);
    playNote (plan, 1, 64.0f);
    float left = 0.0f, right = 0.0f;
    for (int b = 0; b < 20; ++b)
    {
        plan.process (block);
        for (int i = 0; i < block; ++i)
        {
            left = std::max (left, std::abs (buffer (plan, plan.finalOutputBufferIndex)[i]));
            right = std::max (right, std::abs (buffer (plan, plan.finalOutputBufferIndexRight)[i]));
        }
    }
    CHECK (right > left); // panned to the right
    CHECK (left > 0.0f);
}

TEST_CASE ("A hundred instances reduce through a tree, in sum and in average", "[engine][instanceAxis][2b]")
{
    // One reduce takes 16 instances (32 channels for stereo); more go through
    // leaves and a combine. Three instances playing one note sum to three
    // times one instance alone, or average back to it.
    const auto factory = buildDefaultNodeFactory();
    for (const auto average : { false, true })
    {
        auto graph = voicePatch (false, true);
        graph.findNode ("merge")->parameters["life.merge.mode"] = average ? 1.0f : 0.0f;
        auto result = compileWithInstances (graph, layoutOf (graph, 100), factory, { sampleRate, block }, 1);
        REQUIRE (result.success);
        auto& plan = result.plan;
        REQUIRE (plan.instanceGroups[0]->count == 100);
        REQUIRE (plan.finalOutputBufferIndexRight >= 0);
        for (const auto instance : { 3, 40, 99 })
            playNote (plan, instance, 60.0f);

        auto solo = compileWithInstances (voicePatch (false, true), layoutOf (voicePatch (false, true), 2), factory, { sampleRate, block }, 1);
        REQUIRE (solo.success);
        playNote (solo.plan, 0, 60.0f);

        float loudest = 0.0f;
        for (int b = 0; b < 20; ++b)
        {
            plan.process (block);
            solo.plan.process (block);
            for (const auto channel : { plan.finalOutputBufferIndex, plan.finalOutputBufferIndexRight })
            {
                const auto soloChannel = channel == plan.finalOutputBufferIndex ? solo.plan.finalOutputBufferIndex
                                                                                 : solo.plan.finalOutputBufferIndexRight;
                for (int i = 0; i < block; ++i)
                {
                    const auto expected = buffer (solo.plan, soloChannel)[i] * (average ? 1.0f : 3.0f);
                    REQUIRE (buffer (plan, channel)[i] == Catch::Approx (expected).margin (1.0e-5));
                    loudest = std::max (loudest, std::abs (buffer (plan, channel)[i]));
                }
            }
        }
        CHECK (loudest > 0.1f);
        for (const auto instance : { 3, 40, 99 })
            CHECK (plan.instanceGroups[0]->instances[(size_t) instance].peak > 0.1f); // each leaf measures its own
    }
}

TEST_CASE ("The output itself can run per instance; a fading instance ramps out", "[engine][instanceAxis]")
{
    // No Merge: the output is the voice's own signal, summed in front of it.
    auto graph = voicePatch();
    graph.removeNode ("merge");
    graph.removeNode ("out");
    graph.setOutput ("amp", "out");
    const auto factory = buildDefaultNodeFactory();
    auto result = compileWithInstances (graph, layoutOf (graph, 2), factory, { sampleRate, block }, 1);
    REQUIRE (result.success);
    auto& plan = result.plan;
    playNote (plan, 0, 69.0f);
    for (int b = 0; b < 10; ++b)
        plan.process (block);

    auto& state = plan.instanceGroups[0]->instances[0];
    state.fading = true;
    state.gainStart = 1.0f;
    state.gainEnd = 0.0f;
    state.rampSamples = 32;
    state.peak = 0.0f;
    plan.process (block);
    const auto* out = buffer (plan, plan.finalOutputBufferIndex);
    for (int i = 32; i < block; ++i)
        CHECK (out[i] == 0.0f); // past the ramp: gone
    CHECK (state.peak > 0.1f);  // measured before the gain
}

TEST_CASE ("A Population born Always and a merge of it compile as one instanced plan", "[engine][instanceAxis]")
{
    NodeGraph graph;
    graph.addNode ({ "swarm", "life.population", {}, { { "life.population.born", 1.0f } }, {} });
    graph.addNode ({ "osc", "source.sine", {}, { { "source.sine.frequency", 220.0f } }, {} });
    graph.addNode ({ "merge", "life.merge", {}, {}, {} });
    graph.addConnection ({ "swarm", "gate", "osc", "source.sine.amplitude" });
    graph.addConnection ({ "osc", "out", "merge", "in" });
    graph.setOutput ("merge", "out");
    const auto factory = buildDefaultNodeFactory();
    auto result = compileWithInstances (graph, layoutOf (graph, 8), factory, { sampleRate, block }, 1);
    REQUIRE (result.success);
    CHECK (result.plan.instanceGroups[0]->originNodes[7] != nullptr);
    CHECK (result.plan.instanceGroups[0]->noteInNodes[0] == nullptr);
}

TEST_CASE ("'@' in a node id is refused: it is reserved for instances", "[engine][instanceAxis]")
{
    auto graph = voicePatch();
    graph.addNode ({ "bad@1", "math.add", {}, {}, {} });
    const auto factory = buildDefaultNodeFactory();
    CHECK_FALSE (compileWithInstances (graph, layoutOf (graph, 2), factory, { sampleRate, block }, 1).success);
}

TEST_CASE ("An internal trigger chain runs once and only the birth copy hears it", "[engine][instanceAxis]")
{
    // A clock that spawns the swarm, and a toggle the same clock flips (which
    // runs once, outside the voices): legal now that the chain isn't copied.
    NodeGraph graph;
    graph.addNode ({ "clock", "time.clock", {}, {}, {} });
    graph.addNode ({ "swarm", "life.population", {}, { { "life.population.lives", 1.0f } }, {} });
    graph.addNode ({ "side", "logic.toggle", {}, {}, {} });
    graph.addNode ({ "osc", "source.sine", {}, {}, {} });
    graph.addNode ({ "merge", "life.merge", {}, {}, {} });
    graph.addConnection ({ "clock", "tick", "swarm", "born" });
    graph.addConnection ({ "clock", "tick", "side", "trigger" });
    graph.addConnection ({ "swarm", "gate", "osc", "source.sine.amplitude" });
    graph.addConnection ({ "osc", "out", "merge", "in" });
    graph.setOutput ("merge", "out");

    const auto layout = layoutOf (graph, 4);
    REQUIRE (layout.groups.size() == 1);
    CHECK (layout.groups[0].internallyTriggered);
    CHECK (layout.groupOf ("clock") == -1);
    CHECK (layout.groupOf ("swarm") == 0);

    const auto expanded = expandInstances (graph, layout, 1);
    REQUIRE (expanded.success);
    int spawnFeeds = 0;
    for (const auto& c : expanded.graph.getConnections())
        if (c.fromNodeId == "clock" && c.toPortId == "born")
        {
            ++spawnFeeds;
            CHECK (c.toNodeId == "swarm@birth");
        }
    CHECK (spawnFeeds == 1);

    const auto factory = buildDefaultNodeFactory();
    CHECK (compileWithInstances (graph, layout, factory, { sampleRate, block }, 1).success);
}

TEST_CASE ("A MIDI trigger chain still runs per instance", "[engine][instanceAxis]")
{
    const auto layout = layoutOf (voicePatch(), 4);
    CHECK_FALSE (layout.groups[0].internallyTriggered);
    CHECK (layout.groupOf ("noteIn") == 0);
}
