#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/LogicGateNodes.h"
#include "bazalt/engine/nodes/LogicEventNodes.h"
#include "bazalt/engine/nodes/LogicNotNode.h"
#include "bazalt/engine/nodes/LogicToggleNode.h"
#include <limits>
#include <vector>

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

TEST_CASE ("LogicToggleNode's initialState parameter starts true and reset()/reset-port both return to it",
           "[engine][nodes][logic][props-edit]")
{
    LogicToggleNode node;
    node.setParameter ("logic.toggle.initialState", 1.0f);

    float out = -1.0f;
    auto step = [&] (float trigger, float reset)
    {
        const float inputs[2] = { trigger, reset };
        node.processSample (inputs, &out);
        return out;
    };

    CHECK (step (0.0f, 0.0f) == 1.0f); // starts true, not the old hardcoded false
    CHECK (step (1.0f, 0.0f) == 0.0f); // flips off
    CHECK (step (0.0f, 1.0f) == 1.0f); // reset -> initialState (true), not unconditionally false

    node.reset();
    CHECK (step (0.0f, 0.0f) == 1.0f); // voice-restart reset() also returns to initialState

    // Re-arming initialState to false still works (not a one-way latch).
    node.setParameter ("logic.toggle.initialState", 0.0f);
    node.reset();
    CHECK (step (0.0f, 0.0f) == 0.0f);
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

TEST_CASE ("And / Or / Xor apply over only their WIRED inputs; Invert gives Nand / Nor / Xnor", "[engine][nodes][logic]")
{
    const auto unwired = std::numeric_limits<float>::quiet_NaN(); // GraphCompiler's sentinel for an unwired fallback port

    auto resultOf = [&] (LogicGateOp op, bool invert, std::vector<float> inputs)
    {
        LogicGateNode node (op);
        node.setGroupPortCount ((int) inputs.size());
        node.setParameter (node.typeId() + ".invert", invert ? 1.0f : 0.0f);
        float out = -1.0f;
        node.processSample (inputs.data(), &out);
        return out;
    };
    using enum LogicGateOp;

    // AND: every wired input true. The spare port must not force false.
    CHECK (resultOf (And, false, { 1.0f, 1.0f }) == 1.0f);
    CHECK (resultOf (And, false, { 1.0f, 0.0f }) == 0.0f);
    CHECK (resultOf (And, false, { 1.0f, unwired }) == 1.0f);
    CHECK (resultOf (And, false, { 1.0f, 1.0f, unwired }) == 1.0f);

    CHECK (resultOf (Or, false, { 0.0f, 1.0f }) == 1.0f);
    CHECK (resultOf (Or, false, { 0.0f, 0.0f }) == 0.0f);
    CHECK (resultOf (Or, false, { 0.0f, unwired }) == 0.0f);

    // XOR is parity across however many are wired.
    CHECK (resultOf (Xor, false, { 1.0f, 1.0f, 1.0f }) == 1.0f);
    CHECK (resultOf (Xor, false, { 1.0f, 1.0f, 0.0f }) == 0.0f);
    CHECK (resultOf (Xor, false, { 1.0f, unwired, unwired }) == 1.0f);

    // Invert: Nand / Nor / Xnor.
    CHECK (resultOf (And, true, { 1.0f, 1.0f }) == 0.0f);
    CHECK (resultOf (And, true, { 1.0f, 0.0f }) == 1.0f);
    CHECK (resultOf (Or, true, { 0.0f, 0.0f }) == 1.0f);
    CHECK (resultOf (Or, true, { 1.0f, 0.0f }) == 0.0f);
    CHECK (resultOf (Xor, true, { 1.0f, 1.0f }) == 1.0f);

    // Nothing wired: false for every gate, inverted or not.
    for (const auto op : { And, Or, Xor })
        for (const auto invert : { false, true })
            CHECK (resultOf (op, invert, { unwired, unwired }) == 0.0f);

    // "True" is > 0.5, the threshold env.adsr's gate uses.
    CHECK (resultOf (And, false, { 0.6f, 0.6f }) == 1.0f);
    CHECK (resultOf (And, false, { 0.6f, 0.4f }) == 0.0f);

    CHECK (LogicAndNode {}.typeId() == "logic.and");
    CHECK (LogicXorNode {}.getTitle() == "Xor");
}

TEST_CASE ("Event Group fires when any input fires, merging same-sample events into the strongest", "[engine][nodes][logic]")
{
    LogicEventGroupNode node;
    node.setGroupPortCount (3);
    REQUIRE (node.getInputPorts().size() == 3);
    CHECK (node.getInputPorts()[2].type == SignalType::Event);
    CHECK (node.getOutputPorts()[0].type == SignalType::Event);

    auto out = [&] (std::vector<float> in)
    {
        float result = -1.0f;
        node.processSample (in.data(), &result);
        return result;
    };
    CHECK (out ({ 0.0f, 0.0f, 0.0f }) == 0.0f);
    CHECK (out ({ 0.0f, 0.0f, 1.0f }) == 1.0f);
    CHECK (out ({ 0.3f, 0.0f, 0.8f }) == 0.8f);
}

TEST_CASE ("Edge turns a Boolean's changes into events: rising, falling, or both", "[engine][nodes][logic]")
{
    auto edges = [] (float mode)
    {
        LogicEdgeNode node;
        node.setParameter ("logic.edge.mode", mode);
        std::vector<float> fired;
        for (const auto v : { 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f })
        {
            float out = 0.0f;
            node.processSample (&v, &out);
            fired.push_back (out);
        }
        return fired;
    };
    CHECK (edges (0.0f) == std::vector<float> { 0, 1, 0, 0, 0, 1 }); // rising
    CHECK (edges (1.0f) == std::vector<float> { 0, 0, 0, 1, 0, 0 }); // falling
    CHECK (edges (2.0f) == std::vector<float> { 0, 1, 0, 1, 0, 1 }); // both
}

TEST_CASE ("Latch holds true from Set until Reset, and Reset wins a tie", "[engine][nodes][logic]")
{
    LogicLatchNode node;
    auto step = [&] (float set, float reset)
    {
        const float in[2] = { set, reset };
        float out = -1.0f;
        node.processSample (in, &out);
        return out;
    };
    CHECK (step (0, 0) == 0.0f);
    CHECK (step (1, 0) == 1.0f);
    CHECK (step (0, 0) == 1.0f); // held
    CHECK (step (1, 0) == 1.0f); // a repeated set is harmless (unlike Toggle)
    CHECK (step (0, 1) == 0.0f);
    CHECK (step (1, 1) == 0.0f); // both on one sample: reset wins
}

TEST_CASE ("A logic gate's group size is clamped to 2..16 and drives its declared ports", "[engine][nodes][logic][M21]")
{
    LogicOrNode node;
    CHECK (node.getInputPorts().size() == 2);
    CHECK (node.getNumInputPorts() == 2);

    node.setGroupPortCount (5);
    REQUIRE (node.getInputPorts().size() == 5);
    CHECK (node.getNumInputPorts() == 5);
    CHECK (node.getInputPorts()[4].id == "in.4");
    CHECK (node.getInputPorts()[4].type == SignalType::Boolean);

    node.setGroupPortCount (99);
    CHECK (node.getGroupPortCount() == 16);
    node.setGroupPortCount (0);
    CHECK (node.getGroupPortCount() == 2);
}
