// wiki/plans/DataAndWavetable.md D5/D6: one node that plays a curve — the
// Oscillator (Cycle) and the Envelope (Time).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/CurvePlayerNode.h"
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    const float unwired = std::numeric_limits<float>::quiet_NaN();
    constexpr double sampleRate = 48000.0;

    /** Runs a node sample by sample through processBlock (blocks of 64), with
        `inputs` held constant except where `perSample` changes them. */
    template <typename PerSample>
    std::vector<float> render (Node& node, std::vector<float> inputs, int numSamples, PerSample perSample)
    {
        std::vector<float> out;
        out.reserve ((size_t) numSamples);
        constexpr int block = 64;
        std::vector<std::vector<float>> channels (inputs.size(), std::vector<float> (block));
        std::vector<float> output (block);
        for (int start = 0; start < numSamples; start += block)
        {
            const auto n = std::min (block, numSamples - start);
            for (int i = 0; i < n; ++i)
            {
                perSample (start + i, inputs);
                for (size_t c = 0; c < inputs.size(); ++c)
                    channels[c][(size_t) i] = inputs[c];
            }
            std::vector<const float*> in;
            for (auto& c : channels)
                in.push_back (c.data());
            float* outs[1] = { output.data() };
            node.processBlock (in.data(), outs, n);
            out.insert (out.end(), output.begin(), output.begin() + n);
        }
        return out;
    }

    std::vector<float> render (Node& node, std::vector<float> inputs, int numSamples)
    {
        return render (node, std::move (inputs), numSamples, [] (int, std::vector<float>&) {});
    }

    // Oscillator inputs: shape, frequency, amplitude, phase, trigger, loop.
    std::vector<float> oscillatorInputs (float frequency) { return { 0.0f, frequency, unwired, unwired, 0.0f, unwired }; }

    double magnitudeAt (const std::vector<float>& signal, double frequency)
    {
        std::complex<double> sum;
        for (size_t i = 0; i < signal.size(); ++i)
            sum += (double) signal[i] * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * frequency * (double) i / sampleRate);
        return std::abs (sum) / (double) signal.size();
    }
}

TEST_CASE ("The Oscillator's default sine is a sine", "[engine][curvePlayer]")
{
    CurveOscillatorNode oscillator;
    oscillator.prepare ({ sampleRate, 64 });
    const auto out = render (oscillator, oscillatorInputs (440.0f), 4800);
    for (size_t i = 0; i < out.size(); ++i)
        REQUIRE (out[i] == Catch::Approx (std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) i / sampleRate)).margin (2.0e-3));
}

TEST_CASE ("A saw played high is band-limited: nothing folds back", "[engine][curvePlayer]")
{
    CurveOscillatorNode oscillator;
    oscillator.prepare ({ sampleRate, 64 });
    REQUIRE (oscillator.setContent (CurveDocument::saw().toVar()));

    const auto out = render (oscillator, oscillatorInputs (7000.0f), 9600);
    const auto fundamental = magnitudeAt (out, 7000.0);
    CHECK (fundamental > 0.2);
    // The 4th harmonic (28 kHz) would alias to 20 kHz; the 5th (35 kHz) to 13 kHz.
    CHECK (magnitudeAt (out, 20000.0) < fundamental * 1.0e-3);
    CHECK (magnitudeAt (out, 13000.0) < fundamental * 1.0e-3);
}

TEST_CASE ("At LFO rates the curve plays exactly as drawn", "[engine][curvePlayer]")
{
    CurveOscillatorNode oscillator;
    oscillator.prepare ({ sampleRate, 64 });
    REQUIRE (oscillator.setContent (CurveDocument::square().toVar()));
    const auto out = render (oscillator, oscillatorInputs (2.0f), 24000); // one cycle
    CHECK (out[100] == 1.0f);
    CHECK (out[18000] == -1.0f);
}

TEST_CASE ("Trigger restarts the cycle; with Loop off one cycle plays and holds", "[engine][curvePlayer]")
{
    CurveOscillatorNode oscillator;
    oscillator.prepare ({ sampleRate, 64 });
    REQUIRE (oscillator.setContent (CurveDocument::saw().toVar()));

    auto inputs = oscillatorInputs (100.0f); // 480 samples per cycle
    inputs[5] = 0.0f;                        // Loop off
    const auto out = render (oscillator, inputs, 2000, [] (int i, std::vector<float>& in) { in[4] = i == 1000 ? 1.0f : 0.0f; });
    CHECK (out[700] == Catch::Approx (out[900]).margin (1.0e-6)); // held after the first cycle
    // Trigger: the cycle starts again, sample for sample.
    for (const auto i : { 20, 120, 240, 400 })
        CHECK (out[(size_t) (1000 + i)] == Catch::Approx (out[(size_t) i]).margin (1.0e-4));
}

TEST_CASE ("A shape change mid-note never clicks", "[engine][curvePlayer]")
{
    CurveOscillatorNode oscillator;
    oscillator.prepare ({ sampleRate, 64 });
    auto out = render (oscillator, oscillatorInputs (220.0f), 1000);

    auto inverted = CurveDocument::sine();
    for (auto& point : inverted.points)
        point.y = -point.y;
    REQUIRE (oscillator.setContent (inverted.toVar()));
    const auto after = render (oscillator, oscillatorInputs (220.0f), 1000);

    // Without smoothing the first sample would jump by up to 2.
    CHECK (std::abs (after[0] - out.back()) < 0.05f);
    auto largestStep = 0.0f;
    for (size_t i = 1; i < after.size(); ++i)
        largestStep = std::max (largestStep, std::abs (after[i] - after[i - 1]));
    CHECK (largestStep < 0.1f);
}

TEST_CASE ("The Envelope's straight ADSR is the old env.adsr's linear shape", "[engine][curvePlayer]")
{
    // env.adsr was juce::ADSR: straight lines up to 1, down to S, and from S to 0.
    constexpr float attack = 0.01f, decay = 0.1f, sustain = 0.5f, release = 0.2f;
    CurveEnvelopeNode envelope;
    envelope.prepare ({ sampleRate, 64 });
    REQUIRE (envelope.setContent (CurveDocument::adsr (attack, decay, sustain, release, 0.0f).toVar()));

    constexpr int releaseAt = 14400; // 300 ms
    const auto ours = render (envelope, { 0.0f, 0.0f, unwired, unwired }, 28800,
                              [] (int i, std::vector<float>& in) { in[1] = i < releaseAt ? 1.0f : 0.0f; });
    const auto expected = [&] (int i)
    {
        const auto t = (double) i / sampleRate;
        if (i >= releaseAt)
            return std::max (0.0, sustain * (1.0 - ((double) (i - releaseAt) / sampleRate) / release));
        if (t < attack)
            return t / attack;
        if (t < attack + decay)
            return 1.0 - (1.0 - sustain) * (t - attack) / decay;
        return (double) sustain;
    };
    for (int i = 0; i < (int) ours.size(); i += 16)
        REQUIRE (ours[(size_t) i] == Catch::Approx (expected (i)).margin (0.002));
    CHECK (ours[10000] == Catch::Approx (sustain).margin (1.0e-4)); // holding at S
    CHECK (ours.back() == Catch::Approx (0.0f).margin (1.0e-4));    // released
}

TEST_CASE ("An envelope without an S marker plays whole on each gate; Time Scale stretches it", "[engine][curvePlayer]")
{
    CurveDocument oneShot;
    oneShot.timeBase = CurveDocument::TimeBase::Time;
    oneShot.lengthSeconds = 0.1f;
    oneShot.points = { { 0.0f, 0.0f }, { 0.05f, 1.0f }, { 0.1f, 0.0f } };

    CurveEnvelopeNode envelope;
    envelope.prepare ({ sampleRate, 64 });
    REQUIRE (envelope.setContent (oneShot.toVar()));
    const auto gatePulse = [] (int i, std::vector<float>& in) { in[1] = i < 10 ? 1.0f : 0.0f; };
    const auto out = render (envelope, { 0.0f, 0.0f, unwired, unwired }, 9600, gatePulse);
    CHECK (out[2400] == Catch::Approx (1.0f).margin (0.01)); // peaks at 50 ms although the gate is long gone
    CHECK (out[6000] == Catch::Approx (0.0f).margin (1.0e-4));

    CurveEnvelopeNode slow;
    slow.prepare ({ sampleRate, 64 });
    REQUIRE (slow.setContent (oneShot.toVar()));
    const auto stretched = render (slow, { 0.0f, 0.0f, unwired, 2.0f }, 9600, gatePulse);
    CHECK (stretched[4800] == Catch::Approx (1.0f).margin (0.01)); // twice as long
}

TEST_CASE ("A retriggered envelope starts from where it is, not from zero", "[engine][curvePlayer]")
{
    CurveEnvelopeNode envelope;
    envelope.prepare ({ sampleRate, 64 });
    REQUIRE (envelope.setContent (CurveDocument::adsr (0.05f, 0.1f, 0.6f, 0.3f, 0.0f).toVar()));
    // Gate on, off during the release, on again.
    const auto out = render (envelope, { 0.0f, 0.0f, unwired, unwired }, 24000,
                             [] (int i, std::vector<float>& in) { in[1] = (i < 9600 || i >= 12000) ? 1.0f : 0.0f; });
    CHECK (out[11999] > 0.2f);
    CHECK (std::abs (out[12001] - out[11999]) < 0.01f);
}

TEST_CASE ("A wired Shape plays the cable's curve; unplugged, the node's own again", "[engine][curvePlayer][GraphCompiler]")
{
    const auto factory = buildDefaultNodeFactory();
    const auto build = [] (bool wired)
    {
        NodeGraph graph;
        NodeInstance curve { "curve", "data.curve", {}, {}, {} };
        curve.content = CurveDocument::square().toVar();
        graph.addNode (curve);
        graph.addNode ({ "osc", "source.oscillator", {}, { { "source.oscillator.frequency", 1.0f } }, {} });
        if (wired)
            graph.addConnection ({ "curve", "curve", "osc", "shape" });
        graph.setOutput ("osc", "out");
        return graph;
    };
    const auto lastSample = [] (CompileResult& result)
    {
        for (int block = 0; block < 20; ++block) // past the ~5 ms shape-change smoothing
            result.plan.process (64);
        return result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0)[63];
    };

    auto wired = GraphCompiler::compile (build (true), factory, { sampleRate, 64 }, 1);
    REQUIRE (wired.success);
    CHECK (lastSample (wired) == 1.0f); // the square, at 1 Hz, in its first half

    auto unplugged = GraphCompiler::compile (build (false), factory, { sampleRate, 64 }, 2, &wired.plan);
    REQUIRE (unplugged.success);
    CHECK (unplugged.plan.getNodeById ("osc") == wired.plan.getNodeById ("osc")); // the same running node
    const auto value = lastSample (unplugged);
    CHECK (value > 0.0f);
    CHECK (value < 0.5f); // its own sine, early in its cycle (the phase kept running) — not the square's 1
}

TEST_CASE ("A reset envelope hears its next gate as a new note", "[engine][curvePlayer]")
{
    // A voice that is reset (its bundle deactivated and reused) while its gate
    // was high must still start on the next high gate.
    CurveEnvelopeNode envelope;
    envelope.prepare ({ sampleRate, 64 });
    REQUIRE (envelope.setContent (CurveDocument::adsr (0.01f, 0.1f, 0.8f, 0.1f, 0.0f).toVar()));
    render (envelope, { 0.0f, 1.0f, unwired, unwired }, 4800);
    envelope.reset();
    const auto after = render (envelope, { 0.0f, 1.0f, unwired, unwired }, 9600); // past attack + decay
    CHECK (after[600] > 0.9f); // it started again: past the 10 ms attack
    CHECK (after.back() == Catch::Approx (0.8f).margin (1.0e-3));
}

TEST_CASE ("A one-shot holds where its cycle ended, not where it started", "[engine][curvePlayer]")
{
    // A saw rising from -1 to 1, played once (Loop off) as an envelope-like
    // movement: after the cycle it must stay at the top.
    for (const auto frequency : { 2.0f, 200.0f }) // drawn as is, and band-limited
    {
        CurveOscillatorNode oscillator;
        oscillator.prepare ({ sampleRate, 64 });
        REQUIRE (oscillator.setContent (CurveDocument::saw().toVar()));
        auto inputs = oscillatorInputs (frequency);
        inputs[5] = 0.0f; // Loop off
        const auto cycle = (int) (sampleRate / frequency);
        const auto out = render (oscillator, inputs, cycle * 3);
        INFO (frequency);
        CHECK (out[(size_t) (cycle / 2)] == Catch::Approx (0.0f).margin (0.05f));
        CHECK (out.back() == Catch::Approx (1.0f).margin (0.02f));
    }
}

TEST_CASE ("A one-shot wavetable holds the end of its cycle too", "[engine][curvePlayer][wavetable]")
{
    WavetableDocument doc;
    doc.keyframes = { WavetableKeyframe::ofCurve (0.0f, CurveDocument::saw()) };
    const auto buffer = buildWavetableBuffer (doc);
    const WavetableView view (buffer.get());
    CHECK (view.readHeld (1.0 - 1.0e-6, 0.0f) == Catch::Approx (1.0f).margin (0.01f));
    CHECK (view.read (0, 1.0 - 1.0e-6, 0.0f) < 0.0f); // the periodic read blends back into the start
}
