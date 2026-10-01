#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/InstanceSwarmTransientNode.h"
#include <algorithm>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    struct TransientOutputs
    {
        bool gate;
        float instanceIndex;
        float age;
        float random1;
        float random2;
        float position;
        bool start;
        bool stop;
    };

    TransientOutputs sampleOnce (InstanceSwarmTransientNode& node, float spawnInput = 0.0f)
    {
        const float in[1] = { spawnInput };
        float out[8] {};
        node.processSample (in, out);
        return { out[0] >= 0.5f, out[1], out[2], out[3], out[4], out[7], out[5] >= 0.5f, out[6] >= 0.5f };
    }
}

TEST_CASE ("InstanceSwarmTransientNode: a fired spawn Event sets Gate=true and fires start exactly once",
           "[engine][nodes][InstanceSwarmTransientNode]")
{
    InstanceSwarmTransientNode node;
    node.prepare ({ 44100.0, 64 });
    node.reset();

    const auto idle = sampleOnce (node, 0.0f);
    CHECK_FALSE (idle.gate);
    CHECK_FALSE (idle.start);

    const auto fired = sampleOnce (node, 1.0f);
    CHECK (fired.gate);
    CHECK (fired.start);

    const auto next = sampleOnce (node, 0.0f);
    CHECK (next.gate); // still live - duration hasn't elapsed yet
    CHECK_FALSE (next.start); // only fires the one sample it actually spawned
}

TEST_CASE ("InstanceSwarmTransientNode: auto-releases itself once duration elapses - no separate stop input needed",
           "[engine][nodes][InstanceSwarmTransientNode]")
{
    InstanceSwarmTransientNode node;
    node.prepare ({ 44100.0, 64 });
    node.setParameter ("instance.allocate.swarmTransient.duration", 0.001f); // 1ms -> ~44 samples at 44.1kHz
    node.reset();

    sampleOnce (node, 1.0f); // spawn

    bool sawStop = false;
    bool gateWasFalseAfterStop = false;
    for (int i = 0; i < 1000 && ! sawStop; ++i)
    {
        const auto out = sampleOnce (node, 0.0f);
        if (out.stop)
        {
            sawStop = true;
            gateWasFalseAfterStop = ! out.gate;
        }
    }

    CHECK (sawStop); // the transient actually ended on its own
    CHECK (gateWasFalseAfterStop);
}

TEST_CASE ("InstanceSwarmTransientNode: random1/random2/position are deterministic per (seed, spawn ordinal), "
           "and differ across successive spawns",
           "[engine][nodes][InstanceSwarmTransientNode]")
{
    InstanceSwarmTransientNode a;
    a.prepare ({ 44100.0, 64 });
    a.setParameter ("instance.allocate.swarmTransient.seed", 42.0f);
    a.reset();
    const auto spawnA = sampleOnce (a, 1.0f);

    // Same seed, a totally separate node instance, first spawn - must be
    // bit-identical (DOMAINS.md §4's own determinism requirement, reused
    // verbatim from 09-28-InstanceAllocator.2/Batch 2).
    InstanceSwarmTransientNode b;
    b.prepare ({ 44100.0, 64 });
    b.setParameter ("instance.allocate.swarmTransient.seed", 42.0f);
    b.reset();
    const auto spawnB = sampleOnce (b, 1.0f);

    CHECK (spawnA.random1 == spawnB.random1);
    CHECK (spawnA.random2 == spawnB.random2);
    CHECK (spawnA.position == spawnB.position);

    // A SECOND spawn on the same node (different spawn ordinal) must differ
    // (rules out a mutation that just returns a fixed constant).
    const auto spawnA2 = sampleOnce (a, 1.0f);
    CHECK ((spawnA.random1 != spawnA2.random1 || spawnA.random2 != spawnA2.random2
            || spawnA.position != spawnA2.position));
}

TEST_CASE ("InstanceSwarmTransientNode declares the real maxInstances/seed/duration parameters, "
           "one spawn input, and the shared 8-port shape",
           "[engine][nodes][InstanceSwarmTransientNode]")
{
    InstanceSwarmTransientNode node;
    CHECK (node.getNumInputPorts() == 1);

    const auto inputs = node.getInputPorts();
    REQUIRE (inputs.size() == 1);
    CHECK (inputs[0].id == "spawn");
    CHECK (inputs[0].type == SignalType::Event);

    const auto parameters = node.getParameters();
    auto hasParam = [&] (const juce::String& id)
    {
        return std::find_if (parameters.begin(), parameters.end(), [&] (const ParameterDescriptor& p) { return p.id == id; })
               != parameters.end();
    };
    CHECK (hasParam ("instance.allocate.swarmTransient.maxInstances"));
    CHECK (hasParam ("instance.allocate.swarmTransient.seed"));
    CHECK (hasParam ("instance.allocate.swarmTransient.duration"));

    const auto outputs = node.getOutputPorts();
    REQUIRE (outputs.size() == 8);
    CHECK (outputs[0].id == "gate");
    CHECK (outputs[7].id == "position"); // the one output Voice doesn't have

    CHECK (node.getMaxInstances() == 8); // default
}
