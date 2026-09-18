#include <catch2/catch_test_macros.hpp>
#include <limits>
#include "bazalt/engine/nodes/NormaliseNode.h"
#include "bazalt/engine/nodes/ThresholdNode.h"
#include "bazalt/engine/nodes/DownmixNode.h"

using namespace bazalt::engine;

TEST_CASE ("NormaliseNode maps its declared range onto 0..1, clamped", "[engine][NormaliseNode][M16]")
{
    nodes::NormaliseNode node;
    node.setParameter ("adapt.normalise.min", 20.0f);
    node.setParameter ("adapt.normalise.max", 20020.0f);

    float in, out;

    in = 20.0f;
    node.processSample (&in, &out);
    CHECK (out == 0.0f);

    in = 10020.0f; // midpoint
    node.processSample (&in, &out);
    CHECK (out > 0.499f);
    CHECK (out < 0.501f);

    in = 20020.0f;
    node.processSample (&in, &out);
    CHECK (out == 1.0f);

    in = 999999.0f; // out of range — clamped, not extrapolated
    node.processSample (&in, &out);
    CHECK (out == 1.0f);
}

TEST_CASE ("NormaliseNode's output port declares Quantity::Unipolar", "[engine][NormaliseNode][M16]")
{
    const nodes::NormaliseNode node;
    const auto outputs = node.getOutputPorts();
    REQUIRE (outputs.size() == 1);
    CHECK (outputs[0].quantity == Quantity::Unipolar);
}

TEST_CASE ("ThresholdNode fires a rising edge exactly once per crossing, with hysteresis", "[engine][ThresholdNode][M16]")
{
    nodes::ThresholdNode node;
    float inputs[2];
    float out;

    // Below threshold: no fire.
    inputs[0] = 0.2f;
    inputs[1] = 0.5f;
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);

    // Crosses above: fires once.
    inputs[0] = 0.6f;
    node.processSample (inputs, &out);
    CHECK (out == 1.0f);

    // Stays above: does not fire again.
    inputs[0] = 0.7f;
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);

    // Small dip that doesn't clear the hysteresis band: still no re-fire.
    inputs[0] = 0.49f;
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);

    // Drops well below (past the hysteresis band) then crosses again: fires.
    inputs[0] = 0.3f;
    node.processSample (inputs, &out);
    inputs[0] = 0.6f;
    node.processSample (inputs, &out);
    CHECK (out == 1.0f);
}

TEST_CASE ("ThresholdNode's threshold port falls back to 50% when unconnected", "[engine][ThresholdNode][M16]")
{
    nodes::ThresholdNode node;
    float inputs[2] = { 0.6f, std::numeric_limits<float>::quiet_NaN() }; // NaN = GraphCompiler's unconnected sentinel
    float out;
    node.processSample (inputs, &out);
    CHECK (out == 1.0f); // 0.6 >= the 0.5 fallback
}

TEST_CASE ("DownmixNode's five modes compute the expected value", "[engine][DownmixNode][M16]")
{
    nodes::DownmixNode node;
    const float inputs[2] = { 0.6f, 0.2f };
    float out;

    node.setParameter ("mix.downmix.mode", 0.0f); // sum
    node.processSample (inputs, &out);
    CHECK (out > 0.7999f);
    CHECK (out < 0.8001f);

    node.setParameter ("mix.downmix.mode", 1.0f); // left
    node.processSample (inputs, &out);
    CHECK (out == 0.6f);

    node.setParameter ("mix.downmix.mode", 2.0f); // mid
    node.processSample (inputs, &out);
    CHECK (out > 0.3999f);
    CHECK (out < 0.4001f);

    node.setParameter ("mix.downmix.mode", 3.0f); // right
    node.processSample (inputs, &out);
    CHECK (out == 0.2f);

    node.setParameter ("mix.downmix.mode", 4.0f); // side
    node.processSample (inputs, &out);
    CHECK (out > 0.1999f);
    CHECK (out < 0.2001f);
}
