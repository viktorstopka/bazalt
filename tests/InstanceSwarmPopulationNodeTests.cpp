#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/InstanceSwarmPopulationNode.h"
#include <algorithm>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    struct SwarmOutputs
    {
        bool gate;
        float instanceIndex;
        float random1;
        float random2;
        float position;
        bool start;
    };

    SwarmOutputs sampleOnce (InstanceSwarmPopulationNode& node)
    {
        float out[8] {};
        node.processSample (nullptr, out);
        return { out[0] >= 0.5f, out[1], out[3], out[4], out[7], out[5] >= 0.5f };
    }
}

TEST_CASE ("InstanceSwarmPopulationNode: slots below populationSize are Gate=true, "
           "slots at or above it are Gate=false",
           "[engine][nodes][InstanceSwarmPopulationNode]")
{
    InstanceSwarmPopulationNode node;
    node.prepare ({ 44100.0, 64 });
    node.setParameter ("instance.allocate.swarmPopulation.populationSize", 4.0f);

    for (int slot = 0; slot < 8; ++slot)
    {
        node.reset();
        node.setInstanceSlot (slot);
        const auto out = sampleOnce (node);
        CHECK (out.gate == (slot < 4));
        CHECK (out.instanceIndex == (float) slot);
    }
}

TEST_CASE ("InstanceSwarmPopulationNode: fires start exactly once, right after reset()",
           "[engine][nodes][InstanceSwarmPopulationNode]")
{
    InstanceSwarmPopulationNode node;
    node.prepare ({ 44100.0, 64 });
    node.setInstanceSlot (0);
    node.reset();

    const auto first = sampleOnce (node);
    const auto second = sampleOnce (node);

    CHECK (first.start);
    CHECK_FALSE (second.start);
}

TEST_CASE ("InstanceSwarmPopulationNode: random1/random2/position are deterministic per (seed, slot), "
           "and differ across different slots",
           "[engine][nodes][InstanceSwarmPopulationNode]")
{
    InstanceSwarmPopulationNode a;
    a.prepare ({ 44100.0, 64 });
    a.setParameter ("instance.allocate.swarmPopulation.seed", 42.0f);
    a.setInstanceSlot (3);
    a.reset();
    const auto outA = sampleOnce (a);

    // Same seed, same slot, a totally separate node instance - must be
    // bit-identical (DOMAINS.md §4's own determinism requirement).
    InstanceSwarmPopulationNode b;
    b.prepare ({ 44100.0, 64 });
    b.setParameter ("instance.allocate.swarmPopulation.seed", 42.0f);
    b.setInstanceSlot (3);
    b.reset();
    const auto outB = sampleOnce (b);

    CHECK (outA.random1 == outB.random1);
    CHECK (outA.random2 == outB.random2);
    CHECK (outA.position == outB.position);

    // A DIFFERENT slot, same seed, must differ (rules out a mutation that
    // just returns a fixed constant regardless of slot).
    InstanceSwarmPopulationNode c;
    c.prepare ({ 44100.0, 64 });
    c.setParameter ("instance.allocate.swarmPopulation.seed", 42.0f);
    c.setInstanceSlot (4);
    c.reset();
    const auto outC = sampleOnce (c);

    CHECK ((outA.random1 != outC.random1 || outA.random2 != outC.random2 || outA.position != outC.position));
}

TEST_CASE ("InstanceSwarmPopulationNode declares the real populationSize/seed structural parameters, "
           "and no input ports at all",
           "[engine][nodes][InstanceSwarmPopulationNode]")
{
    InstanceSwarmPopulationNode node;
    CHECK (node.getNumInputPorts() == 0); // "fixed count, always live" - no spawn source

    const auto parameters = node.getParameters();
    auto hasParam = [&] (const juce::String& id)
    {
        return std::find_if (parameters.begin(), parameters.end(), [&] (const ParameterDescriptor& p) { return p.id == id; })
               != parameters.end();
    };
    CHECK (hasParam ("instance.allocate.swarmPopulation.populationSize"));
    CHECK (hasParam ("instance.allocate.swarmPopulation.seed"));

    const auto outputs = node.getOutputPorts();
    REQUIRE (outputs.size() == 8);
    CHECK (outputs[0].id == "gate");
    CHECK (outputs[7].id == "position"); // the one output Voice doesn't have
}
