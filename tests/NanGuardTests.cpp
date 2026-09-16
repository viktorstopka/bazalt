#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/NanGuard.h"
#include <limits>
#include <cmath>

TEST_CASE ("NanGuard replaces NaN and Inf with silence and raises the fault flag", "[engine][NanGuard]")
{
    bazalt::engine::NanGuard guard;

    juce::AudioBuffer<float> buffer (2, 8);
    buffer.clear();

    buffer.setSample (0, 2, std::numeric_limits<float>::quiet_NaN());
    buffer.setSample (0, 5, std::numeric_limits<float>::infinity());
    buffer.setSample (1, 1, -std::numeric_limits<float>::infinity());

    CHECK_FALSE (guard.consumeFaultFlag());

    guard.process (buffer);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            CHECK (std::isfinite (buffer.getSample (ch, i)));

    CHECK (guard.consumeFaultFlag());
    CHECK_FALSE (guard.consumeFaultFlag()); // flag clears after being consumed
}

TEST_CASE ("NanGuard leaves finite audio untouched and doesn't raise a false fault", "[engine][NanGuard]")
{
    bazalt::engine::NanGuard guard;

    juce::AudioBuffer<float> buffer (1, 4);
    buffer.setSample (0, 0, 0.5f);
    buffer.setSample (0, 1, -0.25f);
    buffer.setSample (0, 2, 1.0f);
    buffer.setSample (0, 3, -1.0f);

    guard.process (buffer);

    CHECK (buffer.getSample (0, 0) == 0.5f);
    CHECK (buffer.getSample (0, 1) == -0.25f);
    CHECK (buffer.getSample (0, 2) == 1.0f);
    CHECK (buffer.getSample (0, 3) == -1.0f);

    CHECK_FALSE (guard.consumeFaultFlag());
}
