#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/CanConnect.h"
#include "bazalt/engine/nodes/ConstantNode.h"
#include "bazalt/engine/nodes/RerouteNode.h"
#include "bazalt/engine/nodes/MapNode.h"
#include "bazalt/engine/nodes/AddNode.h"
#include "bazalt/engine/nodes/MultiplyNode.h"
#include "bazalt/engine/nodes/RoundNode.h"
#include "bazalt/engine/nodes/ListenNode.h"
#include "bazalt/engine/nodes/OutputNode.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"
#include "bazalt/engine/nodes/PitchFrequencyNodes.h"
#include "bazalt/engine/nodes/GateLengthNode.h"
#include <algorithm>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

TEST_CASE ("ConstantNode outputs whatever its value parameter is set to", "[engine][nodes][util]")
{
    ConstantNode node;
    node.setParameter ("util.constant.value", 3.5f);

    float out = 0.0f;
    node.processSample (nullptr, &out);
    CHECK (out == 3.5f);
}

TEST_CASE ("RerouteNode passes its input straight through", "[engine][nodes][util]")
{
    RerouteNode node;
    const float in = -0.75f;
    float out = 0.0f;
    node.processSample (&in, &out);
    CHECK (out == in);
}

TEST_CASE ("RerouteNode defaults to Audio and adopts the resolved type on both ports",
           "[engine][nodes][util][Reroute]")
{
    RerouteNode node;
    REQUIRE (node.hasPolymorphicPorts());
    CHECK (node.getInputPorts()[0].quantity == Quantity::Audio);
    CHECK (node.getOutputPorts()[0].quantity == Quantity::Audio);

    for (const auto type : { SignalType::Signal, SignalType::Event, SignalType::Note })
    {
        PortDescriptor source { "src", type };
        source.quantity = Quantity::Frequency;
        node.resolveIncomingPort ("in", source);
        CHECK (node.getInputPorts()[0].type == type);
        CHECK (node.getOutputPorts()[0].type == type);
        CHECK (node.getOutputPorts()[0].quantity == Quantity::Frequency);
    }
}

TEST_CASE ("RerouteNode refuses to adopt SignalType::Data, leaving its type unchanged",
           "[engine][nodes][util][Reroute]")
{
    // Data is a DataPublisher-swapped pointer, not a per-sample value —
    // Reroute has no way to forward it, so it must not claim to.
    RerouteNode node;
    node.resolveIncomingPort ("in", PortDescriptor { .id = "src", .type = SignalType::Signal });
    node.resolveIncomingPort ("in", PortDescriptor { "src", SignalType::Data });

    CHECK (node.getInputPorts()[0].type == SignalType::Signal);
    CHECK (node.getOutputPorts()[0].type == SignalType::Signal);
}

TEST_CASE ("RerouteNode carries the source's quantity too, so a rerouted Frequency is still a Frequency",
           "[engine][nodes][util][Reroute]")
{
    // Type alone isn't enough: canConnect decides adapters from the quantity,
    // so a Reroute that dropped it would let a Frequency reach a Pitch port
    // with no adapter, exactly the unit mix-up adapt.map exists to prevent.
    RerouteNode node;
    CHECK (node.getOutputPorts()[0].quantity == Quantity::Audio); // unresolved default: an audio Reroute

    PortDescriptor source { .id = "f", .type = SignalType::Signal };
    source.quantity = Quantity::Frequency;
    node.resolveIncomingPort ("in", source);

    CHECK (node.getInputPorts()[0].quantity == Quantity::Frequency);
    CHECK (node.getOutputPorts()[0].quantity == Quantity::Frequency);
    CHECK (node.getOutputPorts()[0].type == SignalType::Signal);
}

TEST_CASE ("AddNode and MultiplyNode compute a+b and a*b", "[engine][nodes][util]")
{
    AddNode add;
    const float addInputs[2] = { 2.0f, 5.0f };
    float addOut = 0.0f;
    add.processSample (addInputs, &addOut);
    CHECK (addOut == 7.0f);

    MultiplyNode multiply;
    const float mulInputs[2] = { 2.0f, 5.0f };
    float mulOut = 0.0f;
    multiply.processSample (mulInputs, &mulOut);
    CHECK (mulOut == 10.0f);
}

TEST_CASE ("ListenNode accepts an input and produces no output without asserting or crashing",
           "[engine][nodes][util]")
{
    ListenNode node;
    CHECK (node.getNumOutputPorts() == 0);
    const float in = 1.0f;
    node.processSample (&in, nullptr); // must not touch a null outputs pointer
}

TEST_CASE ("OutputNode is a unity pass-through", "[engine][nodes][util]")
{
    OutputNode node;
    const float in = 0.42f;
    float out = 0.0f;
    node.processSample (&in, &out);
    CHECK (out == in);
}

TEST_CASE ("InstanceMixNode outputs its externally-supplied block, ignoring its (Silence) graph input",
           "[engine][nodes][util][M17]")
{
    InstanceMixNode node;
    CHECK_FALSE (node.supportsPerSample()); // a domain seam, never legally inside a feedback cycle

    constexpr int numSamples = 4;
    const float externalBlock[numSamples] = { 0.1f, 0.2f, 0.3f, 0.4f };
    float output[numSamples] = {};
    const float silence[numSamples] = {};

    node.setExternalBlock (externalBlock, numSamples);

    const float* inputs[1] = { silence };
    float* outputs[1] = { output };
    node.processBlock (inputs, outputs, numSamples);

    for (int i = 0; i < numSamples; ++i)
        CHECK (output[i] == externalBlock[i]);
}

TEST_CASE ("InstanceMixNode outputs silence if asked to process a block size that doesn't match what was set",
           "[engine][nodes][util][M17]")
{
    InstanceMixNode node;
    constexpr int setNumSamples = 4;
    const float externalBlock[setNumSamples] = { 1.0f, 1.0f, 1.0f, 1.0f };
    node.setExternalBlock (externalBlock, setNumSamples);

    constexpr int processNumSamples = 8;
    float output[processNumSamples];
    std::fill (std::begin (output), std::end (output), 1.0f);
    const float silence[processNumSamples] = {};

    const float* inputs[1] = { silence };
    float* outputs[1] = { output };
    node.processBlock (inputs, outputs, processNumSamples);

    for (int i = 0; i < processNumSamples; ++i)
        CHECK (output[i] == 0.0f);
}

TEST_CASE ("RoundNode quantizes to the nearest multiple of step, mode selects rounding direction",
           "[engine][nodes][util][M20]")
{
    RoundNode node;

    // Default: step=1, mode=nearest -> plain integer rounding.
    auto roundOf = [&] (float in)
    {
        float out = 0.0f;
        float inputs[2] = { in, std::numeric_limits<float>::quiet_NaN() };
        node.processSample (inputs, &out);
        return out;
    };
    CHECK (roundOf (2.4f) == 2.0f);
    CHECK (roundOf (2.5f) == 3.0f);
    CHECK (roundOf (-2.4f) == -2.0f);

    // A non-1 step quantizes onto an arbitrary grid, not just integers.
    node.setParameter ("math.round.step", 0.5f);
    CHECK (roundOf (0.63f) == 0.5f);
    CHECK (roundOf (0.8f) == 1.0f);

    // Floor/Ceil modes, step back to 1.
    node.setParameter ("math.round.step", 1.0f);
    node.setParameter ("math.round.mode", 1.0f); // floor
    CHECK (roundOf (2.9f) == 2.0f);
    node.setParameter ("math.round.mode", 2.0f); // ceil
    CHECK (roundOf (2.1f) == 3.0f);
}

TEST_CASE ("RoundNode's step port live-modulates and falls back to setParameter when unconnected",
           "[engine][nodes][util][M20]")
{
    RoundNode node;
    node.setParameter ("math.round.step", 1.0f);

    float out = 0.0f;
    float liveStep[2] = { 7.4f, 2.0f }; // live step of 2, ignoring the statically-configured 1
    node.processSample (liveStep, &out);
    CHECK (out == 8.0f); // 7.4 quantized to the nearest multiple of 2
}

TEST_CASE ("RoundNode's output declares isInteger/kind=Int unconditionally, matching its default (step=1) behaviour",
           "[engine][nodes][util][M20]")
{
    RoundNode node;
    const auto outputs = node.getOutputPorts();
    REQUIRE (outputs.size() == 1);
    CHECK (outputs[0].isInteger);
    CHECK (outputs[0].kind == ValueKind::Int);
}

TEST_CASE ("MapNode rescales in..inMin/inMax onto outMin/outMax, clamped, matching Normalise+Map chained",
           "[engine][nodes][util][M20]")
{
    MapNode node;
    node.setParameter ("math.map.inMin", 0.0f);
    node.setParameter ("math.map.inMax", 127.0f);
    node.setParameter ("math.map.outMin", 20.0f);
    node.setParameter ("math.map.outMax", 20000.0f);

    const auto kNaN = std::numeric_limits<float>::quiet_NaN();
    auto remapOf = [&] (float in)
    {
        float out = 0.0f;
        float inputs[5] = { in, kNaN, kNaN, kNaN, kNaN }; // range ports unconnected -> use setParameter's values
        node.processSample (inputs, &out);
        return out;
    };

    CHECK (remapOf (0.0f) == 20.0f);
    CHECK (remapOf (127.0f) == 20000.0f);
    CHECK (remapOf (63.5f) == Catch::Approx (10010.0f));
    CHECK (remapOf (-10.0f) == 20.0f);   // clamped below inMin
    CHECK (remapOf (200.0f) == 20000.0f); // clamped above inMax
}

TEST_CASE ("MapNode's range ports live-modulate independently of setParameter's static values",
           "[engine][nodes][util][M20]")
{
    MapNode node;
    node.setParameter ("math.map.inMin", 0.0f);
    node.setParameter ("math.map.inMax", 1.0f);
    node.setParameter ("math.map.outMin", 0.0f);
    node.setParameter ("math.map.outMax", 1.0f);

    float out = 0.0f;
    // Live-wired to a completely different range than the static config.
    float inputs[5] = { 5.0f, 0.0f, 10.0f, 100.0f, 200.0f };
    node.processSample (inputs, &out);
    CHECK (out == 150.0f);
}

// ---- adapt.pitchToFrequency / adapt.frequencyToPitch ----
// Direct feedback: canConnect was auto-inserting adapt.map (linear) for
// Pitch -> Frequency, which is quietly wrong - the real relationship is
// exponential.

TEST_CASE ("PitchToFrequencyNode converts MIDI pitch to Hz exactly (A4 = 69 = 440Hz)",
           "[engine][nodes][PitchToFrequencyNode]")
{
    PitchToFrequencyNode node;
    float out = 0.0f;

    float a4 = 69.0f;
    node.processSample (&a4, &out);
    CHECK (out == Catch::Approx (440.0f));

    float aUp = 81.0f; // A5, one octave above A4
    node.processSample (&aUp, &out);
    CHECK (out == Catch::Approx (880.0f));

    float aDown = 57.0f; // A3, one octave below A4
    node.processSample (&aDown, &out);
    CHECK (out == Catch::Approx (220.0f));
}

TEST_CASE ("FrequencyToPitchNode is the exact inverse of PitchToFrequencyNode",
           "[engine][nodes][FrequencyToPitchNode]")
{
    FrequencyToPitchNode node;
    float out = 0.0f;

    float freq440 = 440.0f;
    node.processSample (&freq440, &out);
    CHECK (out == Catch::Approx (69.0f));

    float freq880 = 880.0f;
    node.processSample (&freq880, &out);
    CHECK (out == Catch::Approx (81.0f));
}

TEST_CASE ("canConnect prefers the exact converter over the generic linear remap for Pitch<->Frequency",
           "[engine][CanConnect][PitchToFrequencyNode]")
{
    PortDescriptor pitchPort { .id = "p", .type = SignalType::Signal };
    pitchPort.quantity = Quantity::Pitch;
    PortDescriptor freqPort { .id = "f", .type = SignalType::Signal };
    freqPort.quantity = Quantity::Frequency;

    const auto toFreq = canConnect (pitchPort, freqPort);
    REQUIRE (toFreq.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (toFreq.adapterChain.size() == 1);
    CHECK (toFreq.adapterChain[0].typeId == "math.pitchToFrequency");

    const auto toPitch = canConnect (freqPort, pitchPort);
    REQUIRE (toPitch.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (toPitch.adapterChain.size() == 1);
    CHECK (toPitch.adapterChain[0].typeId == "math.frequencyToPitch");

    // A different real-quantity pair still falls through to the generic
    // remap - this override is scoped to Pitch<->Frequency specifically.
    PortDescriptor timePort { .id = "t", .type = SignalType::Signal };
    timePort.quantity = Quantity::Time;
    const auto pitchToTime = canConnect (pitchPort, timePort);
    REQUIRE (pitchToTime.outcome == ConnectionOutcome::NeedsAdapters);
    CHECK (pitchToTime.adapterChain[0].typeId == "math.map");
}

// ---- adapt.gateLength ----
// Direct feedback: "a simple way to set the duration for the note held...
// using 2 clocks... too complicated."

TEST_CASE ("GateLengthNode opens the gate for exactly `length` seconds from a trigger, sample-accurately",
           "[engine][nodes][GateLengthNode]")
{
    GateLengthNode node;
    node.prepare ({ 10.0, 16 }); // 10Hz sample rate - 1 sample = 0.1s, easy to reason about
    node.reset();
    node.setParameter ("length", 0.3f); // 3 samples at 10Hz

    auto step = [&] (float trigger)
    {
        float inputs[2] = { trigger, 0.3f };
        float out = 0.0f;
        node.processSample (inputs, &out);
        return out;
    };

    CHECK (step (1.0f) == 1.0f); // sample 0: trigger - gate opens
    CHECK (step (0.0f) == 1.0f); // sample 1: still open
    CHECK (step (0.0f) == 1.0f); // sample 2: still open (3rd sample of the 3-sample window)
    CHECK (step (0.0f) == 0.0f); // sample 3: closed
}

TEST_CASE ("GateLengthNode retriggers: a new trigger before the gate closes restarts the countdown",
           "[engine][nodes][GateLengthNode]")
{
    GateLengthNode node;
    node.prepare ({ 10.0, 16 });
    node.reset();
    node.setParameter ("length", 0.3f); // 3 samples

    auto step = [&] (float trigger)
    {
        float inputs[2] = { trigger, 0.3f };
        float out = 0.0f;
        node.processSample (inputs, &out);
        return out;
    };

    CHECK (step (1.0f) == 1.0f); // opens, 3-sample countdown starts
    CHECK (step (0.0f) == 1.0f); // 1 sample in
    CHECK (step (1.0f) == 1.0f); // retriggered - countdown restarts to 3, not extended additively from where it was
    CHECK (step (0.0f) == 1.0f);
    CHECK (step (0.0f) == 1.0f);
    CHECK (step (0.0f) == 0.0f); // closes exactly 3 samples after the retrigger, not the original trigger
}
