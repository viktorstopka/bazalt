#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include <atomic>
#include <cmath>
#include <thread>

using namespace bazalt;

namespace
{
    float rms (const juce::AudioBuffer<float>& buffer, int channel)
    {
        double sumSquares = 0.0;
        const auto* data = buffer.getReadPointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sumSquares += (double) data[i] * (double) data[i];
        return (float) std::sqrt (sumSquares / buffer.getNumSamples());
    }
}

TEST_CASE ("addNode + connect + setParameterValue commands produce the expected compiled audio",
           "[plugin][GraphEditController][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    // Reroute svf's output through a new one-pole damping node instead of
    // straight into amp — proves add/connect/setParameter together produce
    // a graph that actually compiles differently and sounds differently.
    REQUIRE (controller.disconnect ("svf", "out", "amp", "audio").success);
    REQUIRE (controller.addNode ("filter.onepole", "damper", 100.0f, 100.0f).success);
    REQUIRE (controller.connect ("svf", "out", "damper", "in").success);
    REQUIRE (controller.connect ("damper", "out", "amp", "audio").success);
    REQUIRE (controller.setParameterValue ("damper", "filter.onepole.coefficient", 0.8f).success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    for (int block = 0; block < 19; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    CHECK (rms (buffer, 0) > 0.001f);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}

TEST_CASE ("Connecting into an already-wired input replaces the old connection instead of being rejected",
           "[plugin][GraphEditController][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    // The proof graph already wires svf.out -> amp.audio. Wiring a second
    // source (osc.out) into that same already-occupied input used to be
    // rejected outright by GraphCompiler's "Input port already connected"
    // check, rolling the whole command back (wiki/NODES_Gaps.md's
    // `occupied-port-rejects` finding) — it should now succeed and REPLACE
    // the old connection instead, exactly like dropping a new cable onto an
    // occupied jack on a real patchbay.
    const auto result = controller.connectWithAutoAdapt ("osc", "out", "amp", "audio");
    REQUIRE (result.success);

    const auto& connections = controller.getGraph().getConnections();

    auto targetingAmpAudio = 0;
    auto sourcedFromSvf = 0;
    auto sourcedFromOsc = 0;
    for (const auto& c : connections)
    {
        if (c.toNodeId == "amp" && c.toPortId == "audio")
        {
            ++targetingAmpAudio;
            if (c.fromNodeId == "svf") ++sourcedFromSvf;
            if (c.fromNodeId == "osc") ++sourcedFromOsc;
        }
    }

    CHECK (targetingAmpAudio == 1); // never two sources feeding one input
    CHECK (sourcedFromOsc == 1);    // the new connection is really there
    CHECK (sourcedFromSvf == 0);    // the old one was really replaced, not left dangling
}

TEST_CASE ("deleteNode and disconnect commands are reflected in the live graph and in compiled audio",
           "[plugin][GraphEditController][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    // Disconnecting svf's output from amp's audio input leaves it silent
    // (0 — an unconnected Audio port carries no fallback to fall back to,
    // unlike a Control port), so amp's audio*gain output is silent
    // regardless of gain — directly, audibly verifiable, not just a
    // graph-shape assertion. (Not disconnecting env from amp's own "gain"
    // input for this: since wiki/NODES_Gaps.md's `modulation-only-port` fix,
    // an unpatched gain now correctly falls back to unity — "just as loud
    // as before" — rather than silence, so that disconnect alone no longer
    // silences the voice; it isn't meant to any more.)
    REQUIRE (controller.disconnect ("svf", "out", "amp", "audio").success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    for (int block = 0; block < 5; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    CHECK (rms (buffer, 0) < 0.0001f); // amp.audio is silent -> whole voice is silent

    // deleteNode removes the node AND every connection touching it —
    // verified directly against the live graph.
    REQUIRE (controller.deleteNode ("env").success);
    CHECK (controller.getGraph().findNode ("env") == nullptr);

    for (const auto& connection : controller.getGraph().getConnections())
    {
        CHECK (connection.fromNodeId != "env");
        CHECK (connection.toNodeId != "env");
    }
}

TEST_CASE ("An invalid command is rejected and leaves the graph and compiled audio unchanged",
           "[plugin][GraphEditController][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    // 0.x arc, 2026-09-29: the constructor default is a plain master-out-only
    // graph now, not this test's own "osc"/"amp" shape — set explicitly.
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);
    const auto nodesBefore = controller.getGraph().getNodes().size();
    const auto connectionsBefore = controller.getGraph().getConnections().size();

    // Unknown node id — rejected before any mutation is even attempted.
    const auto badConnect = controller.connect ("does-not-exist", "out", "amp", "audio");
    CHECK_FALSE (badConnect.success);
    CHECK (badConnect.errorMessage.isNotEmpty());

    // Unknown node type — same.
    const auto badAdd = controller.addNode ("does.not.exist", "x", 0.0f, 0.0f);
    CHECK_FALSE (badAdd.success);
    CHECK (badAdd.errorMessage.isNotEmpty());

    // A connection to a port that doesn't exist on the target node — this
    // one DOES mutate the graph speculatively, attempts a recompile, fails
    // there, and must roll back (CLAUDE.md rule 5: a bad edit never
    // reaches the audio thread).
    const auto badPort = controller.connect ("osc", "out", "amp", "no-such-port");
    CHECK_FALSE (badPort.success);
    CHECK (badPort.errorMessage.isNotEmpty());

    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);

    // The processor must still work exactly as it did before any of the
    // rejected attempts — proving the previous plan really did stay live.
    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    for (int block = 0; block < 19; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    CHECK (rms (buffer, 0) > 0.01f);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}

TEST_CASE ("Concurrent graph edits never produce a torn read within a single processBlock call",
           "[plugin][GraphEditController][swap-under-load][NODE_EDITOR]")
{
    // Swap-under-load, exercised through the real command path (M2's
    // PlanSwapperTests.cpp proved the underlying mechanism directly; this
    // proves GraphEditController's recompile-and-publish path preserves
    // that guarantee end to end). A single util.constant node's output is
    // identical for every sample in a block by construction, which is what
    // makes "was this whole block computed against one plan generation"
    // trivially checkable — any two different samples in one block would
    // mean a torn/inconsistent read.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 64);

    bazalt::engine::NodeGraph graph;
    graph.addNode ({ "const", "util.constant", {}, { { "util.constant.value", 0.5f } }, {} });
    graph.setOutput ("const", "out");

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (graph).success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> primingBuffer (2, 64);
    primingBuffer.clear();
    processor.processBlock (primingBuffer, noteOn); // one active voice, held indefinitely (no envelope node in this graph)

    std::atomic<bool> stop { false };
    std::atomic<bool> discontinuityWithinBlock { false };
    std::atomic<bool> nonFiniteSample { false };
    std::atomic<uint64_t> blocksProcessed { 0 };

    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer empty;

        while (! stop.load (std::memory_order_relaxed))
        {
            buffer.clear();
            processor.processBlock (buffer, empty);

            const auto* data = buffer.getReadPointer (0);
            const auto first = data[0];

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (! std::isfinite (data[i]))
                    nonFiniteSample.store (true, std::memory_order_relaxed);
                if (data[i] != first)
                    discontinuityWithinBlock.store (true, std::memory_order_relaxed);
            }

            blocksProcessed.fetch_add (1, std::memory_order_relaxed);
        }
    });

    std::thread editThread ([&]
    {
        for (int i = 0; i < 500; ++i)
        {
            const auto value = 0.1f + 0.01f * (float) (i % 50);
            controller.setParameterValue ("const", "util.constant.value", value);
        }
    });

    editThread.join();
    stop.store (true, std::memory_order_relaxed);
    audioThread.join();

    CHECK_FALSE (discontinuityWithinBlock.load());
    CHECK_FALSE (nonFiniteSample.load());
    CHECK (blocksProcessed.load() > 0);
}

TEST_CASE ("applyBatch rolls back every mutation in the batch if the resulting graph doesn't compile",
           "[plugin][GraphEditController][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    const auto nodesBefore = controller.getGraph().getNodes().size();
    const auto connectionsBefore = controller.getGraph().getConnections().size();

    const auto result = controller.applyBatch ([] (bazalt::engine::NodeGraph& graph)
    {
        graph.addNode ({ "extra1", "util.constant", {}, {}, {} });
        graph.addNode ({ "extra2", "util.constant", {}, {}, {} });
        // Bad connection — "no-such-port" doesn't exist on util.constant —
        // makes the whole batch's recompile fail.
        graph.addConnection ({ "extra1", "out", "extra2", "no-such-port" });
    });

    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());

    // Neither extra1 nor extra2 should have survived the rollback.
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);
    CHECK (controller.getGraph().findNode ("extra1") == nullptr);
    CHECK (controller.getGraph().findNode ("extra2") == nullptr);
}

TEST_CASE ("A live-edited graph round-trips exactly through getStateAsJson/loadStateFromJson",
           "[plugin][GraphEditController][patch][NODE_EDITOR]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);
    REQUIRE (controller.addNode ("util.constant", "extra", 42.0f, -17.0f).success);
    REQUIRE (controller.setParameterValue ("extra", "util.constant.value", 0.75f).success);

    const auto json = processor.getStateAsJson();

    BazaltAudioProcessor reloaded;
    reloaded.prepareToPlay (44100.0, 512);
    REQUIRE (reloaded.loadStateFromJson (json));

    const auto* extra = reloaded.getGraphEditController().getGraph().findNode ("extra");
    REQUIRE (extra != nullptr);
    CHECK (extra->type == "util.constant");
    CHECK (extra->position.x == 42.0f);
    CHECK (extra->position.y == -17.0f);
    REQUIRE (extra->parameters.count ("util.constant.value") == 1);
    CHECK (extra->parameters.at ("util.constant.value") == 0.75f);
}

TEST_CASE ("A graph snapshot round-trips through PatchDocument/PatchSerializer and restores via setGraph",
           "[plugin][GraphEditController][M19][ADR-0025]")
{
    // Exercises exactly what the graphGetSnapshot/graphRestoreSnapshot
    // native functions do internally (PluginEditor.cpp) — undo/redo's only
    // two moving parts (ADR-0025), with no WebView involved.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("math.add", "extra", 10.0f, 20.0f).success);
    REQUIRE (controller.setParameterValue ("svf", "filter.svf.cutoff", 1234.0f).success);

    const auto snapshot = bazalt::engine::serializePatchToJson (
        bazalt::engine::PatchDocument::fromNodeGraph (controller.getGraph()), false);

    // Diverge further from the snapshot...
    REQUIRE (controller.deleteNode ("extra").success);
    REQUIRE (controller.setParameterValue ("svf", "filter.svf.cutoff", 999.0f).success);
    REQUIRE (controller.getGraph().findNode ("extra") == nullptr);

    // ...then "undo" by restoring the snapshot.
    const auto parsed = bazalt::engine::parsePatchFromJson (snapshot);
    REQUIRE (parsed.success);
    REQUIRE (controller.setGraph (parsed.document.toNodeGraph()).success);

    REQUIRE (controller.getGraph().findNode ("extra") != nullptr);
    CHECK (controller.getGraph().findNode ("svf")->parameters.at ("filter.svf.cutoff") == 1234.0f);
    CHECK (controller.getGraph().getNodes().size() == 7); // noteIn, allocator, osc, svf, env, amp, extra
}

TEST_CASE ("exportSnapshotToFile writes the live graph as pretty-printed, parseable JSON",
           "[plugin][GraphEditController][dev-export]")
{
    // Direct instruction ("build that", after being asked whether an
    // external session has any quick way to see a patch as it's built) —
    // the graphExportSnapshot native function (PluginEditor.cpp) does
    // exactly this against a fixed path; this test drives the same method
    // directly, no WebView involved, same convention as the snapshot
    // round-trip test just above.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("BazaltExportTests");
    const auto file = tempDir.getChildFile ("exported-patch-test.json");
    file.deleteFile();
    REQUIRE_FALSE (file.existsAsFile());

    const auto result = controller.exportSnapshotToFile (file);
    INFO (result.errorMessage);
    REQUIRE (result.success);
    REQUIRE (file.existsAsFile());

    const auto written = file.loadFileAsString();
    CHECK (written.contains ("\n")); // pretty-printed, not minified onto one line

    const auto parsed = bazalt::engine::parsePatchFromJson (written);
    REQUIRE (parsed.success);
    CHECK (parsed.document.toNodeGraph().getNodes().size() == controller.getGraph().getNodes().size());

    // Overwrites unconditionally on a second call, not appends/rejects.
    REQUIRE (controller.addNode ("math.add", "extra", 0.0f, 0.0f).success);
    REQUIRE (controller.exportSnapshotToFile (file).success);
    const auto reparsed = bazalt::engine::parsePatchFromJson (file.loadFileAsString());
    REQUIRE (reparsed.success);
    CHECK (reparsed.document.toNodeGraph().findNode ("extra") != nullptr);

    file.deleteFile();
    tempDir.deleteRecursively();
}

TEST_CASE ("moveNode updates position without disturbing the node's DSP object identity",
           "[plugin][GraphEditController][M19]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    auto* before = processor.getOriginVoicePlanSwapper (0, 0).peekCurrentPlan()->getNodeById ("osc");
    REQUIRE (before != nullptr);

    REQUIRE (controller.moveNode ("osc", 123.0f, 456.0f).success);
    CHECK (controller.getGraph().findNode ("osc")->position.x == 123.0f);
    CHECK (controller.getGraph().findNode ("osc")->position.y == 456.0f);

    // M17's state pool: a position-only edit doesn't change (id, type,
    // parameters), so the exact same compiled Node object survives —
    // moving a node mid-note must not reset its DSP state.
    auto* after = processor.getOriginVoicePlanSwapper (0, 0).peekCurrentPlan()->getNodeById ("osc");
    CHECK (after == before);

    CHECK_FALSE (controller.moveNode ("nonexistent", 0.0f, 0.0f).success);
}

TEST_CASE ("moveNode preserves a GLOBAL-domain node's DSP object identity too, not just a voice-domain one",
           "[plugin][GraphEditController][InstanceAllocator]")
{
    // Direct feedback: "moving a node's position restarts the whole
    // sound" - real, and specifically about the global domain (everything
    // from instance.sum onward: the Init Patch's own "pan"/"masterOut").
    // The sibling test above already covers a voice-domain node via
    // buildVoiceProofGraph(), which has no instance.sum/global domain at
    // all (hasGlobalDomain is false there) - this is the case it can't
    // reach.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildInitPatchGraph()).success);

    auto* before = processor.getGlobalPlanSwapper().peekCurrentPlan()->getNodeById ("pan");
    REQUIRE (before != nullptr);

    REQUIRE (controller.moveNode ("pan", 111.0f, 222.0f).success);
    CHECK (controller.getGraph().findNode ("pan")->position.x == 111.0f);

    auto* after = processor.getGlobalPlanSwapper().peekCurrentPlan()->getNodeById ("pan");
    CHECK (after == before);
}

TEST_CASE ("setProperty writes into NodeInstance::properties and round-trips through a snapshot",
           "[plugin][GraphEditController][M19]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.setProperty ("osc", "title", juce::var ("My Oscillator")).success);
    REQUIRE (controller.setProperty ("osc", "bypassed", juce::var (true)).success);

    const auto* node = controller.getGraph().findNode ("osc");
    REQUIRE (node != nullptr);
    CHECK (node->properties.at ("title").toString() == "My Oscillator");
    CHECK ((bool) node->properties.at ("bypassed"));

    const auto snapshot = bazalt::engine::serializePatchToJson (
        bazalt::engine::PatchDocument::fromNodeGraph (controller.getGraph()), false);
    const auto parsed = bazalt::engine::parsePatchFromJson (snapshot);
    REQUIRE (parsed.success);

    const auto restoredGraph = parsed.document.toNodeGraph();
    const auto* restoredNode = restoredGraph.findNode ("osc");
    REQUIRE (restoredNode != nullptr);
    CHECK (restoredNode->properties.at ("title").toString() == "My Oscillator");
    CHECK ((bool) restoredNode->properties.at ("bypassed"));

    CHECK_FALSE (controller.setProperty ("nonexistent", "title", juce::var ("x")).success);
}

TEST_CASE ("Wiring a growable group's spare port through the controller grows it; a removed cable leaves a hole",
           "[plugin][GraphEditController][PortGroups][M21]")
{
    // This is the exact path the editor takes when a cable is dropped on the
    // spare `in.N` port revealed after the last wired one: the controller has
    // to accept a port that the node's DEFAULT descriptor doesn't list yet.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("math.add", "sum", 0.0f, 0.0f).success);
    for (const auto* id : { "k0", "k1", "k2" })
        REQUIRE (controller.addNode ("util.constant", id, 0.0f, 0.0f).success);

    REQUIRE (controller.connectWithAutoAdapt ("k0", "out", "sum", "in.0").success);
    REQUIRE (controller.connectWithAutoAdapt ("k1", "out", "sum", "in.1").success);
    REQUIRE (controller.connectWithAutoAdapt ("k2", "out", "sum", "in.2").success); // beyond the default two ports

    auto connectionsInto = [&] (const juce::String& nodeId)
    {
        std::vector<juce::String> ports;
        for (const auto& c : controller.getGraph().getConnections())
            if (c.toNodeId == nodeId)
                ports.push_back (c.toPortId);
        return ports;
    };

    CHECK (connectionsInto ("sum").size() == 3);

    // Remove the middle cable: in.0 and in.2 stay exactly where they were.
    REQUIRE (controller.disconnect ("k1", "out", "sum", "in.1").success);
    const std::vector<juce::String> expected { "in.0", "in.2" };
    CHECK (connectionsInto ("sum") == expected);

    // The port list is a function of the connections, so this still compiles
    // and a later cable into the hole is accepted.
    REQUIRE (controller.connect ("k1", "out", "sum", "in.1").success);
    CHECK (connectionsInto ("sum").size() == 3);
}

TEST_CASE ("Wiring past a growable group's maximum is rejected and leaves the graph unchanged",
           "[plugin][GraphEditController][PortGroups][M21]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("math.add", "sum", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("util.constant", "k", 0.0f, 0.0f).success);
    REQUIRE (controller.connect ("k", "out", "sum", "in.15").success); // the last valid port

    const auto before = controller.getGraph().getConnections().size();

    CHECK_FALSE (controller.connect ("k", "out", "sum", "in.16").success);
    CHECK_FALSE (controller.connectWithAutoAdapt ("k", "out", "sum", "in.16").success);
    CHECK_FALSE (controller.connect ("k", "out", "sum", "in.03").success); // non-canonical id names no port
    CHECK (controller.getGraph().getConnections().size() == before);
}

TEST_CASE ("A Reroute accepts a non-Audio cable through the controller, and forwards it to a matching port",
           "[plugin][GraphEditController][Reroute]")
{
    // The Priority-1 Reroute fix was first tested only against GraphCompiler.
    // The editor goes through connectWithAutoAdapt, which pre-checks with
    // canConnect against the node's DEFAULT descriptor (Audio) — so this is the
    // path that has to work for the fix to be real for a user.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("util.constant", "k", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("util.reroute", "rr", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("math.subtract", "sub", 0.0f, 0.0f).success);

    const auto intoReroute = controller.connectWithAutoAdapt ("k", "out", "rr", "in");
    INFO (intoReroute.errorMessage);
    REQUIRE (intoReroute.success);

    const auto outOfReroute = controller.connectWithAutoAdapt ("rr", "out", "sub", "a");
    INFO (outOfReroute.errorMessage);
    CHECK (outOfReroute.success);
}

TEST_CASE ("A Reroute still rejects, through the controller, a downstream port its resolved type can't feed",
           "[plugin][GraphEditController][Reroute]")
{
    // Skipping the default-descriptor pre-check must not turn "polymorphic"
    // into "accepts anything": the compiler's resolved-type canConnect still
    // rejects, and a rejected command leaves the graph exactly as it was.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("io.noteIn", "notes", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("util.reroute", "rr", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("math.subtract", "sub", 0.0f, 0.0f).success);

    REQUIRE (controller.connectWithAutoAdapt ("notes", "notes", "rr", "in").success); // a Note cable into a Reroute

    const auto connectionsBefore = controller.getGraph().getConnections().size();
    const auto rejected = controller.connectWithAutoAdapt ("rr", "out", "sub", "a"); // Note -> Control
    CHECK_FALSE (rejected.success);
    CHECK (rejected.errorMessage.isNotEmpty());
    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);
}

TEST_CASE ("logic.select through the controller: data cables of any plain type, a fixed Boolean condition, and Note refused",
           "[plugin][GraphEditController][inheriting][M21]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("logic.select", "sel", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("logic.not", "cond", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("util.constant", "k", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("io.noteIn", "notes", 0.0f, 0.0f).success);

    // A Boolean cable on `condition`, a Control cable on `whenTrue`: both fine.
    CHECK (controller.connectWithAutoAdapt ("cond", "out", "sel", "condition").success);
    CHECK (controller.connectWithAutoAdapt ("k", "out", "sel", "whenTrue").success);

    // A Control cable must NOT be accepted on the fixed Boolean condition of an
    // already-resolved select — polymorphic nodes don't become "accepts anything".
    REQUIRE (controller.addNode ("util.constant", "k2", 0.0f, 0.0f).success);
    const auto connectionsBefore = controller.getGraph().getConnections().size();
    // (condition is already wired, so target a second select's condition instead)
    REQUIRE (controller.addNode ("logic.select", "sel2", 0.0f, 0.0f).success);
    CHECK_FALSE (controller.connectWithAutoAdapt ("k2", "out", "sel2", "condition").success);

    // A Note can't ride through a select: it isn't a plain per-sample value.
    CHECK_FALSE (controller.connectWithAutoAdapt ("notes", "notes", "sel2", "whenFalse").success);

    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);
}

TEST_CASE ("getNodeDomains() classifies every node as voice/global/mono after a real recompile, "
           "and a rejected command leaves it exactly as it was",
           "[plugin][GraphEditController][InstanceAllocator]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();

    // The Init Patch (no longer the constructor default since the 0.x arc's
    // master-out-only default, 2026-09-29 - set explicitly here instead):
    // its allocator + instance.sum -> everything should read "voice" or
    // "global" once it's compiled.
    REQUIRE (controller.setGraph (bazalt::engine::buildInitPatchGraph()).success);
    REQUIRE (controller.getNodeDomains().count ("allocator") == 1);
    CHECK (controller.getNodeDomains().at ("allocator") == "voice");
    REQUIRE (controller.getNodeDomains().count ("voiceMix") == 1);
    CHECK (controller.getNodeDomains().at ("voiceMix") == "global");
    REQUIRE (controller.getNodeDomains().count ("masterOut") == 1);
    CHECK (controller.getNodeDomains().at ("masterOut") == "global");

    // A plain audio-effect graph (no allocator at all) reads "mono" for
    // every node - the monoOnly branch, exercised separately from the
    // bridged one above.
    bazalt::engine::NodeGraph mono;
    mono.addNode ({ "in", "io.audioIn", {}, {}, {} });
    mono.addNode ({ "out", "io.output", {}, {}, {} });
    mono.addConnection ({ "in", "channel.0", "out", "in" });
    mono.setOutput ("out", "out");
    REQUIRE (controller.setGraph (mono).success);
    REQUIRE (controller.getNodeDomains().count ("in") == 1);
    CHECK (controller.getNodeDomains().at ("in") == "mono");
    CHECK (controller.getNodeDomains().at ("out") == "mono");

    // A rejected command must leave getNodeDomains() exactly as it was -
    // the same rollback contract every other piece of controller-owned
    // state (getGraph(), getHasGlobalDomain()) already gets. Uses a graph
    // shape where the ATTEMPTED (but never published) topology would
    // genuinely reclassify a node's domain if the rollback didn't hold -
    // a same-domains-either-way scenario (e.g. just a bad port id on an
    // already-settled graph) wouldn't actually exercise the rollback path,
    // since nothing about node membership would differ regardless.
    REQUIRE (controller.setGraph (bazalt::engine::buildInitPatchGraph()).success);
    REQUIRE (controller.addNode ("osc.analog", "orphanOsc", 0.0f, 0.0f).success);
    REQUIRE (controller.getNodeDomains().count ("orphanOsc") == 1);
    CHECK (controller.getNodeDomains().at ("orphanOsc") == "global"); // unconnected, fed by nothing

    const auto domainsBefore = controller.getNodeDomains();
    // Would reclassify orphanOsc to "voice" (fed by the voice-domain
    // allocator) IF this published - but "no-such-port" doesn't exist on
    // osc.analog, so GraphCompiler rejects it and nothing should change.
    const auto rejected = controller.connect ("allocator", "gate", "orphanOsc", "no-such-port");
    CHECK_FALSE (rejected.success);
    CHECK (controller.getNodeDomains() == domainsBefore);
    CHECK (controller.getNodeDomains().at ("orphanOsc") == "global"); // still, not "voice"
}
