// wiki/plans/DomainRedesign.md Batch 2 — the real multi-origin runtime.
// Verifies §10.4's own required end-to-end proofs: multiple simultaneous
// instance.allocate.voice origins actually run independently, and an
// internally-sequenced origin (clock -> gate length -> note.assemble ->
// spawn, no io.noteIn anywhere) genuinely plays with zero MIDI input —
// the concrete fix for the MIDI-independence bug §10.3 diagnoses.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/ProofGraphs.h"
#include "PluginProcessor.h"
#include "bazalt/engine/graph/NodeGraph.h"
#include "bazalt/engine/nodes/InstanceVoiceNode.h"
#include <cmath>

using namespace bazalt;

namespace
{
    using bazalt::engine::NodeGraph;

    float rms (const juce::AudioBuffer<float>& buffer, int channel)
    {
        double sumSquares = 0.0;
        const auto* data = buffer.getReadPointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sumSquares += (double) data[i] * (double) data[i];
        return (float) std::sqrt (sumSquares / buffer.getNumSamples());
    }

    // One origin driven purely by its own internal clock -> gate length ->
    // note.assemble chain — no io.noteIn anywhere, nothing for real MIDI to
    // poke at all. `idPrefix` keeps two instances of this shape from
    // colliding when both are added to the same graph. `rateHz` lets two
    // origins in the same graph tick at deliberately different rates.
    void addInternallySequencedOrigin (NodeGraph& graph, const juce::String& idPrefix, float rateHz, float pitch)
    {
        graph.addNode ({ idPrefix + "clock", "time.clock", {}, { { "time.clock.rate", rateHz } }, {} });
        graph.addNode ({ idPrefix + "gateLen", "time.gateLength", {}, { { "length", 0.5f } }, {} });
        graph.addNode ({ idPrefix + "assemble", "note.assemble", {}, { { "pitch", pitch } }, {} });
        graph.addNode ({ idPrefix + "alloc", "life.voice", {}, {}, {} });
        graph.addNode (bazalt::engine::withContent ({ idPrefix + "osc", "source.oscillator", {}, {}, {} }, bazalt::engine::CurveDocument::saw()));
        graph.addNode ({ idPrefix + "sum", "life.merge", {}, {}, {} }); // instance.sum, DomainRedesign.md Batch 1b's rename

        graph.addConnection ({ idPrefix + "clock", "tick", idPrefix + "gateLen", "trigger" });
        graph.addConnection ({ idPrefix + "gateLen", "gate", idPrefix + "assemble", "gate" });
        graph.addConnection ({ idPrefix + "assemble", "notes", idPrefix + "alloc", "spawn" });
        graph.addNode ({ (idPrefix + "osc") + "ToFreq", "math.pitchToFrequency", {}, {}, {} });
        graph.addConnection ({ idPrefix + "alloc", "pitch", (idPrefix + "osc") + "ToFreq", "pitch" });
        graph.addConnection ({ (idPrefix + "osc") + "ToFreq", "frequency", idPrefix + "osc", "source.oscillator.frequency" });
        graph.addConnection ({ idPrefix + "osc", "out", idPrefix + "sum", "in" });
    }
}

TEST_CASE ("An internally-sequenced origin (clock -> gate length -> note.assemble -> spawn, no io.noteIn) "
           "genuinely plays with zero MIDI input",
           "[plugin][DomainRedesign][MIDI-independence]")
{
    // The exact bug §10.3 diagnoses: VoiceManager's lanes only ever left
    // Idle via real host MIDI before this fix, so a purely graph-driven
    // trigger reached the allocator's own noteOn()/noteOff() correctly
    // (real M18 Note-port delivery) but never mattered, since the lane
    // never rendered in the first place.
    NodeGraph graph;
    addInternallySequencedOrigin (graph, "a", 50.0f, 67.0f); // 50 Hz - several ticks well within the test's render window
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "asum", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    const auto result = processor.getGraphEditController().setGraph (graph);
    INFO (result.errorMessage);
    REQUIRE (result.success);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer noMidi; // zero MIDI input, for every block, ever

    for (int block = 0; block < 40; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, noMidi);
    }

    const auto out = rms (buffer, 0);
    INFO ("rms = " << out);
    CHECK (out > 0.01f);
    CHECK (std::isfinite (out));

    // The origin's own bundle genuinely activated a voice - not just
    // "some noise happened to come out of the master bus".
    REQUIRE (processor.isOriginBundleActive (0));
    CHECK (processor.getOriginBundleOriginId (0) == "aalloc");

    auto anyVoiceEverRan = false;
    for (int v = 0; v < BazaltAudioProcessor::numVoices; ++v)
        if (processor.getOriginBundle (0).voiceManager.getStage (v) != bazalt::engine::VoiceStage::Idle)
            anyVoiceEverRan = true;
    CHECK (anyVoiceEverRan);
}

TEST_CASE ("Two independent origin/sum pairs, each driven by its own internal clock -> note.assemble chain, "
           "both audibly render with zero MIDI input - DomainRedesign.md sec 4 and sec 10.4",
           "[plugin][DomainRedesign][MIDI-independence]")
{
    // The concrete feature this whole redesign buys: a seq-driven main
    // voice and a separately-triggered sub-oscillator, each with its own
    // sequencing - two ordinary, unremarkable allocator/sum pairs.
    NodeGraph graph;
    addInternallySequencedOrigin (graph, "a", 47.0f, 60.0f);
    addInternallySequencedOrigin (graph, "b", 83.0f, 72.0f); // a deliberately different, co-prime-ish rate

    graph.addNode ({ "mixdown", "math.add", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "asum", "out", "mixdown", "in.0" });
    graph.addConnection ({ "bsum", "out", "mixdown", "in.1" });
    graph.addConnection ({ "mixdown", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    const auto result = processor.getGraphEditController().setGraph (graph);
    INFO (result.errorMessage);
    REQUIRE (result.success);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer noMidi;

    for (int block = 0; block < 40; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, noMidi);
    }

    const auto out = rms (buffer, 0);
    INFO ("rms = " << out);
    CHECK (out > 0.01f);
    CHECK (std::isfinite (out));

    // Both origins genuinely got their own bundle (deterministic slot
    // assignment: declaration order on a fresh processor, "a" first).
    REQUIRE (processor.isOriginBundleActive (0));
    REQUIRE (processor.isOriginBundleActive (1));
    CHECK (processor.getOriginBundleOriginId (0) == "aalloc");
    CHECK (processor.getOriginBundleOriginId (1) == "balloc");

    for (int bundle = 0; bundle < 2; ++bundle)
    {
        auto anyVoiceEverRan = false;
        for (int v = 0; v < BazaltAudioProcessor::numVoices; ++v)
            if (processor.getOriginBundle (bundle).voiceManager.getStage (v) != bazalt::engine::VoiceStage::Idle)
                anyVoiceEverRan = true;
        CHECK (anyVoiceEverRan);
    }
}

TEST_CASE ("getNodeDomains() labels nodes correctly across two simultaneous origins",
           "[plugin][DomainRedesign][GraphEditController]")
{
    NodeGraph graph;
    graph.addNode ({ "allocA", "life.voice", {}, {}, {} });
    graph.addNode (bazalt::engine::withContent ({ "oscA", "source.oscillator", {}, {}, {} }, bazalt::engine::CurveDocument::saw()));
    graph.addNode ({ "sumA", "life.merge", {}, {}, {} });
    graph.addNode ({ "allocB", "life.voice", {}, {}, {} });
    graph.addNode (bazalt::engine::withContent ({ "oscB", "source.oscillator", {}, {}, {} }, bazalt::engine::CurveDocument::saw()));
    graph.addNode ({ "sumB", "life.merge", {}, {}, {} });
    graph.addNode ({ "mixdown", "math.add", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });

    graph.addNode ({ "oscAToFreq", "math.pitchToFrequency", {}, {}, {} });
    graph.addConnection ({ "allocA", "pitch", "oscAToFreq", "pitch" });
    graph.addConnection ({ "oscAToFreq", "frequency", "oscA", "source.oscillator.frequency" });
    graph.addConnection ({ "oscA", "out", "sumA", "in" });
    graph.addNode ({ "oscBToFreq", "math.pitchToFrequency", {}, {}, {} });
    graph.addConnection ({ "allocB", "pitch", "oscBToFreq", "pitch" });
    graph.addConnection ({ "oscBToFreq", "frequency", "oscB", "source.oscillator.frequency" });
    graph.addConnection ({ "oscB", "out", "sumB", "in" });
    graph.addConnection ({ "sumA", "out", "mixdown", "in.0" });
    graph.addConnection ({ "sumB", "out", "mixdown", "in.1" });
    graph.addConnection ({ "mixdown", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (graph).success);

    REQUIRE (controller.getNodeDomains().count ("allocA") == 1);
    CHECK (controller.getNodeDomains().at ("allocA") == "voice");
    CHECK (controller.getNodeDomains().at ("oscA") == "voice");
    CHECK (controller.getNodeDomains().at ("allocB") == "voice");
    CHECK (controller.getNodeDomains().at ("oscB") == "voice");
    CHECK (controller.getNodeDomains().at ("sumA") == "global");
    CHECK (controller.getNodeDomains().at ("sumB") == "global");
    CHECK (controller.getNodeDomains().at ("mixdown") == "global");
    CHECK (controller.getNodeDomains().at ("masterout") == "global");
}

TEST_CASE ("An origin that disappears from the graph deactivates its bundle; a new one in the same slot "
           "starts with fresh voice state",
           "[plugin][DomainRedesign][GraphEditController]")
{
    NodeGraph graph;
    graph.addNode ({ "alloc", "life.voice", {}, {}, {} });
    graph.addNode (bazalt::engine::withContent ({ "osc", "source.oscillator", {}, {}, {} }, bazalt::engine::CurveDocument::saw()));
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addNode ({ "oscToFreq", "math.pitchToFrequency", {}, {}, {} });
    graph.addConnection ({ "alloc", "pitch", "oscToFreq", "pitch" });
    graph.addConnection ({ "oscToFreq", "frequency", "osc", "source.oscillator.frequency" });
    graph.addConnection ({ "osc", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (graph).success);
    REQUIRE (processor.isOriginBundleActive (0));

    // Delete the only origin - back to a plain mono-shaped graph (no
    // allocator at all left).
    NodeGraph withoutOrigin;
    withoutOrigin.addNode ({ "in", "io.audioIn", {}, {}, {} });
    withoutOrigin.addNode ({ "masterout", "io.output", {}, {}, {} });
    withoutOrigin.addConnection ({ "in", "channel.0", "masterout", "in" });
    withoutOrigin.setOutput ("masterout", "out");
    REQUIRE (controller.setGraph (withoutOrigin).success);
    CHECK_FALSE (processor.isOriginBundleActive (0));

    // A brand-new origin, same graph shape - lands back in slot 0 (the
    // first free one), with a freshly prepared VoiceManager (every voice
    // Idle), not whatever the deleted origin's own VoiceManager happened
    // to be holding.
    REQUIRE (controller.setGraph (graph).success);
    REQUIRE (processor.isOriginBundleActive (0));
    CHECK (processor.getOriginBundleOriginId (0) == "alloc");
    for (int v = 0; v < BazaltAudioProcessor::numVoices; ++v)
        CHECK (processor.getOriginBundle (0).voiceManager.getStage (v) == bazalt::engine::VoiceStage::Idle);
}

TEST_CASE ("getPortMultiplicity() reports poly for a voice-region node's ports (with the right originId), "
           "scalar for global nodes, and instance.sum's own mixed per-port shape - "
           "wiki/plans/DomainRedesign.md Batch 4",
           "[plugin][DomainRedesign][GraphEditController]")
{
    NodeGraph graph;
    graph.addNode ({ "alloc", "life.voice", {}, {}, {} });
    graph.addNode (bazalt::engine::withContent ({ "osc", "source.oscillator", {}, {}, {} }, bazalt::engine::CurveDocument::saw()));
    graph.addNode ({ "sum", "life.merge", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addNode ({ "oscToFreq", "math.pitchToFrequency", {}, {}, {} });
    graph.addConnection ({ "alloc", "pitch", "oscToFreq", "pitch" });
    graph.addConnection ({ "oscToFreq", "frequency", "osc", "source.oscillator.frequency" });
    graph.addConnection ({ "osc", "out", "sum", "in" });
    graph.addConnection ({ "sum", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (graph).success);

    const auto& ports = controller.getPortMultiplicity();

    REQUIRE (ports.count ("alloc") == 1);
    REQUIRE (ports.at ("alloc").count ("gate") == 1);
    CHECK (ports.at ("alloc").at ("gate").kind == "poly");
    CHECK (ports.at ("alloc").at ("gate").originId == "alloc");

    REQUIRE (ports.count ("osc") == 1);
    REQUIRE (ports.at ("osc").count ("out") == 1);
    CHECK (ports.at ("osc").at ("out").kind == "poly");
    CHECK (ports.at ("osc").at ("out").originId == "alloc");

    REQUIRE (ports.count ("masterout") == 1);
    CHECK (ports.at ("masterout").at ("in").kind == "scalar");
    CHECK (ports.at ("masterout").at ("in").originId.isEmpty());

    // instance.sum: "in" reduces this origin's Poly signal; "out" is
    // ordinary Scalar - the one real mixed-per-port shape (§2.4).
    REQUIRE (ports.count ("sum") == 1);
    CHECK (ports.at ("sum").at ("in").kind == "poly");
    CHECK (ports.at ("sum").at ("in").originId == "alloc");
    CHECK (ports.at ("sum").at ("out").kind == "scalar");

    const auto& badges = controller.getOriginBundleIndices();
    REQUIRE (badges.count ("alloc") == 1);
    CHECK (badges.at ("alloc") == 0); // the only origin on a fresh processor lands in slot 0
}

TEST_CASE ("A recompile enforces instance.allocate.voice.maxInstances for real, and the live badge "
           "numbers follow real voice activity - wiki/plans/DomainRedesign.md Batch 4",
           "[plugin][DomainRedesign][GraphEditController]")
{
    NodeGraph graph;
    graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
    graph.addNode ({ "alloc", "life.voice", {}, { { "life.voice.maxInstances", 2.0f } }, {} });
    graph.addNode (bazalt::engine::withContent ({ "osc", "source.oscillator", {}, {}, {} }, bazalt::engine::CurveDocument::saw()));
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "noteIn", "notes", "alloc", "spawn" });
    graph.addNode ({ "oscToFreq", "math.pitchToFrequency", {}, {}, {} });
    graph.addConnection ({ "alloc", "pitch", "oscToFreq", "pitch" });
    graph.addConnection ({ "oscToFreq", "frequency", "osc", "source.oscillator.frequency" });
    graph.addConnection ({ "osc", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graph).success);

    REQUIRE (processor.isOriginBundleActive (0));
    CHECK (processor.getOriginMaxVoices (0) == 2); // enforced, not just declared
    CHECK (processor.getOriginActiveVoiceCount (0) == 0);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer chord;
    chord.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    chord.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
    chord.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0); // a 3rd note - steals, never a 3rd real lane
    processor.processBlock (buffer, chord);

    CHECK (processor.getOriginActiveVoiceCount (0) == 2); // never past the enforced ceiling
}

TEST_CASE ("Disconnecting a Note source from instance.allocate.voice.spawn WHILE a note is held "
           "releases the gate instead of leaving it stuck at 1 forever",
           "[plugin][DomainRedesign][GraphEditController][NoteStream]")
{
    // Real, found-live bug: io.noteIn -> note.quantize -> alloc.spawn, held
    // note, then disconnect note.quantize's output from spawn (exactly the
    // user's own reported repro - "when I disconnect scale quantize from
    // Voice it sometimes gets stuck on gate being 1"). InstanceVoiceNode's
    // own gate/pitch/velocity state only ever changes on a Note-block
    // start/stop edge (consumeNoteBlock -> noteOn()/noteOff()) - once
    // nothing feeds "spawn" at all, consumeNoteBlock() is never called
    // again, so a gate that was true the instant the wire disappeared
    // stays true forever. GraphCompiler's own state-pool reuse (M17) then
    // carries that exact stuck C++ object forward across every later
    // recompile too, since removing an unrelated incoming connection never
    // changes this node's own (id, type, parameters) - the same mechanism
    // that correctly preserves a filter's memory across an edit was, for
    // this ONE node, preserving a stuck "note held forever" instead.
    NodeGraph graph;
    graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
    graph.addNode ({ "scale", "data.scale", {}, {}, {} });
    graph.addNode ({ "quantize", "note.quantize", {}, {}, {} });
    graph.addNode ({ "alloc", "life.voice", {}, {}, {} });
    graph.addNode (bazalt::engine::withContent ({ "osc", "source.oscillator", {}, {}, {} }, bazalt::engine::CurveDocument::saw()));
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "noteIn", "notes", "quantize", "notes" });
    graph.addConnection ({ "scale", "data", "quantize", "scale" });
    graph.addConnection ({ "quantize", "notesOut", "alloc", "spawn" });
    graph.addNode ({ "oscToFreq", "math.pitchToFrequency", {}, {}, {} });
    graph.addConnection ({ "alloc", "pitch", "oscToFreq", "pitch" });
    graph.addConnection ({ "oscToFreq", "frequency", "osc", "source.oscillator.frequency" });
    graph.addConnection ({ "osc", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graph).success);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    processor.processBlock (buffer, noteOn);

    auto* voiceNode = dynamic_cast<bazalt::engine::nodes::InstanceVoiceNode*> (
        processor.getOriginVoicePlanSwapper (0, 0).peekCurrentPlan()->getNodeById ("alloc"));
    REQUIRE (voiceNode != nullptr);
    REQUIRE (voiceNode->getGate()); // held, exactly as expected before the disconnect

    const auto disconnectResult = processor.getGraphEditController().disconnect ("quantize", "notesOut", "alloc", "spawn");
    INFO (disconnectResult.errorMessage);
    REQUIRE (disconnectResult.success);

    // The recompile may or may not have reused the exact same C++ object
    // (irrelevant to this test either way - re-fetch whatever is live now,
    // same as the real UI/audio path would see it).
    voiceNode = dynamic_cast<bazalt::engine::nodes::InstanceVoiceNode*> (
        processor.getOriginVoicePlanSwapper (0, 0).peekCurrentPlan()->getNodeById ("alloc"));
    REQUIRE (voiceNode != nullptr);
    CHECK_FALSE (voiceNode->getGate()); // the actual fix - not stuck at 1 forever

    // A real note-off arriving after the disconnect must not crash or
    // resurrect anything (there is no longer any wired path to spawn) -
    // silence, not a leftover drone, is what the master bus should show.
    juce::MidiBuffer noteOff;
    noteOff.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    processor.processBlock (buffer, noteOff);
    CHECK_FALSE (voiceNode->getGate());
}
