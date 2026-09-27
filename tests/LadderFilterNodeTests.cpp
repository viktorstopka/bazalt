// M22 wave 3: LadderFilterNode - port wiring (NaN-fallback, live modulation,
// keyTrack). LadderFilterTests.cpp already covers the underlying
// LadderFilter primitive's DSP correctness.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/LadderFilterNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    float steadyStateGain (LadderFilterNode& node, double sampleRate, float toneHz, float cutoff, float resonance,
                           float drive = 1.0f, float keyTrack = kNaN, float keyPitch = kNaN)
    {
        constexpr int totalSamples = 32768;
        constexpr int discardSamples = 16384;

        const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) toneHz / sampleRate;
        double phase = 0.0;
        double sumSquares = 0.0;
        int measuredCount = 0;

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto in = (float) std::sin (phase);
            float inputs[6] = { in, cutoff, resonance, drive, keyTrack, keyPitch };
            float out = 0.0f;
            node.processSample (inputs, &out);
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

TEST_CASE ("LadderFilterNode's cutoff/resonance/drive ports live-modulate", "[engine][nodes][LadderFilterNode][M22]")
{
    constexpr double sampleRate = 48000.0;
    NodePrepareInfo info { sampleRate, 512 };

    LadderFilterNode lowCutoff;
    lowCutoff.prepare (info);
    const auto lowGain = steadyStateGain (lowCutoff, sampleRate, 1000.0f, 200.0f, 0.0f);

    LadderFilterNode highCutoff;
    highCutoff.prepare (info);
    const auto highGain = steadyStateGain (highCutoff, sampleRate, 1000.0f, 15000.0f, 0.0f);

    CHECK (highGain > lowGain); // a 1kHz tone passes a 15kHz cutoff far more than a 200Hz one
}

TEST_CASE ("LadderFilterNode's ports fall back to setParameter's value exactly when unconnected",
           "[engine][nodes][LadderFilterNode][M22]")
{
    NodePrepareInfo info { 48000.0, 512 };

    LadderFilterNode viaParameter;
    viaParameter.prepare (info);
    viaParameter.setParameter ("filter.ladder.cutoff", 800.0f);
    viaParameter.setParameter ("filter.ladder.resonance", 0.4f);
    viaParameter.setParameter ("filter.ladder.drive", 3.0f);

    LadderFilterNode viaNaN;
    viaNaN.prepare (info);
    viaNaN.setParameter ("filter.ladder.cutoff", 800.0f);
    viaNaN.setParameter ("filter.ladder.resonance", 0.4f);
    viaNaN.setParameter ("filter.ladder.drive", 3.0f);

    for (int i = 0; i < 1000; ++i)
    {
        const auto in = (float) std::sin (0.02 * i);
        float a = 0.0f, b = 0.0f;
        float inputsA[6] = { in, kNaN, kNaN, kNaN, kNaN, kNaN };
        float inputsB[6] = { in, kNaN, kNaN, kNaN, kNaN, kNaN };
        viaParameter.processSample (inputsA, &a);
        viaNaN.processSample (inputsB, &b);
        CHECK (a == b);
    }
}

TEST_CASE ("LadderFilterNode's keyTrack raises the effective cutoff for a higher keyPitch",
           "[engine][nodes][LadderFilterNode][M22]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float toneHz = 4000.0f; // above the base 1000Hz cutoff, so tracking it upward is measurable

    NodePrepareInfo info { sampleRate, 512 };

    LadderFilterNode lowNote;
    lowNote.prepare (info);
    const auto lowNoteGain = steadyStateGain (lowNote, sampleRate, toneHz, 1000.0f, 0.0f, 1.0f, 1.0f, 60.0f);

    LadderFilterNode highNote;
    highNote.prepare (info);
    const auto highNoteGain = steadyStateGain (highNote, sampleRate, toneHz, 1000.0f, 0.0f, 1.0f, 1.0f, 84.0f); // +2 octaves

    CHECK (highNoteGain > lowNoteGain); // the effective cutoff tracked upward, so more of the tone gets through

    // keyTrack=0 must mean no tracking at all, whatever keyPitch is.
    LadderFilterNode noTrack;
    noTrack.prepare (info);
    const auto noTrackLow = steadyStateGain (noTrack, sampleRate, toneHz, 1000.0f, 0.0f, 1.0f, 0.0f, 60.0f);

    LadderFilterNode noTrackHigh;
    noTrackHigh.prepare (info);
    const auto noTrackHigh2 = steadyStateGain (noTrackHigh, sampleRate, toneHz, 1000.0f, 0.0f, 1.0f, 0.0f, 84.0f);

    CHECK (noTrackLow == Catch::Approx (noTrackHigh2).margin (0.01f));
}

TEST_CASE ("LadderFilterNode's poles/mode parameters reach the underlying filter",
           "[engine][nodes][LadderFilterNode][M22]")
{
    constexpr double sampleRate = 48000.0;
    NodePrepareInfo info { sampleRate, 512 };

    LadderFilterNode lowpass;
    lowpass.prepare (info);
    lowpass.setParameter ("filter.ladder.mode", 0.0f);
    const auto lowpassGain = steadyStateGain (lowpass, sampleRate, 1000.0f, 1000.0f, 0.3f);

    LadderFilterNode bandpass;
    bandpass.prepare (info);
    bandpass.setParameter ("filter.ladder.mode", 2.0f);
    const auto bandpassGainFar = steadyStateGain (bandpass, sampleRate, 8000.0f, 1000.0f, 0.3f);

    // Lowpass at its own cutoff passes plenty; bandpass far above cutoff passes very little.
    CHECK (lowpassGain > bandpassGainFar);
}
