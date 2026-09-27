// M22 wave 1: env.follower — the Audio -> Control adapter.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/EnvelopeFollowerNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    float runFor (EnvelopeFollowerNode& node, float in, int numSamples)
    {
        float out = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float inputs[3] = { in, kNaN, kNaN };
            node.processSample (inputs, &out);
        }
        return out;
    }
}

TEST_CASE ("EnvelopeFollowerNode (Peak) rises toward a held input's magnitude and falls toward silence",
           "[engine][nodes][EnvelopeFollowerNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };
    EnvelopeFollowerNode follower;
    follower.prepare (info);
    follower.setParameter ("env.follower.attack", 5.0f);
    follower.setParameter ("env.follower.release", 100.0f);

    const auto risen = runFor (follower, 0.8f, 2000); // several attack time constants
    CHECK (risen > 0.7f);
    CHECK (risen <= 0.8f + 1.0e-4f);

    const auto fallen = runFor (follower, 0.0f, 44100); // a full second of silence, several release constants
    CHECK (fallen < 0.01f);
}

TEST_CASE ("EnvelopeFollowerNode's attack port live-modulates: a shorter attack rises faster",
           "[engine][nodes][EnvelopeFollowerNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    EnvelopeFollowerNode fast;
    fast.prepare (info);
    EnvelopeFollowerNode slow;
    slow.prepare (info);

    float fastOut = 0.0f, slowOut = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        float fastIn[3] = { 0.8f, 0.1f, kNaN };
        float slowIn[3] = { 0.8f, 200.0f, kNaN };
        fast.processSample (fastIn, &fastOut);
        slow.processSample (slowIn, &slowOut);
    }

    CHECK (fastOut > slowOut);
}

TEST_CASE ("EnvelopeFollowerNode's attack/release ports fall back to setParameter's value exactly when unconnected",
           "[engine][nodes][EnvelopeFollowerNode][M22]")
{
    NodePrepareInfo info { 44100.0, 512 };

    EnvelopeFollowerNode viaParameter;
    viaParameter.prepare (info);
    viaParameter.setParameter ("env.follower.attack", 15.0f);
    viaParameter.setParameter ("env.follower.release", 250.0f);

    EnvelopeFollowerNode viaNaN;
    viaNaN.prepare (info);
    viaNaN.setParameter ("env.follower.attack", 15.0f);
    viaNaN.setParameter ("env.follower.release", 250.0f);

    for (int i = 0; i < 500; ++i)
    {
        float a = 0.0f, b = 0.0f;
        float inputs[3] = { 0.5f, kNaN, kNaN };
        viaParameter.processSample (inputs, &a);
        viaNaN.processSample (inputs, &b);
        CHECK (a == b);
    }
}

TEST_CASE ("EnvelopeFollowerNode's RMS mode reads a full-scale sine's steady-state RMS (~0.707)",
           "[engine][nodes][EnvelopeFollowerNode][M22]")
{
    constexpr double sampleRate = 44100.0;
    constexpr float toneHz = 1000.0f;
    NodePrepareInfo info { sampleRate, 512 };

    EnvelopeFollowerNode follower;
    follower.prepare (info);
    follower.setParameter ("env.follower.detection", 1.0f); // Rms
    follower.setParameter ("env.follower.attack", 5.0f);
    follower.setParameter ("env.follower.release", 5.0f);

    float out = 0.0f;
    for (int i = 0; i < 20000; ++i) // several attack/release constants of settling
    {
        const auto in = (float) std::sin (2.0 * juce::MathConstants<double>::pi * (double) toneHz * (double) i / sampleRate);
        float inputs[3] = { in, kNaN, kNaN };
        follower.processSample (inputs, &out);
    }

    CHECK (out == Catch::Approx (0.7071f).margin (0.02f));
}

TEST_CASE ("EnvelopeFollowerNode's Peak and RMS modes disagree on the same sine (Peak reads near 1, RMS near 0.707)",
           "[engine][nodes][EnvelopeFollowerNode][M22]")
{
    // Peak needs a time constant much SHORTER than the tone's period (1ms at
    // 1kHz) to actually reach each cycle's true peak rather than smoothing
    // through it; RMS needs one much LONGER than the period to average over
    // many cycles instead of riding the waveform. Using the same time constant
    // for both (as an earlier version of this test did) is why 2ms - too slow
    // for Peak to reach 1.0, too fast for RMS to reach a stable 0.7071 - made
    // neither assertion meaningful; this is the fix, not a loosened margin.
    constexpr double sampleRate = 44100.0;
    constexpr float toneHz = 1000.0f;
    constexpr int samplesPerCycle = (int) (sampleRate / toneHz);
    NodePrepareInfo info { sampleRate, 512 };

    EnvelopeFollowerNode peak;
    peak.prepare (info);
    peak.setParameter ("env.follower.attack", 0.05f);
    peak.setParameter ("env.follower.release", 0.05f);

    EnvelopeFollowerNode rms;
    rms.prepare (info);
    rms.setParameter ("env.follower.detection", 1.0f);
    rms.setParameter ("env.follower.attack", 20.0f);
    rms.setParameter ("env.follower.release", 20.0f);

    float peakPeak = 0.0f, rmsOut = 0.0f;
    for (int i = 0; i < 20000; ++i)
    {
        const auto in = (float) std::sin (2.0 * juce::MathConstants<double>::pi * (double) toneHz * (double) i / sampleRate);
        float inputs[3] = { in, kNaN, kNaN };
        float peakOut = 0.0f;
        peak.processSample (inputs, &peakOut);
        rms.processSample (inputs, &rmsOut);

        if (i >= 20000 - samplesPerCycle) // the max over the final settled cycle
            peakPeak = std::max (peakPeak, peakOut);
    }

    CHECK (peakPeak > rmsOut);
    CHECK (peakPeak == Catch::Approx (1.0f).margin (0.1f));
    CHECK (rmsOut == Catch::Approx (0.7071f).margin (0.02f));
}
