// wiki/plans/LifeCycle.md 2b.2: Population. Births from inside the patch go
// through the birth copy and start a member at the exact sample, whatever the
// block size; the driver (here a minimal test sink, in the plugin the
// processor's VoiceManager) only picks the slot.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/InstanceExpansion.h"
#include "bazalt/engine/graph/MultiplicityResolver.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/LifePopulationNode.h"
#include <cmath>
#include <vector>

using namespace bazalt::engine;

namespace
{
    constexpr double sampleRate = 48000.0;

    /** First free slot; nothing is ever freed (the tests are short). */
    struct TestSink : InstanceBirthSink
    {
        ExecutionPlan* plan = nullptr;
        std::vector<int> bornAt; // absolute sample of each birth
        int blockStart = 0;

        int born (InstanceGroupRuntime& group, int offset, float pitch, float velocity, float duration, int parent) noexcept override
        {
            const auto first = parent >= 0 ? parent * group.membersPerParent : 0;
            const auto last = parent >= 0 ? first + group.membersPerParent : group.count;
            for (int i = first; i < last; ++i)
            {
                auto& state = group.instances[(size_t) i];
                if (state.run)
                    continue;
                plan->resetInstance (group.groupIndex, i);
                state.run = state.audible = true;
                dynamic_cast<nodes::LifePopulationNode*> (group.originNodes[(size_t) i])
                    ->spawnAt (offset, pitch, velocity, duration, group.births++);
                bornAt.push_back (blockStart + offset);
                return i;
            }
            return -1;
        }
        void release (InstanceGroupRuntime& group, int instance, int offset) noexcept override
        {
            dynamic_cast<nodes::LifePopulationNode*> (group.originNodes[(size_t) instance])->releaseAt (offset);
        }
    };

    /** clock → Population (born) → gate into a Merge → out. */
    NodeGraph clockedPopulation (float rateHz, int lives, float durationSeconds)
    {
        NodeGraph graph;
        graph.addNode ({ "clock", "time.clock", {}, { { "time.clock.rate", rateHz } }, {} });
        graph.addNode ({ "pop", "life.population", {}, { { "life.population.size", 16.0f }, { "life.population.lives", (float) lives },
                                                          { "life.population.duration", durationSeconds } }, {} });
        graph.addNode ({ "merge", "life.merge", {}, {}, {} });
        graph.addNode ({ "out", "io.output", {}, {}, {} });
        graph.addConnection ({ "clock", "tick", "pop", "born" });
        graph.addConnection ({ "pop", "gate", "merge", "in" });
        graph.addConnection ({ "merge", "out", "out", "in" });
        graph.setOutput ("out", "out");
        return graph;
    }

    struct Rendered
    {
        std::vector<float> out;
        std::vector<int> bornAt;
        std::vector<int> orders;     // each member's order, by slot
        std::vector<float> randoms;  // each member's random 1, by slot
    };

    Rendered render (const NodeGraph& graph, int blockSize, int totalSamples)
    {
        const auto factory = buildDefaultNodeFactory();
        const auto split = MultiplicityResolver::split (graph);
        REQUIRE (split.success);
        const auto layout = computeInstanceLayout (split, [] (const juce::String&) { return 16; });
        auto result = compileWithInstances (graph, layout, factory, { sampleRate, blockSize }, 1);
        INFO (result.errorMessage);
        REQUIRE (result.success);
        auto& plan = result.plan;
        REQUIRE (plan.instanceGroups.size() == 1);
        REQUIRE (plan.instanceGroups[0]->birthNode != nullptr);

        TestSink sink;
        sink.plan = &plan;
        plan.instanceGroups[0]->birthSink = &sink;

        Rendered rendered;
        for (int done = 0; done < totalSamples; done += blockSize)
        {
            const auto n = std::min (blockSize, totalSamples - done);
            sink.blockStart = done;
            plan.process (n);
            const auto* out = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
            rendered.out.insert (rendered.out.end(), out, out + n);
        }
        rendered.bornAt = sink.bornAt;
        for (auto* node : plan.instanceGroups[0]->originNodes)
        {
            auto* member = dynamic_cast<nodes::LifePopulationNode*> (node);
            rendered.orders.push_back (member->getOrder());
            rendered.randoms.push_back (member->getRandom1());
        }
        return rendered;
    }
}

TEST_CASE ("Population: a member is born at the sample of its event, lives its Duration, whatever the block size",
           "[engine][population][2b]")
{
    // 1 kHz clock: a birth every 48 samples; each member's gate is up 24 samples.
    const auto graph = clockedPopulation (1000.0f, 1, 0.0005f);
    const auto a = render (graph, 64, 4800);
    const auto b = render (graph, 37, 4800);

    REQUIRE (a.bornAt.size() >= 10);
    CHECK (a.bornAt == b.bornAt);
    CHECK (a.out == b.out); // block-size invariant (CLAUDE.md rule 6)

    for (size_t k = 0; k < 10 && k < a.bornAt.size(); ++k)
    {
        const auto t = a.bornAt[k];
        if (k > 0)
            CHECK (std::abs (t - a.bornAt[k - 1] - 48) <= 1); // the clock's float phase
        CHECK (a.out[(size_t) t] == 1.0f);        // up at its birth sample
        CHECK (a.out[(size_t) t + 23] == 1.0f);   // for its Duration
        CHECK (a.out[(size_t) t + 24] == 0.0f);   // then down
        if (t > 0)
            CHECK (a.out[(size_t) t - 1] == 0.0f); // not a sample early
    }
}

TEST_CASE ("Population: a Release event ends every member held, and Always fills every slot",
           "[engine][population][2b]")
{
    // Held members (Lives = While held) pile up: each birth adds one gate.
    const auto held = render (clockedPopulation (1000.0f, 0, 0.1f), 64, 48 * 4 + 1);
    CHECK (held.out.back() == 5.0f);

    NodeGraph graph;
    graph.addNode ({ "pop", "life.population", {}, { { "life.population.size", 16.0f }, { "life.population.born", 1.0f } }, {} });
    graph.addNode ({ "merge", "life.merge", {}, {}, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "pop", "gate", "merge", "in" });
    graph.addConnection ({ "merge", "out", "out", "in" });
    graph.setOutput ("out", "out");
    const auto always = render (graph, 64, 128);
    CHECK (always.bornAt.size() == 16);
    CHECK (always.out.front() == 16.0f);
    CHECK (always.out.back() == 16.0f);
}

TEST_CASE ("Population: each member knows its order, and its randoms follow from it", "[engine][population][2b]")
{
    const auto rendered = render (clockedPopulation (1000.0f, 0, 0.1f), 64, 48 * 3 + 1);
    REQUIRE (rendered.bornAt.size() == 4);
    for (int i = 0; i < 4; ++i)
        CHECK (rendered.orders[(size_t) i] == i);
    CHECK (rendered.randoms[0] != rendered.randoms[1]);

    // The same patch replays the same randoms (Seed).
    CHECK (render (clockedPopulation (1000.0f, 0, 0.1f), 37, 48 * 3 + 1).randoms == rendered.randoms);
}

namespace
{
    /** Held members born by a clock; Split by Order into two branches, branch 0
        counted ×1 and branch 1 ×10, each summed by its own Merge. */
    NodeGraph splitPatch (float rule, float at1, float decide)
    {
        auto graph = clockedPopulation (1000.0f, 0, 0.1f);
        graph.removeConnection ("pop", "gate", "merge", "in");
        graph.removeNode ("merge");
        graph.addNode ({ "split", "life.split", {}, { { "life.split.rule", rule }, { "life.split.at1", at1 },
                                                      { "life.split.decide", decide } }, {} });
        graph.addNode ({ "one", "math.multiply", {}, { { "in.1", 1.0f } }, {} });
        graph.addNode ({ "ten", "math.multiply", {}, { { "in.1", 10.0f } }, {} });
        graph.addNode ({ "merge0", "life.merge", {}, {}, {} });
        graph.addNode ({ "merge1", "life.merge", {}, {}, {} });
        graph.addNode ({ "sum", "math.add", {}, {}, {} });
        graph.addConnection ({ "pop", "gate", "split", "in" });
        graph.addConnection ({ "pop", "order", "split", "key" });
        graph.addConnection ({ "split", "branch.0", "one", "in.0" });
        graph.addConnection ({ "split", "branch.1", "ten", "in.0" });
        graph.addConnection ({ "one", "out", "merge0", "in" });
        graph.addConnection ({ "ten", "out", "merge1", "in" });
        graph.addConnection ({ "merge0", "out", "sum", "in.0" });
        graph.addConnection ({ "merge1", "out", "sum", "in.1" });
        graph.addConnection ({ "sum", "out", "out", "in" });
        return graph;
    }
}

TEST_CASE ("Split sorts members into branches by Ranges, Modulo and Rank, and a branch runs only for its own",
           "[engine][population][split][2b]")
{
    constexpr int afterFourBirths = 48 * 3 + 1;
    // Modulo 2 on Order: 0 and 2 in branch 0, 1 and 3 in branch 1 → 2 + 20.
    CHECK (render (splitPatch (1.0f, 0.0f, 0.0f), 64, afterFourBirths).out.back() == 22.0f);
    // Ranges at 0.5: order 0 below, 1-3 above → 1 + 30.
    CHECK (render (splitPatch (0.0f, 0.5f, 0.0f), 64, afterFourBirths).out.back() == 31.0f);
    // Rank, re-decided always: only the newest (highest order) leads → 1 + 30.
    CHECK (render (splitPatch (2.0f, 0.0f, 1.0f), 64, afterFourBirths).out.back() == 31.0f);
    // Rank decided at birth: each newborn was the highest when it was born → 4 × 1.
    CHECK (render (splitPatch (2.0f, 0.0f, 0.0f), 64, afterFourBirths).out.back() == 4.0f);
    // Block-size invariant.
    CHECK (render (splitPatch (1.0f, 0.0f, 0.0f), 64, 1000).out == render (splitPatch (1.0f, 0.0f, 0.0f), 37, 1000).out);
}

TEST_CASE ("A branch's steps are skipped for members outside it", "[engine][population][split][2b]")
{
    const auto graph = splitPatch (1.0f, 0.0f, 0.0f);
    const auto factory = buildDefaultNodeFactory();
    const auto split = MultiplicityResolver::split (graph);
    REQUIRE (split.success);
    const auto layout = computeInstanceLayout (split, [] (const juce::String&) { return 16; });
    REQUIRE (layout.branchesOfNode.count ("one") == 1);
    REQUIRE (layout.branchesOfNode.at ("one").front() == std::pair<juce::String, int> { "split", 0 });
    REQUIRE (layout.branchesOfNode.at ("ten").front() == std::pair<juce::String, int> { "split", 1 });
    CHECK (layout.branchesOfNode.count ("split") == 0);

    auto result = compileWithInstances (graph, layout, factory, { sampleRate, 64 }, 1);
    REQUIRE (result.success);
    int gated = 0;
    for (const auto& step : result.plan.steps)
        gated += step.kind == ExecutionPlan::Step::Kind::Block && step.block.numGates > 0;
    CHECK (gated == 32); // one and ten, 16 copies each
}

namespace
{
    /** noteIn → Voice; a unison Population (born Always, 3 members, no inputs)
        inside each voice detunes it; Merge per voice, then Merge of voices. */
    NodeGraph unisonPatch()
    {
        NodeGraph g;
        g.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
        g.addNode ({ "voice", "life.population", {}, { { "life.population.size", 4.0f } }, {} });
        g.addNode ({ "uni", "life.population", {}, { { "life.population.born", 1.0f }, { "life.population.size", 3.0f } }, {} });
        g.addNode ({ "detune", "math.add", {}, {}, {} });
        g.addNode ({ "p2f", "math.pitchToFrequency", {}, {}, {} });
        g.addNode ({ "osc", "source.saw", {}, {}, {} });
        g.addNode ({ "amp", "math.multiply", {}, {}, {} });
        g.addNode ({ "perVoice", "life.merge", {}, {}, {} });
        g.addNode ({ "voices", "life.merge", {}, {}, {} });
        g.addNode ({ "out", "io.output", {}, {}, {} });
        g.addConnection ({ "noteIn", "notes", "voice", "notes" });
        g.addConnection ({ "voice", "pitch", "detune", "in.0" });
        g.addConnection ({ "uni", "random1", "detune", "in.1" });
        g.addConnection ({ "detune", "out", "p2f", "pitch" });
        g.addConnection ({ "p2f", "frequency", "osc", "source.saw.frequency" });
        g.addConnection ({ "osc", "out", "amp", "in.0" });
        g.addConnection ({ "voice", "gate", "amp", "in.1" });
        g.addConnection ({ "amp", "out", "perVoice", "in" });
        g.addConnection ({ "perVoice", "out", "voices", "in" });
        g.addConnection ({ "voices", "out", "out", "in" });
        g.setOutput ("out", "out");
        return g;
    }
}

TEST_CASE ("A Population with no inputs that meets another one's members lives inside it (unison)",
           "[engine][population][nested][2b]")
{
    const auto graph = unisonPatch();
    const auto split = MultiplicityResolver::split (graph);
    INFO (split.errorMessage);
    REQUIRE (split.success);
    REQUIRE (split.origins.size() == 2);
    const auto& uni = split.origins[0].originId == "uni" ? split.origins[0] : split.origins[1];
    CHECK (uni.parentOriginId == "voice");

    const auto layout = computeInstanceLayout (split, [] (const juce::String& id) { return id == "uni" ? 3 : 4; });
    REQUIRE (layout.errorMessage.isEmpty());
    const auto uniGroup = layout.groupOf ("uni");
    const auto voiceGroup = layout.groupOf ("voice");
    REQUIRE (uniGroup >= 0);
    CHECK (layout.groups[(size_t) uniGroup].parent == voiceGroup);
    CHECK (layout.groups[(size_t) uniGroup].count == 12); // 3 per voice × 4 voices
    CHECK (layout.groupOf ("osc") == uniGroup);
    CHECK (layout.groupOf ("perVoice") == voiceGroup); // one Merge per voice
    CHECK (layout.groupOf ("voices") == -1);

    const auto expanded = expandInstances (graph, layout, 1);
    INFO (expanded.errorMessage);
    REQUIRE (expanded.success);
    CHECK (expanded.graph.findNode ("uni@birth@3") != nullptr); // a birth copy per voice
    const auto* reduce = expanded.graph.findNode ("perVoice@2");
    REQUIRE (reduce != nullptr);
    CHECK (reduce->type == "life.reduce");
    CHECK (reduce->parameters.at ("life.reduce.first") == 6.0f); // voice 2's slice: members 6-8
    // Voice 1's pitch reaches its own three members only.
    int fromVoice1 = 0;
    for (const auto& c : expanded.graph.getConnections())
        if (c.fromNodeId == "voice@1" && c.fromPortId == "pitch")
        {
            ++fromVoice1;
            CHECK ((c.toNodeId == "detune@3" || c.toNodeId == "detune@4" || c.toNodeId == "detune@5"));
        }
    CHECK (fromVoice1 == 3);

    const auto factory = buildDefaultNodeFactory();
    auto compiled = compileWithInstances (graph, layout, factory, { sampleRate, 64 }, 1);
    INFO (compiled.errorMessage);
    CHECK (compiled.success);
}

TEST_CASE ("Too many nested members is an error, not a silent cap", "[engine][population][nested][2b]")
{
    const auto split = MultiplicityResolver::split (unisonPatch());
    REQUIRE (split.success);
    const auto layout = computeInstanceLayout (split, [] (const juce::String& id) { return id == "uni" ? 16 : 16; });
    CHECK (layout.errorMessage.contains ("256"));
}

#include "bazalt/engine/nodes/InstanceSeeding.h"

TEST_CASE ("Population: Random 1, Random 2 and Position each spread across members", "[engine][population][R0.4]")
{
    // wiki/plans/Release.md R0.4 "Random 1 doesn't work": each member draws
    // its three values from juce::Random seeded by (Seed, order). The old seed
    // mix moved only the low bits between neighbouring orders and JUCE's first
    // draw barely depends on them: Random 1 was nearly the same for everyone.
    for (const int seed : { 0, 1, 7, 12345 })
    {
        std::array<std::vector<float>, 3> draws;
        for (int order = 0; order < 64; ++order)
        {
            auto random = juce::Random (nodes::combineInstanceSeed (seed, order));
            for (auto& d : draws)
                d.push_back (random.nextFloat() * 2.0f - 1.0f);
        }
        for (const auto& d : draws)
        {
            const auto [lo, hi] = std::minmax_element (d.begin(), d.end());
            CHECK (*hi - *lo > 1.6f);
            double mean = 0.0, neighbour = 0.0;
            for (size_t i = 0; i < d.size(); ++i)
            {
                mean += d[i];
                if (i > 0)
                    neighbour += std::abs (d[i] - d[i - 1]);
            }
            CHECK (std::abs (mean / (double) d.size()) < 0.3);
            CHECK (neighbour / (double) (d.size() - 1) > 0.4); // uniform: ~0.67
        }
    }
}
