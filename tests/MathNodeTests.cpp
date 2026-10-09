#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/SubtractNode.h"
#include "bazalt/engine/nodes/DivideNode.h"
#include "bazalt/engine/nodes/AbsNode.h"
#include "bazalt/engine/nodes/MinMaxNode.h"
#include "bazalt/engine/nodes/PowerNode.h"
#include "bazalt/engine/nodes/ModuloNode.h"
#include "bazalt/engine/nodes/SlewNode.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    // GraphCompiler's "this fallback port is unconnected" sentinel.
    const float unconnectedSentinel = std::numeric_limits<float>::quiet_NaN();
}

TEST_CASE ("SubtractNode computes a - b, and order matters", "[engine][nodes][math][M21]")
{
    SubtractNode node;
    float out = 0.0f;

    float inputs[2] = { 5.0f, 3.0f };
    node.processSample (inputs, &out);
    CHECK (out == 2.0f);

    inputs[0] = 3.0f;
    inputs[1] = 5.0f;
    node.processSample (inputs, &out);
    CHECK (out == -2.0f);
}

TEST_CASE ("DivideNode divides, and safeZero turns a divide-by-zero into 0 instead of NaN/inf",
           "[engine][nodes][math][M21]")
{
    DivideNode node;
    float out = 0.0f;

    float inputs[2] = { 6.0f, 3.0f };
    node.processSample (inputs, &out);
    CHECK (out == 2.0f);

    inputs[1] = 0.0f; // safeZero defaults to on
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);

    inputs[0] = 0.0f; // 0/0 is also safe
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);

    // Turned off: plain IEEE, on purpose.
    node.setParameter ("math.divide.safeZero", 0.0f);
    inputs[0] = 1.0f;
    node.processSample (inputs, &out);
    CHECK (std::isinf (out));
    inputs[0] = 0.0f;
    node.processSample (inputs, &out);
    CHECK (std::isnan (out));

    // Back on.
    node.setParameter ("math.divide.safeZero", 1.0f);
    inputs[0] = 1.0f;
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);
}

TEST_CASE ("DivideNode's safeZero is an editable parameter, not a port that would get a dot with no control",
           "[engine][nodes][math][M21]")
{
    const DivideNode node;
    CHECK (node.getInputPorts().size() == 2); // a, b only

    const auto parameters = node.getParameters();
    REQUIRE (parameters.size() == 1);
    CHECK (parameters[0].id == "math.divide.safeZero");
    CHECK (parameters[0].kind == ValueKind::Bool);
    CHECK (parameters[0].defaultValue == 1.0f);
}

TEST_CASE ("AbsNode outputs the magnitude", "[engine][nodes][math][M21]")
{
    AbsNode node;
    float out = 0.0f;

    float in = -0.75f;
    node.processSample (&in, &out);
    CHECK (out == 0.75f);

    in = 0.25f;
    node.processSample (&in, &out);
    CHECK (out == 0.25f);
}

TEST_CASE ("MinMaxNode picks the smaller by default and the larger in max mode", "[engine][nodes][math][M21]")
{
    MinMaxNode node;
    float out = 0.0f;
    const float inputs[2] = { 0.3f, -2.0f };

    node.processSample (inputs, &out);
    CHECK (out == -2.0f); // min is the default

    node.setParameter ("math.minmax.mode", 1.0f);
    node.processSample (inputs, &out);
    CHECK (out == 0.3f);

    node.setParameter ("math.minmax.mode", 0.0f);
    node.processSample (inputs, &out);
    CHECK (out == -2.0f);
}

TEST_CASE ("PowerNode shapes with its exponent, sign-preserving so a bipolar input never goes NaN",
           "[engine][nodes][math][M21]")
{
    PowerNode node;
    float out = 0.0f;

    float inputs[2] = { 0.5f, unconnectedSentinel };
    node.processSample (inputs, &out);
    CHECK (out == Catch::Approx (0.5f)); // stored exponent defaults to 1: identity

    node.setParameter ("math.power.exponent", 2.0f);
    node.processSample (inputs, &out);
    CHECK (out == Catch::Approx (0.25f));

    // std::pow(-0.5, 2.5) is NaN; the odd-symmetric extension is not.
    inputs[0] = -0.5f;
    inputs[1] = 2.0f; // a connected exponent overrides the stored one
    node.processSample (inputs, &out);
    CHECK (std::isfinite (out));
    CHECK (out == Catch::Approx (-0.25f));

    inputs[1] = 2.5f;
    node.processSample (inputs, &out);
    CHECK (std::isfinite (out));
    CHECK (out < 0.0f);

    inputs[0] = 0.0f;
    node.processSample (inputs, &out);
    CHECK (out == 0.0f);
}

TEST_CASE ("ModuloNode is floored: a negative input wraps up into [0, divisor) instead of mirroring",
           "[engine][nodes][math][M21]")
{
    ModuloNode node;
    float out = 0.0f;

    auto modOf = [&] (float in, float divisor)
    {
        const float inputs[2] = { in, divisor };
        node.processSample (inputs, &out);
        return out;
    };

    CHECK (modOf (5.5f, 2.0f) == Catch::Approx (1.5f));
    CHECK (modOf (-0.5f, 1.0f) == Catch::Approx (0.5f)); // std::fmod would give -0.5
    CHECK (modOf (-3.25f, 1.0f) == Catch::Approx (0.75f));
    CHECK (modOf (5.0f, -3.0f) == Catch::Approx (-1.0f)); // result takes the divisor's sign
    CHECK (modOf (4.0f, 2.0f) == 0.0f);

    CHECK (modOf (3.0f, 0.0f) == 0.0f); // divisor 0 -> 0, never NaN
    CHECK (modOf (unconnectedSentinel, 1.0f) == 0.0f);

    // A tiny negative input must stay strictly inside the half-open range,
    // even though float rounding wants to land it exactly on the divisor.
    const auto wrapped = modOf (-1.0e-9f, 1.0f);
    CHECK (wrapped >= 0.0f);
    CHECK (wrapped < 1.0f);

    // The divisor port falls back to its stored value when unconnected.
    node.setParameter ("math.modulo.divisor", 4.0f);
    const float unconnected[2] = { 9.0f, unconnectedSentinel };
    node.processSample (unconnected, &out);
    CHECK (out == Catch::Approx (1.0f));
}

TEST_CASE ("SlewNode lags toward its target with a per-direction time constant, at any sample rate",
           "[engine][nodes][math][M21]")
{
    const auto runTo = [] (double sampleRate, float startValue, float targetValue, float riseSeconds, float fallSeconds, float seconds)
    {
        SlewNode node;
        NodePrepareInfo info;
        info.sampleRate = sampleRate;
        node.prepare (info);
        node.setParameter ("math.slew.rise", riseSeconds);
        node.setParameter ("math.slew.fall", fallSeconds);

        float out = 0.0f;
        float inputs[3] = { startValue, unconnectedSentinel, unconnectedSentinel };
        node.processSample (inputs, &out); // first sample: adopts the input, no glide from 0

        inputs[0] = targetValue;
        const auto numSamples = (int) std::lround (seconds * sampleRate);
        for (int i = 0; i < numSamples; ++i)
            node.processSample (inputs, &out);
        return out;
    };

    // One time constant after a 1 -> 0 step is 1/e of the way left — and it
    // is one time constant of REAL time, so the sample count scales with the
    // rate rather than being a hardcoded coefficient (CLAUDE.md rule 6).
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        CHECK (runTo (rate, 1.0f, 0.0f, 0.5f, 0.010f, 0.010f) == Catch::Approx (std::exp (-1.0f)).margin (0.003));

    // Rising uses `rise`, not `fall`.
    CHECK (runTo (48000.0, 0.0f, 1.0f, 0.010f, 5.0f, 0.010f) == Catch::Approx (1.0f - std::exp (-1.0f)).margin (0.003));

    // A time of 0 in the direction of travel means jump.
    CHECK (runTo (48000.0, 0.0f, 1.0f, 0.0f, 5.0f, 0.001f) == 1.0f);
    CHECK (runTo (48000.0, 1.0f, 0.0f, 5.0f, 0.0f, 0.001f) == 0.0f);
}

TEST_CASE ("SlewNode starts at its first input, holds through NaN, and reset() re-arms that",
           "[engine][nodes][math][M21]")
{
    SlewNode node;
    node.setParameter ("math.slew.rise", 1.0f);
    node.setParameter ("math.slew.fall", 1.0f);

    float out = 0.0f;
    float inputs[3] = { 0.8f, unconnectedSentinel, unconnectedSentinel };
    node.processSample (inputs, &out);
    CHECK (out == 0.8f); // not a slow sweep up from 0 at patch start

    inputs[0] = unconnectedSentinel;
    node.processSample (inputs, &out);
    CHECK (out == 0.8f); // held; the filter state was not poisoned
    inputs[0] = 0.8f;
    node.processSample (inputs, &out);
    CHECK (std::isfinite (out));

    node.reset();
    inputs[0] = -0.5f;
    node.processSample (inputs, &out);
    CHECK (out == -0.5f);
}

TEST_CASE ("The M21 wave-1 nodes declare consistent, well-formed ports through the real factory",
           "[engine][nodes][M21]")
{
    const auto factory = buildDefaultNodeFactory();

    for (const auto* typeId : { "math.subtract", "math.divide", "math.abs", "math.minmax", "math.power",
                                "math.modulo", "math.slew", "math.blend", "logic.switch", "logic.not", "logic.toggle" })
    {
        DYNAMIC_SECTION (typeId)
        {
            const auto node = factory.create (typeId);
            REQUIRE (node != nullptr);

            const auto inputs = node->getInputPorts();
            const auto outputs = node->getOutputPorts();
            CHECK ((int) inputs.size() == node->getNumInputPorts());
            CHECK ((int) outputs.size() == node->getNumOutputPorts());
            CHECK (node->getTitle().isNotEmpty());
            CHECK (node->getCategory().isNotEmpty());

            std::set<juce::String> ids;
            for (const auto& port : inputs)
                CHECK (ids.insert (port.id).second);
            for (const auto& port : outputs)
                CHECK (ids.insert (port.id).second);

            CHECK (std::count_if (outputs.begin(), outputs.end(), [] (const auto& p) { return p.isPrimaryOutput; }) == 1);

            // Every parameter's enum options (if any) must cover its declared range.
            for (const auto& parameter : node->getParameters())
                if (parameter.kind == ValueKind::Enum)
                    CHECK ((int) parameter.enumOptions.size() == (int) parameter.maxValue - (int) parameter.minValue + 1);
        }
    }
}

TEST_CASE ("math.divide through the real compiler: safeZero set from NodeInstance::parameters decides divide-by-zero",
           "[engine][nodes][math][M21][GraphCompiler]")
{
    // Proves the parameter written by a patch/command actually reaches the
    // node via GraphCompiler's setParameter pass, not just via a direct call.
    const auto factory = buildDefaultNodeFactory();

    auto outputOf = [&] (float safeZeroParameter)
    {
        NodeGraph graph;
        graph.addNode ({ "a", "util.constant", {}, { { "util.constant.value", 6.0f } }, {} });
        graph.addNode ({ "b", "util.constant", {}, { { "util.constant.value", 0.0f } }, {} });
        graph.addNode ({ "div", "math.divide", {}, { { "math.divide.safeZero", safeZeroParameter } }, {} });
        graph.addConnection ({ "a", "out", "div", "a" });
        graph.addConnection ({ "b", "out", "div", "b" });
        graph.setOutput ("div", "out");

        auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
        REQUIRE (result.success);
        result.plan.process (8);
        return result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0)[7];
    };

    CHECK (outputOf (1.0f) == 0.0f);     // safeZero on: 6 / 0 -> 0
    CHECK (std::isinf (outputOf (0.0f))); // safeZero off: plain IEEE
}
