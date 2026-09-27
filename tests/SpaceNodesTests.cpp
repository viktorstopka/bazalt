// M22 wave 5: space.pan, space.width - the first real left/right stereo
// nodes (ADR-0023 Amendment). Calibrated against a throwaway probe (not
// committed) that caught a real bug before it shipped: pow() of a
// fractional exponent on a base that rounds a hair below 0 at hard-panned
// extremes (cos(pi/2) != exactly 0 in float) produced NaN.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/PanNode.h"
#include "bazalt/engine/nodes/WidthNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

// ---- space.pan ----

TEST_CASE ("PanNode sends a unity-amplitude source entirely to one side when hard-panned",
           "[engine][nodes][PanNode][M22]")
{
    for (float law : { 0.0f, 1.0f, 2.0f, 3.0f })
    {
        PanNode node;
        node.setParameter ("space.pan.law", law);

        float leftInputs[3] = { 1.0f, -1.0f, 1.0f };
        float leftOutputs[2] = {};
        node.processSample (leftInputs, leftOutputs);

        INFO ("law = " << law);
        CHECK (leftOutputs[0] == Catch::Approx (1.0f).margin (1.0e-4f));
        CHECK (leftOutputs[1] == Catch::Approx (0.0f).margin (1.0e-4f));

        PanNode nodeRight;
        nodeRight.setParameter ("space.pan.law", law);
        float rightInputs[3] = { 1.0f, 1.0f, 1.0f };
        float rightOutputs[2] = {};
        nodeRight.processSample (rightInputs, rightOutputs);

        CHECK (rightOutputs[0] == Catch::Approx (0.0f).margin (1.0e-4f));
        CHECK (rightOutputs[1] == Catch::Approx (1.0f).margin (1.0e-4f));
    }
}

TEST_CASE ("PanNode never produces a non-finite output at either hard-panned extreme, any law",
           "[engine][nodes][PanNode][M22]")
{
    // The exact scenario that caught a real NaN bug: pow() of a fractional
    // exponent on cos/sin evaluated right at the extreme, where float
    // rounding can land a hair below 0.
    for (float law : { 0.0f, 1.0f, 2.0f, 3.0f })
        for (float pan : { -1.0f, 1.0f })
        {
            PanNode node;
            node.setParameter ("space.pan.law", law);
            float inputs[3] = { 1.0f, pan, 1.0f };
            float outputs[2] = {};
            node.processSample (inputs, outputs);

            INFO ("law = " << law << " pan = " << pan);
            CHECK (std::isfinite (outputs[0]));
            CHECK (std::isfinite (outputs[1]));
        }
}

TEST_CASE ("PanNode's linear law sums to unity at centre; constant-power sums to unity in POWER, not amplitude",
           "[engine][nodes][PanNode][M22]")
{
    PanNode linear;
    linear.setParameter ("space.pan.law", 0.0f); // Linear
    float linearInputs[3] = { 1.0f, 0.0f, 1.0f };
    float linearOutputs[2] = {};
    linear.processSample (linearInputs, linearOutputs);
    CHECK (linearOutputs[0] + linearOutputs[1] == Catch::Approx (1.0f).margin (1.0e-4f));

    PanNode constantPower;
    constantPower.setParameter ("space.pan.law", 3.0f);
    float cpInputs[3] = { 1.0f, 0.0f, 1.0f };
    float cpOutputs[2] = {};
    constantPower.processSample (cpInputs, cpOutputs);
    // The defining identity of constant-power panning: L^2 + R^2 == 1 (not L+R).
    CHECK (cpOutputs[0] * cpOutputs[0] + cpOutputs[1] * cpOutputs[1] == Catch::Approx (1.0f).margin (1.0e-4f));
    CHECK (cpOutputs[0] == Catch::Approx (0.70710678f).margin (1.0e-4f)); // -3.01dB
}

TEST_CASE ("PanNode's -4.5dB law sits between linear and constant-power at centre",
           "[engine][nodes][PanNode][M22]")
{
    auto centreGain = [] (float law)
    {
        PanNode node;
        node.setParameter ("space.pan.law", law);
        float inputs[3] = { 1.0f, 0.0f, 1.0f };
        float outputs[2] = {};
        node.processSample (inputs, outputs);
        return outputs[0];
    };

    const auto linear = centreGain (0.0f);       // 0.5   (-6.02dB)
    const auto minus4_5 = centreGain (2.0f);      // ~0.596 (-4.5dB)
    const auto constantPower = centreGain (3.0f); // ~0.707 (-3.01dB)

    CHECK (minus4_5 > linear);
    CHECK (minus4_5 < constantPower);
    CHECK (minus4_5 == Catch::Approx (0.59566f).margin (1.0e-3f));
}

TEST_CASE ("PanNode's width is a true no-op at 1, collapses to mono at 0, and exaggerates at 2",
           "[engine][nodes][PanNode][M22]")
{
    auto panAt = [] (float pan, float width)
    {
        PanNode node;
        node.setParameter ("space.pan.law", 3.0f);
        float inputs[3] = { 1.0f, pan, width };
        float outputs[2] = {};
        node.processSample (inputs, outputs);
        return std::pair<float, float> { outputs[0], outputs[1] };
    };

    const auto [plainL, plainR] = panAt (-0.5f, 1.0f);
    const auto [rawL, rawR] = panAt (-0.5f, kNaN); // unconnected -> falls back to the same default (1.0)
    CHECK (plainL == rawL);
    CHECK (plainR == rawR);

    const auto [monoL, monoR] = panAt (-0.5f, 0.0f);
    CHECK (monoL == Catch::Approx (monoR).margin (1.0e-5f));
    CHECK (monoL == Catch::Approx ((plainL + plainR) * 0.5f).margin (1.0e-5f));

    const auto [wideL, wideR] = panAt (-0.5f, 2.0f);
    CHECK (wideL > plainL); // exaggerated beyond the plain panned difference
    CHECK (wideR < plainR);
}

TEST_CASE ("PanNode's pan port falls back to setParameter's value exactly when unconnected",
           "[engine][nodes][PanNode][M22]")
{
    PanNode viaParameter;
    viaParameter.setParameter ("space.pan.pan", 0.3f);

    PanNode viaNaN;
    viaNaN.setParameter ("space.pan.pan", 0.3f);

    for (int i = 0; i < 20; ++i)
    {
        float a[2] = {}, b[2] = {};
        float inputsA[3] = { 0.5f, kNaN, kNaN };
        float inputsB[3] = { 0.5f, kNaN, kNaN };
        viaParameter.processSample (inputsA, a);
        viaNaN.processSample (inputsB, b);
        CHECK (a[0] == b[0]);
        CHECK (a[1] == b[1]);
    }
}

// ---- space.width ----

TEST_CASE ("WidthNode's width is a true no-op at 1, for content entirely above the crossover",
           "[engine][nodes][WidthNode][M22]")
{
    // A CONSTANT (DC / 0Hz) input is entirely bass content - always below
    // any crossover - so it always sums to mono by design, regardless of
    // width (an earlier version of this test used exactly that as its
    // signal and found the "no-op" claim false: 0.4/0.4 out of 0.6/0.2 in,
    // which is the crossover working correctly, not a width bug). To
    // isolate width's own no-op property, this uses a tone well ABOVE the
    // crossover, entirely in the band width actually touches.
    constexpr double sampleRate = 44100.0;
    constexpr float toneHz = 4000.0f;
    constexpr float crossoverHz = 120.0f;
    NodePrepareInfo info { sampleRate, 512 };

    WidthNode node;
    node.prepare (info);
    node.reset();

    const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) toneHz / sampleRate;
    double phase = 0.0;
    float maxDiff = 0.0f;

    for (int i = 0; i < 4000; ++i)
    {
        const auto l = (float) std::sin (phase);
        const auto r = (float) std::sin (phase + 0.9); // decorrelated from l, so width has something to act on
        float inputs[4] = { l, r, 1.0f, crossoverHz };
        float outputs[2] = {};
        node.processSample (inputs, outputs);
        phase += phaseIncrement;

        if (i > 500) // past the (fast, at this frequency) crossover settling
        {
            maxDiff = std::max (maxDiff, std::fabs (outputs[0] - l));
            maxDiff = std::max (maxDiff, std::fabs (outputs[1] - r));
        }
    }

    // Not bit-exact: a one-pole crossover's rejection well above cutoff is
    // real but finite (roughly 1/33 amplitude at 4kHz vs a 120Hz cutoff),
    // and that small residual low-band content still gets forced to mono
    // (by design - "bass mono" applies regardless of width) even here,
    // leaving a small but real gap between output and input. Measured at
    // ~0.013 for this exact tone/crossover pair; the margin below is
    // deliberately looser than that measurement, not tuned to just pass.
    CHECK (maxDiff < 0.02f);
}

TEST_CASE ("WidthNode's width=0 collapses left and right to the same (mono) signal",
           "[engine][nodes][WidthNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };
    WidthNode node;
    node.prepare (info);
    node.reset();

    for (int i = 0; i < 2000; ++i)
    {
        const auto l = (float) std::sin (0.01 * i);
        const auto r = (float) std::sin (0.01 * i + 0.7); // decorrelated from l
        float inputs[4] = { l, r, 0.0f, 120.0f };
        float outputs[2] = {};
        node.processSample (inputs, outputs);
        CHECK (outputs[0] == Catch::Approx (outputs[1]).margin (1.0e-5f));
    }
}

TEST_CASE ("WidthNode keeps far-below-crossover content closer to mono than far-above-crossover content",
           "[engine][nodes][WidthNode][M22]")
{
    constexpr double sampleRate = 44100.0;
    NodePrepareInfo info { sampleRate, 512 };

    auto maxLeftRightDiff = [&] (float toneHz, float crossoverHz)
    {
        WidthNode node;
        node.prepare (info);
        node.reset();
        const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) toneHz / sampleRate;
        double phase = 0.0;
        float maxDiff = 0.0f;
        for (int i = 0; i < 40000; ++i)
        {
            const auto l = (float) std::sin (phase);
            const auto r = -l; // fully anti-phase: a "sum to mono" operation should null this out
            float inputs[4] = { l, r, 2.0f, crossoverHz };
            float outputs[2] = {};
            node.processSample (inputs, outputs);
            phase += phaseIncrement;
            if (i > 10000) // past the crossover filter's own settling
                maxDiff = std::max (maxDiff, std::fabs (outputs[0] - outputs[1]));
        }
        return maxDiff;
    };

    // A one-pole crossover is gentle (6dB/octave), so this needs real
    // separation (a decade, not an octave) to show clearly - confirmed
    // empirically via a throwaway probe before picking these frequencies.
    const auto belowCrossover = maxLeftRightDiff (30.0f, 300.0f);
    const auto aboveCrossover = maxLeftRightDiff (2000.0f, 300.0f);

    CHECK (aboveCrossover > belowCrossover * 5.0f);
}

TEST_CASE ("WidthNode's ports fall back to setParameter's value exactly when unconnected",
           "[engine][nodes][WidthNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    WidthNode viaParameter;
    viaParameter.prepare (info);
    viaParameter.setParameter ("space.width.width", 1.5f);
    viaParameter.setParameter ("space.width.bassMonoBelow", 200.0f);

    WidthNode viaNaN;
    viaNaN.prepare (info);
    viaNaN.setParameter ("space.width.width", 1.5f);
    viaNaN.setParameter ("space.width.bassMonoBelow", 200.0f);

    for (int i = 0; i < 500; ++i)
    {
        const auto l = (float) std::sin (0.03 * i);
        const auto r = (float) std::sin (0.03 * i + 1.1);
        float a[2] = {}, b[2] = {};
        float inputsA[4] = { l, r, kNaN, kNaN };
        float inputsB[4] = { l, r, kNaN, kNaN };
        viaParameter.processSample (inputsA, a);
        viaNaN.processSample (inputsB, b);
        CHECK (a[0] == b[0]);
        CHECK (a[1] == b[1]);
    }
}
