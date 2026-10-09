#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/CurveData.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include <algorithm>
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
    REQUIRE (controller.disconnect ("svf", "out", "amp", "in.0").success);
    REQUIRE (controller.addNode ("filter.onepole", "damper", 100.0f, 100.0f).success);
    REQUIRE (controller.connect ("svf", "out", "damper", "in").success);
    REQUIRE (controller.connect ("damper", "out", "amp", "in.0").success);
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
    const auto result = controller.connectWithAutoAdapt ("osc", "out", "amp", "in.0");
    REQUIRE (result.success);

    const auto& connections = controller.getGraph().getConnections();

    auto targetingAmpAudio = 0;
    auto sourcedFromSvf = 0;
    auto sourcedFromOsc = 0;
    for (const auto& c : connections)
    {
        if (c.toNodeId == "amp" && c.toPortId == "in.0")
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

    // Disconnecting the oscillator from svf's audio input leaves the filter
    // silent (an unconnected Audio input reads 0), so the whole voice is
    // silent — directly, audibly verifiable, not just a graph-shape
    // assertion. (Not disconnecting svf from amp: amp is a Multiply since
    // schema v13, and its unwired input falls back to 1, so amp would then
    // pass the envelope through instead of going quiet.)
    REQUIRE (controller.disconnect ("osc", "out", "svf", "in").success);

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

    CHECK (rms (buffer, 0) < 0.0001f); // svf.in is silent -> whole voice is silent

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
    const auto badConnect = controller.connect ("does-not-exist", "out", "amp", "in.0");
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
    CHECK (controller.getGraph().getNodes().size() == 8); // noteIn, allocator, pitchToFreq, osc, svf, env, amp, extra
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
    REQUIRE (controller.addNode ("deco.reroute", "rr", 0.0f, 0.0f).success);
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
    REQUIRE (controller.addNode ("deco.reroute", "rr", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("math.subtract", "sub", 0.0f, 0.0f).success);

    REQUIRE (controller.connectWithAutoAdapt ("notes", "notes", "rr", "in").success); // a Note cable into a Reroute

    const auto connectionsBefore = controller.getGraph().getConnections().size();
    const auto rejected = controller.connectWithAutoAdapt ("rr", "out", "sub", "a"); // Note -> Control
    CHECK_FALSE (rejected.success);
    CHECK (rejected.errorMessage.isNotEmpty());
    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);
}

TEST_CASE ("math.blend through the controller: value cables of any plain type, any value as the Amount, and Note refused",
           "[plugin][GraphEditController][inheriting][M21]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("math.blend", "sel", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("logic.not", "cond", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("util.constant", "k", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("io.noteIn", "notes", 0.0f, 0.0f).success);

    // A Boolean cable on Amount (the old Select), a Control cable on B: both fine.
    CHECK (controller.connectWithAutoAdapt ("cond", "out", "sel", "math.blend.amount").success);
    CHECK (controller.connectWithAutoAdapt ("k", "out", "sel", "b").success);

    // A plain value on Amount is fine too.
    REQUIRE (controller.addNode ("util.constant", "k2", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("math.blend", "sel2", 0.0f, 0.0f).success);
    CHECK (controller.connectWithAutoAdapt ("k2", "out", "sel2", "math.blend.amount").success);
    const auto connectionsBefore = controller.getGraph().getConnections().size();

    // A Note can't ride through a blend: it isn't a plain per-sample value.
    CHECK_FALSE (controller.connectWithAutoAdapt ("notes", "notes", "sel2", "a").success);

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
    REQUIRE (controller.addNode ("source.oscillator", "orphanOsc", 0.0f, 0.0f).success);
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

// wiki/plans/UtilMacro.md, Batch 2 — deriveMacroMappings()/macroSlotCollisionError()
// (GraphEditController.cpp) and setMacroMappings() (PluginProcessor).

TEST_CASE ("Placing a util.macro node, claiming a slot, and wiring it into a real Control "
           "input makes host automation reach that target through a real processBlock",
           "[plugin][GraphEditController][macro]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("util.macro", "macro1", 0.0f, 0.0f).success);
    REQUIRE (controller.setParameterValue ("macro1", "util.macro.slot", 5.0f).success);
    REQUIRE (controller.setParameterValue ("macro1", "util.macro.min", 200.0f).success);
    REQUIRE (controller.setParameterValue ("macro1", "util.macro.max", 12000.0f).success);
    // svf already has a wired-free default (3000Hz, set on the node itself) -
    // wiring the macro in REPLACES that with the live mapped value, same
    // "dropping a new cable onto an occupied jack" rule any other cable
    // follows (see the "already-wired input" test above).
    REQUIRE (controller.connectWithAutoAdapt ("macro1", "out", "svf", "filter.svf.cutoff").success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    auto settleAndMeasure = [&] (float macroRawValue)
    {
        processor.getMacroParameter (5).setValueNotifyingHost (macroRawValue);

        juce::AudioBuffer<float> settled (2, 512);
        for (int block = 0; block < 10; ++block) // >> the 20ms smoothing ramp
        {
            settled.clear();
            juce::MidiBuffer empty;
            processor.processBlock (settled, empty);
        }
        return rms (settled, 0);
    };

    // A low cutoff (macro raw 0.0 -> mapped to 200Hz) attenuates a saw wave
    // through a resonant SVF far more than a high one (raw 1.0 -> 12000Hz) -
    // if the mapping never reached the real compiled node (Finding A's own
    // failure mode, or any other break in the add -> slot -> connect chain),
    // both would measure identically instead.
    const auto rmsLowCutoff = settleAndMeasure (0.0f);
    const auto rmsHighCutoff = settleAndMeasure (1.0f);

    CHECK (rmsHighCutoff > rmsLowCutoff * 1.5f);
}

TEST_CASE ("A Trigger-type util.macro connects directly into a real Event-typed input with no adapter "
           "silently inserted, and a Bool-type util.macro connects directly into a real Boolean-typed "
           "input instead of being rejected",
           "[plugin][GraphEditController][macro]")
{
    // Direct, reproducible feedback: "trigger macro cannot be plugged into
    // trigger. same for bool." Root cause: GraphEditController::
    // connectWithAutoAdapt's own pre-check (findOutputPort/findInputPort)
    // used to read a freshly-constructed, bare-default node's ports
    // instead of the REAL placed instance's — util.macro/util.constant's
    // own output type is config-driven (TypedValueNodeBase.h's
    // buildTypedOutputPort), always defaulting to Control, so a Trigger-
    // or Bool-configured macro was pre-checked as if it were still an
    // ordinary Control macro: a Control->Event mismatch silently spliced
    // in an adapt.threshold adapter instead of a direct wire, and a
    // Control->Boolean mismatch had no adapter at all (CanConnect.cpp only
    // has one for the REVERSE, Boolean->Control) and was flatly rejected.
    // GraphCompiler's own later, authoritative canConnect pass already
    // read post-setParameter ports correctly — only this earlier,
    // pre-GraphCompiler gate was guessing.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("util.macro", "triggerMacro", 0.0f, 0.0f).success);
    REQUIRE (controller.setParameterValue ("triggerMacro", "util.macro.slot", 10.0f).success);
    // commonParameters's own typeOptions order: control=0, bool=1, trigger=2.
    REQUIRE (controller.setParameterValue ("triggerMacro", "util.macro.type", 2.0f).success);
    REQUIRE (controller.addNode ("view.ripple", "ripple1", 200.0f, 0.0f).success);

    REQUIRE (controller.connectWithAutoAdapt ("triggerMacro", "out", "ripple1", "in").success);

    const auto& graphAfterTrigger = controller.getGraph();
    const auto hasThresholdAdapter = std::any_of (graphAfterTrigger.getNodes().begin(), graphAfterTrigger.getNodes().end(),
                                                   [] (const auto& n) { return n.type == "logic.threshold"; });
    CHECK_FALSE (hasThresholdAdapter);

    const auto& connectionsAfterTrigger = graphAfterTrigger.getConnections();
    const auto hasDirectTriggerWire = std::any_of (
        connectionsAfterTrigger.begin(), connectionsAfterTrigger.end(),
        [] (const auto& c) { return c.fromNodeId == "triggerMacro" && c.fromPortId == "out" && c.toNodeId == "ripple1" && c.toPortId == "in"; });
    CHECK (hasDirectTriggerWire);

    REQUIRE (controller.addNode ("util.macro", "boolMacro", 0.0f, 100.0f).success);
    REQUIRE (controller.setParameterValue ("boolMacro", "util.macro.slot", 11.0f).success);
    REQUIRE (controller.setParameterValue ("boolMacro", "util.macro.type", 1.0f).success);
    REQUIRE (controller.addNode ("source.envelope", "env1", 200.0f, 100.0f).success);

    REQUIRE (controller.connectWithAutoAdapt ("boolMacro", "out", "env1", "gate").success);
}

TEST_CASE ("Two util.macro nodes claiming the same slot are rejected with both node ids named, "
           "and the graph is left exactly as it was",
           "[plugin][GraphEditController][macro]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildMasterOutOnlyGraph()).success);

    REQUIRE (controller.addNode ("util.macro", "m1", 0.0f, 0.0f).success);
    REQUIRE (controller.setParameterValue ("m1", "util.macro.slot", 3.0f).success);

    REQUIRE (controller.addNode ("util.macro", "m2", 100.0f, 0.0f).success); // still unclaimed (-1) - no collision yet (Finding B)

    const auto graphBeforeRejection = controller.getGraph();
    const auto rejected = controller.setParameterValue ("m2", "util.macro.slot", 3.0f);

    CHECK_FALSE (rejected.success);
    CHECK (rejected.errorMessage.contains ("m1"));
    CHECK (rejected.errorMessage.contains ("m2"));
    CHECK (rejected.errorMessage.contains ("3"));

    // CLAUDE.md rule 5: a rejected compile leaves the previous valid plan
    // (and graph) live - m2 never actually got its "util.macro.slot" write
    // (setParameterValue rolls the whole NodeGraph back to previousGraph on
    // rejection, same as every other command), so the key is still simply
    // absent - which macroSlotOf() itself already treats as "unclaimed".
    const auto& graphAfter = controller.getGraph();
    REQUIRE (graphAfter.getNodes().size() == graphBeforeRejection.getNodes().size());
    for (const auto& node : graphAfter.getNodes())
    {
        if (node.id != "m2")
            continue;
        CHECK (node.parameters.count ("util.macro.slot") == 0);
    }
}

TEST_CASE ("Deleting a util.macro node frees its slot for a fresh claim on the very next recompile",
           "[plugin][GraphEditController][macro]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildMasterOutOnlyGraph()).success);

    REQUIRE (controller.addNode ("util.macro", "m1", 0.0f, 0.0f).success);
    REQUIRE (controller.setParameterValue ("m1", "util.macro.slot", 7.0f).success);

    REQUIRE (controller.addNode ("util.macro", "m2", 100.0f, 0.0f).success);
    CHECK_FALSE (controller.setParameterValue ("m2", "util.macro.slot", 7.0f).success); // still claimed by m1

    REQUIRE (controller.deleteNode ("m1").success);
    CHECK (controller.setParameterValue ("m2", "util.macro.slot", 7.0f).success); // slot 7 is free now
}

TEST_CASE ("Finding A: a macro wired into GLOBAL-domain content is still applied while voices are "
           "active, not just in the monoOnly case",
           "[plugin][GraphEditController][macro]")
{
    // buildInitPatchGraph() has both a real active origin (allocator) AND
    // real global-domain content downstream of its own instance.sum
    // ("voiceMix" -> "pan" -> "masterOut") - exactly the shape
    // PluginProcessor::processBlock's non-monoOnly branch used to never run
    // MacroParameters::applyToPlans over (the global plan was only ever
    // fetched later, inside finalizeInstanceMixIntoOutput, after
    // applyToPlans had already returned). Wiring a macro into "pan"'s own
    // Control input - real global-domain content - and checking that
    // sweeping the macro's raw value actually swings the stereo balance is
    // a direct, audible proof the mapping reaches it.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildInitPatchGraph()).success);

    REQUIRE (controller.addNode ("util.macro", "panMacro", 0.0f, 0.0f).success);
    REQUIRE (controller.setParameterValue ("panMacro", "util.macro.slot", 9.0f).success);
    REQUIRE (controller.setParameterValue ("panMacro", "util.macro.min", -1.0f).success);
    REQUIRE (controller.setParameterValue ("panMacro", "util.macro.max", 1.0f).success);
    REQUIRE (controller.connectWithAutoAdapt ("panMacro", "out", "pan", "space.pan.pan").success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    auto settleAndMeasureBalance = [&] (float macroRawValue)
    {
        processor.getMacroParameter (9).setValueNotifyingHost (macroRawValue);

        juce::AudioBuffer<float> settled (2, 512);
        for (int block = 0; block < 10; ++block)
        {
            settled.clear();
            juce::MidiBuffer empty;
            processor.processBlock (settled, empty);
        }
        return rms (settled, 0) - rms (settled, 1); // left-minus-right: sign flips with pan side
    };

    const auto balanceFullLeft = settleAndMeasureBalance (0.0f);  // mapped to pan = -1.0
    const auto balanceFullRight = settleAndMeasureBalance (1.0f); // mapped to pan = +1.0

    // If the mapping never reached the global plan, both would measure the
    // same (whatever pan's own unconnected-port default already gave it) -
    // the sign must flip between the two extremes.
    CHECK (balanceFullLeft > 0.01f);
    CHECK (balanceFullRight < -0.01f);
}

// wiki/plans/UtilMacro.md P2.1 (post-ship sweep) - GraphEditController::createMacro:
// one atomic recompile instead of the drag gesture's old 6-recompile
// addNode+5xsetParameterValue+setProperty sequence.

TEST_CASE ("createMacro adds a fully-configured util.macro node in one recompile",
           "[plugin][GraphEditController][macro]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildMasterOutOnlyGraph()).success);

    const auto result = controller.createMacro ("m1", 10.0f, 20.0f, 5, 200.0f, 12000.0f, false, 1 /* Frequency */, "Hz", 0.5f);
    REQUIRE (result.success);

    const auto& nodes = controller.getGraph().getNodes();
    const auto it = std::find_if (nodes.begin(), nodes.end(), [] (const auto& n) { return n.id == "m1"; });
    REQUIRE (it != nodes.end());
    CHECK (it->type == "util.macro");
    CHECK (it->position.x == 10.0f);
    CHECK (it->position.y == 20.0f);
    CHECK (it->parameters.at ("util.macro.slot") == 5.0f);
    CHECK (it->parameters.at ("util.macro.min") == 200.0f);
    CHECK (it->parameters.at ("util.macro.max") == 12000.0f);
    CHECK (it->parameters.at ("util.macro.isInteger") == 0.0f);
    CHECK (it->parameters.at ("util.macro.quantity") == 1.0f);
    CHECK (it->parameters.at ("util.macro.value") == 0.5f);
    REQUIRE (it->properties.count ("util.macro.unit") == 1);
    CHECK (it->properties.at ("util.macro.unit").toString() == "Hz");
}

// A macro dragged out of a Boolean/Event input is created Bool/Trigger typed,
// so its own output is that type and wires straight in (there is no
// Control->Boolean adapter), and an Int macro reaches a view.count with no
// Map inserted between them.
TEST_CASE ("createMacro's type makes a Bool/Trigger macro wire straight into Boolean/Event inputs",
           "[plugin][GraphEditController][macro]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildMasterOutOnlyGraph()).success);
    REQUIRE (controller.addNode ("view.scope", "gate", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("view.ripple", "ripple", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("view.count", "count", 0.0f, 0.0f).success);

    REQUIRE (controller.createMacro ("bool", 0.0f, 0.0f, 0, 0.0f, 1.0f, false, 0, "", 1.0f, 1 /* Bool */).success);
    REQUIRE (controller.createMacro ("trig", 0.0f, 0.0f, 1, 0.0f, 1.0f, false, 0, "", 0.0f, 2 /* Trigger */).success);
    REQUIRE (controller.createMacro ("int", 0.0f, 0.0f, 2, 0.0f, 16.0f, true, 1 /* Frequency */, "", 0.25f).success);
    CHECK (controller.getGraph().findNode ("bool")->parameters.at ("util.macro.type") == 1.0f);

    const auto nodesBefore = controller.getGraph().getNodes().size();
    CHECK (controller.connectWithAutoAdapt ("bool", "out", "gate", "in").success);
    CHECK (controller.connectWithAutoAdapt ("trig", "out", "ripple", "in").success);
    CHECK (controller.connectWithAutoAdapt ("int", "out", "count", "in").success);
    // Direct wires only - no adapter node was needed for any of them.
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
}

TEST_CASE ("createMacro rejects a slot collision as one atomic no-op - nothing is added at all",
           "[plugin][GraphEditController][macro]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildMasterOutOnlyGraph()).success);

    REQUIRE (controller.createMacro ("m1", 0.0f, 0.0f, 3, 0.0f, 1.0f, false, 0, "", 0.0f).success);

    const auto graphBefore = controller.getGraph();
    const auto rejected = controller.createMacro ("m2", 100.0f, 0.0f, 3, 0.0f, 1.0f, false, 0, "", 0.0f);

    CHECK_FALSE (rejected.success);
    CHECK (rejected.errorMessage.contains ("m1"));
    CHECK (rejected.errorMessage.contains ("3"));

    // Unlike the old multi-command gesture, there is no partial/orphaned
    // node left behind - the whole create is one atomic unit that either
    // fully lands or doesn't exist at all.
    const auto& graphAfter = controller.getGraph();
    REQUIRE (graphAfter.getNodes().size() == graphBefore.getNodes().size());
    CHECK (graphAfter.findNode ("m2") == nullptr);
}

TEST_CASE ("createMacro's slot survives into a real derived mapping, same as the old multi-command path",
           "[plugin][GraphEditController][macro]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.createMacro ("macro1", 0.0f, 0.0f, 7, 200.0f, 12000.0f, false, 0, "", 0.0f).success);
    REQUIRE (controller.connectWithAutoAdapt ("macro1", "out", "svf", "filter.svf.cutoff").success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    auto settleAndMeasure = [&] (float macroRawValue)
    {
        processor.getMacroParameter (7).setValueNotifyingHost (macroRawValue);
        juce::AudioBuffer<float> settled (2, 512);
        for (int block = 0; block < 10; ++block)
        {
            settled.clear();
            juce::MidiBuffer empty;
            processor.processBlock (settled, empty);
        }
        return rms (settled, 0);
    };

    const auto rmsLowCutoff = settleAndMeasure (0.0f);
    const auto rmsHighCutoff = settleAndMeasure (1.0f);
    CHECK (rmsHighCutoff > rmsLowCutoff * 1.5f);
}

// A type-following node (math.add here) takes on what's wired into it, so a
// Time-valued Add into a Frequency cutoff is a real quantity mismatch: it
// must get a Map, exactly as the same cable straight from the macro would —
// not go through unadapted and be refused by the compiler.
TEST_CASE ("connectWithAutoAdapt resolves a polymorphic source before choosing an adapter",
           "[plugin][GraphEditController][adapter]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);
    REQUIRE (controller.createMacro ("time", 0.0f, 0.0f, 0, 0.01f, 2.0f, false, 3 /* Time */, "", 0.5f).success);
    REQUIRE (controller.addNode ("math.add", "add", 0.0f, 0.0f).success);
    REQUIRE (controller.connectWithAutoAdapt ("time", "out", "add", "in.0").success);

    const auto result = controller.connectWithAutoAdapt ("add", "out", "svf", "filter.svf.cutoff");
    INFO (result.errorMessage);
    REQUIRE (result.success);

    const auto& nodes = controller.getGraph().getNodes();
    CHECK (std::any_of (nodes.begin(), nodes.end(), [] (const auto& n) { return n.type == "math.map"; }));
}

// One voice allocator feeding two Voice Sums: a main chain and a separate
// layer, each summed on its own and added only after both (the Plate layer
// case — it must not run through the main chain's effects, and summing it
// into the mono chain later must not turn that chain back into Poly).
TEST_CASE ("Two instance.sum nodes reducing one origin each carry that origin's voices",
           "[plugin][GraphEditController][instanceSum]")
{
    auto buildGraph = [] (float mainGain, float layerGain)
    {
        bazalt::engine::NodeGraph graph;
        graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
        graph.addNode ({ "alloc", "life.voice", {}, {}, {} });
        graph.addNode (bazalt::engine::withContent ({ "osc", "source.oscillator", {}, {}, {} }, bazalt::engine::CurveDocument::saw()));
        graph.addNode ({ "sumMain", "life.merge", {}, {}, {} });
        graph.addNode ({ "sumLayer", "life.merge", {}, {}, {} });
        graph.addNode ({ "gainMain", "math.multiply", {}, { { "in.1", mainGain } }, {} });
        graph.addNode ({ "gainLayer", "math.multiply", {}, { { "in.1", layerGain } }, {} });
        graph.addNode ({ "add", "math.add", {}, {}, {} });
        graph.addNode ({ "masterOut", "io.output", {}, {}, {} });

        graph.addConnection ({ "noteIn", "notes", "alloc", "spawn" });
        graph.addNode ({ "oscToFreq", "math.pitchToFrequency", {}, {}, {} });
        graph.addConnection ({ "alloc", "pitch", "oscToFreq", "pitch" });
        graph.addConnection ({ "oscToFreq", "frequency", "osc", "source.oscillator.frequency" });
        graph.addConnection ({ "osc", "out", "sumMain", "in" });
        graph.addConnection ({ "osc", "out", "sumLayer", "in" });
        graph.addConnection ({ "sumMain", "out", "gainMain", "in.0" });
        graph.addConnection ({ "sumLayer", "out", "gainLayer", "in.0" });
        graph.addConnection ({ "gainMain", "out", "add", "in.0" });
        graph.addConnection ({ "gainLayer", "out", "add", "in.1" });
        graph.addConnection ({ "add", "out", "masterOut", "in" });
        graph.setOutput ("masterOut", "out");
        return graph;
    };

    auto render = [&] (float mainGain, float layerGain)
    {
        BazaltAudioProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        const auto result = processor.getGraphEditController().setGraph (buildGraph (mainGain, layerGain));
        INFO (result.errorMessage);
        REQUIRE (result.success);

        juce::MidiBuffer noteOn;
        noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        processor.processBlock (buffer, noteOn);
        for (int block = 0; block < 9; ++block)
        {
            buffer.clear();
            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);
        }
        return rms (buffer, 0);
    };

    // Quiet enough that both together stay clear of the output limiter.
    const auto mainOnly = render (0.2f, 0.0f);
    const auto layerOnly = render (0.0f, 0.2f);
    const auto both = render (0.2f, 0.2f);

    CHECK (mainOnly > 0.01f);
    CHECK (layerOnly > 0.01f);
    CHECK (std::abs (mainOnly - layerOnly) < 0.01f * mainOnly);
    // The same voice through both sums adds coherently: twice the level.
    CHECK (std::abs (both - 2.0f * mainOnly) < 0.05f * mainOnly);
}

// wiki/ROADMAP.md stage 0: a wired Listen overrides Master Out, and is never
// saved with the patch.
TEST_CASE ("A Listen overrides Master Out and is left out of the saved graph",
           "[plugin][GraphEditController][listen]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildMasterOutOnlyGraph()).success);
    REQUIRE (controller.addNode ("source.oscillator", "sine", 0.0f, 0.0f).success);

    auto renderRms = [&]
    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int block = 0; block < 4; ++block)
        {
            buffer.clear();
            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);
        }
        return rms (buffer, 0);
    };

    // The sine feeds nothing audible yet: Master Out is unconnected.
    CHECK (renderRms() < 1.0e-6f);

    REQUIRE (controller.addNode ("view.listen", "listen", 0.0f, 0.0f).success);
    REQUIRE (controller.connect ("sine", "out", "listen", "in").success);
    CHECK (renderRms() > 0.1f);

    // The edited graph keeps Master Out as its output; saving drops the Listen.
    const auto masterOutId = controller.getGraph().getOutputNodeId();
    CHECK (controller.getGraph().findNode (masterOutId)->type == "io.output");
    const auto saved = controller.getGraphForSaving();
    CHECK (saved.findNode ("listen") == nullptr);
    CHECK (saved.findNode ("sine") != nullptr);
    CHECK (std::none_of (saved.getConnections().begin(), saved.getConnections().end(),
                         [] (const auto& c) { return c.toNodeId == "listen"; }));

    // Removing the Listen goes straight back to Master Out (silent here).
    REQUIRE (controller.deleteNode ("listen").success);
    CHECK (renderRms() < 1.0e-6f);
}

// wiki/plans/DataAndWavetable.md 1b: a factory node's content, committed and live.
TEST_CASE ("setContent stores a node's content and keeps the running node; setContentLive reaches it without an edit",
           "[plugin][GraphEditController][content]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildMasterOutOnlyGraph()).success);
    REQUIRE (controller.addNode ("source.oscillator", "osc", 0.0f, 0.0f).success);

    const auto* before = processor.getGlobalPlanSwapper().peekCurrentPlan()->getNodeById ("osc");
    REQUIRE (before != nullptr);

    const auto square = bazalt::engine::CurveDocument::square().toVar();
    REQUIRE (controller.setContent ("osc", square).success);
    CHECK (juce::JSON::toString (controller.getGraph().findNode ("osc")->content) == juce::JSON::toString (square));
    CHECK (processor.getGlobalPlanSwapper().peekCurrentPlan()->getNodeById ("osc") == before); // kept, not rebuilt

    const auto graphBefore = juce::JSON::toString (controller.getGraph().findNode ("osc")->content);
    CHECK (controller.setContentLive ("osc", bazalt::engine::CurveDocument::saw().toVar()).success);
    CHECK (juce::JSON::toString (controller.getGraph().findNode ("osc")->content) == graphBefore); // live is not an edit
    CHECK_FALSE (controller.setContent ("nope", square).success);
}
