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

        int born (InstanceGroupRuntime& group, int offset, float pitch, float velocity, float duration) noexcept override
        {
            for (int i = 0; i < group.count; ++i)
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
