// time.delay carries signals and Events: a trigger once more, later.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/DelayNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int block = 256;

    /** Index of the first non-zero output sample of `graph`. */
    int firstSound (const NodeGraph& graph)
    {
        const auto factory = buildDefaultNodeFactory();
        auto result = GraphCompiler::compile (graph, factory, { sampleRate, block }, 1);
        INFO (result.errorMessage);
        REQUIRE (result.success);
        auto& plan = result.plan;
        plan.reset();
        for (int b = 0; b < 40; ++b)
        {
            plan.process (block);
            const auto* out = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
            for (int i = 0; i < block; ++i)
                if (out[i] != 0.0f)
                    return b * block + i;
        }
        return -1;
    }

    NodeGraph clockIntoImpulse (float delaySamples)
    {
        NodeGraph graph;
        graph.addNode ({ "clock", "time.clock", {}, { { "time.clock.rate", 2.0f } }, {} });
        graph.addNode ({ "impulse", "excite.impulse", {}, {}, {} });
        graph.addNode ({ "out", "io.output", {}, {}, {} });
        if (delaySamples > 0.0f)
        {
            graph.addNode ({ "later", "time.delay", {}, { { "time.delay.samples", delaySamples } }, {} });
            graph.addConnection ({ "clock", "tick", "later", "in" });
            graph.addConnection ({ "later", "out", "impulse", "trigger" });
        }
        else
        {
            graph.addConnection ({ "clock", "tick", "impulse", "trigger" });
        }
        graph.addConnection ({ "impulse", "out", "out", "in" });
        graph.setOutput ("out", "out");
        return graph;
    }
}

TEST_CASE ("A Delay moves an Event by exactly its length", "[engine][delay]")
{
    const auto direct = firstSound (clockIntoImpulse (0.0f));
    REQUIRE (direct >= 0);
    CHECK (firstSound (clockIntoImpulse (1440.0f)) == direct + 1440);
    CHECK (firstSound (clockIntoImpulse (1440.4f)) == direct + 1440); // a moment is never smeared
}

TEST_CASE ("A Delay reads a signal between samples", "[engine][delay]")
{
    nodes::DelayNode delay;
    delay.prepare ({ sampleRate, 1 });
    delay.setParameter ("time.delay.samples", 1.5f);
    const float unwired = std::numeric_limits<float>::quiet_NaN();
    float out = 0.0f;
    std::vector<float> got;
    for (float x : { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f })
    {
        const float in[2] = { x, unwired };
        delay.processSample (in, &out);
        got.push_back (out);
    }
    // The 1 arrives half at 1 sample and half at 2 samples later.
    CHECK (got[2] == Catch::Approx (0.5f));
    CHECK (got[3] == Catch::Approx (0.5f));
    CHECK (got[4] == Catch::Approx (0.0f));
}
