// A real output safety ceiling, added in response to direct feedback
// ("getting my ears blown off once every few seconds" while testing patches)
// — NanGuard alone only ever replaced non-finite samples with silence, never
// capped a loud-but-finite signal, which is exactly what an unattenuated
// resonant feedback chain (resonator.comb/modal/string/plate) can easily
// produce by accident.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/OutputLimiter.h"
#include <cmath>

using namespace bazalt::engine;

namespace
{
    juce::AudioBuffer<float> monoBufferOf (float value, int numSamples)
    {
        juce::AudioBuffer<float> buffer (1, numSamples);
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (0, i, value);
        return buffer;
    }
}

TEST_CASE ("OutputLimiter leaves a quiet signal essentially untouched", "[engine][OutputLimiter]")
{
    OutputLimiter limiter;
    limiter.prepare (44100.0);

    auto buffer = monoBufferOf (0.3f, 100);
    limiter.process (buffer);

    for (int i = 0; i < 100; ++i)
        CHECK (buffer.getSample (0, i) == Catch::Approx (0.3f).margin (0.001f));
}

TEST_CASE ("OutputLimiter reduces a sustained signal above the ceiling", "[engine][OutputLimiter]")
{
    OutputLimiter limiter;
    limiter.prepare (44100.0);

    // Well above the ceiling (0.891), sustained long enough for the ~1ms
    // attack envelope to fully catch up.
    auto buffer = monoBufferOf (2.0f, 2000);
    limiter.process (buffer);

    const auto settled = buffer.getSample (0, 1999);
    CHECK (settled <= OutputLimiter::ceiling + 0.01f);
    CHECK (settled > 0.0f); // still a real, non-zero signal - this is limiting, not killing it
}

TEST_CASE ("OutputLimiter's hard-clamp backstop catches an isolated spike the envelope hasn't reacted to yet",
           "[engine][OutputLimiter]")
{
    OutputLimiter limiter;
    limiter.prepare (44100.0);

    // A single wildly loud sample with silence around it - the envelope
    // (gainReduction starts at 1.0) can't have reduced gain in time for
    // THIS exact sample, so the final hard clamp is what has to catch it.
    juce::AudioBuffer<float> buffer (1, 3);
    buffer.setSample (0, 0, 0.0f);
    buffer.setSample (0, 1, 50.0f);
    buffer.setSample (0, 2, 0.0f);

    limiter.process (buffer);

    for (int i = 0; i < 3; ++i)
    {
        REQUIRE (std::isfinite (buffer.getSample (0, i)));
        REQUIRE (std::fabs (buffer.getSample (0, i)) <= 1.0f);
    }
}

TEST_CASE ("OutputLimiter's gain reduction recovers (releases) after the loud signal ends",
           "[engine][OutputLimiter]")
{
    OutputLimiter limiter;
    limiter.prepare (44100.0);

    auto loud = monoBufferOf (3.0f, 2000);
    limiter.process (loud); // drive gain reduction down

    auto quiet = monoBufferOf (0.1f, 20000); // ~450ms at 44100Hz - several release time constants
    limiter.process (quiet);

    // 0.1 is well under the ceiling even at unity gain - once the envelope
    // has recovered, output should equal input again.
    CHECK (quiet.getSample (0, 19999) == Catch::Approx (0.1f).margin (0.01f));
}

TEST_CASE ("OutputLimiter never lets a multi-channel buffer's peak (across all channels) exceed unity",
           "[engine][OutputLimiter]")
{
    OutputLimiter limiter;
    limiter.prepare (44100.0);

    juce::AudioBuffer<float> buffer (2, 500);
    for (int i = 0; i < 500; ++i)
    {
        buffer.setSample (0, i, 5.0f);
        buffer.setSample (1, i, -5.0f); // opposite sign, same magnitude - both channels must be caught
    }

    limiter.process (buffer);

    for (int i = 0; i < 500; ++i)
    {
        REQUIRE (std::fabs (buffer.getSample (0, i)) <= 1.0f);
        REQUIRE (std::fabs (buffer.getSample (1, i)) <= 1.0f);
    }
}
