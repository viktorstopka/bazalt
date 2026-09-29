// Note Stream batch (wiki/NODES.Status.md's own build-next order, step 3):
// note.gate, note.value, note.transpose, note.filter, note.humanize,
// note.quantize. note.hold/note.select/note.chord deliberately deferred -
// see NoteFilterNode.h's own comment for the real engine limit
// (ExecutionPlan::BlockStep supports only one Note output per node).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/NoteGateNode.h"
#include "bazalt/engine/nodes/NoteValueNode.h"
#include "bazalt/engine/nodes/NoteTransposeNode.h"
#include "bazalt/engine/nodes/NoteFilterNode.h"
#include "bazalt/engine/nodes/NoteHumanizeNode.h"
#include "bazalt/engine/nodes/NoteQuantizeNode.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include <cmath>
#include <limits>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    NoteEvent noteOn (float pitch, float velocity) noexcept
    {
        NoteEvent n;
        n.gate = true;
        n.pitch = pitch;
        n.velocity = velocity;
        n.startEvent = true;
        return n;
    }

    NoteEvent held (float pitch, float velocity) noexcept
    {
        NoteEvent n;
        n.gate = true;
        n.pitch = pitch;
        n.velocity = velocity;
        return n;
    }

    NoteEvent noteOff (float pitch) noexcept
    {
        NoteEvent n;
        n.gate = false;
        n.pitch = pitch;
        n.stopEvent = true;
        return n;
    }
}

// ---- note.gate ----

TEST_CASE ("NoteGateNode turns start/stop/gate into Event/Boolean/Control outputs",
           "[engine][nodes][NoteGateNode][NoteStream]")
{
    NoteGateNode node;
    node.reset();

    NoteEvent events[4] = { noteOn (60.0f, 0.8f), held (60.0f, 0.8f), noteOff (60.0f), NoteEvent {} };
    node.consumeNoteBlock (events, 4);

    float noteOnOut[4] = {}, noteOffOut[4] = {}, gateOut[4] = {}, countOut[4] = {};
    float* const outputs[4] = { noteOnOut, noteOffOut, gateOut, countOut };
    node.processBlock (nullptr, outputs, 4);

    CHECK (std::vector<float> (noteOnOut, noteOnOut + 4) == std::vector<float> { 1, 0, 0, 0 });
    CHECK (std::vector<float> (noteOffOut, noteOffOut + 4) == std::vector<float> { 0, 0, 1, 0 });
    CHECK (std::vector<float> (gateOut, gateOut + 4) == std::vector<float> { 1, 1, 0, 0 });
    CHECK (std::vector<float> (countOut, countOut + 4) == std::vector<float> { 1, 1, 1, 1 });
}

TEST_CASE ("NoteGateNode's count increments once per note-on, across multiple notes",
           "[engine][nodes][NoteGateNode][NoteStream]")
{
    NoteGateNode node;
    node.reset();

    NoteEvent events[3] = { noteOn (60.0f, 0.8f), noteOff (60.0f), noteOn (64.0f, 0.5f) };
    node.consumeNoteBlock (events, 3);

    float noteOnOut[3] = {}, noteOffOut[3] = {}, gateOut[3] = {}, countOut[3] = {};
    float* const outputs[4] = { noteOnOut, noteOffOut, gateOut, countOut };
    node.processBlock (nullptr, outputs, 3);

    CHECK (countOut[2] == 2.0f);
}

// ---- note.value ----

TEST_CASE ("NoteValueNode's select picks among currently-held notes correctly",
           "[engine][nodes][NoteValueNode][NoteStream]")
{
    // Three overlapping notes accumulate over 3 samples: 60, then 72, then 48 - none stopped yet.
    NoteEvent events[3] = { noteOn (60.0f, 0.5f), noteOn (72.0f, 0.6f), noteOn (48.0f, 0.7f) };

    auto runFor = [&] (float selectMode)
    {
        NoteValueNode node;
        node.reset();
        node.setParameter ("note.value.select", selectMode);
        node.consumeNoteBlock (events, 3);
        float pitchOut[3] = {}, velocityOut[3] = {};
        float* const outputs[2] = { pitchOut, velocityOut };
        node.processBlock (nullptr, outputs, 3);
        return pitchOut[2]; // after all three have been added
    };

    CHECK (runFor (0.0f) == 48.0f);  // last
    CHECK (runFor (1.0f) == 48.0f);  // lowest
    CHECK (runFor (2.0f) == 72.0f);  // highest
    CHECK (runFor (3.0f) == 60.0f);  // first
}

TEST_CASE ("NoteValueNode removes a note from its held memory on stopEvent",
           "[engine][nodes][NoteValueNode][NoteStream]")
{
    NoteValueNode node;
    node.reset();
    node.setParameter ("note.value.select", 0.0f); // last

    NoteEvent events[3] = { noteOn (60.0f, 0.5f), noteOn (72.0f, 0.6f), noteOff (72.0f) };
    node.consumeNoteBlock (events, 3);
    float pitchOut[3] = {}, velocityOut[3] = {};
    float* const outputs[2] = { pitchOut, velocityOut };
    node.processBlock (nullptr, outputs, 3);

    CHECK (pitchOut[2] == 60.0f); // 72 removed, 60 is now "last" remaining
}

// ---- note.transpose ----

TEST_CASE ("NoteTransposeNode shifts pitch by semitones + octaves*12", "[engine][nodes][NoteTransposeNode][NoteStream]")
{
    NoteTransposeNode node;
    node.prepare ({ 44100.0, 512 });

    NoteEvent events[1] = { noteOn (60.0f, 0.5f) };
    node.consumeNoteBlock (events, 1);

    const float semitonesIn[1] = { 7.0f };
    const float octavesIn[1] = { 1.0f };
    const float notesIn[1] = { 0.0f }; // unused - Note port bypasses this slot
    const float* const inputs[3] = { notesIn, semitonesIn, octavesIn };
    float dummyOut[1] = {};
    float* const outputs[1] = { dummyOut };
    node.processBlock (inputs, outputs, 1);

    NoteEvent produced[1] = {};
    node.produceNoteBlock (produced, 1);

    CHECK (produced[0].pitch == Catch::Approx (60.0f + 7.0f + 12.0f));
    CHECK (produced[0].startEvent);
    CHECK (produced[0].velocity == Catch::Approx (0.5f));
}

TEST_CASE ("NoteTransposeNode falls back to its stored value when semitones/octaves are unconnected (NaN)",
           "[engine][nodes][NoteTransposeNode][NoteStream]")
{
    NoteTransposeNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("note.transpose.semitones", -5.0f);

    NoteEvent events[1] = { noteOn (60.0f, 0.5f) };
    node.consumeNoteBlock (events, 1);

    const float notesIn[1] = { 0.0f };
    const float semitonesIn[1] = { kNaN };
    const float octavesIn[1] = { kNaN };
    const float* const inputs[3] = { notesIn, semitonesIn, octavesIn };
    float dummyOut[1] = {};
    float* const outputs[1] = { dummyOut };
    node.processBlock (inputs, outputs, 1);

    NoteEvent produced[1] = {};
    node.produceNoteBlock (produced, 1);

    CHECK (produced[0].pitch == Catch::Approx (55.0f));
}

// ---- note.filter ----

TEST_CASE ("NoteFilterNode passes a note through unchanged when it's in range",
           "[engine][nodes][NoteFilterNode][NoteStream]")
{
    NoteFilterNode node;
    node.prepare ({ 44100.0, 512 });

    NoteEvent events[1] = { noteOn (64.0f, 0.5f) };
    node.consumeNoteBlock (events, 1);

    const float notesIn[1] = { 0.0f };
    const float lowPitch[1] = { 60.0f };
    const float highPitch[1] = { 72.0f };
    const float lowVel[1] = { 0.0f };
    const float highVel[1] = { 1.0f };
    const float* const inputs[5] = { notesIn, lowPitch, highPitch, lowVel, highVel };
    float notesOut[1] = {}, inRangeOut[1] = {};
    float* const outputs[2] = { notesOut, inRangeOut };
    node.processBlock (inputs, outputs, 1);

    NoteEvent produced[1] = {};
    node.produceNoteBlock (produced, 1);

    CHECK (inRangeOut[0] == 1.0f);
    CHECK (produced[0].gate);
    CHECK (produced[0].pitch == Catch::Approx (64.0f));
    CHECK (produced[0].startEvent);
}

TEST_CASE ("NoteFilterNode suppresses a note entirely when it's out of range",
           "[engine][nodes][NoteFilterNode][NoteStream]")
{
    NoteFilterNode node;
    node.prepare ({ 44100.0, 512 });

    NoteEvent events[1] = { noteOn (80.0f, 0.5f) }; // above the range below
    node.consumeNoteBlock (events, 1);

    const float notesIn[1] = { 0.0f };
    const float lowPitch[1] = { 60.0f };
    const float highPitch[1] = { 72.0f };
    const float lowVel[1] = { 0.0f };
    const float highVel[1] = { 1.0f };
    const float* const inputs[5] = { notesIn, lowPitch, highPitch, lowVel, highVel };
    float notesOut[1] = {}, inRangeOut[1] = {};
    float* const outputs[2] = { notesOut, inRangeOut };
    node.processBlock (inputs, outputs, 1);

    NoteEvent produced[1] = {};
    node.produceNoteBlock (produced, 1);

    CHECK (inRangeOut[0] == 0.0f);
    CHECK_FALSE (produced[0].gate);
    CHECK_FALSE (produced[0].startEvent);
}

// ---- note.humanize ----

TEST_CASE ("NoteHumanizeNode is a transparent passthrough when every amount is zero",
           "[engine][nodes][NoteHumanizeNode][NoteStream]")
{
    NoteHumanizeNode node;
    node.prepare ({ 44100.0, 512 });
    node.reset();

    NoteEvent events[1] = { noteOn (60.0f, 0.5f) };
    node.consumeNoteBlock (events, 1);

    const float notesIn[1] = { 0.0f };
    const float zero[1] = { 0.0f };
    const float* const inputs[4] = { notesIn, zero, zero, zero };
    float dummyOut[1] = {};
    float* const outputs[1] = { dummyOut };
    node.processBlock (inputs, outputs, 1);

    NoteEvent produced[1] = {};
    node.produceNoteBlock (produced, 1);

    CHECK (produced[0].startEvent);
    CHECK (produced[0].pitch == Catch::Approx (60.0f));
    CHECK (produced[0].velocity == Catch::Approx (0.5f));
}

TEST_CASE ("NoteHumanizeNode's timing delays a note-on into a later sample", "[engine][nodes][NoteHumanizeNode][NoteStream]")
{
    constexpr int numSamples = 2205; // 50ms at 44100Hz - the full possible jitter window

    NoteHumanizeNode node;
    node.prepare ({ 44100.0, numSamples }); // scratch buffers must cover the whole block this test runs in one call
    node.reset();
    node.setParameter ("note.humanize.seed", 42.0f);

    std::vector<NoteEvent> events (numSamples);
    events[0] = noteOn (60.0f, 0.5f);

    node.consumeNoteBlock (events.data(), numSamples);

    std::vector<const float*> inputRows (4);
    std::vector<float> notesIn (numSamples, 0.0f), timing (numSamples, 1.0f), zero (numSamples, 0.0f);
    inputRows[0] = notesIn.data();
    inputRows[1] = timing.data();
    inputRows[2] = zero.data();
    inputRows[3] = zero.data();
    std::vector<float> dummyOut (numSamples);
    float* const outputs[1] = { dummyOut.data() };
    node.processBlock (inputRows.data(), outputs, numSamples);

    std::vector<NoteEvent> produced (numSamples);
    node.produceNoteBlock (produced.data(), numSamples);

    CHECK_FALSE (produced[0].startEvent); // delayed, not immediate

    auto firedAt = -1;
    for (int i = 0; i < numSamples; ++i)
        if (produced[(size_t) i].startEvent)
            firedAt = i;

    REQUIRE (firedAt > 0);
    CHECK (produced[(size_t) firedAt].pitch != Catch::Approx (0.0f)); // a real note, not a default-constructed one
}

TEST_CASE ("NoteHumanizeNode is deterministic for a given seed", "[engine][nodes][NoteHumanizeNode][NoteStream]")
{
    auto runFor = [&] (float seed)
    {
        NoteHumanizeNode node;
        node.prepare ({ 44100.0, 512 });
        node.reset();
        node.setParameter ("note.humanize.seed", seed);
        node.setParameter ("note.humanize.pitch", 1.0f);
        node.setParameter ("note.humanize.velocity", 1.0f);

        NoteEvent events[1] = { noteOn (60.0f, 0.5f) };
        node.consumeNoteBlock (events, 1);

        const float notesIn[1] = { 0.0f };
        const float zero[1] = { 0.0f };
        const float one[1] = { 1.0f };
        const float* const inputs[4] = { notesIn, zero, one, one };
        float dummyOut[1] = {};
        float* const outputs[1] = { dummyOut };
        node.processBlock (inputs, outputs, 1);

        NoteEvent produced[1] = {};
        node.produceNoteBlock (produced, 1);
        return produced[0].pitch;
    };

    const auto a1 = runFor (7.0f);
    const auto a2 = runFor (7.0f);
    const auto b = runFor (8.0f);
    CHECK (a1 == a2);
    CHECK (a1 != b);
}

// ---- note.quantize ----

TEST_CASE ("NoteQuantizeNode snaps pitch to the nearest/up/down scale degree",
           "[engine][nodes][NoteQuantizeNode][NoteStream]")
{
    DataPublisher publisher;
    publisher.publish (std::make_unique<DataBuffer> (DataTag::Scale, std::vector<float> { 0, 4, 7 }, 1));

    auto runFor = [&] (float direction)
    {
        NoteQuantizeNode node;
        node.prepare ({ 44100.0, 512 });
        node.reset();
        node.setDataInput ("scale", &publisher);
        node.setParameter ("note.quantize.direction", direction);

        NoteEvent events[1] = { noteOn (63.0f, 0.5f) }; // 60(-3),64(+1) are the nearest degrees either side
        node.consumeNoteBlock (events, 1);

        const float notesIn[1] = { 0.0f };
        const float rootIn[1] = { 0.0f };
        const float strengthIn[1] = { 1.0f };
        const float* const inputs[4] = { notesIn, nullptr, rootIn, strengthIn };
        float dummyOut[1] = {};
        float* const outputs[1] = { dummyOut };
        node.processBlock (inputs, outputs, 1);

        NoteEvent produced[1] = {};
        node.produceNoteBlock (produced, 1);
        return produced[0].pitch;
    };

    CHECK (runFor (0.0f) == Catch::Approx (64.0f)); // nearest: 64 is 1 semitone away, 60 is 3
    CHECK (runFor (1.0f) == Catch::Approx (64.0f)); // up
    CHECK (runFor (2.0f) == Catch::Approx (60.0f)); // down
}

TEST_CASE ("NoteQuantizeNode's strength blends between raw and quantized pitch",
           "[engine][nodes][NoteQuantizeNode][NoteStream]")
{
    DataPublisher publisher;
    publisher.publish (std::make_unique<DataBuffer> (DataTag::Scale, std::vector<float> { 0, 4, 7 }, 1));

    auto runFor = [&] (float strength)
    {
        NoteQuantizeNode node;
        node.prepare ({ 44100.0, 512 });
        node.reset();
        node.setDataInput ("scale", &publisher);

        NoteEvent events[1] = { noteOn (63.0f, 0.5f) };
        node.consumeNoteBlock (events, 1);

        const float notesIn[1] = { 0.0f };
        const float rootIn[1] = { 0.0f };
        const float strengthIn[1] = { strength };
        const float* const inputs[4] = { notesIn, nullptr, rootIn, strengthIn };
        float dummyOut[1] = {};
        float* const outputs[1] = { dummyOut };
        node.processBlock (inputs, outputs, 1);

        NoteEvent produced[1] = {};
        node.produceNoteBlock (produced, 1);
        return produced[0].pitch;
    };

    CHECK (runFor (0.0f) == Catch::Approx (63.0f));  // untouched
    CHECK (runFor (1.0f) == Catch::Approx (64.0f));  // fully quantized
    CHECK (runFor (0.5f) == Catch::Approx (63.5f));  // halfway
}

TEST_CASE ("NoteQuantizeNode's root is a post-quantization offset, independent of the scale's own rotation",
           "[engine][nodes][NoteQuantizeNode][NoteStream]")
{
    DataPublisher publisher;
    publisher.publish (std::make_unique<DataBuffer> (DataTag::Scale, std::vector<float> { 0, 4, 7 }, 1));

    NoteQuantizeNode node;
    node.prepare ({ 44100.0, 512 });
    node.reset();
    node.setDataInput ("scale", &publisher);

    NoteEvent events[1] = { noteOn (63.0f, 0.5f) }; // quantizes to 64 at strength 1
    node.consumeNoteBlock (events, 1);

    const float notesIn[1] = { 0.0f };
    const float rootIn[1] = { 2.0f };
    const float strengthIn[1] = { 1.0f };
    const float* const inputs[4] = { notesIn, nullptr, rootIn, strengthIn };
    float dummyOut[1] = {};
    float* const outputs[1] = { dummyOut };
    node.processBlock (inputs, outputs, 1);

    NoteEvent produced[1] = {};
    node.produceNoteBlock (produced, 1);

    CHECK (produced[0].pitch == Catch::Approx (66.0f)); // 64 + root(2)
}

TEST_CASE ("NoteQuantizeNode's onNoteOnOnly mode holds the target for the note's whole duration",
           "[engine][nodes][NoteQuantizeNode][NoteStream]")
{
    DataPublisher publisher;
    publisher.publish (std::make_unique<DataBuffer> (DataTag::Scale, std::vector<float> { 0, 4, 7 }, 1));

    NoteQuantizeNode node;
    node.prepare ({ 44100.0, 512 });
    node.reset();
    node.setDataInput ("scale", &publisher);
    node.setParameter ("note.quantize.applyTo", 1.0f); // onNoteOnOnly

    // Starts at 63 (quantizes to 64), then "bends" to 70 while still held (no new startEvent).
    NoteEvent events[2] = { noteOn (63.0f, 0.5f), held (70.0f, 0.5f) };
    node.consumeNoteBlock (events, 2);

    const float notesIn[2] = { 0.0f, 0.0f };
    const float rootIn[2] = { 0.0f, 0.0f };
    const float strengthIn[2] = { 1.0f, 1.0f };
    const float* const inputs[4] = { notesIn, nullptr, rootIn, strengthIn };
    float dummyOut[2] = {};
    float* const outputs[1] = { dummyOut };
    node.processBlock (inputs, outputs, 2);

    NoteEvent produced[2] = {};
    node.produceNoteBlock (produced, 2);

    CHECK (produced[0].pitch == Catch::Approx (64.0f));
    CHECK (produced[1].pitch == Catch::Approx (64.0f)); // still 64, ignores the "bend" to 70
}

TEST_CASE ("NoteQuantizeNode passes pitch through unchanged when nothing is wired to scale",
           "[engine][nodes][NoteQuantizeNode][NoteStream]")
{
    NoteQuantizeNode node;
    node.prepare ({ 44100.0, 512 });
    node.reset();

    NoteEvent events[1] = { noteOn (63.0f, 0.5f) };
    node.consumeNoteBlock (events, 1);

    const float notesIn[1] = { 0.0f };
    const float rootIn[1] = { 0.0f };
    const float strengthIn[1] = { 1.0f };
    const float* const inputs[4] = { notesIn, nullptr, rootIn, strengthIn };
    float dummyOut[1] = {};
    float* const outputs[1] = { dummyOut };
    node.processBlock (inputs, outputs, 1);

    NoteEvent produced[1] = {};
    node.produceNoteBlock (produced, 1);

    CHECK (produced[0].pitch == Catch::Approx (63.0f));
}

// ---- End-to-end: real compiled graph, Note AND Data wiring together ----

TEST_CASE ("A real compiled graph quantizes io.noteIn through data.scale via note.quantize, read back through note.value",
           "[engine][GraphCompiler][NoteStream]")
{
    auto factory = buildDefaultNodeFactory();

    NodeGraph graph;
    graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
    graph.addNode ({ "scale", "data.scale", {}, {}, {} }); // major, root 60 -> {0,2,4,5,7,9,11}
    graph.addNode ({ "quantize", "note.quantize", {}, {}, {} }); // nearest, strength 1 by default
    graph.addNode ({ "value", "note.value", {}, {}, {} });
    graph.addConnection ({ "noteIn", "notes", "quantize", "notes" });
    graph.addConnection ({ "scale", "data", "quantize", "scale" });
    graph.addConnection ({ "quantize", "notesOut", "value", "notes" });
    graph.setOutput ("value", "pitch");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    auto* noteInNode = dynamic_cast<IoNoteInNode*> (result.plan.nodes[(size_t) result.plan.nodeIdToSlot.at ("noteIn")].get());
    REQUIRE (noteInNode != nullptr);
    // 63.0 is exactly equidistant between the major scale's 62 and 64 (a real
    // tie the algorithm resolves deterministically but not usefully as a test
    // fixture) - 64.4 is unambiguously nearest to 64 alone.
    noteInNode->injectNoteOn (64.4f, 0.8f);

    result.plan.process (64);
    const auto output = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex]
                             .getBlock().getChannelPointer (0)[63];
    CHECK (output == Catch::Approx (64.0f));
}
