#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/SmoothedParameter.h"

TEST_CASE ("SmoothedParameter ramps monotonically to its target then settles", "[engine][SmoothedParameter]")
{
    bazalt::engine::SmoothedParameter param;
    param.prepare (1000.0, 0.01); // 10ms ramp = 10 samples at 1kHz
    param.reset (0.0f);
    param.setTargetValue (1.0f);

    CHECK (param.isSmoothing());

    float last = param.getCurrentValue();
    for (int i = 0; i < 10; ++i)
    {
        const auto next = param.getNextValue();
        CHECK (next >= last);
        last = next;
    }

    CHECK_FALSE (param.isSmoothing());
    CHECK (param.getCurrentValue() == Catch::Approx (1.0f));
}

TEST_CASE ("SmoothedParameter::reset jumps immediately with no ramp", "[engine][SmoothedParameter]")
{
    bazalt::engine::SmoothedParameter param;
    param.prepare (44100.0, 0.02);
    param.reset (5.0f);

    CHECK_FALSE (param.isSmoothing());
    CHECK (param.getCurrentValue() == Catch::Approx (5.0f));
}
