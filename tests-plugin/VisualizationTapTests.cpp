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

    CHECK_FALSE (processor.subscribeVisualizationTap ("no-such-node", "out"));
    CHECK_FALSE (processor.subscribeVisualizationTap ("osc", "no-such-port"));
}

TEST_CASE ("subscribeVisualizationTap on a real voice-domain port receives real pushed values",
           "[plugin][telemetry][M20]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    REQUIRE (processor.subscribeVisualizationTap ("osc", "out"));
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

    REQUIRE (processor.subscribeVisualizationTap ("allocator", "pitch"));

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

    REQUIRE (processor.subscribeVisualizationTap ("allocator", "pitch"));
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
    REQUIRE (processor.subscribeVisualizationTap ("allocator", "pitch"));
    playNote (processor, 84);
    auto* reTap = processor.getTelemetryHub().subscribeTap ("node:allocator:pitch");
    REQUIRE (reTap != nullptr);
    available = reTap->readLatest (readBack, 512);
    REQUIRE (available > 0);
    CHECK (readBack[available - 1] == 84.0f);
}
