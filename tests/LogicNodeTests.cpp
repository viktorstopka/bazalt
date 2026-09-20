#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/LogicNotNode.h"
#include "bazalt/engine/nodes/LogicToggleNode.h"
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

TEST_CASE ("LogicNotNode inverts a Boolean at the 0.5 threshold", "[engine][nodes][logic][M21]")
{
    LogicNotNode node;
    float out = -1.0f;

    auto notOf = [&] (float in)
    {
        node.processSample (&in, &out);
        return out;
    };

    CHECK (notOf (0.0f) == 1.0f); // an unconnected input reads 0, so its NOT is true
    CHECK (notOf (1.0f) == 0.0f);
    CHECK (notOf (0.4f) == 1.0f);
    CHECK (notOf (0.6f) == 0.0f);
}

TEST_CASE ("LogicToggleNode flips on each trigger event and holds between them", "[engine][nodes][logic][M21]")
{
    LogicToggleNode node;
    float out = -1.0f;

    auto step = [&] (float trigger, float reset)
    {
        const float inputs[2] = { trigger, reset };
        node.processSample (inputs, &out);
        return out;
    };

    CHECK (step (0.0f, 0.0f) == 0.0f); // starts false
    CHECK (step (1.0f, 0.0f) == 1.0f); // flip on
    CHECK (step (0.0f, 0.0f) == 1.0f); // held
    CHECK (step (0.0f, 0.0f) == 1.0f);
    CHECK (step (1.0f, 0.0f) == 0.0f); // flip off
    CHECK (step (1.0f, 0.0f) == 1.0f); // and on again
}

TEST_CASE ("LogicToggleNode's reset forces false and wins over a coincident trigger", "[engine][nodes][logic][M21]")
{
    LogicToggleNode node;
    float out = -1.0f;

    auto step = [&] (float trigger, float reset)
    {
        const float inputs[2] = { trigger, reset };
        node.processSample (inputs, &out);
        return out;
    };

    step (1.0f, 0.0f);
    REQUIRE (out == 1.0f);

    CHECK (step (0.0f, 1.0f) == 0.0f); // reset -> false
    CHECK (step (0.0f, 1.0f) == 0.0f); // reset while already false stays false

    CHECK (step (1.0f, 1.0f) == 0.0f); // both on the same sample: reset wins, not a flip to true
    CHECK (step (1.0f, 0.0f) == 1.0f); // and a lone trigger works again afterwards
}

TEST_CASE ("LogicToggleNode ignores NaN as an event, and reset() returns it to false", "[engine][nodes][logic][M21]")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();

    LogicToggleNode node;
    float out = -1.0f;

    float inputs[2] = { nan, nan };
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);

    inputs[0] = 1.0f;
    inputs[1] = 0.0f;
    node.processSample (inputs, &out);
    REQUIRE (out == 1.0f);

    node.reset();
    inputs[0] = 0.0f;
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);
}

TEST_CASE ("threshold -> logic.toggle -> env.adsr gate compiles and toggles, so Event and Boolean ports interconnect",
           "[engine][nodes][logic][M21][GraphCompiler]")
{
    // First real consumer of a Boolean OUTPUT wired into another node's
    // Boolean input, and of an Event feeding a non-Threshold node: proves
    // canConnect accepts Event->Event and Boolean->Boolean for these ports
    // and that the chain actually schedules and runs.
    const auto factory = buildDefaultNodeFactory();

    NodeGraph graph;
    graph.addNode ({ "level", "util.constant", {}, { { "util.constant.value", 0.6f } }, {} });
    graph.addNode ({ "edge", "adapt.threshold", {}, {}, {} });
    graph.addNode ({ "flip", "logic.toggle", {}, {}, {} });
    graph.addNode ({ "env", "env.adsr", {}, {}, {} });
    graph.addConnection ({ "level", "out", "edge", "by" });
    graph.addConnection ({ "edge", "onThreshold", "flip", "trigger" });
    graph.addConnection ({ "flip", "out", "env", "gate" });
    graph.setOutput ("flip", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    result.plan.process (8);
    const auto* output = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);

    // 0.6 crosses the default 0.5 threshold on the very first sample (one
    // rising edge, then it stays above), so the toggle flips on once and holds.
    for (int i = 0; i < 8; ++i)
        CHECK (output[i] == 1.0f);
}
