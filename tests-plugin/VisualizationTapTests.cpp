// M20 (B1): BazaltAudioProcessor::subscribeVisualizationTap/
// unsubscribeVisualizationTap — the full integration, end to end, through
// the real Standalone-app-shaped processor (buildVoiceProofGraph's default
// graph, real MIDI, real processBlock), not just ExecutionPlan in isolation
// (that's ExecutionPlanTapTests.cpp's job).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/telemetry/Tap.h"
#include "bazalt/engine/graph/ProofGraphs.h"

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

TEST_CASE ("09-28-InstanceAllocator.1 (part 3): a voice domain not reaching the output still dispatches "
           "real MIDI and its allocator's gate tap shows real movement - the user's own literal repro "
           "(Note In -> Voice -> Glance on gate, nothing wired to Master Out yet)",
           "[plugin][telemetry][InstanceAllocator]")
{
    using namespace bazalt::engine;

    // Shaped exactly like the reported patch: a voice chain that never
    // reaches the designated output (nothing plugged into masterOut at
    // all), plus a completely unrelated node feeding masterOut instead
    // (io.audioIn here stands in for "whatever else was on the canvas").
    // Before this fix, DomainSplitter::split() set monoOnly=true for the
    // WHOLE graph in this shape (masterOut isn't allocator-reachable),
    // and PluginProcessor::handleMidiEvent's `if (monoOnlyGraph...)
    // return;` meant MIDI was dispatched nowhere — the allocator's gate
    // never moved, matching the user's exact report ("I tried plugging
    // in Glance to the gate in instance allocator and it had 0
    // movement").
    NodeGraph graph;
    graph.addNode ({ "noteIn", "io.noteIn", { 40.0f, 40.0f }, {}, {} });
    graph.addNode ({ "allocator", "life.voice", { 340.0f, 40.0f }, {}, {} });
    graph.addNode ({ "audioIn", "io.audioIn", { 40.0f, 400.0f }, {}, {} });
    graph.addNode ({ "masterOut", "io.output", { 340.0f, 400.0f }, {}, {} });

    graph.addConnection ({ "noteIn", "notes", "allocator", "spawn" });
    graph.addConnection ({ "audioIn", "channel.0", "masterOut", "in" });
    graph.setOutput ("masterOut", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graph).success);

    REQUIRE (processor.subscribeVisualizationTap ("allocator", "gate", PreviewKind::Waveform));
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:allocator:gate");
    REQUIRE (tap != nullptr);

    playNote (processor, 60);

    float readBack[512] {};
    const auto available = tap->readLatest (readBack, 512);
    REQUIRE (available > 0);

    bool sawGateOn = false;
    for (int i = 0; i < available; ++i)
        if (readBack[i] != 0.0f) sawGateOn = true;
    CHECK (sawGateOn);
}

TEST_CASE ("subscribeVisualizationTap on a real voice-domain port receives real pushed values",
           "[plugin][telemetry][M20]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

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

TEST_CASE ("subscribeVisualizationTap on instance.allocate.voice's pitch output re-points as new notes trigger",
           "[plugin][telemetry][M20]")
{
    // instance.allocate.voice's "pitch" output is an exact, predictable value
    // (the triggered note's own MIDI number) — ideal for proving the tap
    // actually follows the most-recently-triggered voice, not a fixed one.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    // 0.x arc, 2026-09-29: the constructor default is a plain master-out-only
    // graph now, with no "allocator" node - set a real voice graph explicitly.
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

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
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

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
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (processor.subscribeVisualizationTap ("osc", "out", bazalt::engine::PreviewKind::Waveform));
    auto* tap = processor.getTelemetryHub().subscribeTap ("node:osc:out");
    REQUIRE (tap != nullptr);

    playNote (processor, 60);
    runBlocks (processor, 2);
    const auto beforeEdit = tap->getTotalPushed();
    REQUIRE (beforeEdit > 0);

    REQUIRE (processor.getGraphEditController().setParameterValue ("osc", "source.oscillator.amplitude", 0.9f).success);

    // The same voice is still held and no note-on arrives, so nothing here can
    // re-point the tap by luck: only the re-attach can keep it alive.
    runBlocks (processor, 3);
    CHECK (tap->getTotalPushed() > beforeEdit);
}

TEST_CASE ("A voice-domain preview tap is fed by exactly one voice at a time", "[plugin][telemetry][M20][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

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

// ---- ADR-0029: viewers (view.cycle / view.spectrum / view.meter) ----
//
// A viewer has one input and no outputs; its preview taps the buffer wired into
// that input.

namespace
{
    // audioIn -> output, plus the given viewers; none wired yet.
    bazalt::engine::NodeGraph graphWithViewers (std::initializer_list<std::pair<const char*, const char*>> viewers)
    {
        auto graph = audioInToOutputGraph();
        for (const auto& [id, type] : viewers)
            graph.addNode ({ id, type, {}, {}, {} });
        return graph;
    }

    void wire (BazaltAudioProcessor& processor, const char* viewerId)
    {
        const auto result = processor.getGraphEditController().connect ("in", "channel.0", viewerId, "in");
        INFO (result.errorMessage);
        REQUIRE (result.success);
    }

    bazalt::engine::Tap* tapFor (BazaltAudioProcessor& processor, const juce::String& nodeId, const juce::String& portId)
    {
        return processor.getTelemetryHub().subscribeTap ("node:" + nodeId + ":" + portId);
    }
}

TEST_CASE ("A viewer's preview taps the signal wired into its input", "[plugin][telemetry][view][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graphWithViewers ({ { "scope", "view.meter" } })).success);
    wire (processor, "scope");

    REQUIRE (processor.subscribeVisualizationTap ("scope", "in", bazalt::engine::PreviewKind::Waveform));
    auto* tap = tapFor (processor, "scope", "in");
    REQUIRE (tap != nullptr);

    runBlocks (processor, 2, 0.25f);
    CHECK (tap->getTotalPushed() == 2 * 512);

    float readBack[512] {};
    REQUIRE (tap->readLatest (readBack, 512) == 512);
    for (auto sample : readBack)
        CHECK (sample == 0.25f); // exactly what was wired in, unmodified
}

TEST_CASE ("A viewer placed before it is wired is pending, and comes alive when a cable arrives",
           "[plugin][telemetry][view][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graphWithViewers ({ { "scope", "view.meter" } })).success);

    // The port exists, nothing is wired to it: accepted, but silent.
    REQUIRE (processor.subscribeVisualizationTap ("scope", "in", bazalt::engine::PreviewKind::Waveform));
    auto* tap = tapFor (processor, "scope", "in");
    REQUIRE (tap != nullptr);

    runBlocks (processor, 2, 0.25f);
    CHECK (tap->getTotalPushed() == 0);

    wire (processor, "scope"); // a graph edit, so a recompile: the re-attach binds it

    runBlocks (processor, 2, 0.25f);
    CHECK (tap->getTotalPushed() == 2 * 512);

    // ... and unplugging it stops the flow again without breaking anything.
    REQUIRE (processor.getGraphEditController().disconnect ("in", "channel.0", "scope", "in").success);
    const auto atDisconnect = tap->getTotalPushed();
    runBlocks (processor, 2, 0.25f);
    CHECK (tap->getTotalPushed() == atDisconnect);
}

TEST_CASE ("A subscription to a port the node does not have is still refused",
           "[plugin][telemetry][view][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graphWithViewers ({ { "scope", "view.meter" } })).success);

    CHECK_FALSE (processor.subscribeVisualizationTap ("scope", "out", bazalt::engine::PreviewKind::Waveform)); // a viewer has no output
    CHECK_FALSE (processor.subscribeVisualizationTap ("no-such-viewer", "in", bazalt::engine::PreviewKind::Waveform));
}

TEST_CASE ("A source's own preview and two viewers on the same cable all receive it",
           "[plugin][telemetry][view][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (
                 graphWithViewers ({ { "scope", "view.cycle" }, { "meter", "view.meter" } })).success);
    wire (processor, "scope");
    wire (processor, "meter");

    // The source node's own preview on its output, and the two viewers on its cable:
    // three subscriptions, one buffer.
    REQUIRE (processor.subscribeVisualizationTap ("in", "channel.0", bazalt::engine::PreviewKind::Waveform));
    REQUIRE (processor.subscribeVisualizationTap ("scope", "in", bazalt::engine::PreviewKind::Waveform));
    REQUIRE (processor.subscribeVisualizationTap ("meter", "in", bazalt::engine::PreviewKind::Meter));

    auto* own = tapFor (processor, "in", "channel.0");
    auto* scope = tapFor (processor, "scope", "in");
    auto* meter = tapFor (processor, "meter", "in");
    REQUIRE ((own != nullptr && scope != nullptr && meter != nullptr));
    CHECK (own != scope);
    CHECK (scope != meter);

    runBlocks (processor, 2, 0.5f);
    CHECK (own->getTotalPushed() == 2 * 512);
    CHECK (scope->getTotalPushed() == 2 * 512);
    CHECK (meter->getTotalPushed() == 2 * 512);

    // They survive an edit together, too.
    REQUIRE (processor.getGraphEditController().setParameterValue ("in", "io.audioIn.bus", 1.0f).success);
    runBlocks (processor, 1);
    CHECK (own->getTotalPushed() == 3 * 512);
    CHECK (scope->getTotalPushed() == 3 * 512);
    CHECK (meter->getTotalPushed() == 3 * 512);
}

TEST_CASE ("view.cycle and view.meter take Audio and Control; view.spectrum takes Audio only; none takes a Note",
           "[plugin][view][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();

    bazalt::engine::NodeGraph graph;
    graph.addNode ({ "audio", "io.audioIn", {}, {}, {} });
    graph.addNode ({ "control", "util.constant", {}, { { "util.constant.value", 0.5f } }, {} });
    graph.addNode ({ "notes", "io.noteIn", {}, {}, {} });
    graph.addNode ({ "scope", "view.cycle", {}, {}, {} });
    graph.addNode ({ "spectrum", "view.spectrum", {}, {}, {} });
    graph.addNode ({ "meter", "view.meter", {}, {}, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "audio", "channel.0", "out", "in" });
    graph.setOutput ("out", "out");
    REQUIRE (controller.setGraph (graph).success);

    CHECK (controller.connect ("audio", "channel.0", "scope", "in").success);
    CHECK (controller.connect ("audio", "channel.0", "spectrum", "in").success);
    CHECK (controller.connect ("audio", "channel.0", "meter", "in").success);

    REQUIRE (controller.disconnect ("audio", "channel.0", "scope", "in").success);
    REQUIRE (controller.disconnect ("audio", "channel.0", "meter", "in").success);

    CHECK (controller.connect ("control", "out", "scope", "in").success);
    CHECK (controller.connect ("control", "out", "meter", "in").success);
    CHECK (controller.connect ("control", "out", "spectrum", "in").success); // one numeric signal (wiki/plans/DataAndWavetable.md D1)
    REQUIRE (controller.disconnect ("control", "out", "spectrum", "in").success);

    REQUIRE (controller.disconnect ("control", "out", "scope", "in").success);
    CHECK_FALSE (controller.connect ("notes", "notes", "scope", "in").success);
}

// ---- ADR-0029 step 3: a viewer's parameters become its tap's analysis settings ----

namespace
{
    // The settings the analysis thread would currently apply to this tap.
    bazalt::engine::TapSettings settingsOf (BazaltAudioProcessor& processor, bazalt::engine::Tap* tap)
    {
        auto& hub = processor.getTelemetryHub();
        bazalt::engine::TapSettings settings;
        auto found = false;

        for (size_t slot = 0; slot < bazalt::engine::TelemetryHub::maxTaps && ! found; ++slot)
        {
            if (hub.getTapBySlot (slot) == tap && hub.isSlotActive (slot))
            {
                settings = hub.getTapSettingsBySlot (slot);
                found = true;
            }
        }

        REQUIRE (found); // the tap must be in an active slot
        return settings;
    }
}

TEST_CASE ("A viewer's parameters set the analysis of its tap, and follow edits", "[plugin][telemetry][view][ADR-0029]")
{
    using bazalt::engine::MeterMode;

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (graphWithViewers ({ { "spectrum", "view.spectrum" }, { "meter", "view.meter" } })).success);
    wire (processor, "spectrum");
    wire (processor, "meter");

    REQUIRE (processor.subscribeVisualizationTap ("spectrum", "in", bazalt::engine::PreviewKind::Spectrum));
    REQUIRE (processor.subscribeVisualizationTap ("meter", "in", bazalt::engine::PreviewKind::Meter));
    auto* spectrumTap = tapFor (processor, "spectrum", "in");
    auto* meterTap = tapFor (processor, "meter", "in");

    // As placed: every default.
    auto spectrum = settingsOf (processor, spectrumTap);
    CHECK (spectrum.fftOrder == 11);
    CHECK (spectrum.spectrumTiltDbPerOctave == 0.0f);
    CHECK (settingsOf (processor, meterTap).meterMode == MeterMode::Peak);

    // Each edit recompiles; the re-attach must pick the new value up.
    REQUIRE (controller.setParameterValue ("spectrum", "view.spectrum.fftSize", 4.0f).success);
    REQUIRE (controller.setParameterValue ("spectrum", "view.spectrum.tilt", 3.0f).success);
    REQUIRE (controller.setParameterValue ("spectrum", "view.spectrum.averaging", 0.5f).success);
    spectrum = settingsOf (processor, spectrumTap);
    CHECK (spectrum.fftOrder == 13);
    CHECK (spectrum.spectrumTiltDbPerOctave == Catch::Approx (3.0f));
    CHECK (spectrum.spectrumAveraging == Catch::Approx (0.5f));

    REQUIRE (controller.setParameterValue ("meter", "view.meter.mode", 2.0f).success);
    CHECK (settingsOf (processor, meterTap).meterMode == MeterMode::TruePeak);

    // Editing one viewer leaves the others alone.
    REQUIRE (controller.setParameterValue ("meter", "view.meter.mode", 1.0f).success);
    CHECK (settingsOf (processor, meterTap).meterMode == MeterMode::Rms);
    CHECK (settingsOf (processor, spectrumTap).fftOrder == 13);
}

TEST_CASE ("A viewer placed with saved parameters starts with them, and a preview on an ordinary node uses its declaration",
           "[plugin][telemetry][view][ADR-0029]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto graph = audioInToOutputGraph();
    graph.addNode ({ "spectrum", "view.spectrum", {}, { { "view.spectrum.fftSize", 4.0f } }, {} });
    graph.addConnection ({ "in", "channel.0", "spectrum", "in" });
    graph.addNode ({ "cycle", "view.cycle", {}, {}, {} });
    graph.addConnection ({ "in", "channel.0", "cycle", "in" });
    REQUIRE (processor.getGraphEditController().setGraph (graph).success);

    REQUIRE (processor.subscribeVisualizationTap ("spectrum", "in", bazalt::engine::PreviewKind::Spectrum));
    CHECK (settingsOf (processor, tapFor (processor, "spectrum", "in")).fftOrder == 13);

    // view.cycle declares a phase-locked preview that folds its real samples.
    REQUIRE (processor.subscribeVisualizationTap ("cycle", "out", bazalt::engine::PreviewKind::PhaseLocked));
    CHECK (settingsOf (processor, tapFor (processor, "cycle", "out")).phaseLockedFold);

    // osc.analog declares a phase-locked preview of its own waveform
    // (rendered from its snapshot, not folded).
    BazaltAudioProcessor voiceProcessor;
    voiceProcessor.prepareToPlay (44100.0, 512);
    REQUIRE (voiceProcessor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);
    REQUIRE (voiceProcessor.subscribeVisualizationTap ("osc", "out", bazalt::engine::PreviewKind::PhaseLocked));
    CHECK_FALSE (settingsOf (voiceProcessor, tapFor (voiceProcessor, "osc", "out")).phaseLockedFold);
}
