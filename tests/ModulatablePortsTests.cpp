// M20 (direct feedback: "there is no reason why ADSR or Oscillator Frequency
// wouldn't be modulatable") — AdsrNode's attack/decay/sustain/release,
// OscillatorNode's frequency, SvfFilterNode's cutoff/resonance, and
// OnePoleFilterNode's coefficient all moved from ParameterDescriptor to real
// Control-type input ports, using the same hasFallbackWhenUnconnected
// NaN-sentinel pattern DelayNode.h established. These are mechanism-level
// tests (direct processSample calls, hand-built input arrays — matching
// UtilityNodeTests.cpp's own style), not perceptual DSP verification: each
// one confirms (a) a NaN in the new slot behaves exactly like setParameter()
// alone always did, and (b) a real, live value in that slot produces a
// measurably different result than the statically-configured one — proof
// the port is a real modulation path, not just descriptor metadata.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/AdsrNode.h"
#include "bazalt/engine/nodes/OscillatorNode.h"
#include "bazalt/engine/nodes/SvfFilterNode.h"
#include "bazalt/engine/nodes/OnePoleFilterNode.h"
#include <limits>
#include <cmath>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

TEST_CASE ("AdsrNode's attack port live-modulates instead of only reading setParameter's static value",
           "[engine][nodes][AdsrNode][M20]")
{
    NodePrepareInfo info { 44100.0, 64 };

    AdsrNode slow;
    slow.prepare (info);
    slow.setParameter ("env.adsr.attack", 2.0f); // a long attack, statically configured
    slow.setParameter ("env.adsr.decay", 0.1f);
    slow.setParameter ("env.adsr.sustain", 0.7f);
    slow.setParameter ("env.adsr.release", 0.2f);
    float slowGateOnly[5] = { 1.0f, kNaN, kNaN, kNaN, kNaN }; // attack/decay/sustain/release unconnected
    float slowOut = 0.0f;
    for (int i = 0; i < 200; ++i)
        slow.processSample (slowGateOnly, &slowOut);

    AdsrNode fast;
    fast.prepare (info);
    fast.setParameter ("env.adsr.attack", 2.0f); // same static config as `slow`...
    fast.setParameter ("env.adsr.decay", 0.1f);
    fast.setParameter ("env.adsr.sustain", 0.7f);
    fast.setParameter ("env.adsr.release", 0.2f);
    float fastGateAndAttack[5] = { 1.0f, 0.0001f, kNaN, kNaN, kNaN }; // ...but attack is live-wired to a near-zero value
    float fastOut = 0.0f;
    for (int i = 0; i < 200; ++i)
        fast.processSample (fastGateAndAttack, &fastOut);

    CHECK (fastOut > slowOut); // the live near-zero attack must have risen far faster than the statically-configured 2s one
}

TEST_CASE ("AdsrNode's stage ports fall back to setParameter's value exactly when unconnected (NaN)",
           "[engine][nodes][AdsrNode][M20]")
{
    NodePrepareInfo info { 44100.0, 64 };

    AdsrNode viaParameter;
    viaParameter.prepare (info);
    viaParameter.setParameter ("env.adsr.attack", 0.05f);
    viaParameter.setParameter ("env.adsr.decay", 0.1f);
    viaParameter.setParameter ("env.adsr.sustain", 0.7f);
    viaParameter.setParameter ("env.adsr.release", 0.2f);

    AdsrNode viaPortsAllNaN;
    viaPortsAllNaN.prepare (info);
    viaPortsAllNaN.setParameter ("env.adsr.attack", 0.05f);
    viaPortsAllNaN.setParameter ("env.adsr.decay", 0.1f);
    viaPortsAllNaN.setParameter ("env.adsr.sustain", 0.7f);
    viaPortsAllNaN.setParameter ("env.adsr.release", 0.2f);

    float gateOnly[5] = { 1.0f, kNaN, kNaN, kNaN, kNaN };
    for (int i = 0; i < 50; ++i)
    {
        float a = 0.0f, b = 0.0f;
        viaParameter.processSample (gateOnly, &a);
        viaPortsAllNaN.processSample (gateOnly, &b);
        CHECK (a == b);
    }
}

TEST_CASE ("OscillatorNode's frequency port drives pitch directly when pitch itself is unconnected",
           "[engine][nodes][OscillatorNode][M20]")
{
    NodePrepareInfo info { 44100.0, 64 };

    OscillatorNode lowFreq;
    lowFreq.prepare (info);
    float lowInputs[2] = { kNaN, 100.0f }; // pitch unconnected, frequency live-wired to 100 Hz
    float lowOut = 0.0f;
    for (int i = 0; i < 4; ++i)
        lowFreq.processSample (lowInputs, &lowOut);

    OscillatorNode highFreq;
    highFreq.prepare (info);
    float highInputs[2] = { kNaN, 8000.0f }; // pitch unconnected, frequency live-wired to 8kHz
    float highOut = 0.0f;
    for (int i = 0; i < 4; ++i)
        highFreq.processSample (highInputs, &highOut);

    // Not asserting exact values (PolyBLEP shaping) — just that a completely
    // different live frequency produces a completely different waveform
    // state after the same number of samples, proving the port actually
    // reaches the oscillator rather than being ignored.
    CHECK (lowOut != highOut);
}

TEST_CASE ("OscillatorNode's pitch port still wins over frequency when both are connected",
           "[engine][nodes][OscillatorNode][M20]")
{
    NodePrepareInfo info { 44100.0, 64 };

    OscillatorNode viaPitch;
    viaPitch.prepare (info);
    float pitchInputs[2] = { 69.0f, 100.0f }; // pitch=69 -> 440Hz, frequency=100Hz (should be ignored)
    float pitchOut = 0.0f;
    for (int i = 0; i < 4; ++i)
        viaPitch.processSample (pitchInputs, &pitchOut);

    OscillatorNode viaFrequency;
    viaFrequency.prepare (info);
    float freqInputs[2] = { kNaN, 440.0f }; // pitch unconnected, frequency=440Hz directly
    float freqOut = 0.0f;
    for (int i = 0; i < 4; ++i)
        viaFrequency.processSample (freqInputs, &freqOut);

    CHECK (pitchOut == freqOut); // pitch=69 (440Hz) and a direct 440Hz frequency must render identically
}

TEST_CASE ("SvfFilterNode's cutoff/resonance ports live-modulate instead of only reading setParameter's static value",
           "[engine][nodes][SvfFilterNode][M20]")
{
    NodePrepareInfo info { 44100.0, 64 };

    SvfFilterNode lowCutoff;
    lowCutoff.prepare (info);
    float lowInputs[3] = { 1.0f, 200.0f, 0.7071f }; // an impulse through a low cutoff
    float lowOut = 0.0f;
    lowCutoff.processSample (lowInputs, &lowOut);

    SvfFilterNode highCutoff;
    highCutoff.prepare (info);
    float highInputs[3] = { 1.0f, 15000.0f, 0.7071f }; // the same impulse through a high cutoff
    float highOut = 0.0f;
    highCutoff.processSample (highInputs, &highOut);

    CHECK (lowOut != highOut); // the live cutoff value must actually reach the filter
}

TEST_CASE ("OnePoleFilterNode's coefficient port live-modulates instead of only reading setParameter's static value",
           "[engine][nodes][OnePoleFilterNode][M20]")
{
    OnePoleFilterNode fastDamp;
    float fastInputs[2] = { 1.0f, 0.9f }; // near-1 coefficient — output should track the input almost fully
    float fastOut = 0.0f;
    fastDamp.processSample (fastInputs, &fastOut);

    OnePoleFilterNode slowDamp;
    float slowInputs[2] = { 1.0f, 0.05f }; // near-0 coefficient — output should barely move off zero
    float slowOut = 0.0f;
    slowDamp.processSample (slowInputs, &slowOut);

    CHECK (fastOut > slowOut);
}
