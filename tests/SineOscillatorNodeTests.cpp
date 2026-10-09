// M22 wave 1: source.sine (was osc.sine) — a cheap, exact sine for FM/modal-excitation inner
// loops. Mechanism tests (matches ModulatablePortsTests.cpp's own idiom for
// osc.analog) plus one analytical frequency check, since a direct
// phase-accumulator sine has no band-limiting to make analytical
// verification hard the way PolyBlepOscillator's does.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/SineOscillatorNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

TEST_CASE ("SineOscillatorNode with nothing wired to frequency sounds at its displayed 440 Hz default",
           "[engine][nodes][SineOscillatorNode][M22]")
{
    // Same real bug class M21 found in osc.analog: a node whose card displays
    // a default must actually start there, not at silent 0 Hz - this is the
    // regression test for that pattern applied to the new node.
    NodePrepareInfo info { 44100.0, 4410 };
    SineOscillatorNode osc;
    osc.prepare (info);

    float peak = 0.0f;
    int crossings = 0;
    float previous = 0.0f;

    for (int i = 0; i < 4410; ++i) // 0.1s
    {
        float inputs[4] = { kNaN, 1.0f, 0.0f, 0.0f };
        float out = 0.0f;
        osc.processSample (inputs, &out);
        peak = std::max (peak, std::fabs (out));
        if (i > 0 && (previous < 0.0f) != (out < 0.0f))
            ++crossings;
        previous = out;
    }

    CHECK (peak > 0.9f); // a sine reaches near unity
    CHECK (crossings >= 84); // ~2*44 zero crossings at 440 Hz over 0.1s
    CHECK (crossings <= 92);
}

TEST_CASE ("SineOscillatorNode's frequency port live-modulates instead of only reading setParameter's static value",
           "[engine][nodes][SineOscillatorNode][M22]")
{
    NodePrepareInfo info { 44100.0, 64 };

    SineOscillatorNode low;
    low.prepare (info);
    SineOscillatorNode high;
    high.prepare (info);

    float lowOut = 0.0f, highOut = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        float lowIn[4] = { 100.0f, 1.0f, 0.0f, 0.0f };
        float highIn[4] = { 8000.0f, 1.0f, 0.0f, 0.0f };
        low.processSample (lowIn, &lowOut);
        high.processSample (highIn, &highOut);
    }

    CHECK (lowOut != highOut);
}

TEST_CASE ("SineOscillatorNode matches an analytical sine at a fixed frequency", "[engine][nodes][SineOscillatorNode][M22]")
{
    constexpr double sampleRate = 44100.0;
    constexpr float frequencyHz = 1000.0f;
    NodePrepareInfo info { sampleRate, 512 };

    SineOscillatorNode osc;
    osc.prepare (info);

    for (int i = 0; i < 200; ++i)
    {
        float inputs[4] = { frequencyHz, 1.0f, 0.0f, 0.0f };
        float out = 0.0f;
        osc.processSample (inputs, &out);

        const auto expected = std::sin (2.0 * juce::MathConstants<double>::pi * (double) frequencyHz * (double) i / sampleRate);
        CHECK (out == Catch::Approx ((float) expected).margin (1.0e-4f));
    }
}

TEST_CASE ("SineOscillatorNode's sync resets phase to zero", "[engine][nodes][SineOscillatorNode][M22]")
{
    NodePrepareInfo info { 44100.0, 64 };
    SineOscillatorNode osc;
    osc.prepare (info);

    // Advance partway through a cycle first.
    for (int i = 0; i < 5; ++i)
    {
        float inputs[4] = { 1000.0f, 1.0f, 0.0f, 0.0f };
        float out = 0.0f;
        osc.processSample (inputs, &out);
    }

    float syncInputs[4] = { 1000.0f, 1.0f, 0.0f, 1.0f }; // sync fires
    float afterSync = 0.0f;
    osc.processSample (syncInputs, &afterSync);

    // Freshly reset then advanced by one increment should equal a fresh
    // oscillator's very first sample at the same frequency.
    SineOscillatorNode fresh;
    fresh.prepare (info);
    float freshInputs[4] = { 1000.0f, 1.0f, 0.0f, 0.0f };
    float freshOut = 0.0f;
    fresh.processSample (freshInputs, &freshOut);

    CHECK (afterSync == Catch::Approx (freshOut).margin (1.0e-6f));
}

TEST_CASE ("SineOscillatorNode's Phase port offsets the read point without detuning the running phase",
           "[engine][nodes][SineOscillatorNode][M22]")
{
    NodePrepareInfo info { 44100.0, 64 };
    SineOscillatorNode modulated;
    modulated.prepare (info);
    SineOscillatorNode plain;
    plain.prepare (info);

    // Modulate for a while, then stop modulating (Phase back to 0).
    for (int i = 0; i < 50; ++i)
    {
        float modIn[4] = { 500.0f, 1.0f, 0.25f, 0.0f };
        float plainIn[4] = { 500.0f, 1.0f, 0.0f, 0.0f };
        float m = 0.0f, p = 0.0f;
        modulated.processSample (modIn, &m);
        plain.processSample (plainIn, &p);
        CHECK (m != p); // Phase is actually offsetting the read
    }

    // With modulation removed, both oscillators' underlying phase accumulators
    // advanced by the exact same amount each sample (Phase never touched
    // the accumulator itself), so they must now read identically.
    float modIn[4] = { 500.0f, 1.0f, 0.0f, 0.0f };
    float plainIn[4] = { 500.0f, 1.0f, 0.0f, 0.0f };
    float m = 0.0f, p = 0.0f;
    modulated.processSample (modIn, &m);
    plain.processSample (plainIn, &p);
    CHECK (m == Catch::Approx (p).margin (1.0e-6f));
}

// ---- 2026-10-04: shared port set (Amplitude, inline Phase) and the three
// band-limited siblings osc.saw / osc.square / osc.triangle ----------------

TEST_CASE ("Amplitude scales the output, and an unwired port uses its inline value",
           "[engine][nodes][SineOscillatorNode]")
{
    NodePrepareInfo info { 44100.0, 64 };
    SineOscillatorNode full, half;
    full.prepare (info);
    half.prepare (info);
    half.setParameter ("source.sine.amplitude", 0.5f);

    for (int i = 0; i < 20; ++i)
    {
        float in[4] = { 1000.0f, kNaN, kNaN, 0.0f }; // amplitude/phase unwired
        float a = 0.0f, b = 0.0f;
        full.processSample (in, &a);
        half.processSample (in, &b);
        CHECK (b == Catch::Approx (a * 0.5f).margin (1.0e-6f));
    }
}

TEST_CASE ("All four oscillators share one port layout; only Square adds Pulse Width",
           "[engine][nodes][SineOscillatorNode]")
{
    auto ids = [] (const BasicOscillatorNode& node)
    {
        juce::StringArray result;
        for (const auto& port : node.getInputPorts())
            result.add (port.id);
        CHECK ((int) node.getInputPorts().size() == node.getNumInputPorts());
        return result.joinIntoString (",");
    };
    CHECK (ids (SineOscillatorNode {}) == "source.sine.frequency,source.sine.amplitude,source.sine.phase,sync");
    CHECK (ids (SawOscillatorNode {}) == "source.saw.frequency,source.saw.amplitude,source.saw.phase,sync");
    CHECK (ids (TriangleOscillatorNode {}) == "source.triangle.frequency,source.triangle.amplitude,source.triangle.phase,sync");
    CHECK (ids (SquareOscillatorNode {}) == "source.square.frequency,source.square.amplitude,source.square.phase,source.square.pulseWidth,sync");

    const auto phase = SawOscillatorNode {}.getInputPorts()[2];
    CHECK (phase.quantity == Quantity::Bipolar); // a Modulation row
    CHECK (phase.hasFallbackWhenUnconnected);    // with its own inline value
    CHECK (SawOscillatorNode {}.getInputPorts()[3].type == SignalType::Event);
}

TEST_CASE ("Saw, Square and Triangle are band-limited: much closer to the ideal Nyquist-truncated waveform than naive",
           "[engine][nodes][SineOscillatorNode][bandlimited]")
{
    // 2205 Hz at 44.1 kHz: exactly 20 samples per cycle, only 9 harmonics
    // fit below Nyquist — the regime where a naive ramp/step aliases badly.
    constexpr double sampleRate = 44100.0, frequency = 2205.0;

    // The ideal is each shape's Fourier series truncated at Nyquist, phase-
    // aligned with the node's own t = 0. Band-limiting must cut the naive
    // shape's distance from it substantially.
    auto naiveError = [&] (double (*naive) (double), double (*ideal) (double))
    {
        const auto period = (int) std::lround (sampleRate / frequency);
        double sumSquares = 0.0;
        for (int i = 0; i < period; ++i)
        {
            const auto t = (double) i / period;
            sumSquares += std::pow (naive (t) - ideal (t), 2.0);
        }
        return std::sqrt (sumSquares / period);
    };

    static auto idealOf = [] (double t, int shape)
    {
        const auto maxHarmonic = (int) (44100.0 / 2.0 / 2205.0);
        double v = 0.0;
        for (int k = 1; k <= maxHarmonic; ++k)
        {
            const auto w = juce::MathConstants<double>::twoPi * k * t;
            if (shape == 0) v += -2.0 / (juce::MathConstants<double>::pi * k) * std::sin (w);                         // saw 2t-1
            if (shape == 1 && k % 2 == 1) v += 4.0 / (juce::MathConstants<double>::pi * k) * std::sin (w);           // square, high first half
            if (shape == 2 && k % 2 == 1) v += 8.0 / (juce::MathConstants<double>::pi * juce::MathConstants<double>::pi * k * k) * std::cos (w); // triangle, 1 at t=0
        }
        return v;
    };

    // A naive triangle already aliases little (harmonics fall as 1/k^2, not
    // 1/k), so its required improvement is smaller.
    struct Case { BasicWaveform waveform; double (*naive) (double); double (*ideal) (double); double maxRatio; };
    const Case cases[] = {
        { BasicWaveform::Saw, [] (double t) { return 2.0 * t - 1.0; }, [] (double t) { return idealOf (t, 0); }, 0.6 },
        { BasicWaveform::Square, [] (double t) { return t < 0.5 ? 1.0 : -1.0; }, [] (double t) { return idealOf (t, 1); }, 0.6 },
        { BasicWaveform::Triangle, [] (double t) { return 4.0 * std::fabs (t - 0.5) - 1.0; }, [] (double t) { return idealOf (t, 2); }, 0.8 },
    };

    for (const auto& c : cases)
    {
        BasicOscillatorNode node (c.waveform);
        node.prepare ({ sampleRate, 512 });
        const auto period = (int) std::lround (sampleRate / frequency);
        const auto hasPulseWidth = node.getNumInputPorts() == 5;
        double sumSquares = 0.0;
        for (int i = 0; i < period; ++i)
        {
            float in[5] = { (float) frequency, 1.0f, 0.0f, hasPulseWidth ? 0.5f : 0.0f, 0.0f };
            float out = 0.0f;
            node.processSample (in, &out);
            sumSquares += std::pow (out - c.ideal ((double) i / period), 2.0);
        }
        const auto bandLimitedError = std::sqrt (sumSquares / period);
        INFO (node.getTitle() << ": band-limited " << bandLimitedError << " vs naive " << naiveError (c.naive, c.ideal));
        CHECK (bandLimitedError < c.maxRatio * naiveError (c.naive, c.ideal));
    }
}

TEST_CASE ("Square's pulse width sets its duty cycle", "[engine][nodes][SineOscillatorNode]")
{
    SquareOscillatorNode node;
    node.prepare ({ 44100.0, 512 });
    int high = 0;
    constexpr int samples = 44100; // 100 Hz, 1 s: 100 whole cycles
    for (int i = 0; i < samples; ++i)
    {
        float in[5] = { 100.0f, 1.0f, 0.0f, 0.25f, 0.0f };
        float out = 0.0f;
        node.processSample (in, &out);
        if (out > 0.0f)
            ++high;
    }
    CHECK ((double) high / samples == Catch::Approx (0.25).margin (0.01));
}
