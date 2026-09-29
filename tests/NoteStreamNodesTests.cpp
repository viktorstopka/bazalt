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
#include "bazalt/engine/nodes/NoteAssembleNode.h"
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

    // note.assemble's own 5-input shape (revised, same session: trigger +
    // release Events merged into one gate Boolean): gate, pitch, velocity,
    // confidence, confidenceGate, one float per sample per input.
    std::vector<NoteEvent> assembleBlock (NoteAssembleNode& node,
                                           const std::vector<float>& gate,
                                           const std::vector<float>& pitch,
                                           const std::vector<float>& velocity,
                                           const std::vector<float>& confidence,
                                           const std::vector<float>& confidenceGate)
    {
        const auto n = (int) gate.size();
        // NoteAssembleNode preallocates its own scratch buffer in prepare()
        // (CLAUDE.md rule 2: no allocation on the audio thread) - skipping
        // this call is exactly the out-of-bounds-vector hang this
        // codebase's own history already names (MILESTONES.md's Note Stream
        // batch entry): MSVC's Debug STL blocks on an invisible assertion
        // dialog rather than a visible crash, silently hanging the test.
        node.prepare ({ 44100.0, n });
        const float* const inputs[5] = { gate.data(), pitch.data(), velocity.data(), confidence.data(), confidenceGate.data() };
        node.processBlock (inputs, nullptr, n);
        std::vector<NoteEvent> out ((size_t) n);
        node.produceNoteBlock (out.data(), n);
        return out;
    }

    std::vector<float> constBlock (int n, float value) { return std::vector<float> ((size_t) n, value); }
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

// ---- note.assemble ----
// The one node in this family that PRODUCES a Note stream from scratch
// (direct feedback: "no way to create a Note from scratch" - the gap this
// whole node exists to close), rather than reshaping one that already
// exists. Zero Note inputs, one Note output - no engine-limit concerns.

TEST_CASE ("NoteAssembleNode starts and ends a note on gate's rising/falling edge, ordinary MIDI semantics",
           "[engine][nodes][NoteAssembleNode][NoteStream]")
{
    NoteAssembleNode node;
    node.reset();

    // sample 0: gate rises. samples 1-2: held. sample 3: gate falls.
    const auto gate = std::vector<float> { 1.0f, 1.0f, 1.0f, 0.0f };
    const auto pitch = constBlock (4, 67.0f);
    const auto velocity = constBlock (4, 0.9f);
    const auto confidence = constBlock (4, 1.0f);
    const auto confidenceGate = constBlock (4, 0.5f);

    const auto out = assembleBlock (node, gate, pitch, velocity, confidence, confidenceGate);

    CHECK (out[0].startEvent);
    CHECK (out[0].gate);
    CHECK (out[0].pitch == Catch::Approx (67.0f));
    CHECK (out[0].velocity == Catch::Approx (0.9f));

    CHECK_FALSE (out[1].startEvent);
    CHECK (out[1].gate);
    CHECK_FALSE (out[2].startEvent);
    CHECK (out[2].gate);

    CHECK (out[3].stopEvent);
    CHECK_FALSE (out[3].gate);
}

TEST_CASE ("NoteAssembleNode: a full low-then-high gate cycle stops the old note before starting the new one",
           "[engine][nodes][NoteAssembleNode][NoteStream]")
{
    // Revised, same session: trigger/release Events merged into one gate
    // Boolean (direct feedback: "use gate for notes everywhere else"). A
    // level-based gate can't express "retrigger while still held" the way
    // two separate Events could - it needs a real low-then-high cycle
    // instead, which is exactly ordinary sequential-note MIDI semantics.
    NoteAssembleNode node;
    node.reset();

    const auto gate = std::vector<float> { 1.0f, 0.0f, 1.0f };
    const auto pitch = std::vector<float> { 60.0f, 60.0f, 72.0f };
    const auto velocity = constBlock (3, 1.0f);
    const auto confidence = constBlock (3, 1.0f);
    const auto confidenceGate = constBlock (3, 0.5f);

    const auto out = assembleBlock (node, gate, pitch, velocity, confidence, confidenceGate);

    CHECK (out[0].startEvent);
    CHECK (out[1].stopEvent); // gate fell - the first note ends
    CHECK_FALSE (out[1].gate);
    CHECK (out[2].startEvent); // gate rose again - a fresh note starts
    CHECK (out[2].pitch == Catch::Approx (72.0f));
}

TEST_CASE ("NoteAssembleNode tracks pitch continuously while held, but captures velocity only at the start",
           "[engine][nodes][NoteAssembleNode][NoteStream]")
{
    NoteAssembleNode node;
    node.reset();

    const auto gate = constBlock (3, 1.0f); // rises on sample 0, stays high throughout
    const auto pitch = std::vector<float> { 60.0f, 61.0f, 62.5f }; // a live bend/vibrato while held
    const auto velocity = std::vector<float> { 0.5f, 0.9f, 0.1f }; // changes after the start - must be ignored
    const auto confidence = constBlock (3, 1.0f);
    const auto confidenceGate = constBlock (3, 0.5f);

    const auto out = assembleBlock (node, gate, pitch, velocity, confidence, confidenceGate);

    CHECK (out[0].pitch == Catch::Approx (60.0f));
    CHECK (out[1].pitch == Catch::Approx (61.0f));
    CHECK (out[2].pitch == Catch::Approx (62.5f));

    CHECK (out[0].velocity == Catch::Approx (0.5f));
    CHECK (out[1].velocity == Catch::Approx (0.5f)); // held from the start, NOT re-read
    CHECK (out[2].velocity == Catch::Approx (0.5f));
}

TEST_CASE ("NoteAssembleNode suppresses a gate rise below confidenceGate, so a low-confidence reading can't spawn a note",
           "[engine][nodes][NoteAssembleNode][NoteStream]")
{
    NoteAssembleNode node;
    node.reset();

    const auto gate = constBlock (1, 1.0f);
    const auto pitch = constBlock (1, 60.0f);
    const auto velocity = constBlock (1, 1.0f);
    const auto confidence = constBlock (1, 0.2f); // below the gate
    const auto confidenceGate = constBlock (1, 0.5f);

    const auto out = assembleBlock (node, gate, pitch, velocity, confidence, confidenceGate);

    CHECK_FALSE (out[0].startEvent);
    CHECK_FALSE (out[0].gate);
}

TEST_CASE ("NoteAssembleNode auto-releases when confidence drops below gate, even while the gate input stays high",
           "[engine][nodes][NoteAssembleNode][NoteStream]")
{
    // The audio-pitch-tracking use case: a monophonic tracker has no
    // discrete note-off of its own, so a note ends when confidence drops
    // instead - the gate INPUT stays high for the whole block on purpose.
    NoteAssembleNode node;
    node.reset();

    const auto gate = constBlock (3, 1.0f);
    const auto pitch = constBlock (3, 60.0f);
    const auto velocity = constBlock (3, 1.0f);
    const auto confidence = std::vector<float> { 0.9f, 0.9f, 0.1f }; // drops below gate on sample 2
    const auto confidenceGate = constBlock (3, 0.5f);

    const auto out = assembleBlock (node, gate, pitch, velocity, confidence, confidenceGate);

    CHECK (out[0].gate);
    CHECK (out[1].gate);
    CHECK_FALSE (out[2].gate);
    CHECK (out[2].stopEvent);
}

TEST_CASE ("NoteAssembleNode with nothing wired but gate (NaN confidence) behaves as a plain generative gate+pitch source",
           "[engine][nodes][NoteAssembleNode][NoteStream]")
{
    // The purely generative use case: confidence/confidenceGate are never
    // wired at all (NaN, hasFallbackWhenUnconnected) - their defaults (1.0
    // confidence, 0.5 gate) must never block a plain gate.
    NoteAssembleNode node;
    node.reset();

    const auto gate = constBlock (2, 1.0f);
    const auto pitch = constBlock (2, 65.0f);
    const auto velocity = constBlock (2, 1.0f);
    const auto confidence = constBlock (2, kNaN);
    const auto confidenceGate = constBlock (2, kNaN);

    const auto out = assembleBlock (node, gate, pitch, velocity, confidence, confidenceGate);

    CHECK (out[0].startEvent);
    CHECK (out[0].gate);
    CHECK (out[1].gate); // still held - nothing auto-released it
}

TEST_CASE ("A real compiled graph assembles a Note from clock.pulse via adapt.gateLength, read back through note.value",
           "[engine][GraphCompiler][NoteAssembleNode][NoteStream]")
{
    // Also the real end-to-end proof of adapt.gateLength itself: a bare
    // Event tick turned into a timed Boolean gate, exactly the primitive
    // direct feedback asked for instead of "2 clocks, one retriggering the
    // other."
    auto factory = buildDefaultNodeFactory();

    NodeGraph graph;
    graph.addNode ({ "clk", "clock.pulse", {}, { { "clock.pulse.rate", 1000.0f } }, {} });
    graph.addNode ({ "gateLen", "adapt.gateLength", {}, { { "length", 0.05f } }, {} });
    graph.addNode ({ "pitchConst", "util.constant", {}, { { "util.constant.value", 67.0f } }, {} });
    graph.addNode ({ "assemble", "note.assemble", {}, {}, {} });
    graph.addNode ({ "value", "note.value", {}, {}, {} });
    graph.addConnection ({ "clk", "tick", "gateLen", "trigger" });
    graph.addConnection ({ "gateLen", "gate", "assemble", "gate" });
    graph.addConnection ({ "pitchConst", "out", "assemble", "pitch" });
    graph.addConnection ({ "assemble", "notes", "value", "notes" });
    graph.setOutput ("value", "pitch");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    const auto* outputPtr = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);

    float lastSample = 0.0f;
    for (int block = 0; block < 8; ++block) // ~11.6ms @44.1kHz - clock.pulse@1000Hz ticks and gateLen's 50ms gate both comfortably span this
    {
        result.plan.process (64);
        lastSample = outputPtr[63];
    }

    CHECK (lastSample == Catch::Approx (67.0f));
}
