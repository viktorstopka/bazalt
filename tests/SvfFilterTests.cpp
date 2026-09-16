#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/SvfFilter.h"
#include <juce_core/juce_core.h>
#include <cmath>

// Analytical reference: for the classic 2nd-order analog state variable
// filter (the continuous-time prototype the TPT/Zavalishin discretization
// tracks almost exactly, away from Nyquist), evaluated exactly AT the
// cutoff frequency w = wc, for the UNITY-PEAK ("RBJ Cookbook") bandpass:
//   |H_LP(jwc)| = Q
//   |H_HP(jwc)| = Q
//   |H_BP(jwc)| = 1        (unity peak, independent of Q)
// Derivation: H_LP(s) = wc^2 / (s^2 + (wc/Q)s + wc^2); at s = jwc the s^2
// and wc^2 terms cancel in the denominator, leaving wc^2 / ((wc/Q)(jwc)) =
// Q / j, magnitude Q. H_HP follows the same way.
//
// JUCE's StateVariableTPTFilter::processSample returns the RAW (non-RBJ)
// bandpass output, which its own header documents as differing from the
// unity-peak one by a factor of R2 = 1/Q ("For the classic 0 dB bandpass,
// we need to multiply the result by R2"): raw_BP = classic_BP * Q, so at
// cutoff, |raw_BP(jwc)| = Q, confirmed empirically below (measured gain
// tracks resonance almost exactly at both Q = 1/sqrt(2) and Q = 4).
namespace
{
    struct MeasuredGain
    {
        float lowpass;
        float highpass;
        float bandpass;
    };

    // Feeds a sine at exactly the filter's cutoff, discards the transient,
    // and measures steady-state RMS gain relative to the (unity-amplitude)
    // input for each SVF output type.
    MeasuredGain measureGainAtCutoff (double sampleRate, float cutoffHz, float resonance)
    {
        constexpr int totalSamples = 8192;
        constexpr int discardSamples = 4096;

        const auto measure = [&] (bazalt::engine::SvfFilterType type)
        {
            bazalt::engine::SvfFilter filter;
            filter.prepare (sampleRate, totalSamples, 1);
            filter.setType (type);
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
            const auto inputRms = 1.0 / std::sqrt (2.0); // unity-amplitude sine
            return (float) (outputRms / inputRms);
        };

        return { measure (bazalt::engine::SvfFilterType::Lowpass),
                 measure (bazalt::engine::SvfFilterType::Highpass),
                 measure (bazalt::engine::SvfFilterType::Bandpass) };
    }
}

TEST_CASE ("SvfFilter matches the analytical SVF response at cutoff (Butterworth Q)", "[engine][SvfFilter]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float resonance = 0.70710678f; // 1/sqrt(2) -> Q = 1/sqrt(2)

    for (float cutoffHz : { 200.0f, 1000.0f, 5000.0f })
    {
        INFO ("cutoff = " << cutoffHz << " Hz");
        const auto gains = measureGainAtCutoff (sampleRate, cutoffHz, resonance);

        CHECK (gains.lowpass == Catch::Approx (resonance).margin (0.01));
        CHECK (gains.highpass == Catch::Approx (resonance).margin (0.01));
        CHECK (gains.bandpass == Catch::Approx (resonance).margin (0.01));
    }
}

TEST_CASE ("SvfFilter's resonant peak height scales with Q", "[engine][SvfFilter]")
{
    constexpr double sampleRate = 48000.0;
    constexpr float cutoffHz = 1000.0f;
    constexpr float resonance = 4.0f; // Q = 4 -> ~12 dB peak

    const auto gains = measureGainAtCutoff (sampleRate, cutoffHz, resonance);

    CHECK (gains.lowpass == Catch::Approx (resonance).margin (0.05f));
    CHECK (gains.highpass == Catch::Approx (resonance).margin (0.05f));
    CHECK (gains.bandpass == Catch::Approx (resonance).margin (0.02f));
}
