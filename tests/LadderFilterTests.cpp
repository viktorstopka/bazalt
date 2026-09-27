// M22 wave 3: LadderFilter — see LadderFilter.h's own header comment for the
// full ZDF derivation and the documented resonance-clamping/post-drive
// design calls. Verified empirically against the closed-form theory before
// writing these assertions (a throwaway probe swept gain across
// resonance/frequency/mode and confirmed every number below matches what
// the math predicts, not just "looks plausible").
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/LadderFilter.h"
#include <cmath>

using namespace bazalt::engine;

namespace
{
    float measureGain (LadderFilter& filter, double sampleRate, float toneHz, float driveLinear = 1.0f)
    {
        constexpr int totalSamples = 32768;
        constexpr int discardSamples = 16384;

        const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) toneHz / sampleRate;
        double phase = 0.0;
        double sumSquares = 0.0;
        int measuredCount = 0;

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto input = (float) std::sin (phase);
            const auto output = filter.processSample (input, driveLinear);
            phase += phaseIncrement;

            if (i >= discardSamples)
            {
                sumSquares += (double) output * (double) output;
                ++measuredCount;
            }
        }

        const auto outputRms = std::sqrt (sumSquares / measuredCount);
        return (float) (outputRms / (1.0 / std::sqrt (2.0)));
    }
}

TEST_CASE ("LadderFilter (lowpass, no resonance) matches the analytical 4-cascaded-TPT-one-pole gain at cutoff",
           "[engine][LadderFilter][M22]")
{
    // Four identical TPT one-poles, each -3dB (1/sqrt(2)) at the shared
    // cutoff, cascaded: (1/sqrt(2))^4 = 0.25 exactly, with no resonance
    // feedback to alter it.
    constexpr double sampleRate = 48000.0;
    constexpr float cutoffHz = 1000.0f;

    LadderFilter filter;
    filter.prepare (sampleRate);
    filter.setCutoffFrequency (cutoffHz);
    filter.setResonance (0.0f);
    filter.setPoles (4);

    CHECK (measureGain (filter, sampleRate, cutoffHz) == Catch::Approx (0.25f).margin (0.02f));
}

TEST_CASE ("LadderFilter (lowpass, no resonance) rolls off monotonically above cutoff", "[engine][LadderFilter][M22]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float cutoffHz = 1000.0f;

    LadderFilter filter;
    filter.prepare (sampleRate);
    filter.setCutoffFrequency (cutoffHz);
    filter.setResonance (0.0f);
    filter.setPoles (4);

    float previousGain = 1.0f;
    for (float ratio : { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f })
    {
        LadderFilter fresh;
        fresh.prepare (sampleRate);
        fresh.setCutoffFrequency (cutoffHz);
        fresh.setResonance (0.0f);
        fresh.setPoles (4);
        const auto gain = measureGain (fresh, sampleRate, cutoffHz * ratio);
        INFO ("ratio = " << ratio);
        CHECK (gain < previousGain);
        previousGain = gain;
    }
}

TEST_CASE ("LadderFilter's resonant peak at cutoff grows as resonance approaches 1, staying bounded",
           "[engine][LadderFilter][M22]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float cutoffHz = 1000.0f;

    float previousGain = -1.0f;
    for (float resonance : { 0.0f, 0.5f, 0.9f, 0.99f, 0.999f })
    {
        LadderFilter filter;
        filter.prepare (sampleRate);
        filter.setCutoffFrequency (cutoffHz);
        filter.setResonance (resonance);
        filter.setPoles (4);

        const auto gain = measureGain (filter, sampleRate, cutoffHz);
        INFO ("resonance = " << resonance);
        CHECK (gain > previousGain); // monotonically growing resonant peak
        CHECK (std::isfinite (gain));
        CHECK (gain < 10.0f); // never explodes - the whole point of the resonance clamp
        previousGain = gain;
    }
}

TEST_CASE ("LadderFilter at high resonance rings far longer after an impulse than at low resonance",
           "[engine][LadderFilter][M22]")
{
    // The audible proxy for "self-oscillates at resonance=1" this
    // implementation actually delivers (LadderFilter.h's own header comment
    // explains why it's clamped just under literal instability rather than
    // put there exactly): a 4th-order system this close to its stability
    // boundary keeps ringing long after any real self-oscillating filter's
    // resonance would have settled, and long after this same filter does at
    // low resonance.
    constexpr double sampleRate = 48000.0;

    auto tailPeakAfter = [&] (float resonance, int startSample, int totalSamples) -> float
    {
        LadderFilter filter;
        filter.prepare (sampleRate);
        filter.setCutoffFrequency (1000.0f);
        filter.setResonance (resonance);
        filter.setPoles (4);

        float peak = 0.0f;
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto in = i == 0 ? 1.0f : 0.0f;
            const auto out = filter.processSample (in, 1.0f);
            if (i >= startSample)
                peak = std::max (peak, std::fabs (out));
        }
        return peak;
    };

    constexpr int totalSamples = 48000; // a full second
    constexpr int lateWindowStart = 40000;

    const auto lowResonanceTail = tailPeakAfter (0.2f, lateWindowStart, totalSamples);
    const auto highResonanceTail = tailPeakAfter (0.999f, lateWindowStart, totalSamples);

    CHECK (highResonanceTail > lowResonanceTail * 100.0f);
    CHECK (highResonanceTail > 0.001f); // still audibly ringing nearly a second later
    CHECK (std::isfinite (highResonanceTail));
}

TEST_CASE ("LadderFilter's bandpass mode peaks near cutoff and falls off on both sides", "[engine][LadderFilter][M22]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float cutoffHz = 1000.0f;

    auto gainAt = [&] (float ratio)
    {
        LadderFilter filter;
        filter.prepare (sampleRate);
        filter.setCutoffFrequency (cutoffHz);
        filter.setResonance (0.3f);
        filter.setPoles (4);
        filter.setMode (LadderFilter::Mode::Bandpass);
        return measureGain (filter, sampleRate, cutoffHz * ratio);
    };

    const auto below = gainAt (0.125f);
    const auto atCutoff = gainAt (1.0f);
    const auto above = gainAt (8.0f);

    CHECK (atCutoff > below);
    CHECK (atCutoff > above);
}

TEST_CASE ("LadderFilter's highpass mode rejects well below cutoff far more than at cutoff", "[engine][LadderFilter][M22]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float cutoffHz = 1000.0f;

    auto gainAt = [&] (float ratio)
    {
        LadderFilter filter;
        filter.prepare (sampleRate);
        filter.setCutoffFrequency (cutoffHz);
        filter.setResonance (0.3f);
        filter.setPoles (4);
        filter.setMode (LadderFilter::Mode::Highpass);
        return measureGain (filter, sampleRate, cutoffHz * ratio);
    };

    CHECK (gainAt (1.0f) > gainAt (0.125f) * 2.0f);
}

TEST_CASE ("LadderFilter's poles setting shapes the rolloff: fewer poles attenuate less well above cutoff",
           "[engine][LadderFilter][M22]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float cutoffHz = 1000.0f;

    auto gainAtWithPoles = [&] (int poles)
    {
        LadderFilter filter;
        filter.prepare (sampleRate);
        filter.setCutoffFrequency (cutoffHz);
        filter.setResonance (0.0f);
        filter.setPoles (poles);
        return measureGain (filter, sampleRate, cutoffHz * 4.0f);
    };

    CHECK (gainAtWithPoles (1) > gainAtWithPoles (4));
}

TEST_CASE ("LadderFilter's drive saturates a large output more than a small one", "[engine][LadderFilter][M22]")
{
    constexpr double sampleRate = 48000.0;

    LadderFilter loud;
    loud.prepare (sampleRate);
    loud.setCutoffFrequency (5000.0f); // well above the tone, minimal filtering
    loud.setResonance (0.0f);
    const auto loudGain = measureGain (loud, sampleRate, 200.0f, 20.0f); // heavy drive on a near-unfiltered signal

    LadderFilter gentle;
    gentle.prepare (sampleRate);
    gentle.setCutoffFrequency (5000.0f);
    gentle.setResonance (0.0f);
    const auto gentleGain = measureGain (gentle, sampleRate, 200.0f, 1.0f); // no extra drive

    // tanh compresses: heavy drive must NOT scale RMS gain up proportionally
    // to the drive amount (20x) - it saturates well below that.
    CHECK (loudGain < gentleGain * 20.0f);
    CHECK (loudGain > gentleGain); // still audibly louder, just not linearly so
}
