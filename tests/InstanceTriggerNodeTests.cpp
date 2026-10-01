#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/InstanceTriggerNode.h"
#include <algorithm>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    struct TriggerOutputs
    {
        bool gate;
        float instanceIndex;
        float age;
        float random1;
        float random2;
        bool start;
        bool stop;
    };

    TriggerOutputs sampleOnce (InstanceTriggerNode& node, float triggerInput = 0.0f)
    {
        const float in[1] = { triggerInput };
        float out[7] {};
        node.processSample (in, out);
        return { out[0] >= 0.5f, out[1], out[2], out[3], out[4], out[5] >= 0.5f, out[6] >= 0.5f };
    }
}

TEST_CASE ("InstanceTriggerNode: a fired trigger sets Gate=true and fires start exactly once",
           "[engine][nodes][InstanceTriggerNode]")
{
    InstanceTriggerNode node;
    node.prepare ({ 44100.0, 64 });
    node.reset();

    const auto idle = sampleOnce (node, 0.0f);
    CHECK_FALSE (idle.gate);
    CHECK_FALSE (idle.start);

    const auto fired = sampleOnce (node, 1.0f);
    CHECK (fired.gate);
    CHECK (fired.start);

    const auto next = sampleOnce (node, 0.0f);
    CHECK (next.gate); // stays asserted - no auto-release, unlike Swarm-transient
    CHECK_FALSE (next.start);
}

TEST_CASE ("InstanceTriggerNode: a second trigger re-triggers the same instance - fresh age/random, "
           "gate never drops in between",
           "[engine][nodes][InstanceTriggerNode]")
{
    InstanceTriggerNode node;
    node.prepare ({ 44100.0, 64 });
    node.reset();

    const auto first = sampleOnce (node, 1.0f);
    CHECK (first.gate);
    CHECK (first.instanceIndex == 1.0f);

    // Let some age accumulate before retriggering.
    for (int i = 0; i < 100; ++i)
        sampleOnce (node, 0.0f);

    const auto second = sampleOnce (node, 1.0f);
    CHECK (second.gate); // still gated - never dropped to false at any point
    CHECK (second.start); // re-fires start on the SAME instance
    CHECK (second.instanceIndex == 2.0f); // a fresh spawn ordinal, not a no-op
    CHECK (second.age == 0.0f); // age genuinely reset on the retrigger sample, not left accumulating
    CHECK ((second.random1 != first.random1 || second.random2 != first.random2)); // a fresh draw
}

TEST_CASE ("InstanceTriggerNode: random1/random2 are deterministic per (seed, spawn ordinal)",
           "[engine][nodes][InstanceTriggerNode]")
{
    InstanceTriggerNode a;
    a.prepare ({ 44100.0, 64 });
    a.setParameter ("instance.allocate.trigger.seed", 7.0f);
    a.reset();
    const auto spawnA = sampleOnce (a, 1.0f);

    InstanceTriggerNode b;
    b.prepare ({ 44100.0, 64 });
    b.setParameter ("instance.allocate.trigger.seed", 7.0f);
    b.reset();
    const auto spawnB = sampleOnce (b, 1.0f);

    CHECK (spawnA.random1 == spawnB.random1);
    CHECK (spawnA.random2 == spawnB.random2);
}

TEST_CASE ("InstanceTriggerNode declares only the real seed parameter (maxInstances implicitly 1, "
           "never exposed), one trigger input, and the 7-port shape with no position output",
           "[engine][nodes][InstanceTriggerNode]")
{
    InstanceTriggerNode node;
    CHECK (node.getNumInputPorts() == 1);

    const auto inputs = node.getInputPorts();
    REQUIRE (inputs.size() == 1);
    CHECK (inputs[0].id == "trigger");
    CHECK (inputs[0].type == SignalType::Event);

    const auto parameters = node.getParameters();
    REQUIRE (parameters.size() == 1);
    CHECK (parameters[0].id == "instance.allocate.trigger.seed");

    const auto outputs = node.getOutputPorts();
    REQUIRE (outputs.size() == 7); // no "position" - not a swarm type
    CHECK (outputs[0].id == "gate");
    CHECK (outputs[6].id == "stop");

    CHECK (node.getMaxInstances() == 1); // InstanceOriginNode's own default, never overridden
}
