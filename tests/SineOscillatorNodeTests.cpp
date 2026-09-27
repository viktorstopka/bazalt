// M22 wave 1: osc.sine — a cheap, exact sine for FM/modal-excitation inner
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
        float inputs[3] = { kNaN, 0.0f, 0.0f };
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
        float lowIn[3] = { 100.0f, 0.0f, 0.0f };
        float highIn[3] = { 8000.0f, 0.0f, 0.0f };
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
        float inputs[3] = { frequencyHz, 0.0f, 0.0f };
        float out = 0.0f;
        osc.processSample (inputs, &out);

        const auto expected = std::sin (2.0 * juce::MathConstants<double>::pi * (double) frequencyHz * (double) (i + 1) / sampleRate);
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
        float inputs[3] = { 1000.0f, 0.0f, 0.0f };
        float out = 0.0f;
        osc.processSample (inputs, &out);
    }

    float syncInputs[3] = { 1000.0f, 0.0f, 1.0f }; // sync fires
    float afterSync = 0.0f;
    osc.processSample (syncInputs, &afterSync);

    // Freshly reset then advanced by one increment should equal a fresh
    // oscillator's very first sample at the same frequency.
    SineOscillatorNode fresh;
    fresh.prepare (info);
    float freshInputs[3] = { 1000.0f, 0.0f, 0.0f };
    float freshOut = 0.0f;
    fresh.processSample (freshInputs, &freshOut);

    CHECK (afterSync == Catch::Approx (freshOut).margin (1.0e-6f));
}

TEST_CASE ("SineOscillatorNode's phaseMod offsets the read point without detuning the running phase",
           "[engine][nodes][SineOscillatorNode][M22]")
{
    NodePrepareInfo info { 44100.0, 64 };
    SineOscillatorNode modulated;
    modulated.prepare (info);
    SineOscillatorNode plain;
    plain.prepare (info);

    // Modulate for a while, then stop modulating (phaseMod back to 0).
    for (int i = 0; i < 50; ++i)
    {
        float modIn[3] = { 500.0f, 0.25f, 0.0f };
        float plainIn[3] = { 500.0f, 0.0f, 0.0f };
        float m = 0.0f, p = 0.0f;
        modulated.processSample (modIn, &m);
        plain.processSample (plainIn, &p);
        CHECK (m != p); // phaseMod is actually offsetting the read
    }

    // With modulation removed, both oscillators' underlying phase accumulators
    // advanced by the exact same amount each sample (phaseMod never touched
    // the accumulator itself), so they must now read identically.
    float modIn[3] = { 500.0f, 0.0f, 0.0f };
    float plainIn[3] = { 500.0f, 0.0f, 0.0f };
    float m = 0.0f, p = 0.0f;
    modulated.processSample (modIn, &m);
    plain.processSample (plainIn, &p);
    CHECK (m == Catch::Approx (p).margin (1.0e-6f));
}
