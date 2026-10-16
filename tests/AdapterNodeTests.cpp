#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <limits>
#include "bazalt/engine/nodes/ThresholdNode.h"
#include "bazalt/engine/nodes/DownmixNode.h"

using namespace bazalt::engine;

TEST_CASE ("ThresholdNode fires a rising edge exactly once per crossing, with hysteresis", "[engine][ThresholdNode][M16]")
{
    nodes::ThresholdNode node;
    float inputs[3] = { 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN() };
    float outs[3] {};
    float& out = outs[0];

    // Below threshold: no fire.
    inputs[0] = 0.2f;
    inputs[1] = 0.5f;
    node.processSample (inputs, outs);
    CHECK (out == 0.0f);

    // Crosses above: fires once.
    inputs[0] = 0.6f;
    node.processSample (inputs, outs);
    CHECK (out == 1.0f);

    // Stays above: does not fire again.
    inputs[0] = 0.7f;
    node.processSample (inputs, outs);
    CHECK (out == 0.0f);

    // Small dip that doesn't clear the hysteresis band: still no re-fire.
    inputs[0] = 0.49f;
    node.processSample (inputs, outs);
    CHECK (out == 0.0f);

    // Drops well below (past the hysteresis band) then crosses again: fires.
    inputs[0] = 0.3f;
    node.processSample (inputs, outs);
    inputs[0] = 0.6f;
    node.processSample (inputs, outs);
    CHECK (out == 1.0f);
}

TEST_CASE ("ThresholdNode's threshold port falls back to 50% when unconnected", "[engine][ThresholdNode][M16]")
{
    nodes::ThresholdNode node;
    float inputs[3] = { 0.6f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() }; // NaN = GraphCompiler's unconnected sentinel
    float outs[3] {};
    float& out = outs[0];
    node.processSample (inputs, outs);
    CHECK (out == 1.0f); // 0.6 >= the 0.5 fallback
}

TEST_CASE ("ThresholdNode's unconnected slider value actually changes when it fires (docs/CLEANUP.md P1 #3)",
           "[engine][ThresholdNode]")
{
    // setParameter() used to be a literal no-op and the fallback was a
    // hardcoded 0.5f, so dragging the slider changed the displayed number
    // with zero effect on the sound.
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    float outs[3] {};
    float& out = outs[0];

    {
        nodes::ThresholdNode node;
        node.setParameter ("threshold", 0.8f);

        float inputs[3] = { 0.6f, nan, nan };
        node.processSample (inputs, outs);
        CHECK (out == 0.0f); // 0.6 is below the slider's 0.8 — under the old hardcoded 0.5 this fired

        inputs[0] = 0.9f;
        node.processSample (inputs, outs);
        CHECK (out == 1.0f);
    }

    {
        // A connected threshold always wins over the stored slider value.
        nodes::ThresholdNode node;
        node.setParameter ("threshold", 0.8f);

        float inputs[3] = { 0.6f, 0.4f, nan };
        node.processSample (inputs, outs);
        CHECK (out == 1.0f); // 0.6 >= the connected 0.4, stored 0.8 ignored
    }
}

TEST_CASE ("Threshold is a Schmitt trigger: hysteresis, direction, Off and Gate", "[engine][ThresholdNode]")
{
    nodes::ThresholdNode node;
    node.setParameter ("threshold", 0.5f);
    node.setParameter ("logic.threshold.hysteresis", 0.2f);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    float outs[3] {};
    const auto step = [&] (float by) { const float in[3] = { by, nan, nan }; node.processSample (in, outs); };

    step (0.6f);
    CHECK (outs[0] == 1.0f);
    CHECK (outs[2] == 1.0f);
    step (0.35f); // inside the band: still on
    CHECK (outs[1] == 0.0f);
    CHECK (outs[2] == 1.0f);
    step (0.25f); // past On - Hysteresis: off, once
    CHECK (outs[1] == 1.0f);
    CHECK (outs[2] == 0.0f);
    step (0.2f);
    CHECK (outs[1] == 0.0f);

    // Falling: on below the level (a sound dying away).
    nodes::ThresholdNode falling;
    falling.setParameter ("threshold", 0.1f);
    falling.setParameter ("logic.threshold.direction", 1.0f);
    float fouts[3] {};
    const float loud[3] = { 0.8f, nan, nan }, quiet[3] = { 0.05f, nan, nan };
    falling.processSample (loud, fouts);
    CHECK (fouts[2] == 0.0f);
    falling.processSample (quiet, fouts);
    CHECK (fouts[0] == 1.0f);
    CHECK (fouts[2] == 1.0f);
}

TEST_CASE ("DownmixNode's five modes compute the expected value", "[engine][DownmixNode][M16]")
{
    nodes::DownmixNode node;
    const float inputs[2] = { 0.6f, 0.2f };
    float out;

    node.setParameter ("channels.downmix.mode", 0.0f); // sum
    node.processSample (inputs, &out);
    CHECK (out > 0.7999f);
    CHECK (out < 0.8001f);

    node.setParameter ("channels.downmix.mode", 1.0f); // left
    node.processSample (inputs, &out);
    CHECK (out == 0.6f);

    node.setParameter ("channels.downmix.mode", 2.0f); // mid
    node.processSample (inputs, &out);
    CHECK (out > 0.3999f);
    CHECK (out < 0.4001f);

    node.setParameter ("channels.downmix.mode", 3.0f); // right
    node.processSample (inputs, &out);
    CHECK (out == 0.2f);

    node.setParameter ("channels.downmix.mode", 4.0f); // side
    node.processSample (inputs, &out);
    CHECK (out > 0.1999f);
    CHECK (out < 0.2001f);
}
