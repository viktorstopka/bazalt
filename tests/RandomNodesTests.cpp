// M22 wave 4: random.stepped, random.drift. Calibrated against a throwaway
// probe (not committed) that measured actual behaviour before writing these
// assertions - both the free-run rate and the quantization/comparative-
// amplitude claims below are known-true from that probe, not guesses.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/RandomSteppedNode.h"
#include "bazalt/engine/nodes/RandomDriftNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

// ---- random.stepped ----

TEST_CASE ("RandomSteppedNode free-runs at exactly its rate when trigger is unconnected",
           "[engine][nodes][RandomSteppedNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };
    RandomSteppedNode node;
    node.prepare (info);
    node.reset();
    node.setParameter ("random.stepped.rate", 10.0f);

    int changes = 0;
    for (int i = 0; i < 44100; ++i) // 1 second
    {
        float inputs[8] = { kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN };
        float outputs[2] = { 0.0f, 0.0f };
        node.processSample (inputs, outputs);
        if (outputs[1] > 0.5f)
            ++changes;
    }

    CHECK (changes == 10);
}

TEST_CASE ("RandomSteppedNode does not free-run once a real trigger is connected",
           "[engine][nodes][RandomSteppedNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };
    RandomSteppedNode node;
    node.prepare (info);
    node.reset();
    node.setParameter ("random.stepped.rate", 10.0f); // would free-run at 10Hz if unconnected

    int changes = 0;
    for (int i = 0; i < 44100; ++i)
    {
        float inputs[8] = { 0.0f, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN }; // trigger CONNECTED, held low
        float outputs[2] = { 0.0f, 0.0f };
        node.processSample (inputs, outputs);
        if (outputs[1] > 0.5f)
            ++changes;
    }

    CHECK (changes == 0);
}

TEST_CASE ("RandomSteppedNode's steps quantizes the output to one of N evenly-spaced levels",
           "[engine][nodes][RandomSteppedNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    for (int trial = 0; trial < 20; ++trial)
    {
        RandomSteppedNode node;
        node.prepare (info);
        node.reset();
        node.setParameter ("random.stepped.seed", (float) (trial + 1));
        node.setParameter ("random.stepped.steps", 4.0f);
        node.setParameter ("random.stepped.smooth", 0.0f); // instant jump, so the raw quantized value is readable immediately

        float out = 0.0f;
        for (int i = 0; i < 5; ++i)
        {
            float inputs[8] = { i == 0 ? 1.0f : 0.0f, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN };
            float outputs[2] = { 0.0f, 0.0f };
            node.processSample (inputs, outputs);
            out = outputs[0];
        }

        // One of exactly 5 levels: -1, -0.5, 0, 0.5, 1.
        const auto scaled = (out + 1.0f) * 2.0f; // -> 0, 1, 2, 3, 4
        INFO ("trial " << trial << " out = " << out);
        CHECK (scaled == Catch::Approx (std::round (scaled)).margin (1.0e-4f));
    }
}

TEST_CASE ("RandomSteppedNode's smooth glides toward the target instead of jumping",
           "[engine][nodes][RandomSteppedNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    RandomSteppedNode instant;
    instant.prepare (info);
    instant.reset();
    instant.setParameter ("random.stepped.smooth", 0.0f);
    instant.setParameter ("random.stepped.distribution", 0.0f);

    RandomSteppedNode glided;
    glided.prepare (info);
    glided.reset();
    glided.setParameter ("random.stepped.smooth", 1.0f);
    glided.setParameter ("random.stepped.distribution", 0.0f);

    float instantOut = 0.0f, glidedOut = 0.0f;
    float instantInputs[8] = { 1.0f, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN };
    float glidedInputs[8] = { 1.0f, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN };
    float instantOutputs[2] = {}, glidedOutputs[2] = {};
    instant.processSample (instantInputs, instantOutputs);
    glided.processSample (glidedInputs, glidedOutputs);
    instantOut = instantOutputs[0];
    glidedOut = glidedOutputs[0];

    // On the very sample a new target is drawn: instant jumps straight there,
    // glided (starting from 0) has barely moved off 0 yet.
    CHECK (std::fabs (glidedOut) < std::fabs (instantOut));
}

TEST_CASE ("RandomSteppedNode's chance can suppress a trigger from producing a new value",
           "[engine][nodes][RandomSteppedNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };
    RandomSteppedNode node;
    node.prepare (info);
    node.reset();
    node.setParameter ("random.stepped.chance", 0.0f); // never actually changes
    node.setParameter ("random.stepped.smooth", 0.0f);

    int changes = 0;
    for (int i = 0; i < 100; ++i)
    {
        float inputs[8] = { 1.0f, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN }; // fires every sample
        float outputs[2] = { 0.0f, 0.0f };
        node.processSample (inputs, outputs);
        if (outputs[1] > 0.5f)
            ++changes;
    }

    CHECK (changes == 0);
}

TEST_CASE ("RandomSteppedNode is deterministic for a given seed, and different seeds diverge",
           "[engine][nodes][RandomSteppedNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    auto runFor = [&] (float seed)
    {
        RandomSteppedNode node;
        node.prepare (info);
        node.setParameter ("random.stepped.seed", seed);
        node.reset();
        std::vector<float> values;
        for (int i = 0; i < 200; ++i)
        {
            float inputs[8] = { i % 10 == 0 ? 1.0f : 0.0f, 20.0f, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN };
            float outputs[2] = { 0.0f, 0.0f };
            node.processSample (inputs, outputs);
            values.push_back (outputs[0]);
        }
        return values;
    };

    const auto a1 = runFor (7.0f);
    const auto a2 = runFor (7.0f);
    const auto b = runFor (8.0f);

    CHECK (a1 == a2);
    CHECK (a1 != b);
}

// ---- random.drift ----

TEST_CASE ("RandomDriftNode wanders more freely at centering=0 than at centering=1 over the same window",
           "[engine][nodes][RandomDriftNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    auto meanAbsOver = [&] (float centering, int numSamples)
    {
        RandomDriftNode node;
        node.prepare (info);
        node.reset();
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            float inputs[3] = { kNaN, kNaN, centering };
            float out = 0.0f;
            node.processSample (inputs, &out);
            sum += std::fabs (out);
        }
        return (float) (sum / numSamples);
    };

    constexpr int tenSeconds = 441000;
    const auto free = meanAbsOver (0.0f, tenSeconds);
    const auto centred = meanAbsOver (1.0f, tenSeconds);

    CHECK (free > centred * 3.0f); // "strongly pulled back" must be clearly tighter
}

TEST_CASE ("RandomDriftNode never leaves [-1, 1], even fully uncentred", "[engine][nodes][RandomDriftNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };
    RandomDriftNode node;
    node.prepare (info);
    node.reset();

    for (int i = 0; i < 441000; ++i) // 10s free walk - the case most likely to reach the clamp
    {
        float inputs[3] = { kNaN, kNaN, 0.0f };
        float out = 0.0f;
        node.processSample (inputs, &out);
        REQUIRE (out >= -1.0f);
        REQUIRE (out <= 1.0f);
        REQUIRE (std::isfinite (out));
    }
}

TEST_CASE ("RandomDriftNode's rate port live-modulates: a faster rate wanders more in the same time",
           "[engine][nodes][RandomDriftNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    auto meanAbsAtRate = [&] (float rate)
    {
        RandomDriftNode node;
        node.prepare (info);
        node.reset();
        double sum = 0.0;
        constexpr int numSamples = 44100 * 3;
        for (int i = 0; i < numSamples; ++i)
        {
            float inputs[3] = { rate, kNaN, 0.0f };
            float out = 0.0f;
            node.processSample (inputs, &out);
            sum += std::fabs (out);
        }
        return (float) (sum / numSamples);
    };

    CHECK (meanAbsAtRate (5.0f) > meanAbsAtRate (0.05f));
}

TEST_CASE ("RandomDriftNode's spectrum setting changes how fast the walk varies, not just its amplitude",
           "[engine][nodes][RandomDriftNode][M22]")
{
    // brown (heavy pre-smoothing) and whiteFiltered (none) land on SIMILAR
    // steady-state RMS - the OU leak's own implicit lowpass dominates once
    // the driving noise is already smoothed near or below its bandwidth, so
    // amplitude alone doesn't distinguish them (an earlier version of this
    // test measured mean-abs and found them nearly identical: 0.02624 vs
    // 0.02626). What genuinely differs is how fast the signal moves -
    // measured here as the mean absolute sample-to-sample step.
    NodePrepareInfo info { 44100.0, 512 };

    auto meanAbsStepForSpectrum = [&] (float spectrum)
    {
        RandomDriftNode node;
        node.prepare (info);
        node.setParameter ("random.drift.spectrum", spectrum);
        node.reset();
        double sum = 0.0;
        float previous = 0.0f;
        constexpr int numSamples = 44100 * 3;
        for (int i = 0; i < numSamples; ++i)
        {
            float inputs[3] = { kNaN, kNaN, 0.3f };
            float out = 0.0f;
            node.processSample (inputs, &out);
            sum += std::fabs (out - previous);
            previous = out;
        }
        return (float) (sum / numSamples);
    };

    const auto brown = meanAbsStepForSpectrum (0.0f);
    const auto whiteFiltered = meanAbsStepForSpectrum (2.0f);
    CHECK (whiteFiltered > brown * 2.0f); // the least-smoothed option moves visibly faster, step to step
}

TEST_CASE ("RandomDriftNode is deterministic for a given seed, and different seeds diverge",
           "[engine][nodes][RandomDriftNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    auto runFor = [&] (float seed)
    {
        RandomDriftNode node;
        node.prepare (info);
        node.setParameter ("random.drift.seed", seed);
        node.reset();
        std::vector<float> values;
        for (int i = 0; i < 500; ++i)
        {
            float inputs[3] = { kNaN, kNaN, kNaN };
            float out = 0.0f;
            node.processSample (inputs, &out);
            values.push_back (out);
        }
        return values;
    };

    const auto a1 = runFor (3.0f);
    const auto a2 = runFor (3.0f);
    const auto b = runFor (4.0f);

    CHECK (a1 == a2);
    CHECK (a1 != b);
}
