// wiki/plans/DomainRedesign.md Batch 2 — the real multi-origin runtime.
// Verifies §10.4's own required end-to-end proofs: multiple simultaneous
// instance.allocate.voice origins actually run independently, and an
// internally-sequenced origin (clock -> gate length -> note.assemble ->
// spawn, no io.noteIn anywhere) genuinely plays with zero MIDI input —
// the concrete fix for the MIDI-independence bug §10.3 diagnoses.
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/NodeGraph.h"
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
        graph.addNode ({ idPrefix + "clock", "clock.pulse", {}, { { "clock.pulse.rate", rateHz } }, {} });
        graph.addNode ({ idPrefix + "gateLen", "adapt.gateLength", {}, { { "length", 0.5f } }, {} });
        graph.addNode ({ idPrefix + "assemble", "note.assemble", {}, { { "pitch", pitch } }, {} });
        graph.addNode ({ idPrefix + "alloc", "instance.allocate.voice", {}, {}, {} });
        graph.addNode ({ idPrefix + "osc", "osc.analog", {}, {}, {} });
        graph.addNode ({ idPrefix + "sum", "instance.sum", {}, {}, {} }); // instance.sum, DomainRedesign.md Batch 1b's rename

        graph.addConnection ({ idPrefix + "clock", "tick", idPrefix + "gateLen", "trigger" });
        graph.addConnection ({ idPrefix + "gateLen", "gate", idPrefix + "assemble", "gate" });
        graph.addConnection ({ idPrefix + "assemble", "notes", idPrefix + "alloc", "spawn" });
        graph.addConnection ({ idPrefix + "alloc", "pitch", idPrefix + "osc", "pitch" });
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

    graph.addNode ({ "mixdown", "mix.sum", {}, {}, {} });
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
    graph.addNode ({ "allocA", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "oscA", "osc.analog", {}, {}, {} });
    graph.addNode ({ "sumA", "instance.sum", {}, {}, {} });
    graph.addNode ({ "allocB", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "oscB", "osc.analog", {}, {}, {} });
    graph.addNode ({ "sumB", "instance.sum", {}, {}, {} });
    graph.addNode ({ "mixdown", "mix.sum", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });

    graph.addConnection ({ "allocA", "pitch", "oscA", "pitch" });
    graph.addConnection ({ "oscA", "out", "sumA", "in" });
    graph.addConnection ({ "allocB", "pitch", "oscB", "pitch" });
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
    graph.addNode ({ "alloc", "instance.allocate.voice", {}, {}, {} });
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "alloc", "pitch", "osc", "pitch" });
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
