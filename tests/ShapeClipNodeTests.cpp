// shape.clip - direct feedback: a real, wireable, mid-chain safety limiter
// ("the node you put in a feedback loop so a slider can't destroy a
// speaker"), complementing the plugin's own always-on master-output
// OutputLimiter (which can't be inserted mid-chain at all).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/ShapeClipNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    // A symmetric ±ceiling range (the old Clip) through the merged Low/High ports.
    std::pair<float, float> runOneSample (Node& node, float in, float ceiling = kNaN, float knee = kNaN)
    {
        const float inputs[4] = { in, std::isnan (ceiling) ? kNaN : -ceiling, ceiling, knee };
        float outputs[2] = { 0.0f, 0.0f };
        node.processSample (inputs, outputs);
        return { outputs[0], outputs[1] };
    }
}

TEST_CASE ("ShapeClipNode (hard mode) passes a signal well under the ceiling through unchanged",
           "[engine][nodes][ShapeClipNode]")
{
    ShapeClipNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("shape.clip.knee", 0.0f);

    const auto [out, clipping] = runOneSample (node, 0.5f, 1.0f, 0.0f);
    CHECK (out == Catch::Approx (0.5f));
    CHECK (clipping == 0.0f);
}

TEST_CASE ("ShapeClipNode (hard mode, knee=0) clamps exactly at the ceiling", "[engine][nodes][ShapeClipNode]")
{
    ShapeClipNode node;
    node.prepare ({ 44100.0, 512 });

    CHECK (runOneSample (node, 5.0f, 1.0f, 0.0f).first == Catch::Approx (1.0f));
    CHECK (runOneSample (node, -5.0f, 1.0f, 0.0f).first == Catch::Approx (-1.0f));
    CHECK (runOneSample (node, 5.0f, 1.0f, 0.0f).second == 1.0f); // clipping == true
}

TEST_CASE ("ShapeClipNode (hard mode) with a knee smoothly pulls down before the hard ceiling",
           "[engine][nodes][ShapeClipNode]")
{
    ShapeClipNode node;
    node.prepare ({ 44100.0, 512 });

    // ceiling=1.0, knee=0.4 -> kneeWidth=0.4, halfKnee=0.2, region [0.8, 1.2].
    const auto belowKnee = runOneSample (node, 0.7f, 1.0f, 0.4f);
    CHECK (belowKnee.first == Catch::Approx (0.7f)); // untouched, below the knee region
    CHECK (belowKnee.second == 0.0f);

    const auto inKnee = runOneSample (node, 0.9f, 1.0f, 0.4f);
    CHECK (inKnee.first < 0.9f);       // pulled down...
    CHECK (inKnee.first > 0.8f);       // ...but not all the way to the lower bound
    CHECK (inKnee.second == 1.0f);

    const auto aboveKnee = runOneSample (node, 2.0f, 1.0f, 0.4f);
    CHECK (aboveKnee.first == Catch::Approx (1.0f)); // fully clamped past the knee's own upper bound
}

TEST_CASE ("ShapeClipNode (soft mode) never hard-clips - asymptotically approaches the ceiling",
           "[engine][nodes][ShapeClipNode]")
{
    ShapeClipNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("shape.clip.mode", 1.0f); // soft
    node.setParameter ("shape.clip.knee", 1.0f); // fully curved from zero

    const auto small = runOneSample (node, 0.1f, 1.0f, 1.0f);
    CHECK (small.first < 0.1f); // tanh(x) < x for x > 0 - even a small signal is very slightly shaped

    // Not 100.0 - tanh() genuinely saturates to EXACTLY 1.0f in float32
    // precision well before that (correct behavior, asymptotic in real
    // math), which would make a strict "< ceiling" assertion fail for the
    // wrong reason. 5.0 is loud enough to be deep into the curve without
    // having fully saturated in float precision yet.
    const auto loud = runOneSample (node, 5.0f, 1.0f, 1.0f);
    CHECK (loud.first < 1.0f);
    CHECK (loud.first > 0.95f); // deep into saturation, close to the ceiling
    REQUIRE (std::isfinite (loud.first));
}

TEST_CASE ("ShapeClipNode (soft mode) stays linear below kneeStart when knee < 1", "[engine][nodes][ShapeClipNode]")
{
    ShapeClipNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("shape.clip.mode", 1.0f); // soft
    node.setParameter ("shape.clip.knee", 0.2f); // kneeStart = ceiling*(1-0.2) = 0.8

    const auto linear = runOneSample (node, 0.5f, 1.0f, 0.2f);
    CHECK (linear.first == Catch::Approx (0.5f)); // below kneeStart - untouched
    CHECK (linear.second == 0.0f);

    const auto curved = runOneSample (node, 0.95f, 1.0f, 0.2f);
    CHECK (curved.first < 0.95f); // above kneeStart - shaped
    CHECK (curved.second == 1.0f);
}

TEST_CASE ("ShapeClipNode (limiter mode) settles near the ceiling under sustained loud input",
           "[engine][nodes][ShapeClipNode]")
{
    ShapeClipNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("shape.clip.mode", 2.0f); // limiter
    node.setParameter ("shape.clip.knee", 0.0f);

    // Even the very first sample is safety-clamped immediately (the hard
    // backstop under the smoothed envelope - see the node's own comment),
    // so the OBSERVABLE first-sample value matches hard mode here; the real
    // difference between the two modes is the RELEASE LAG, tested below.
    const auto firstSample = runOneSample (node, 3.0f, 1.0f, 0.0f);
    CHECK (firstSample.first == Catch::Approx (1.0f));

    float lastOut = 0.0f;
    for (int i = 0; i < 2000; ++i)
        lastOut = runOneSample (node, 3.0f, 1.0f, 0.0f).first;
    CHECK (lastOut == Catch::Approx (1.0f).margin (0.02f));
}

TEST_CASE ("ShapeClipNode (limiter mode) remembers a loud burst: gain reduction lingers into the quiet signal right after it, unlike hard mode",
           "[engine][nodes][ShapeClipNode]")
{
    ShapeClipNode limiter, hard;
    limiter.prepare ({ 44100.0, 512 });
    hard.prepare ({ 44100.0, 512 });
    limiter.setParameter ("shape.clip.mode", 2.0f); // limiter
    hard.setParameter ("shape.clip.mode", 0.0f);    // hard
    limiter.setParameter ("shape.clip.knee", 0.0f);
    hard.setParameter ("shape.clip.knee", 0.0f);

    // Drive both hard with a loud sustained burst.
    for (int i = 0; i < 2000; ++i)
    {
        runOneSample (limiter, 3.0f, 1.0f, 0.0f);
        runOneSample (hard, 3.0f, 1.0f, 0.0f);
    }

    // Immediately after, a QUIET signal well under the ceiling - hard mode
    // (purely per-sample, no memory) passes it through completely
    // unaffected; limiter mode's gain reduction is still recovering
    // (release ~100ms) and measurably attenuates it for a moment.
    const auto quietLimiter = runOneSample (limiter, 0.2f, 1.0f, 0.0f).first;
    const auto quietHard = runOneSample (hard, 0.2f, 1.0f, 0.0f).first;

    CHECK (quietHard == Catch::Approx (0.2f));
    CHECK (quietLimiter < quietHard);
}

TEST_CASE ("ShapeClipNode's range and knee fall back to setParameter()-set values when unconnected",
           "[engine][nodes][ShapeClipNode]")
{
    ShapeClipNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("shape.clip.low", -0.5f);
    node.setParameter ("shape.clip.high", 0.5f);
    node.setParameter ("shape.clip.knee", 0.0f);

    CHECK (runOneSample (node, 2.0f, kNaN, kNaN).first == Catch::Approx (0.5f));
}

TEST_CASE ("ShapeClipNode stays finite and bounded for extreme inputs in every mode", "[engine][nodes][ShapeClipNode]")
{
    for (float modeValue : { 0.0f, 1.0f, 2.0f })
    {
        ShapeClipNode node;
        node.prepare ({ 44100.0, 512 });
        node.setParameter ("shape.clip.mode", modeValue);

        for (float in : { 1000.0f, -1000.0f, 0.0f, 1e6f })
        {
            const auto [out, clipping] = runOneSample (node, in, 1.0f, 0.3f);
            juce::ignoreUnused (clipping);
            REQUIRE (std::isfinite (out));
            REQUIRE (std::fabs (out) <= 1.001f); // a hair of float slack, never meaningfully past the ceiling
        }
    }
}

// wiki/plans/DataAndWavetable.md §2: Clamp merged into Clip. Hard mode with
// knee 0 over an arbitrary Low..High range is exactly the old Clamp.
TEST_CASE ("ShapeClipNode clamps exactly to an asymmetric Low..High range (the old Clamp), swapped if Low > High",
           "[engine][nodes][ShapeClipNode][sweep]")
{
    ShapeClipNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("shape.clip.knee", 0.0f);

    auto clampOf = [&] (float in, float low, float high)
    {
        const float inputs[4] = { in, low, high, kNaN };
        float outputs[2] = { 0.0f, 0.0f };
        node.processSample (inputs, outputs);
        return outputs[0];
    };

    CHECK (clampOf (0.5f, 0.0f, 1.0f) == Catch::Approx (0.5f));
    CHECK (clampOf (-1.0f, 0.0f, 1.0f) == Catch::Approx (0.0f));
    CHECK (clampOf (2.0f, 0.0f, 1.0f) == Catch::Approx (1.0f));
    CHECK (clampOf (0.5f, 1.0f, 0.0f) == Catch::Approx (0.5f));
    CHECK (clampOf (5000.0f, 200.0f, 2000.0f) == Catch::Approx (2000.0f)); // a frequency range
    CHECK (clampOf (50.0f, 200.0f, 2000.0f) == Catch::Approx (200.0f));
}

TEST_CASE ("ShapeClipNode takes on the type and quantity of what feeds it",
           "[engine][nodes][ShapeClipNode][sweep][inheriting]")
{
    auto port = [] (const std::vector<PortDescriptor>& ports, const juce::String& id)
    {
        for (const auto& p : ports)
            if (p.id == id)
                return p;
        return PortDescriptor {};
    };

    ShapeClipNode node;
    CHECK (port (node.getInputPorts(), "in").quantity == Quantity::Audio); // the default: an audio safety clip

    PortDescriptor frequency { .id = "src", .type = SignalType::Signal };
    frequency.quantity = Quantity::Frequency;
    node.resolveIncomingPort ("in", frequency);
    CHECK (port (node.getInputPorts(), "in").type == SignalType::Signal);
    CHECK (port (node.getInputPorts(), "in").quantity == Quantity::Frequency);
    CHECK (port (node.getInputPorts(), "shape.clip.low").quantity == Quantity::Frequency);
    CHECK (port (node.getInputPorts(), "shape.clip.high").quantity == Quantity::Frequency);
    CHECK (port (node.getOutputPorts(), "out").quantity == Quantity::Frequency);
}
