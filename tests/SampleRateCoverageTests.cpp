#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/PolyBlepOscillator.h"
#include "bazalt/engine/SvfFilter.h"
#include <juce_core/juce_core.h>
#include <cmath>

TEST_CASE ("Oscillator + SvfFilter stay finite and bounded across the sample-rate range", "[engine][sample-rate-coverage]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0);
    INFO ("sample rate = " << sampleRate);

    bazalt::engine::PolyBlepOscillator oscillator;
    oscillator.prepare (sampleRate);
    oscillator.setWaveform (bazalt::engine::OscillatorWaveform::Saw);
    oscillator.setFrequency (440.0f);

    bazalt::engine::SvfFilter filter;
    filter.prepare (sampleRate, 512, 1);
    filter.setType (bazalt::engine::SvfFilterType::Lowpass);
    filter.setCutoffFrequency (2000.0f);
    filter.setResonance (0.7071f);

    const auto numSamples = (int) (sampleRate * 0.1); // 100ms

    for (int i = 0; i < numSamples; ++i)
    {
        const auto raw = oscillator.renderNextSample();
        const auto filtered = filter.processSample (0, raw);

        REQUIRE (std::isfinite (filtered));
        REQUIRE (std::abs (filtered) < 10.0f);
    }
}

TEST_CASE ("SvfFilter's cutoff-frequency coefficient derivation holds across sample rates", "[engine][sample-rate-coverage][SvfFilter]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0, 192000.0);
    INFO ("sample rate = " << sampleRate);

    constexpr float cutoffHz = 1000.0f;
    constexpr float resonance = 0.70710678f;
    constexpr int totalSamples = 8192;
    constexpr int discardSamples = 4096;

    bazalt::engine::SvfFilter filter;
    filter.prepare (sampleRate, totalSamples, 1);
    filter.setType (bazalt::engine::SvfFilterType::Lowpass);
    filter.setCutoffFrequency (cutoffHz);
    filter.setResonance (resonance);

    const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) cutoffHz / sampleRate;
    double phase = 0.0;
    double sumSquares = 0.0;
    int measuredCount = 0;

    for (int i = 0; i < totalSamples; ++i)
    {
        const auto input = (float) std::sin (phase);
        const auto output = filter.processSample (0, input);
        phase += phaseIncrement;

        if (i >= discardSamples)
        {
            sumSquares += (double) output * (double) output;
            ++measuredCount;
        }
    }

    const auto outputRms = std::sqrt (sumSquares / measuredCount);
    const auto inputRms = 1.0 / std::sqrt (2.0);
    const auto gain = (float) (outputRms / inputRms);

    CHECK (gain == Catch::Approx (resonance).margin (0.01f));
}
