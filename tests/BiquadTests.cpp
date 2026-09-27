// M22 wave 2: the Biquad primitive's RBJ cookbook coefficient formulas,
// verified analytically the same way tests/SvfFilterTests.cpp verifies
// SvfFilter — feed a steady-state sine, measure output/input RMS gain, and
// check it against the closed-form value the formula promises.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/Biquad.h"
#include <cmath>

using namespace bazalt::engine;

namespace
{
    // Feeds a sine at `toneHz`, discards the transient, measures steady-state
    // RMS gain (and, for allpass, the phase shift via a zero-crossing
    // comparison) relative to the unity-amplitude input.
    float measureGain (Biquad& filter, double sampleRate, float toneHz)
    {
        constexpr int totalSamples = 16384;
        constexpr int discardSamples = 8192;

        const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) toneHz / sampleRate;
        double phase = 0.0;
        double sumSquares = 0.0;
        int measuredCount = 0;

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto input = (float) std::sin (phase);
            const auto output = filter.processSample (input);
            phase += phaseIncrement;

            if (i >= discardSamples)
            {
                sumSquares += (double) output * (double) output;
                ++measuredCount;
            }
        }

        const auto outputRms = std::sqrt (sumSquares / measuredCount);
        const auto inputRms = 1.0 / std::sqrt (2.0);
        return (float) (outputRms / inputRms);
    }
}

TEST_CASE ("Biquad allpass has unity gain at every frequency", "[engine][Biquad]")
{
    constexpr double sampleRate = 48000.0;

    for (float toneHz : { 100.0f, 1000.0f, 5000.0f, 15000.0f })
    {
        Biquad filter;
        filter.setAllpass (sampleRate, 1000.0f, 2.0f); // fixed centre, varying test tone
        INFO ("tone = " << toneHz << " Hz");
        CHECK (measureGain (filter, sampleRate, toneHz) == Catch::Approx (1.0f).margin (0.02f));
    }
}

TEST_CASE ("Biquad allpass shifts phase by exactly 180 degrees at its own centre frequency", "[engine][Biquad]")
{
    // The defining property of an allpass at its centre: output lags input by
    // pi radians (a sign flip at steady state), independent of Q.
    constexpr double sampleRate = 48000.0;
    constexpr float centreHz = 1000.0f;

    Biquad filter;
    filter.setAllpass (sampleRate, centreHz, 1.0f);

    const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) centreHz / sampleRate;
    double phase = 0.0;
    float input = 0.0f, output = 0.0f;

    for (int i = 0; i < 8192; ++i)
    {
        input = (float) std::sin (phase);
        output = filter.processSample (input);
        phase += phaseIncrement;
    }

    CHECK (output == Catch::Approx (-input).margin (0.02f));
}

TEST_CASE ("Biquad peak boosts at its centre frequency by the requested linear gain, unity elsewhere",
           "[engine][Biquad]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float centreHz = 1000.0f;
    constexpr float gainLinear = 2.0f; // +6.02 dB

    Biquad atCentre;
    atCentre.setPeak (sampleRate, centreHz, 1.0f, gainLinear);
    CHECK (measureGain (atCentre, sampleRate, centreHz) == Catch::Approx (gainLinear).margin (0.03f));

    Biquad farAway;
    farAway.setPeak (sampleRate, centreHz, 1.0f, gainLinear);
    CHECK (measureGain (farAway, sampleRate, centreHz * 8.0f) == Catch::Approx (1.0f).margin (0.05f));
}

TEST_CASE ("Biquad peak with unity gain is a true bypass", "[engine][Biquad]")
{
    constexpr double sampleRate = 48000.0;

    Biquad filter;
    filter.setPeak (sampleRate, 1000.0f, 2.0f, 1.0f); // 0 dB

    for (float toneHz : { 100.0f, 1000.0f, 10000.0f })
    {
        INFO ("tone = " << toneHz << " Hz");
        CHECK (measureGain (filter, sampleRate, toneHz) == Catch::Approx (1.0f).margin (0.01f));
    }
}

TEST_CASE ("Biquad low shelf boosts well below its frequency and is flat well above it", "[engine][Biquad]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float shelfHz = 1000.0f;
    constexpr float gainLinear = 2.0f;

    Biquad low;
    low.setShelf (sampleRate, shelfHz, gainLinear, 0.5f, false);
    CHECK (measureGain (low, sampleRate, shelfHz / 8.0f) == Catch::Approx (gainLinear).margin (0.05f));

    Biquad flat;
    flat.setShelf (sampleRate, shelfHz, gainLinear, 0.5f, false);
    CHECK (measureGain (flat, sampleRate, shelfHz * 8.0f) == Catch::Approx (1.0f).margin (0.05f));
}

TEST_CASE ("Biquad high shelf boosts well above its frequency and is flat well below it", "[engine][Biquad]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float shelfHz = 1000.0f;
    constexpr float gainLinear = 2.0f;

    Biquad high;
    high.setShelf (sampleRate, shelfHz, gainLinear, 0.5f, true);
    CHECK (measureGain (high, sampleRate, shelfHz * 8.0f) == Catch::Approx (gainLinear).margin (0.05f));

    Biquad flat;
    flat.setShelf (sampleRate, shelfHz, gainLinear, 0.5f, true);
    CHECK (measureGain (flat, sampleRate, shelfHz / 8.0f) == Catch::Approx (1.0f).margin (0.05f));
}

TEST_CASE ("Biquad shelf with unity gain is a true bypass, both types", "[engine][Biquad]")
{
    constexpr double sampleRate = 48000.0;

    for (auto isHigh : { false, true })
    {
        Biquad filter;
        filter.setShelf (sampleRate, 1000.0f, 1.0f, 0.5f, isHigh);
        INFO ("isHighShelf = " << isHigh);
        CHECK (measureGain (filter, sampleRate, 200.0f) == Catch::Approx (1.0f).margin (0.01f));
        CHECK (measureGain (filter, sampleRate, 5000.0f) == Catch::Approx (1.0f).margin (0.01f));
    }
}
