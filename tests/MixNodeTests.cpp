#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/BlendNode.h"
#include "bazalt/engine/nodes/LogicSwitchNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    const float unwired = std::numeric_limits<float>::quiet_NaN(); // GraphCompiler's unconnected-fallback sentinel
}

TEST_CASE ("math.blend mixes A and B by Amount under either law", "[engine][nodes][blend]")
{
    BlendNode node; // not prepared: no smoothing
    float out = 0.0f;

    auto mixOf = [&] (float a, float b, float amount)
    {
        const float inputs[3] = { a, b, amount };
        node.processSample (inputs, &out);
        return out;
    };

    // Linear (the default): unity-sum gains.
    CHECK (mixOf (1.0f, 0.0f, 0.0f) == 1.0f);
    CHECK (mixOf (1.0f, 0.0f, 1.0f) == 0.0f);
    CHECK (mixOf (0.8f, 0.4f, 0.5f) == Catch::Approx (0.6f));
    CHECK (mixOf (1.0f, 1.0f, 0.25f) == Catch::Approx (1.0f));

    node.setParameter ("math.blend.law", 1.0f);
    CHECK (mixOf (1.0f, 1.0f, 0.5f) == Catch::Approx (std::sqrt (2.0f))); // cos(pi/4) + sin(pi/4)

    // Equal power: the two gains carry constant power along the whole sweep.
    for (const auto amount : { 0.0f, 0.3f, 0.5f, 0.8f, 1.0f })
    {
        const auto gainA = mixOf (1.0f, 0.0f, amount);
        const auto gainB = mixOf (0.0f, 1.0f, amount);
        CHECK (gainA * gainA + gainB * gainB == Catch::Approx (1.0f));
    }
}

TEST_CASE ("math.blend clamps Amount, and an unwired Amount uses its stored value", "[engine][nodes][blend]")
{
    BlendNode node;
    float out = 0.0f;

    float inputs[3] = { 1.0f, 0.0f, 2.0f }; // clamps to 1 -> all B
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);

    inputs[2] = -3.0f; // -> 0 -> all A
    node.processSample (inputs, &out);
    CHECK (out == 1.0f);

    inputs[2] = unwired;
    node.setParameter ("math.blend.amount", 1.0f);
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);
}

TEST_CASE ("logic.switch plays one input, steps on Next with wrap-around, and crossfades the change",
           "[engine][nodes][switch]")
{
    LogicSwitchNode node;
    node.setGroupPortCount (3);
    node.prepare ({ 1000.0, 64 }); // 10 ms fade = 10 samples
    float outputs[2] = {};

    auto step = [&] (float next)
    {
        const float inputs[4] = { 1.0f, 2.0f, 3.0f, next };
        node.processSample (inputs, outputs);
        return outputs[0];
    };

    CHECK (step (0.0f) == 1.0f);
    CHECK (outputs[1] == 0.0f);

    // Next: from A towards B, never a step.
    const auto first = step (1.0f);
    CHECK (outputs[1] == 1.0f);
    CHECK (first > 1.0f);
    CHECK (first < 2.0f);
    for (int i = 0; i < 12; ++i)
        step (0.0f);
    CHECK (step (0.0f) == 2.0f);

    // Next twice more wraps back to A.
    step (1.0f);
    for (int i = 0; i < 12; ++i)
        step (0.0f);
    CHECK (step (0.0f) == 3.0f);
    step (1.0f);
    for (int i = 0; i < 12; ++i)
        step (0.0f);
    CHECK (step (0.0f) == 1.0f);
    CHECK (outputs[1] == 0.0f);

    // Active picks directly, clamped to the inputs there are.
    node.setParameter ("logic.switch.active", 7.0f);
    for (int i = 0; i < 12; ++i)
        step (0.0f);
    CHECK (step (0.0f) == 3.0f);
}

TEST_CASE ("logic.switch's inputs adopt the lowest-numbered wired type and quantity", "[engine][nodes][switch]")
{
    LogicSwitchNode node;
    PortDescriptor frequency { .id = "src", .type = SignalType::Signal };
    frequency.quantity = Quantity::Frequency;
    PortDescriptor audio { .id = "src", .type = SignalType::Signal, .quantity = Quantity::Audio };

    node.resolveIncomingPort ("in.1", frequency);
    node.resolveIncomingPort ("in.0", audio);
    node.resolveIncomingPort ("next", frequency); // never a source of the switched type
    CHECK (node.getInputPorts()[0].quantity == Quantity::Audio);
    CHECK (node.getOutputPorts()[0].quantity == Quantity::Audio);
    CHECK (node.getInputPorts().back().id == "next");
    CHECK (node.getInputPorts().back().type == SignalType::Event);
}
