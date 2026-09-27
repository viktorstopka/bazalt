// M22 wave 1: filter.dcBlock.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/DcBlockNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

TEST_CASE ("DcBlockNode removes a constant DC offset, converging toward zero", "[engine][nodes][DcBlockNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };
    DcBlockNode blocker;
    blocker.prepare (info);

    float out = 0.0f;
    for (int i = 0; i < 44100; ++i) // a full second at the default 20Hz cutoff
    {
        float inputs[2] = { 0.5f, kNaN };
        blocker.processSample (inputs, &out);
    }

    CHECK (std::fabs (out) < 0.01f);
}

TEST_CASE ("DcBlockNode passes a well-above-cutoff signal through close to unchanged",
           "[engine][nodes][DcBlockNode][M22]")
{
    constexpr double sampleRate = 44100.0;
    constexpr float toneHz = 2000.0f; // two decades above the default 20Hz cutoff
    NodePrepareInfo info { sampleRate, 512 };

    DcBlockNode blocker;
    blocker.prepare (info);

    float peak = 0.0f;
    for (int i = 0; i < 2000; ++i)
    {
        const auto in = (float) std::sin (2.0 * juce::MathConstants<double>::pi * (double) toneHz * (double) i / sampleRate);
        float inputs[2] = { in, kNaN };
        float out = 0.0f;
        blocker.processSample (inputs, &out);
        if (i > 200) // past the initial transient
            peak = std::max (peak, std::fabs (out));
    }

    CHECK (peak > 0.9f); // unity-ish gain far above cutoff
}

TEST_CASE ("DcBlockNode's cutoff port live-modulates: a higher cutoff blocks DC faster",
           "[engine][nodes][DcBlockNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    DcBlockNode lowCutoff;
    lowCutoff.prepare (info);
    DcBlockNode highCutoff;
    highCutoff.prepare (info);

    float lowOut = 0.0f, highOut = 0.0f;
    constexpr int numSamples = 200;
    for (int i = 0; i < numSamples; ++i)
    {
        float lowIn[2] = { 0.5f, 5.0f };   // cutoff well below default
        float highIn[2] = { 0.5f, 90.0f }; // cutoff near the top of the range
        lowCutoff.processSample (lowIn, &lowOut);
        highCutoff.processSample (highIn, &highOut);
    }

    // A DC step has decayed further toward zero under the faster (higher-cutoff) blocker.
    CHECK (std::fabs (highOut) < std::fabs (lowOut));
}
