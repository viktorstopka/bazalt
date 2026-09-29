// Clock+Seq batch (wiki/NODES.Status.md's own build-next order, step 1):
// clock.pulse, clock.divide, clock.counter, seq.steps, seq.euclid.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/ClockPulseNode.h"
#include "bazalt/engine/nodes/ClockDivideNode.h"
#include "bazalt/engine/nodes/ClockCounterNode.h"
#include "bazalt/engine/nodes/SeqStepsNode.h"
#include "bazalt/engine/nodes/SeqEuclidNode.h"
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

// ---- clock.pulse ----

TEST_CASE ("ClockPulseNode free-runs at its rate in Free mode, counted over an off-boundary window",
           "[engine][nodes][ClockPulseNode][ClockSeq]")
{
    // 1.05s at 10Hz: basePhase reaches 10.5 - comfortably between the k=10
    // and k=11 thresholds, avoiding any float-rounding ambiguity right at an
    // integer crossing. Ticks fire for k = 0..10 inclusive (11 total),
    // including the immediate k=0 tick at the very first sample.
    NodePrepareInfo info { 44100.0, 512 };
    ClockPulseNode node;
    node.prepare (info);
    node.reset();
    node.setParameter ("clock.pulse.rate", 10.0f);

    const int numSamples = (int) (1.05 * 44100.0);
    int ticks = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        float inputs[5] = { kNaN, kNaN, kNaN, kNaN, 0.0f };
        float outputs[2] = {};
        node.processSample (inputs, outputs);
        if (outputs[0] > 0.5f)
            ++ticks;
        REQUIRE (outputs[1] >= 0.0f);
        REQUIRE (outputs[1] <= 1.0f);
    }

    CHECK (ticks == 11);
}

TEST_CASE ("ClockPulseNode's swing pushes every odd tick later, shortening the following interval",
           "[engine][nodes][ClockPulseNode][ClockSeq]")
{
    NodePrepareInfo info { 44100.0, 512 };
    ClockPulseNode node;
    node.prepare (info);
    node.reset();
    node.setParameter ("clock.pulse.rate", 10.0f);
    node.setParameter ("clock.pulse.swing", 1.0f); // maximal: thresholds 0, 1.5, 2, 3.5, 4, ...

    std::vector<int> tickSamples;
    for (int i = 0; i < 20000 && tickSamples.size() < 3; ++i)
    {
        float inputs[5] = { kNaN, kNaN, kNaN, kNaN, 0.0f };
        float outputs[2] = {};
        node.processSample (inputs, outputs);
        if (outputs[0] > 0.5f)
            tickSamples.push_back (i);
    }

    REQUIRE (tickSamples.size() == 3);
    const auto gapToOdd = tickSamples[1] - tickSamples[0];   // tick0 -> tick1: 1.5 periods
    const auto gapToEven = tickSamples[2] - tickSamples[1];  // tick1 -> tick2: 0.5 periods
    CHECK (gapToOdd > gapToEven * 2); // should be ~3x; a wide margin avoids flakiness
}

TEST_CASE ("ClockPulseNode's jitter is deterministic for a given seed, and different seeds diverge",
           "[engine][nodes][ClockPulseNode][ClockSeq]")
{
    NodePrepareInfo info { 44100.0, 512 };

    auto runFor = [&] (float seed)
    {
        ClockPulseNode node;
        node.prepare (info);
        node.setParameter ("clock.pulse.rate", 50.0f);
        node.setParameter ("clock.pulse.jitter", 0.8f);
        node.setParameter ("clock.pulse.seed", seed);
        node.reset();
        std::vector<float> ticks;
        for (int i = 0; i < 5000; ++i)
        {
            float inputs[5] = { kNaN, kNaN, kNaN, kNaN, 0.0f };
            float outputs[2] = {};
            node.processSample (inputs, outputs);
            ticks.push_back (outputs[0]);
        }
        return ticks;
    };

    const auto a1 = runFor (11.0f);
    const auto a2 = runFor (11.0f);
    const auto b = runFor (12.0f);

    CHECK (a1 == a2);
    CHECK (a1 != b);
}

TEST_CASE ("ClockPulseNode's Division mode reads rate as beats/sec scaled by the division fraction",
           "[engine][nodes][ClockPulseNode][ClockSeq]")
{
    NodePrepareInfo info { 44100.0, 512 };
    const int numSamples = (int) (1.05 * 44100.0); // off-boundary, see the free-run test above

    auto countTicksFor = [&] (float divisionIndex)
    {
        ClockPulseNode node;
        node.prepare (info);
        node.reset();
        node.setParameter ("clock.pulse.rateMode", 1.0f); // Division
        node.setParameter ("clock.pulse.division", divisionIndex);
        node.setParameter ("clock.pulse.rate", 2.0f); // 2 beats/sec, e.g. io.transport.tempo at 120bpm
        int ticks = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            float inputs[5] = { kNaN, kNaN, kNaN, kNaN, 0.0f };
            float outputs[2] = {};
            node.processSample (inputs, outputs);
            if (outputs[0] > 0.5f)
                ++ticks;
        }
        return ticks;
    };

    // Quarter: effectiveHz = 2*1.0 = 2Hz -> basePhase reaches 2.1 -> k=0,1,2 (3 ticks).
    // Eighth:  effectiveHz = 2*2.0 = 4Hz -> basePhase reaches 4.2 -> k=0..4 (5 ticks).
    CHECK (countTicksFor (2.0f) == 3); // "quarter"
    CHECK (countTicksFor (3.0f) == 5); // "eighth"
}

TEST_CASE ("ClockPulseNode's run input halts and resumes ticking; reset reissues an immediate tick",
           "[engine][nodes][ClockPulseNode][ClockSeq]")
{
    NodePrepareInfo info { 44100.0, 512 };
    ClockPulseNode node;
    node.prepare (info);
    node.reset();
    node.setParameter ("clock.pulse.rate", 10.0f);
    node.setParameter ("clock.pulse.run", 0.0f);

    int ticksWhileStopped = 0;
    for (int i = 0; i < 10000; ++i)
    {
        float inputs[5] = { kNaN, kNaN, kNaN, kNaN, 0.0f };
        float outputs[2] = {};
        node.processSample (inputs, outputs);
        if (outputs[0] > 0.5f)
            ++ticksWhileStopped;
    }
    CHECK (ticksWhileStopped == 0);

    node.setParameter ("clock.pulse.run", 1.0f);
    float inputs[5] = { kNaN, kNaN, kNaN, kNaN, 0.0f };
    float outputs[2] = {};
    node.processSample (inputs, outputs); // basePhase is still 0 from before -> fires immediately
    CHECK (outputs[0] > 0.5f);

    // Run a while, then reset - the very next sample should tick again immediately.
    for (int i = 0; i < 1000; ++i)
    {
        float loopInputs[5] = { kNaN, kNaN, kNaN, kNaN, 0.0f };
        float loopOutputs[2] = {};
        node.processSample (loopInputs, loopOutputs);
    }
    float resetInputs[5] = { kNaN, kNaN, kNaN, kNaN, 1.0f };
    float resetOutputs[2] = {};
    node.processSample (resetInputs, resetOutputs);
    CHECK (resetOutputs[0] > 0.5f);
}

// ---- clock.divide ----

TEST_CASE ("ClockDivideNode with divide=1 passes every tick through unchanged",
           "[engine][nodes][ClockDivideNode][ClockSeq]")
{
    ClockDivideNode node;
    node.reset();
    node.setParameter ("clock.divide.divide", 1.0f);

    int outTicks = 0;
    for (int i = 0; i < 20; ++i)
    {
        float inputs[3] = { 1.0f, kNaN, 0.0f };
        float outputs[1] = {};
        node.processSample (inputs, outputs);
        if (outputs[0] > 0.5f)
            ++outTicks;
    }
    CHECK (outTicks == 20);
}

TEST_CASE ("ClockDivideNode fires exactly every Nth incoming tick",
           "[engine][nodes][ClockDivideNode][ClockSeq]")
{
    ClockDivideNode node;
    node.reset();
    node.setParameter ("clock.divide.divide", 3.0f);

    std::vector<int> firedAtTickNumber;
    for (int tick = 1; tick <= 9; ++tick)
    {
        float inputs[3] = { 1.0f, kNaN, 0.0f };
        float outputs[1] = {};
        node.processSample (inputs, outputs);
        if (outputs[0] > 0.5f)
            firedAtTickNumber.push_back (tick);
    }
    CHECK (firedAtTickNumber == std::vector<int> { 3, 6, 9 });
}

TEST_CASE ("ClockDivideNode's reset zeroes the counter without itself firing an output tick",
           "[engine][nodes][ClockDivideNode][ClockSeq]")
{
    ClockDivideNode node;
    node.reset();
    node.setParameter ("clock.divide.divide", 4.0f);

    // Two ticks in (halfway to firing), then reset.
    for (int i = 0; i < 2; ++i)
    {
        float inputs[3] = { 1.0f, kNaN, 0.0f };
        float outputs[1] = {};
        node.processSample (inputs, outputs);
    }

    float resetInputs[3] = { 0.0f, kNaN, 1.0f };
    float resetOutputs[1] = {};
    node.processSample (resetInputs, resetOutputs);
    CHECK (resetOutputs[0] == 0.0f);

    // Needs a fresh 4 ticks now, not just 2 more.
    int outTicks = 0;
    for (int i = 0; i < 3; ++i)
    {
        float inputs[3] = { 1.0f, kNaN, 0.0f };
        float outputs[1] = {};
        node.processSample (inputs, outputs);
        if (outputs[0] > 0.5f)
            ++outTicks;
    }
    CHECK (outTicks == 0);
}

// ---- clock.counter ----

TEST_CASE ("ClockCounterNode Up mode advances by step and wraps with a wrapped event",
           "[engine][nodes][ClockCounterNode][ClockSeq]")
{
    ClockCounterNode node;
    node.reset();
    node.setParameter ("clock.counter.length", 4.0f);
    node.setParameter ("clock.counter.step", 1.0f);
    node.setParameter ("clock.counter.mode", 0.0f); // up

    std::vector<int> indices;
    std::vector<bool> wraps;
    for (int i = 0; i < 8; ++i)
    {
        float inputs[4] = { 1.0f, kNaN, kNaN, kNaN };
        float outputs[3] = {};
        node.processSample (inputs, outputs);
        indices.push_back ((int) outputs[0]);
        wraps.push_back (outputs[2] > 0.5f);
    }

    CHECK (indices == std::vector<int> { 1, 2, 3, 0, 1, 2, 3, 0 });
    CHECK (wraps == std::vector<bool> { false, false, false, true, false, false, false, true });
}

TEST_CASE ("ClockCounterNode Down mode wraps downward", "[engine][nodes][ClockCounterNode][ClockSeq]")
{
    ClockCounterNode node;
    node.reset();
    node.setParameter ("clock.counter.length", 4.0f);
    node.setParameter ("clock.counter.mode", 1.0f); // down

    std::vector<int> indices;
    for (int i = 0; i < 5; ++i)
    {
        float inputs[4] = { 1.0f, kNaN, kNaN, kNaN };
        float outputs[3] = {};
        node.processSample (inputs, outputs);
        indices.push_back ((int) outputs[0]);
    }
    // Starting at 0, the first decrement wraps immediately to length-1.
    CHECK (indices == std::vector<int> { 3, 2, 1, 0, 3 });
}

TEST_CASE ("ClockCounterNode PingPong mode bounces between 0 and length-1",
           "[engine][nodes][ClockCounterNode][ClockSeq]")
{
    ClockCounterNode node;
    node.reset();
    node.setParameter ("clock.counter.length", 4.0f);
    node.setParameter ("clock.counter.mode", 2.0f); // pingPong

    std::vector<int> indices;
    for (int i = 0; i < 8; ++i)
    {
        float inputs[4] = { 1.0f, kNaN, kNaN, kNaN };
        float outputs[3] = {};
        node.processSample (inputs, outputs);
        indices.push_back ((int) outputs[0]);
    }
    CHECK (indices == std::vector<int> { 1, 2, 3, 2, 1, 0, 1, 2 });
}

TEST_CASE ("ClockCounterNode Random mode stays in range and is deterministic for a seed",
           "[engine][nodes][ClockCounterNode][ClockSeq]")
{
    auto runFor = [&] (float seed)
    {
        ClockCounterNode node;
        node.setParameter ("clock.counter.length", 8.0f);
        node.setParameter ("clock.counter.mode", 3.0f); // random
        node.setParameter ("clock.counter.seed", seed);
        node.reset();
        std::vector<int> indices;
        for (int i = 0; i < 100; ++i)
        {
            float inputs[4] = { 1.0f, kNaN, kNaN, kNaN };
            float outputs[3] = {};
            node.processSample (inputs, outputs);
            REQUIRE (outputs[0] >= 0.0f);
            REQUIRE (outputs[0] < 8.0f);
            indices.push_back ((int) outputs[0]);
        }
        return indices;
    };

    const auto a1 = runFor (5.0f);
    const auto a2 = runFor (5.0f);
    const auto b = runFor (6.0f);
    CHECK (a1 == a2);
    CHECK (a1 != b);
}

TEST_CASE ("ClockCounterNode's normalised output is index/(length-1)",
           "[engine][nodes][ClockCounterNode][ClockSeq]")
{
    ClockCounterNode node;
    node.reset();
    node.setParameter ("clock.counter.length", 5.0f); // valid indices 0..4

    float outputs[3] = {};
    for (int i = 0; i < 3; ++i) // advance to index 3
    {
        float inputs[4] = { 1.0f, kNaN, kNaN, kNaN };
        node.processSample (inputs, outputs);
    }
    CHECK (outputs[0] == 3.0f);
    CHECK (outputs[1] == Catch::Approx (3.0f / 4.0f));
}

// ---- seq.steps ----

TEST_CASE ("SeqStepsNode advances on tick, wraps at length, and holds its value between ticks",
           "[engine][nodes][SeqStepsNode][ClockSeq]")
{
    SeqStepsNode node;
    node.reset();
    node.setParameter ("seq.steps.length", 3.0f);
    node.setParameter ("seq.steps.step.0", 0.25f);
    node.setParameter ("seq.steps.step.1", -0.5f);
    node.setParameter ("seq.steps.step.2", 1.0f);

    auto tick = [&] (SeqStepsNode& n) -> std::array<float, 4>
    {
        float inputs[2] = { 1.0f, 0.0f };
        std::array<float, 4> outputs {};
        n.processSample (inputs, outputs.data());
        return outputs;
    };
    auto hold = [&] (SeqStepsNode& n) -> std::array<float, 4>
    {
        float inputs[2] = { 0.0f, 0.0f };
        std::array<float, 4> outputs {};
        n.processSample (inputs, outputs.data());
        return outputs;
    };

    // Rests at step 0 (0.25) until the first tick advances it to step 1.
    auto out = hold (node);
    CHECK (out[0] == Catch::Approx (0.25f));
    CHECK (out[3] == 0.0f);

    out = tick (node);
    CHECK (out[0] == Catch::Approx (-0.5f));
    CHECK (out[3] == 1.0f);
    CHECK (out[2] == 1.0f); // trigger fired this sample

    out = hold (node); // holds between ticks
    CHECK (out[0] == Catch::Approx (-0.5f));
    CHECK (out[2] == 0.0f);

    out = tick (node);
    CHECK (out[0] == Catch::Approx (1.0f));
    CHECK (out[3] == 2.0f);

    out = tick (node); // wraps back to step 0
    CHECK (out[0] == Catch::Approx (0.25f));
    CHECK (out[3] == 0.0f);
}

TEST_CASE ("SeqStepsNode's gate reflects whether the current step is a rest (exactly 0.0)",
           "[engine][nodes][SeqStepsNode][ClockSeq]")
{
    SeqStepsNode node;
    node.reset();
    node.setParameter ("seq.steps.length", 2.0f);
    node.setParameter ("seq.steps.step.0", 0.0f); // rest
    node.setParameter ("seq.steps.step.1", 0.3f); // active

    float inputs0[2] = { 0.0f, 0.0f };
    float outputs0[4] = {};
    node.processSample (inputs0, outputs0);
    CHECK (outputs0[1] == 0.0f); // step 0: gate low

    float inputs1[2] = { 1.0f, 0.0f };
    float outputs1[4] = {};
    node.processSample (inputs1, outputs1);
    CHECK (outputs1[1] == 1.0f); // step 1: gate high
}

TEST_CASE ("SeqStepsNode's range=unipolar remaps stored bipolar values to 0..1",
           "[engine][nodes][SeqStepsNode][ClockSeq]")
{
    SeqStepsNode node;
    node.reset();
    node.setParameter ("seq.steps.length", 1.0f);
    node.setParameter ("seq.steps.step.0", -1.0f);
    node.setParameter ("seq.steps.range", 1.0f); // unipolar

    float inputs[2] = { 0.0f, 0.0f };
    float outputs[4] = {};
    node.processSample (inputs, outputs);
    CHECK (outputs[0] == Catch::Approx (0.0f)); // -1 bipolar -> 0 unipolar
}

TEST_CASE ("SeqStepsNode's reset returns to step 0", "[engine][nodes][SeqStepsNode][ClockSeq]")
{
    SeqStepsNode node;
    node.reset();
    node.setParameter ("seq.steps.length", 4.0f);

    for (int i = 0; i < 3; ++i)
    {
        float inputs[2] = { 1.0f, 0.0f };
        float outputs[4] = {};
        node.processSample (inputs, outputs);
    }
    float resetInputs[2] = { 0.0f, 1.0f };
    float resetOutputs[4] = {};
    node.processSample (resetInputs, resetOutputs);
    CHECK (resetOutputs[3] == 0.0f);
}

// ---- seq.euclid ----

TEST_CASE ("SeqEuclidNode steps=8 pulses=3 produces the classic tresillo pattern (hits at 0, 3, 6)",
           "[engine][nodes][SeqEuclidNode][ClockSeq]")
{
    SeqEuclidNode node;
    node.reset();
    node.setParameter ("seq.euclid.steps", 8.0f);
    node.setParameter ("seq.euclid.pulses", 3.0f);

    // Index 0 is the resting state (no tick yet) - check it directly, then
    // tick through a full 8-step cycle and record which steps gate high.
    float restInputs[5] = { 0.0f, kNaN, kNaN, kNaN, 0.0f };
    float restOutputs[2] = {};
    node.processSample (restInputs, restOutputs);
    CHECK (restOutputs[1] == 1.0f); // step 0 is a pulse

    std::vector<int> pulseSteps;
    for (int step = 1; step < 8; ++step)
    {
        float inputs[5] = { 1.0f, kNaN, kNaN, kNaN, 0.0f };
        float outputs[2] = {};
        node.processSample (inputs, outputs);
        if (outputs[1] > 0.5f)
            pulseSteps.push_back (step);
    }
    CHECK (pulseSteps == std::vector<int> { 3, 6 });
}

TEST_CASE ("SeqEuclidNode: pulses=0 never fires, pulses>=steps fires every step",
           "[engine][nodes][SeqEuclidNode][ClockSeq]")
{
    SeqEuclidNode zero;
    zero.reset();
    zero.setParameter ("seq.euclid.steps", 4.0f);
    zero.setParameter ("seq.euclid.pulses", 0.0f);

    int hits = 0;
    for (int i = 0; i < 8; ++i)
    {
        float inputs[5] = { 1.0f, kNaN, kNaN, kNaN, 0.0f };
        float outputs[2] = {};
        zero.processSample (inputs, outputs);
        if (outputs[1] > 0.5f)
            ++hits;
    }
    CHECK (hits == 0);

    SeqEuclidNode full;
    full.reset();
    full.setParameter ("seq.euclid.steps", 4.0f);
    full.setParameter ("seq.euclid.pulses", 4.0f);

    hits = 0;
    for (int i = 0; i < 8; ++i)
    {
        float inputs[5] = { 1.0f, kNaN, kNaN, kNaN, 0.0f };
        float outputs[2] = {};
        full.processSample (inputs, outputs);
        if (outputs[1] > 0.5f)
            ++hits;
    }
    CHECK (hits == 8);
}

TEST_CASE ("SeqEuclidNode's rotate shifts which step of the fixed pattern is read",
           "[engine][nodes][SeqEuclidNode][ClockSeq]")
{
    // Unrotated steps=8/pulses=3 pattern (from the tresillo test): 0,3,6.
    // Rotating by 3 should make the pattern read as if starting from step 3:
    // effective = (index + rotate) % steps, so index=0 reads pattern[3]=1 (a hit).
    SeqEuclidNode node;
    node.reset();
    node.setParameter ("seq.euclid.steps", 8.0f);
    node.setParameter ("seq.euclid.pulses", 3.0f);
    node.setParameter ("seq.euclid.rotate", 3.0f);

    float inputs[5] = { 0.0f, kNaN, kNaN, kNaN, 0.0f }; // resting at index 0
    float outputs[2] = {};
    node.processSample (inputs, outputs);
    CHECK (outputs[1] == 1.0f); // reads pattern position 3, a hit
}

TEST_CASE ("SeqEuclidNode's trigger only fires the instant a tick lands on a pulse step",
           "[engine][nodes][SeqEuclidNode][ClockSeq]")
{
    SeqEuclidNode node;
    node.reset();
    node.setParameter ("seq.euclid.steps", 8.0f);
    node.setParameter ("seq.euclid.pulses", 3.0f);

    // Step 1 (first tick) is not a pulse - trigger must stay low even though
    // nothing else changed; step 3 (third tick) is a pulse - trigger fires.
    float outputs[2] = {};
    for (int step = 1; step <= 3; ++step)
    {
        float inputs[5] = { 1.0f, kNaN, kNaN, kNaN, 0.0f };
        node.processSample (inputs, outputs);
        if (step == 3)
            CHECK (outputs[0] == 1.0f);
        else
            CHECK (outputs[0] == 0.0f);
    }

    // Holding (no tick) never re-fires trigger even while gate stays high.
    float holdInputs[5] = { 0.0f, kNaN, kNaN, kNaN, 0.0f };
    node.processSample (holdInputs, outputs);
    CHECK (outputs[0] == 0.0f);
    CHECK (outputs[1] == 1.0f); // gate still reflects step 3 being a pulse
}
