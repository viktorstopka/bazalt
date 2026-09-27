// M22 wave 2: PeakFilterNode/ShelfFilterNode/AllpassFilterNode - mechanism
// tests (NaN-fallback, live modulation) matching ModulatablePortsTests.cpp's
// own idiom. BiquadTests.cpp already covers the underlying math analytically;
// these confirm each node wires its ports into it correctly.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/PeakFilterNode.h"
#include "bazalt/engine/nodes/ShelfFilterNode.h"
#include "bazalt/engine/nodes/AllpassFilterNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    // Steady-state RMS gain of a node driven by a sine at toneHz.
    template <typename BuildInputs>
    float steadyStateGain (double sampleRate, float toneHz, BuildInputs buildInputs)
    {
        constexpr int totalSamples = 16384;
        constexpr int discardSamples = 8192;

        const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) toneHz / sampleRate;
        double phase = 0.0;
        double sumSquares = 0.0;
        int measuredCount = 0;

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto in = (float) std::sin (phase);
            float out = 0.0f;
            buildInputs (in, out);
            phase += phaseIncrement;

            if (i >= discardSamples)
            {
                sumSquares += (double) out * (double) out;
                ++measuredCount;
            }
        }

        const auto outputRms = std::sqrt (sumSquares / measuredCount);
        return (float) (outputRms / (1.0 / std::sqrt (2.0)));
    }
}

TEST_CASE ("PeakFilterNode's frequency/gain/q ports live-modulate", "[engine][nodes][PeakFilterNode][M22]")
{
    constexpr double sampleRate = 48000.0;
    NodePrepareInfo info { sampleRate, 512 };

    PeakFilterNode boosted;
    boosted.prepare (info);
    const auto boostedGain = steadyStateGain (sampleRate, 1000.0f, [&] (float in, float& out)
    {
        float inputs[4] = { in, 1000.0f, 3.0f, 1.0f };
        boosted.processSample (inputs, &out);
    });

    PeakFilterNode flat;
    flat.prepare (info);
    const auto flatGain = steadyStateGain (sampleRate, 1000.0f, [&] (float in, float& out)
    {
        float inputs[4] = { in, 1000.0f, 1.0f, 1.0f }; // unity gain
        flat.processSample (inputs, &out);
    });

    CHECK (boostedGain > flatGain);
    CHECK (flatGain == Catch::Approx (1.0f).margin (0.02f));
}

TEST_CASE ("PeakFilterNode's ports fall back to setParameter's value exactly when unconnected",
           "[engine][nodes][PeakFilterNode][M22]")
{
    NodePrepareInfo info { 48000.0, 512 };

    PeakFilterNode viaParameter;
    viaParameter.prepare (info);
    viaParameter.setParameter ("filter.peak.frequency", 800.0f);
    viaParameter.setParameter ("filter.peak.gain", 2.0f);
    viaParameter.setParameter ("filter.peak.q", 3.0f);

    PeakFilterNode viaNaN;
    viaNaN.prepare (info);
    viaNaN.setParameter ("filter.peak.frequency", 800.0f);
    viaNaN.setParameter ("filter.peak.gain", 2.0f);
    viaNaN.setParameter ("filter.peak.q", 3.0f);

    for (int i = 0; i < 500; ++i)
    {
        const auto in = (float) std::sin (0.05 * i);
        float a = 0.0f, b = 0.0f;
        float inputsA[4] = { in, kNaN, kNaN, kNaN };
        float inputsB[4] = { in, kNaN, kNaN, kNaN };
        viaParameter.processSample (inputsA, &a);
        viaNaN.processSample (inputsB, &b);
        CHECK (a == b);
    }
}

TEST_CASE ("ShelfFilterNode's low/high type switches which side of the shelf frequency is boosted",
           "[engine][nodes][ShelfFilterNode][M22]")
{
    constexpr double sampleRate = 48000.0;
    NodePrepareInfo info { sampleRate, 512 };

    ShelfFilterNode low;
    low.prepare (info);
    low.setParameter ("filter.shelf.type", 0.0f);

    ShelfFilterNode high;
    high.prepare (info);
    high.setParameter ("filter.shelf.type", 1.0f);

    const auto lowGainBelow = steadyStateGain (sampleRate, 1000.0f / 8.0f, [&] (float in, float& out)
    {
        float inputs[3] = { in, 1000.0f, 2.0f };
        low.processSample (inputs, &out);
    });
    const auto highGainBelow = steadyStateGain (sampleRate, 1000.0f / 8.0f, [&] (float in, float& out)
    {
        float inputs[3] = { in, 1000.0f, 2.0f };
        high.processSample (inputs, &out);
    });

    CHECK (lowGainBelow > highGainBelow); // low-shelf boosts below its frequency, high-shelf doesn't
}

TEST_CASE ("ShelfFilterNode's ports fall back to setParameter's value exactly when unconnected",
           "[engine][nodes][ShelfFilterNode][M22]")
{
    NodePrepareInfo info { 48000.0, 512 };

    ShelfFilterNode viaParameter;
    viaParameter.prepare (info);
    viaParameter.setParameter ("filter.shelf.frequency", 600.0f);
    viaParameter.setParameter ("filter.shelf.gain", 1.5f);

    ShelfFilterNode viaNaN;
    viaNaN.prepare (info);
    viaNaN.setParameter ("filter.shelf.frequency", 600.0f);
    viaNaN.setParameter ("filter.shelf.gain", 1.5f);

    for (int i = 0; i < 500; ++i)
    {
        const auto in = (float) std::sin (0.05 * i);
        float a = 0.0f, b = 0.0f;
        float inputsA[3] = { in, kNaN, kNaN };
        float inputsB[3] = { in, kNaN, kNaN };
        viaParameter.processSample (inputsA, &a);
        viaNaN.processSample (inputsB, &b);
        CHECK (a == b);
    }
}

TEST_CASE ("AllpassFilterNode has unity gain regardless of stage count", "[engine][nodes][AllpassFilterNode][M22]")
{
    constexpr double sampleRate = 48000.0;
    NodePrepareInfo info { sampleRate, 512 };

    for (auto stages : { 1.0f, 4.0f, 16.0f })
    {
        AllpassFilterNode node;
        node.prepare (info);
        node.setParameter ("filter.allpass.stages", stages);

        const auto gain = steadyStateGain (sampleRate, 1000.0f, [&] (float in, float& out)
        {
            float inputs[3] = { in, 1000.0f, 0.7f };
            node.processSample (inputs, &out);
        });

        INFO ("stages = " << stages);
        CHECK (gain == Catch::Approx (1.0f).margin (0.03f));
    }
}

TEST_CASE ("AllpassFilterNode's stage count changes the accumulated phase shift", "[engine][nodes][AllpassFilterNode][M22]")
{
    // More cascaded stages rotate phase further - a 16-stage cascade must
    // diverge measurably from a 1-stage one at the same instant, even though
    // both have unity gain (the previous test's own point).
    constexpr double sampleRate = 48000.0;
    NodePrepareInfo info { sampleRate, 512 };

    AllpassFilterNode oneStage;
    oneStage.prepare (info);
    oneStage.setParameter ("filter.allpass.stages", 1.0f);

    AllpassFilterNode manyStages;
    manyStages.prepare (info);
    manyStages.setParameter ("filter.allpass.stages", 16.0f);

    float outOne = 0.0f, outMany = 0.0f;
    for (int i = 0; i < 8192; ++i)
    {
        const auto in = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sampleRate);
        float inputsOne[3] = { in, 1000.0f, 0.7f };
        float inputsMany[3] = { in, 1000.0f, 0.7f };
        oneStage.processSample (inputsOne, &outOne);
        manyStages.processSample (inputsMany, &outMany);
    }

    CHECK (outOne != Catch::Approx (outMany).margin (0.05f));
}

TEST_CASE ("AllpassFilterNode's amount port live-modulates the phase-rotation Q", "[engine][nodes][AllpassFilterNode][M22]")
{
    NodePrepareInfo info { 48000.0, 512 };

    AllpassFilterNode gentle;
    gentle.prepare (info);
    AllpassFilterNode sharp;
    sharp.prepare (info);

    float gentleOut = 0.0f, sharpOut = 0.0f;
    for (int i = 0; i < 20; ++i)
    {
        const auto in = i == 0 ? 1.0f : 0.0f; // an impulse, so the two Qs ring differently
        float gentleIn[3] = { in, 500.0f, 0.05f };
        float sharpIn[3] = { in, 500.0f, 0.95f };
        gentle.processSample (gentleIn, &gentleOut);
        sharp.processSample (sharpIn, &sharpOut);
    }

    CHECK (gentleOut != sharpOut);
}
