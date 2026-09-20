#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/CrossfadeNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

TEST_CASE ("CrossfadeNode blends a and b by position under either law", "[engine][nodes][mix][M21]")
{
    CrossfadeNode node;
    float out = 0.0f;

    auto mixOf = [&] (float a, float b, float position)
    {
        const float inputs[3] = { a, b, position };
        node.processSample (inputs, &out);
        return out;
    };

    // Linear (the default): unity-sum gains.
    CHECK (mixOf (1.0f, 0.0f, 0.0f) == 1.0f);
    CHECK (mixOf (1.0f, 0.0f, 1.0f) == 0.0f);
    CHECK (mixOf (0.0f, 1.0f, 1.0f) == 1.0f);
    CHECK (mixOf (0.8f, 0.4f, 0.5f) == Catch::Approx (0.6f));
    CHECK (mixOf (1.0f, 1.0f, 0.25f) == Catch::Approx (1.0f));

    // Switching the law at the SAME position must change the result — the
    // cached gain pair has to be invalidated, not silently reused.
    const auto linearAtHalf = mixOf (1.0f, 1.0f, 0.5f);
    node.setParameter ("mix.crossfade.law", 1.0f);
    const auto equalPowerAtHalf = mixOf (1.0f, 1.0f, 0.5f);
    CHECK (linearAtHalf == Catch::Approx (1.0f));
    CHECK (equalPowerAtHalf == Catch::Approx (std::sqrt (2.0f))); // cos(pi/4) + sin(pi/4)

    // Equal power: the two gains carry constant power along the whole sweep.
    for (const auto position : { 0.0f, 0.3f, 0.5f, 0.8f, 1.0f })
    {
        const auto gainA = mixOf (1.0f, 0.0f, position);
        const auto gainB = mixOf (0.0f, 1.0f, position);
        CHECK (gainA * gainA + gainB * gainB == Catch::Approx (1.0f));
    }
    CHECK (mixOf (1.0f, 0.0f, 0.0f) == Catch::Approx (1.0f));
    CHECK (mixOf (0.0f, 1.0f, 1.0f) == Catch::Approx (1.0f));
}

TEST_CASE ("CrossfadeNode clamps position, and an unconnected position falls back to its stored value",
           "[engine][nodes][mix][M21]")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN(); // GraphCompiler's unconnected-fallback sentinel

    CrossfadeNode node;
    float out = 0.0f;

    float inputs[3] = { 1.0f, 0.0f, 2.0f }; // out-of-range position clamps to 1 -> all b
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);

    inputs[2] = -3.0f; // -> 0 -> all a
    node.processSample (inputs, &out);
    CHECK (out == 1.0f);

    inputs[2] = nan; // unconnected: the stored default, 0.5
    node.processSample (inputs, &out);
    CHECK (out == Catch::Approx (0.5f));

    node.setParameter ("mix.crossfade.position", 1.0f);
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);
}
