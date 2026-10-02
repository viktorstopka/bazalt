// PM Core batch (wiki/NODES.Status.md's own build-next order, step 6) — the
// first real physical-modelling nodes: excite.* produces a signal that
// drives a resonator, resonator.* is the resonating body. This file grows
// batch by batch, same pattern ClockSeqNodesTests.cpp/
// DataFoundationsNodesTests.cpp/NoteStreamNodesTests.cpp already established
// for a multi-node batch landing together.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/ExciteImpulseNode.h"
#include "bazalt/engine/nodes/ResonatorCombNode.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    float runOneSample (Node& node, std::vector<float> inputs)
    {
        float output = 0.0f;
        float* outputs = &output;
        node.processSample (inputs.data(), outputs);
        return output;
    }
}

// ---- excite.impulse ----

TEST_CASE ("ExciteImpulseNode is silent until triggered", "[engine][nodes][ExciteImpulseNode][PMCore]")
{
    ExciteImpulseNode node;
    node.prepare ({ 44100.0, 512 });

    for (int i = 0; i < 100; ++i)
        CHECK (runOneSample (node, { 0.0f, kNaN, kNaN }) == 0.0f);
}

TEST_CASE ("ExciteImpulseNode's width == 0 fires exactly one sample at amplitude", "[engine][nodes][ExciteImpulseNode][PMCore]")
{
    ExciteImpulseNode node;
    node.prepare ({ 44100.0, 512 });

    CHECK (runOneSample (node, { 1.0f, 0.8f, 0.0f }) == Catch::Approx (0.8f));
    CHECK (runOneSample (node, { 0.0f, kNaN, kNaN }) == 0.0f);
    CHECK (runOneSample (node, { 0.0f, kNaN, kNaN }) == 0.0f);
}

TEST_CASE ("ExciteImpulseNode's width > 0 widens into a raised-cosine bump of the right length",
           "[engine][nodes][ExciteImpulseNode][PMCore]")
{
    ExciteImpulseNode node;
    node.prepare ({ 1000.0, 512 }); // 1kHz sample rate -> 10ms = 10 samples, easy arithmetic

    std::vector<float> samples;
    samples.push_back (runOneSample (node, { 1.0f, 1.0f, 10.0f }));
    for (int i = 1; i < 12; ++i)
        samples.push_back (runOneSample (node, { 0.0f, kNaN, kNaN }));

    // 10 non-zero samples, then back to silence.
    CHECK (samples[0] == Catch::Approx (0.0f).margin (1e-5)); // Hann window starts at 0
    CHECK (samples[10] == 0.0f); // 11th sample, past the 10-sample window
    CHECK (samples[11] == 0.0f);

    // Peak lands in the middle, roughly at amplitude.
    const auto peak = *std::max_element (samples.begin(), samples.end());
    CHECK (peak > 0.9f);
    CHECK (peak <= 1.0f + 1e-5f);
}

TEST_CASE ("ExciteImpulseNode retriggering restarts cleanly", "[engine][nodes][ExciteImpulseNode][PMCore]")
{
    ExciteImpulseNode node;
    node.prepare ({ 1000.0, 512 });

    runOneSample (node, { 1.0f, 1.0f, 10.0f }); // start a 10-sample bump
    runOneSample (node, { 0.0f, kNaN, kNaN });
    runOneSample (node, { 0.0f, kNaN, kNaN });
    // Retrigger mid-flight with width 0 — should fire a clean single-sample delta next, not
    // continue the old bump's envelope.
    const auto retriggered = runOneSample (node, { 1.0f, 0.5f, 0.0f });
    CHECK (retriggered == Catch::Approx (0.5f));
    CHECK (runOneSample (node, { 0.0f, kNaN, kNaN }) == 0.0f);
}

// ---- resonator.comb ----

TEST_CASE ("ResonatorCombNode is silent on silent input", "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode node;
    node.prepare ({ 44100.0, 512 });

    for (int i = 0; i < 1000; ++i)
        CHECK (runOneSample (node, { 0.0f, kNaN, kNaN, kNaN }) == 0.0f);
}

TEST_CASE ("ResonatorCombNode feedforward mode: y[n] = x[n] + g*x[n-M], exact arithmetic",
           "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode node;
    node.prepare ({ 1000.0, 512 }); // 1kHz sample rate
    node.setParameter ("resonator.comb.type", 0.0f); // feedforward
    node.setParameter ("resonator.comb.damping", 1.0f); // no smoothing — damped(x) == x exactly

    // frequency = 100Hz -> delaySamples = 1000/100 = 10.
    const float frequency = 100.0f;
    const float feedback = 0.6f;

    std::vector<float> input (30, 0.0f);
    input[0] = 1.0f;
    input[5] = 0.5f;

    std::vector<float> output;
    for (auto x : input)
        output.push_back (runOneSample (node, { x, frequency, feedback, kNaN }));

    // y[0] = x[0] (tap is 0, buffer was empty)
    CHECK (output[0] == Catch::Approx (1.0f));
    // y[10] = x[10] (0) + 0.6 * x[0] (1.0) = 0.6
    CHECK (output[10] == Catch::Approx (0.6f));
    // y[15] = x[15] (0) + 0.6 * x[5] (0.5) = 0.3
    CHECK (output[15] == Catch::Approx (0.3f));
    // Feedforward never re-reads its own output — y[20] should be 0, not
    // influenced by y[10]'s own non-zero value (that's what distinguishes
    // it from feedback mode).
    CHECK (output[20] == Catch::Approx (0.0f).margin (1e-6f));
}

TEST_CASE ("ResonatorCombNode feedback mode rings periodically at the set frequency",
           "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode node;
    node.prepare ({ 1000.0, 512 });
    node.setParameter ("resonator.comb.type", 1.0f); // feedback
    node.setParameter ("resonator.comb.damping", 1.0f); // no smoothing, isolate the periodicity

    const float frequency = 100.0f; // delaySamples = 10
    const float feedback = 0.5f;

    std::vector<float> output;
    output.push_back (runOneSample (node, { 1.0f, frequency, feedback, kNaN })); // single impulse
    for (int i = 1; i < 41; ++i)
        output.push_back (runOneSample (node, { 0.0f, frequency, feedback, kNaN }));

    // Energy should reappear every 10 samples, scaled by `feedback` each
    // round trip: output[0]=1, output[10]=0.5, output[20]=0.25, output[30]=0.125.
    CHECK (output[0] == Catch::Approx (1.0f));
    CHECK (output[10] == Catch::Approx (0.5f));
    CHECK (output[20] == Catch::Approx (0.25f));
    CHECK (output[30] == Catch::Approx (0.125f));

    // Samples strictly between the echoes are silent (no smearing without damping).
    CHECK (output[5] == Catch::Approx (0.0f).margin (1e-6f));
    CHECK (output[15] == Catch::Approx (0.0f).margin (1e-6f));
}

TEST_CASE ("ResonatorCombNode's feedback is hard-limited - stays bounded over a long run even if over-driven",
           "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("resonator.comb.type", 1.0f); // feedback — the mode that could actually diverge

    const auto excited = runOneSample (node, { 1.0f, 440.0f, 5.0f, 0.5f }); // feedback way over 1
    juce::ignoreUnused (excited);

    for (int i = 0; i < 44100 * 2; ++i)
    {
        const auto y = runOneSample (node, { 0.0f, 440.0f, 5.0f, 0.5f });
        REQUIRE (std::isfinite (y));
        REQUIRE (std::fabs (y) < 1000.0f); // nowhere near divergence; a real runaway would be enormous
    }
}

TEST_CASE ("ResonatorCombNode's damping follows filter.onepole's own convention: 0 = darkest, 1 = brightest",
           "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode brightNode, darkNode;
    brightNode.prepare ({ 1000.0, 512 });
    darkNode.prepare ({ 1000.0, 512 });
    brightNode.setParameter ("resonator.comb.type", 1.0f);
    darkNode.setParameter ("resonator.comb.type", 1.0f);
    brightNode.setParameter ("resonator.comb.damping", 1.0f);
    darkNode.setParameter ("resonator.comb.damping", 0.0f);

    const float frequency = 100.0f;
    const float feedback = 0.9f;

    runOneSample (brightNode, { 1.0f, frequency, feedback, kNaN });
    runOneSample (darkNode, { 1.0f, frequency, feedback, kNaN });

    for (int i = 0; i < 9; ++i)
    {
        runOneSample (brightNode, { 0.0f, frequency, feedback, kNaN });
        runOneSample (darkNode, { 0.0f, frequency, feedback, kNaN });
    }

    // At the first echo (sample 10): bright (damping=1, no smoothing) passes
    // the full impulse through at feedback*1 = 0.9. Dark (damping=0, the
    // in-loop filter state never moves off its initial 0) delivers nothing.
    const auto brightEcho = runOneSample (brightNode, { 0.0f, frequency, feedback, kNaN });
    const auto darkEcho = runOneSample (darkNode, { 0.0f, frequency, feedback, kNaN });

    CHECK (brightEcho == Catch::Approx (0.9f));
    CHECK (darkEcho == Catch::Approx (0.0f).margin (1e-6f));
}
