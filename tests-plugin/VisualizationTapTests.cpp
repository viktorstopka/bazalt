// M20 (B1): BazaltAudioProcessor::subscribeVisualizationTap/
// unsubscribeVisualizationTap — the full integration, end to end, through
// the real Standalone-app-shaped processor (buildVoiceProofGraph's default
// graph, real MIDI, real processBlock), not just ExecutionPlan in isolation
// (that's ExecutionPlanTapTests.cpp's job).
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/telemetry/Tap.h"

using namespace bazalt;

namespace
{
    void playNote (BazaltAudioProcessor& processor, int noteNumber)
    {
        juce::MidiBuffer noteOn;
        noteOn.addEvent (juce::MidiMessage::noteOn (1, noteNumber, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        processor.processBlock (buffer, noteOn);
    }
}

TEST_CASE ("subscribeVisualizationTap rejects an unknown node or port", "[plugin][telemetry][M20]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    CHECK_FALSE (processor.subscribeVisualizationTap ("no-such-node", "out", bazalt::engine::PreviewKind::Waveform));
    CHECK_FALSE (processor.subscribeVisualizationTap ("osc", "no-such-port", bazalt::engine::PreviewKind::Waveform));
}

TEST_CASE ("subscribeVisualizationTap on a real voice-domain port receives real pushed values",
           "[plugin][telemetry][M20]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    REQUIRE (processor.subscribeVisualizationTap ("osc", "out", bazalt::engine::PreviewKind::Waveform));
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:osc:out");
    REQUIRE (tap != nullptr);

    playNote (processor, 60);
    for (int i = 0; i < 5; ++i)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    // A real, playing oscillator's output is neither silent nor constant —
    // proves the tap is actually receiving live per-block pushes from
    // process(), not a single stale value from whenever it was subscribed.
    float readBack[512] {};
    const auto available = tap->readLatest (readBack, 512);
    REQUIRE (available > 0);

    bool sawNonZero = false;
    bool sawVariation = false;
    for (int i = 1; i < available; ++i)
    {
        if (readBack[i] != 0.0f) sawNonZero = true;
        if (readBack[i] != readBack[0]) sawVariation = true;
    }
    CHECK (sawNonZero);
    CHECK (sawVariation);
}

TEST_CASE ("subscribeVisualizationTap on instance.allocator's pitch output re-points as new notes trigger",
           "[plugin][telemetry][M20]")
{
    // instance.allocator's "pitch" output is an exact, predictable value
    // (the triggered note's own MIDI number) — ideal for proving the tap
    // actually follows the most-recently-triggered voice, not a fixed one.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    REQUIRE (processor.subscribeVisualizationTap ("allocator", "pitch", bazalt::engine::PreviewKind::Waveform));

    // Read the tap's own raw ring buffer directly (bypassing AnalysisThread,
    // which runs on its own timer/thread and isn't deterministic to await
    // in a unit test) — TelemetryHub::subscribeTap() is idempotent for an
    // already-subscribed name, returning the same stable Tap*.
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:allocator:pitch");
    REQUIRE (tap != nullptr);

    playNote (processor, 60);
    float readBack[512] {};
    auto available = tap->readLatest (readBack, 512);
    REQUIRE (available > 0);
    CHECK (readBack[available - 1] == 60.0f);

    // A second note (voice 0 is still Active, so this allocates a different
    // voice) — the tap should now reflect THAT voice's pitch instead.
    playNote (processor, 72);
    available = tap->readLatest (readBack, 512);
    REQUIRE (available > 0);
    CHECK (readBack[available - 1] == 72.0f);
}

TEST_CASE ("unsubscribeVisualizationTap stops a voice-domain tap from receiving further pushes",
           "[plugin][telemetry][M20]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    REQUIRE (processor.subscribeVisualizationTap ("allocator", "pitch", bazalt::engine::PreviewKind::Waveform));
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:allocator:pitch");
    REQUIRE (tap != nullptr);

    playNote (processor, 60);
    float readBack[512] {};
    auto available = tap->readLatest (readBack, 512);
    REQUIRE (available > 0);
    CHECK (readBack[available - 1] == 60.0f);

    processor.unsubscribeVisualizationTap ("allocator", "pitch");

    // After unsubscribing, the slot is freed and the plan's tap pointer is
    // cleared — re-subscribing must still work cleanly (no stale/dangling
    // state left behind from the first subscription).
    REQUIRE (processor.subscribeVisualizationTap ("allocator", "pitch", bazalt::engine::PreviewKind::Waveform));
    playNote (processor, 84);
    auto* reTap = processor.getTelemetryHub().subscribeTap ("node:allocator:pitch");
    REQUIRE (reTap != nullptr);
    available = reTap->readLatest (readBack, 512);
    REQUIRE (available > 0);
    CHECK (readBack[available - 1] == 84.0f);
}

// ---- ADR-0029 / CLEANUP.md P1 #5: a subscription survives a graph edit ----
//
// A tap pointer lives on an ExecutionPlan and every edit compiles new plans, so
// before ADR-0029 the first edit after subscribing silently detached the
// preview. These compare Tap::getTotalPushed() before and after, which (unlike
// readLatest) cannot be satisfied by stale data left in the ring.

namespace
{
    void runBlocks (BazaltAudioProcessor& processor, int numBlocks, float inputLevel = 0.0f)
    {
        for (int i = 0; i < numBlocks; ++i)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), inputLevel, 512);

            juce::MidiBuffer none;
            processor.processBlock (buffer, none);
        }
    }

    bazalt::engine::NodeGraph audioInToOutputGraph()
    {
        bazalt::engine::NodeGraph graph;
        graph.addNode ({ "in", "io.audioIn", {}, {}, {} });
        graph.addNode ({ "out", "io.output", {}, {}, {} });
        graph.addConnection ({ "in", "channel.0", "out", "in" });
        graph.setOutput ("out", "out");
        return graph;
    }
}

TEST_CASE ("A global-domain preview tap keeps receiving after a graph edit", "[plugin][telemetry][M20][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (audioInToOutputGraph()).success);

    REQUIRE (processor.subscribeVisualizationTap ("in", "channel.0", bazalt::engine::PreviewKind::Waveform));
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:in:channel.0");
    REQUIRE (tap != nullptr);

    runBlocks (processor, 3, 0.25f);
    const auto beforeEdit = tap->getTotalPushed();
    REQUIRE (beforeEdit > 0);

    // A structural parameter change recompiles and republishes every plan.
    REQUIRE (processor.getGraphEditController().setParameterValue ("in", "io.audioIn.bus", 1.0f).success);

    runBlocks (processor, 3, 0.25f);
    CHECK (tap->getTotalPushed() == beforeEdit + 3 * 512);
}

TEST_CASE ("A voice-domain preview tap keeps receiving after a graph edit, with no new note",
           "[plugin][telemetry][M20][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    REQUIRE (processor.subscribeVisualizationTap ("osc", "out", bazalt::engine::PreviewKind::Waveform));
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:osc:out");
    REQUIRE (tap != nullptr);

    playNote (processor, 60);
    runBlocks (processor, 2);
    const auto beforeEdit = tap->getTotalPushed();
    REQUIRE (beforeEdit > 0);

    REQUIRE (processor.getGraphEditController().setParameterValue ("osc", "osc.analog.shape", 1.0f).success);

    // The same voice is still held and no note-on arrives, so nothing here can
    // re-point the tap by luck: only the re-attach can keep it alive.
    runBlocks (processor, 3);
    CHECK (tap->getTotalPushed() > beforeEdit);
}

TEST_CASE ("A voice-domain preview tap is fed by exactly one voice at a time", "[plugin][telemetry][M20][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    REQUIRE (processor.subscribeVisualizationTap ("osc", "out", bazalt::engine::PreviewKind::Waveform));
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:osc:out");
    REQUIRE (tap != nullptr);

    // Two held notes = two sounding voices, each running the same plan topology
    // with the same tap attached. Only the most recent one may push.
    playNote (processor, 60);
    playNote (processor, 64);

    const auto before = tap->getTotalPushed();
    runBlocks (processor, 1);
    CHECK (tap->getTotalPushed() == before + 512);
}

TEST_CASE ("Unsubscribing after an edit stops the tap for good, and later edits don't revive it",
           "[plugin][telemetry][M20][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (audioInToOutputGraph()).success);

    REQUIRE (processor.subscribeVisualizationTap ("in", "channel.0", bazalt::engine::PreviewKind::Waveform));
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:in:channel.0");
    REQUIRE (tap != nullptr);

    REQUIRE (processor.getGraphEditController().setParameterValue ("in", "io.audioIn.bus", 1.0f).success);
    runBlocks (processor, 2);

    processor.unsubscribeVisualizationTap ("in", "channel.0");
    const auto atUnsubscribe = tap->getTotalPushed();

    runBlocks (processor, 2);
    REQUIRE (processor.getGraphEditController().setParameterValue ("in", "io.audioIn.bus", 0.0f).success);
    runBlocks (processor, 2);

    CHECK (tap->getTotalPushed() == atUnsubscribe);
}

TEST_CASE ("A subscription whose node is deleted is harmless, and re-attaches if the node returns",
           "[plugin][telemetry][M20][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (audioInToOutputGraph()).success);

    REQUIRE (processor.subscribeVisualizationTap ("in", "channel.0", bazalt::engine::PreviewKind::Waveform));
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:in:channel.0");
    REQUIRE (tap != nullptr);

    // Replace the graph with one that has no "in" node at all: the subscription
    // stays registered but attaches to nothing, and processing must be fine.
    bazalt::engine::NodeGraph other;
    other.addNode ({ "other", "io.audioIn", {}, {}, {} });
    other.addNode ({ "out", "io.output", {}, {}, {} });
    other.addConnection ({ "other", "channel.0", "out", "in" });
    other.setOutput ("out", "out");
    REQUIRE (controller.setGraph (other).success);
    runBlocks (processor, 2);

    const auto whileAbsent = tap->getTotalPushed();
    runBlocks (processor, 2);
    CHECK (tap->getTotalPushed() == whileAbsent);

    // The node comes back under the same id: the preview comes back with it.
    REQUIRE (controller.setGraph (audioInToOutputGraph()).success);
    runBlocks (processor, 2);
    CHECK (tap->getTotalPushed() == whileAbsent + 2 * 512);
}
