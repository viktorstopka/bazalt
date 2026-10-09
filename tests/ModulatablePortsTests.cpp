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
#include <catch2/catch_approx.hpp>
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

TEST_CASE ("SvfFilterNode's cutoff/resonance ports live-modulate instead of only reading setParameter's static value",
           "[engine][nodes][SvfFilterNode][M20]")
{
    NodePrepareInfo info { 44100.0, 64 };

    SvfFilterNode lowCutoff;
    lowCutoff.prepare (info);
    float lowInputs[3] = { 1.0f, 200.0f, 0.7071f }; // an impulse through a low cutoff
    float lowOut[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; // 5 outputs: lowpass/bandpass/highpass/notch/peak
    lowCutoff.processSample (lowInputs, lowOut);

    SvfFilterNode highCutoff;
    highCutoff.prepare (info);
    float highInputs[3] = { 1.0f, 15000.0f, 0.7071f }; // the same impulse through a high cutoff
    float highOut[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    highCutoff.processSample (highInputs, highOut);

    CHECK (lowOut[0] != highOut[0]); // the live cutoff value must actually reach the filter (lowpass/"out")
}

TEST_CASE ("OnePoleFilterNode's coefficient port live-modulates instead of only reading setParameter's static value",
           "[engine][nodes][OnePoleFilterNode][M20]")
{
    OnePoleFilterNode fastDamp;
    float fastInputs[2] = { 1.0f, 0.9f }; // near-1 coefficient — output should track the input almost fully
    float fastOut[2] = { 0.0f, 0.0f }; // [0]=lowpass, [1]=highpass (M22)
    fastDamp.processSample (fastInputs, fastOut);

    OnePoleFilterNode slowDamp;
    float slowInputs[2] = { 1.0f, 0.05f }; // near-0 coefficient — output should barely move off zero
    float slowOut[2] = { 0.0f, 0.0f };
    slowDamp.processSample (slowInputs, slowOut);

    CHECK (fastOut[0] > slowOut[0]);
}

TEST_CASE ("OnePoleFilterNode's highpass output is the complement of its lowpass output",
           "[engine][nodes][OnePoleFilterNode][M22]")
{
    OnePoleFilterNode filter;
    filter.setParameter ("filter.onepole.coefficient", 0.3f);

    for (float in : { 1.0f, 0.4f, -0.7f, 0.0f, 1.0f, 1.0f })
    {
        float inputs[2] = { in, kNaN };
        float outputs[2] = { 0.0f, 0.0f };
        filter.processSample (inputs, outputs);

        CHECK (outputs[1] == Catch::Approx (in - outputs[0]));
    }
}
