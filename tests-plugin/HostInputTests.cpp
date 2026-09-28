#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"

using namespace bazalt;

namespace
{
    // Activates the main stereo input plus the first `numActiveAux` stereo
    // aux buses and returns the matching input-channel count, the way a host
    // that enabled those buses would size its process buffer.
    int activateInputBuses (BazaltAudioProcessor& processor, int numActiveAux)
    {
        auto layout = processor.getBusesLayout();
        for (int i = 0; i < numActiveAux; ++i)
            layout.inputBuses.getReference (i + 1) = juce::AudioChannelSet::stereo();

        REQUIRE (processor.setBusesLayout (layout));
        return 2 + 2 * numActiveAux;
    }
}

TEST_CASE ("Audio arriving on an activated aux input bus is seen by its level meter",
           "[plugin][host-input]")
{
    // BusLayoutTests only checks layouts; nothing ever fed audio through an
    // input bus. processBlock used to clear EVERY channel of the host buffer
    // before reading it, which wiped the aux inputs (and the main input)
    // before anything could look at them.
    BazaltAudioProcessor processor;
    const auto numChannels = activateInputBuses (processor, 1);
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (numChannels, 256);
    buffer.clear();
    for (int i = 0; i < 256; ++i)
    {
        buffer.setSample (2, i, 0.5f); // aux 1, left
        buffer.setSample (3, i, 0.5f); // aux 1, right
    }

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    CHECK (processor.getAuxPeakLevel (0) == Catch::Approx (0.5f).margin (0.001f));
}

// ---- M21: io.audioIn / io.control / io.transport through a real processor ----

#include "bazalt/engine/graph/NodeGraph.h"
#include "bazalt/engine/graph/ProofGraphs.h"

namespace
{
    using bazalt::engine::NodeGraph;

    // audioIn -> out, no instance.allocator anywhere: a mono graph.
    NodeGraph monoEffectGraph (float bus = 0.0f, const juce::String& channelPort = "channel.0")
    {
        NodeGraph graph;
        graph.addNode ({ "in", "io.audioIn", {}, { { "io.audioIn.bus", bus } }, {} });
        graph.addNode ({ "out", "io.output", {}, {}, {} });
        graph.addConnection ({ "in", channelPort, "out", "in" });
        graph.setOutput ("out", "out");
        return graph;
    }

    // A one-node mono graph whose designated output is `portId` of `typeId`.
    NodeGraph singleNodeGraph (const juce::String& typeId, const std::unordered_map<juce::String, float>& parameters, const juce::String& portId)
    {
        NodeGraph graph;
        graph.addNode ({ "n", typeId, {}, parameters, {} });
        graph.setOutput ("n", portId);
        return graph;
    }

    void fillChannel (juce::AudioBuffer<float>& buffer, int channel, float value)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (channel, i, value);
    }

    // Processes one block: main-input left/right set to the given constants.
    juce::AudioBuffer<float> processWithMainInput (BazaltAudioProcessor& processor, float left, float right, int numSamples,
                                                   juce::MidiBuffer midi = {}, int numChannels = 2)
    {
        juce::AudioBuffer<float> buffer (numChannels, numSamples);
        buffer.clear();
        fillChannel (buffer, 0, left);
        fillChannel (buffer, 1, right);
        processor.processBlock (buffer, midi);
        return buffer;
    }
}

TEST_CASE ("A mono audio-effect graph passes the host main input through with no note held",
           "[plugin][host-input][mono]")
{
    // audioIn -> out has no instance.allocator, so nothing is per-voice: the
    // graph is one plan that runs every block. Before M21 it would have been
    // compiled per voice and run only while a voice was active - silent
    // forever, since no note ever plays.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);
    REQUIRE (processor.getGraphEditController().setGraph (monoEffectGraph()).success);

    const auto out = processWithMainInput (processor, 0.3f, 0.7f, 256);

    // channel.0 (the host left) drives the mono graph; the engine renders mono
    // to both output channels, as it always has.
    for (int i = 0; i < 256; ++i)
    {
        REQUIRE (out.getSample (0, i) == Catch::Approx (0.3f).margin (1.0e-6f));
        REQUIRE (out.getSample (1, i) == Catch::Approx (0.3f).margin (1.0e-6f));
    }

    REQUIRE (processor.getGraphEditController().setGraph (monoEffectGraph (0.0f, "channel.1")).success);
    const auto right = processWithMainInput (processor, 0.3f, 0.7f, 256);
    CHECK (right.getSample (0, 100) == Catch::Approx (0.7f).margin (1.0e-6f));
}

TEST_CASE ("09-28-InstanceAllocator.1: an unrelated, unconnected instance.allocator doesn't silence an otherwise always-on chain",
           "[plugin][host-input][InstanceAllocator]")
{
    // The literal user repro this milestone fixes: a sine wired straight to
    // Master Out played fine; adding a completely unconnected
    // instance.allocator node anywhere in the graph silently converted the
    // whole thing to per-voice (nothing ever triggers a voice, so it just
    // goes silent, permanently, key presses or not).
    using bazalt::engine::NodeGraph;

    NodeGraph graph;
    graph.addNode ({ "sine", "osc.sine", {}, { { "osc.sine.frequency", 220.0f } }, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addNode ({ "alloc", "instance.allocator", {}, {}, {} }); // deliberately unconnected
    graph.addConnection ({ "sine", "out", "out", "in" });
    graph.setOutput ("out", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graph).success);

    const auto out = processWithMainInput (processor, 0.0f, 0.0f, 512); // no note held, no MIDI at all

    double sumSquares = 0.0;
    for (int i = 0; i < 512; ++i)
        sumSquares += (double) out.getSample (0, i) * out.getSample (0, i);
    const auto rms = std::sqrt (sumSquares / 512.0);

    CHECK (rms > 0.1); // the sine is genuinely audible - the allocator's presence didn't gate it
}

TEST_CASE ("09-28-InstanceAllocator.1b: MIDI reaches io.noteIn regardless of its instance id, not only when it happens to be literally 'noteIn'",
           "[plugin][host-input][InstanceAllocator]")
{
    // Another real bug found live in the same session, same symptom
    // ("instance.allocator's gate never moves"): PluginProcessor used to
    // find the plan's io.noteIn node by a HARDCODED instance id ("noteIn")
    // rather than by type. The editor's own Add-menu auto-generates
    // ordinary ids ("node2", "node3", ...) for a placed node - never that
    // specific magic string - so a patch built entirely through the normal
    // UI workflow silently never received a single MIDI note. Deliberately
    // named "node3" here, exactly matching what a real UI-built patch
    // looked like when this was diagnosed.
    //
    // MUST gate a VCA through a real envelope, not just wire osc.analog's
    // pitch from the allocator - a first version of this test read the raw
    // oscillator, which free-runs off allocator.pitch's already-valid
    // default (60.0f) the moment its VOICE SLOT is marked active
    // (VoiceManager::noteOn(), called unconditionally in handleMidiEvent
    // BEFORE findNoteIn/injectNoteOn are ever reached) - completely
    // independent of whether the noteIn poke this milestone fixes actually
    // succeeds. That version passed even with the bug reintroduced by
    // mutation, catching nothing. Gating through env.adsr's own gate input
    // (fed from allocator.gate, which only ever becomes true via a real
    // noteOn() call reaching the allocator) makes silence genuinely mean
    // "the note never arrived."
    using bazalt::engine::NodeGraph;

    NodeGraph graph;
    graph.addNode ({ "node3", "io.noteIn", {}, {}, {} }); // NOT "noteIn"
    graph.addNode ({ "alloc", "instance.allocator", {}, {}, {} });
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "env", "env.adsr", {}, { { "env.adsr.attack", 0.0f } }, {} });
    graph.addNode ({ "vca", "mix.gain", {}, {}, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "node3", "notes", "alloc", "spawn" });
    graph.addConnection ({ "alloc", "pitch", "osc", "pitch" });
    graph.addConnection ({ "alloc", "gate", "env", "gate" });
    graph.addConnection ({ "osc", "out", "vca", "audio" });
    graph.addConnection ({ "env", "out", "vca", "gain" });
    graph.addConnection ({ "vca", "out", "out", "in" });
    graph.setOutput ("out", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graph).success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    processWithMainInput (processor, 0.0f, 0.0f, 512, noteOn);
    juce::AudioBuffer<float> out;
    for (int block = 0; block < 4; ++block) // let the envelope open and the oscillator settle
        out = processWithMainInput (processor, 0.0f, 0.0f, 512);

    double sumSquares = 0.0;
    for (int i = 0; i < 512; ++i)
        sumSquares += (double) out.getSample (0, i) * out.getSample (0, i);
    const auto rms = std::sqrt (sumSquares / 512.0);

    CHECK (rms > 0.1); // the note genuinely reached the voice, opened the envelope, and the VCA let sound through
}

TEST_CASE ("A genuinely stereo graph (space.pan into io.output's stereo pair) sends different signals to each host channel",
           "[plugin][host-input][stereo]")
{
    // Real stereo cable redesign (wiki/NODES.System.md §9): io.output's real
    // second channel actually reaching the host's two physical output
    // channels, proven with a hard pan so left and right are provably
    // different values, not a mono-duplicate fallback. One real stereo
    // cable from pan's single "out" port into out's single "in" port - no
    // separate left/right connections needed. Deliberately voice +
    // global-domain shaped (allocator -> osc -> instance.mix -> pan -> out),
    // the same shape Init Patch itself now uses — the mono-only
    // (no-allocator) render path is a known, documented scope boundary this
    // redesign didn't extend (see wiki/NODES.System.md §9's own notes); a
    // plain io.audioIn-only graph still duplicates mono to both channels,
    // covered by the next test.
    NodeGraph graph;
    graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
    graph.addNode ({ "allocator", "instance.allocator", {}, {}, {} });
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "voiceMix", "instance.mix", {}, {}, {} });
    graph.addNode ({ "pan", "space.pan", {}, { { "space.pan.pan", -1.0f } }, {} }); // hard left
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "noteIn", "notes", "allocator", "spawn" });
    graph.addConnection ({ "allocator", "pitch", "osc", "pitch" });
    graph.addConnection ({ "osc", "out", "voiceMix", "in" });
    graph.addConnection ({ "voiceMix", "out", "pan", "in" });
    graph.addConnection ({ "pan", "out", "out", "in" }); // one real stereo cable
    graph.setOutput ("out", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graph).success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    processWithMainInput (processor, 0.0f, 0.0f, 512, noteOn);
    juce::AudioBuffer<float> out;
    for (int block = 0; block < 4; ++block) // let the oscillator settle into steady output
        out = processWithMainInput (processor, 0.0f, 0.0f, 512);

    double leftSumSquares = 0.0, rightSumSquares = 0.0;
    for (int i = 0; i < 512; ++i)
    {
        leftSumSquares += (double) out.getSample (0, i) * out.getSample (0, i);
        rightSumSquares += (double) out.getSample (1, i) * out.getSample (1, i);
    }
    const auto leftRms = std::sqrt (leftSumSquares / 512.0);
    const auto rightRms = std::sqrt (rightSumSquares / 512.0);

    CHECK (leftRms > 0.1);    // hard left: a real signal on the left channel
    CHECK (rightRms < 0.01);  // ...and (nearly) none on the right - genuinely different, not duplicated
}

TEST_CASE ("A mono source into io.output's stereo 'in' broadcasts to both host channels - real, tested engine behavior, not incidental",
           "[plugin][host-input][stereo]")
{
    // Real stereo cable redesign (wiki/NODES.System.md §9): io.output.in is
    // ONE real Channels::Stereo port now, not a left/right port pair with a
    // "was the second half actually wired" special case (that guard existed
    // only in the superseded Milestone 0.2 point-fix, and is gone). A mono
    // source feeding a stereo port is canConnect's ordinary free-broadcast
    // rule (CanConnect.cpp), now backed by GraphCompiler.cpp actually
    // allocating and filling a real second buffer - this proves that
    // broadcast reaches all the way to the host's two physical channels,
    // matching every graph's behavior before this whole redesign started.
    // NOTE: this must be a voice + global-domain graph (allocator -> ... ->
    // instance.mix -> out), NOT monoEffectGraph() - a no-allocator graph
    // takes PluginProcessor's separate renderMonoRange/monoRenderedThisBlock
    // path, which never consults finalOutputBufferIndexRight at all, so it
    // can't exercise this mechanism either way.
    NodeGraph graph;
    graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
    graph.addNode ({ "allocator", "instance.allocator", {}, {}, {} });
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "voiceMix", "instance.mix", {}, {}, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "noteIn", "notes", "allocator", "spawn" });
    graph.addConnection ({ "allocator", "pitch", "osc", "pitch" });
    graph.addConnection ({ "osc", "out", "voiceMix", "in" });
    graph.addConnection ({ "voiceMix", "out", "out", "in" }); // mono source into the stereo "in" - broadcasts free
    graph.setOutput ("out", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (graph).success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    processWithMainInput (processor, 0.0f, 0.0f, 512, noteOn);
    juce::AudioBuffer<float> out;
    for (int block = 0; block < 4; ++block) // let the oscillator settle into steady output
        out = processWithMainInput (processor, 0.0f, 0.0f, 512);

    double leftSumSquares = 0.0, rightSumSquares = 0.0;
    for (int i = 0; i < 512; ++i)
    {
        leftSumSquares += (double) out.getSample (0, i) * out.getSample (0, i);
        rightSumSquares += (double) out.getSample (1, i) * out.getSample (1, i);
    }

    // Both channels carry the same real (non-silent) signal - duplicated,
    // not silenced, exactly matching a plain mono-out node.
    CHECK (leftSumSquares > 0.0);
    CHECK (rightSumSquares == Catch::Approx (leftSumSquares).margin (1.0e-9));
    for (int i = 0; i < 512; ++i)
        REQUIRE (out.getSample (0, i) == Catch::Approx (out.getSample (1, i)).margin (1.0e-7f));
}

TEST_CASE ("io.audioIn bus setting selects a sidechain aux bus", "[plugin][host-input][mono]")
{
    BazaltAudioProcessor processor;
    const auto numChannels = activateInputBuses (processor, 1);
    processor.prepareToPlay (44100.0, 256);
    REQUIRE (processor.getGraphEditController().setGraph (monoEffectGraph (1.0f)).success); // Aux 1

    juce::AudioBuffer<float> buffer (numChannels, 256);
    buffer.clear();
    fillChannel (buffer, 0, 0.9f); // main: must NOT be what comes out
    fillChannel (buffer, 2, 0.4f); // aux 1 left
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    // The aux bus reaches the output twice: through the graph (0.4) and through
    // the interim sidechain passthrough, which mixes every active aux bus into
    // the main output at -30 dB (PluginProcessor.cpp, sidechainPassthroughGain).
    // The passthrough only started working with this change - the clear at the top
    // of processBlock used to wipe the aux input before it was read.
    constexpr auto sidechainPassthroughGain = 0.0316f;
    CHECK (buffer.getSample (0, 50) == Catch::Approx (0.4f + 0.4f * sidechainPassthroughGain).margin (1.0e-6f));
    CHECK (processor.getAuxPeakLevel (0) == Catch::Approx (0.4f).margin (1.0e-6f));
}

TEST_CASE ("An audioIn feeding the global domain (after an instance.mix) passes through with no note held",
           "[plugin][host-input][mono]")
{
    // The mono source does not sit upstream or downstream of the mix; before
    // M21 DomainSplitter rejected this graph outright.
    NodeGraph graph;
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.addNode ({ "svf", "filter.svf", {}, {}, {} });
    graph.addNode ({ "instancemix", "instance.mix", {}, {}, {} });
    graph.addNode ({ "sum", "mix.sum", {}, {}, {} });
    graph.addNode ({ "audioin", "io.audioIn", {}, {}, {} });
    graph.addNode ({ "masterout", "io.output", {}, {}, {} });
    graph.addConnection ({ "osc", "out", "svf", "in" });
    graph.addConnection ({ "svf", "out", "instancemix", "in" });
    graph.addConnection ({ "instancemix", "out", "sum", "in.0" });
    graph.addConnection ({ "audioin", "channel.0", "sum", "in.1" });
    graph.addConnection ({ "sum", "out", "masterout", "in" });
    graph.setOutput ("masterout", "out");

    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);
    const auto result = processor.getGraphEditController().setGraph (graph);
    INFO (result.errorMessage);
    REQUIRE (result.success);

    const auto out = processWithMainInput (processor, 0.25f, 0.25f, 256);
    CHECK (out.getSample (0, 10) == Catch::Approx (0.25f).margin (1.0e-6f)); // no voices: the mix contributes silence
    CHECK (out.getSample (1, 200) == Catch::Approx (0.25f).margin (1.0e-6f));
}

TEST_CASE ("io.control follows a MIDI CC sample-accurately, and holds it across blocks",
           "[plugin][host-input][mono]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);
    REQUIRE (processor.getGraphEditController().setGraph (
                 singleNodeGraph ("io.control", { { "io.control.source", 0.0f }, { "io.control.cc", 20.0f }, { "io.control.smoothing", 0.0f } }, "value")).success);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 127), 100);

    const auto first = processWithMainInput (processor, 0.0f, 0.0f, 256, midi);
    CHECK (first.getSample (0, 99) == 0.0f);
    CHECK (first.getSample (0, 100) == Catch::Approx (1.0f)); // the very sample the CC landed on
    CHECK (first.getSample (0, 255) == Catch::Approx (1.0f));

    const auto second = processWithMainInput (processor, 0.0f, 0.0f, 256); // no MIDI this block
    CHECK (second.getSample (0, 0) == Catch::Approx (1.0f)); // the wheel stays where it was left
}

TEST_CASE ("io.control maps the mod wheel, channel pressure and pitch bend onto 0..1", "[plugin][host-input][mono]")
{
    auto valueFor = [] (float source, const juce::MidiMessage& message)
    {
        BazaltAudioProcessor processor;
        processor.prepareToPlay (44100.0, 128);
        REQUIRE (processor.getGraphEditController().setGraph (
                     singleNodeGraph ("io.control", { { "io.control.source", source }, { "io.control.smoothing", 0.0f } }, "value")).success);

        juce::MidiBuffer midi;
        midi.addEvent (message, 0);
        return processWithMainInput (processor, 0.0f, 0.0f, 128, midi).getSample (0, 64);
    };

    CHECK (valueFor (1.0f, juce::MidiMessage::controllerEvent (1, 1, 64)) == Catch::Approx (64.0f / 127.0f)); // mod wheel = CC1
    CHECK (valueFor (2.0f, juce::MidiMessage::channelPressureChange (1, 127)) == Catch::Approx (1.0f));
    CHECK (valueFor (3.0f, juce::MidiMessage::pitchWheel (1, 0)) == Catch::Approx (0.0f).margin (1.0e-4f));      // full down
    CHECK (valueFor (3.0f, juce::MidiMessage::pitchWheel (1, 8192)) == Catch::Approx (0.5f).margin (1.0e-4f));   // centre
    CHECK (valueFor (3.0f, juce::MidiMessage::pitchWheel (1, 16383)) == Catch::Approx (1.0f).margin (1.0e-3f));  // full up
    CHECK (valueFor (4.0f, juce::MidiMessage::controllerEvent (1, 64, 127)) == Catch::Approx (1.0f));          // sustain pedal down
}

TEST_CASE ("io.transport runs an internal 120 BPM transport when there is no host timeline",
           "[plugin][host-input][mono]")
{
    // The processor here has no playhead attached - the same situation as the
    // Standalone app - so io.transport falls back to its internal transport.
    auto outputFor = [] (const juce::String& port)
    {
        BazaltAudioProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        REQUIRE (processor.getGraphEditController().setGraph (singleNodeGraph ("io.transport", {}, port)).success);
        return processWithMainInput (processor, 0.0f, 0.0f, 512);
    };

    CHECK (outputFor ("tempo").getSample (0, 10) == Catch::Approx (2.0f));    // 120 BPM = 2 beats per second
    CHECK (outputFor ("playing").getSample (0, 10) == Catch::Approx (1.0f));

    // Beats: a pulse every 22050 samples, none dropped or doubled across block edges.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (singleNodeGraph ("io.transport", {}, "beat")).success);

    std::vector<int> beats;
    for (int block = 0; block < 100; ++block) // 51200 samples
    {
        const auto out = processWithMainInput (processor, 0.0f, 0.0f, 512);
        for (int i = 0; i < 512; ++i)
            if (out.getSample (0, i) != 0.0f)
                beats.push_back (block * 512 + i);
    }
    CHECK (beats == std::vector<int> { 0, 22050, 44100 });

    // Position advances with the samples it has processed.
    BazaltAudioProcessor positionProcessor;
    positionProcessor.prepareToPlay (44100.0, 441);
    REQUIRE (positionProcessor.getGraphEditController().setGraph (singleNodeGraph ("io.transport", {}, "position")).success);
    for (int block = 0; block < 100; ++block) // exactly one second
        processWithMainInput (positionProcessor, 0.0f, 0.0f, 441);
    CHECK (processWithMainInput (positionProcessor, 0.0f, 0.0f, 441).getSample (0, 0) == Catch::Approx (1.0f).margin (0.001f));
}

TEST_CASE ("MIDI notes in a mono graph are ignored safely, and the graph still passes audio",
           "[plugin][host-input][mono]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);
    REQUIRE (processor.getGraphEditController().setGraph (monoEffectGraph()).success);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 10);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 100);
    midi.addEvent (juce::MidiMessage::pitchWheel (1, 12000), 120);

    const auto out = processWithMainInput (processor, 0.3f, 0.3f, 256, midi);
    CHECK (out.getSample (0, 5) == Catch::Approx (0.3f).margin (1.0e-6f));
    CHECK (out.getSample (0, 200) == Catch::Approx (0.3f).margin (1.0e-6f));
}

TEST_CASE ("Switching between a mono graph and a voice graph and back leaves both working",
           "[plugin][host-input][mono]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();

    auto rmsAfterNote = [&]
    {
        juce::MidiBuffer noteOn;
        noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        auto out = processWithMainInput (processor, 0.0f, 0.0f, 512, noteOn);
        for (int block = 0; block < 8; ++block)
            out = processWithMainInput (processor, 0.0f, 0.0f, 512);
        double sumSquares = 0.0;
        for (int i = 0; i < 512; ++i)
            sumSquares += (double) out.getSample (0, i) * out.getSample (0, i);
        return std::sqrt (sumSquares / 512.0);
    };

    // The default graph has an instance.allocator: a note makes sound.
    CHECK (rmsAfterNote() > 0.001);

    // Into a mono graph: audio passes, and a note plays nothing extra.
    REQUIRE (controller.setGraph (monoEffectGraph()).success);
    CHECK (processWithMainInput (processor, 0.5f, 0.5f, 512).getSample (0, 100) == Catch::Approx (0.5f).margin (1.0e-6f));

    // And back to a voice graph: notes work again (the voice plans were republished).
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);
    CHECK (rmsAfterNote() > 0.001);
}

// ---- RT safety (CLAUDE.md rule 2): the host-input path must not allocate ----

#include "bazalt/engine/RtAllocationTrap.h"

namespace
{
    // Runs `blocksToTrap` blocks with the audio-thread allocation trap armed
    // around processBlock (one warm-up block first, outside it, so one-time
    // lazy setup isn't blamed on the path under test). Buffers and MIDI are
    // built outside the trap too - constructing those allocates by design.
    void processTrapped (BazaltAudioProcessor& processor, int numChannels, int numSamples, int blocksToTrap,
                         const juce::MidiBuffer& midi)
    {
        juce::AudioBuffer<float> warmUp (numChannels, numSamples);
        warmUp.clear();
        juce::MidiBuffer none;
        processor.processBlock (warmUp, none);

        for (int block = 0; block < blocksToTrap; ++block)
        {
            juce::AudioBuffer<float> buffer (numChannels, numSamples);
            buffer.clear();
            fillChannel (buffer, 0, 0.3f);
            fillChannel (buffer, 1, 0.3f);
            auto blockMidi = midi;

            {
                bazalt::engine::ScopedAudioThreadAllocationTrap trap;
                processor.processBlock (buffer, blockMidi);
            }
        }
    }
}

TEST_CASE ("The host-input path never allocates on the audio thread",
           "[plugin][host-input][rt-safety]")
{
    // A mono effect graph, a controller + transport graph, the global domain with a
    // mono source, and an activated aux bus - each with MIDI splitting the block
    // into sub-ranges, since every sub-range rebuilds the host inputs.
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 100), 30);
    midi.addEvent (juce::MidiMessage::pitchWheel (1, 9000), 90);
    midi.addEvent (juce::MidiMessage::channelPressureChange (1, 50), 150);

    {
        BazaltAudioProcessor processor;
        processor.prepareToPlay (44100.0, 256);
        REQUIRE (processor.getGraphEditController().setGraph (monoEffectGraph()).success);
        processTrapped (processor, 2, 256, 4, midi);
    }

    {
        NodeGraph graph;
        graph.addNode ({ "ctl", "io.control", {}, { { "io.control.source", 0.0f }, { "io.control.cc", 20.0f } }, {} });
        graph.addNode ({ "clock", "io.transport", {}, {}, {} });
        graph.addNode ({ "sum", "math.add", {}, {}, {} });
        graph.addConnection ({ "ctl", "value", "sum", "in.0" });
        graph.addConnection ({ "clock", "position", "sum", "in.1" });
        graph.setOutput ("sum", "out");

        BazaltAudioProcessor processor;
        processor.prepareToPlay (44100.0, 256);
        REQUIRE (processor.getGraphEditController().setGraph (graph).success);
        processTrapped (processor, 2, 256, 4, midi);
    }

    {
        NodeGraph graph;
        graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
        graph.addNode ({ "svf", "filter.svf", {}, {}, {} });
        graph.addNode ({ "instancemix", "instance.mix", {}, {}, {} });
        graph.addNode ({ "sum", "mix.sum", {}, {}, {} });
        graph.addNode ({ "audioin", "io.audioIn", {}, {}, {} });
        graph.addNode ({ "masterout", "io.output", {}, {}, {} });
        graph.addConnection ({ "osc", "out", "svf", "in" });
        graph.addConnection ({ "svf", "out", "instancemix", "in" });
        graph.addConnection ({ "instancemix", "out", "sum", "in.0" });
        graph.addConnection ({ "audioin", "channel.0", "sum", "in.1" });
        graph.addConnection ({ "sum", "out", "masterout", "in" });
        graph.setOutput ("masterout", "out");

        BazaltAudioProcessor processor;
        processor.prepareToPlay (44100.0, 256);
        REQUIRE (processor.getGraphEditController().setGraph (graph).success);
        processTrapped (processor, 2, 256, 4, midi);
    }

    {
        BazaltAudioProcessor processor;
        const auto numChannels = activateInputBuses (processor, 2);
        processor.prepareToPlay (44100.0, 256);
        REQUIRE (processor.getGraphEditController().setGraph (monoEffectGraph (2.0f)).success);
        processTrapped (processor, numChannels, 256, 4, midi);
    }

    SUCCEED ("no allocation trap fired");
}

// The note paths (note-on, note-off, pitch bend) look their io.noteIn node up on
// the audio thread. They once did so with a string literal - a heap-allocated
// juce::String per call, per voice - and the trap only noticed in M21, because
// no test had ever armed it around a held note. noteOn/noteOff run inside a
// noexcept function, so a regression here shows up as std::terminate rather
// than a catchable violation: still loud, just not a tidy Catch2 failure.
TEST_CASE ("Note-on, pitch bend and note-off never allocate on the audio thread",
           "[plugin][host-input][rt-safety]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    noteOn.addEvent (juce::MidiMessage::pitchWheel (1, 12000), 100);
    processTrapped (processor, 2, 256, 1, noteOn);

    juce::MidiBuffer held; // the note keeps sounding, the wheel moves again
    held.addEvent (juce::MidiMessage::pitchWheel (1, 4000), 50);
    processTrapped (processor, 2, 256, 3, held);

    juce::MidiBuffer noteOff;
    noteOff.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    processTrapped (processor, 2, 256, 1, noteOff);

    SUCCEED ("no allocation trap fired");
}
