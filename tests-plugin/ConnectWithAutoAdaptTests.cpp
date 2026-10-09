#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

using namespace bazalt;

namespace
{
    // A minimal synthetic node with a Stereo-channels Audio output, just to
    // exercise canConnect's channels rule end to end through the real
    // command path — real catalog nodes (space.pan, etc.) produce this too
    // now (the real stereo cable redesign), but a dedicated synthetic
    // source keeps this test focused on the adapter-insertion mechanism
    // itself, not any one real node's own behavior. Registered only for
    // this test, not part of the production NodeFactory (ProofGraphs.h).
    class StereoTestSourceNode : public bazalt::engine::Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        int getNumOutputChannels() const noexcept override { return 2; } // the one descriptor is Stereo

        std::vector<bazalt::engine::PortDescriptor> getOutputPorts() const override
        {
            bazalt::engine::PortDescriptor port { "out", bazalt::engine::SignalType::Signal };
            port.quantity = bazalt::engine::Quantity::Audio;
            port.channels = bazalt::engine::Channels::Stereo;
            return { port };
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = 0.3f;
            outputs[1] = 0.7f;
        }
    };
}

TEST_CASE ("connectWithAutoAdapt connects directly when canConnect already says Ok",
           "[plugin][GraphEditController][CanConnect][M16]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.disconnect ("svf", "out", "amp", "in.0").success);
    const auto result = controller.connectWithAutoAdapt ("svf", "out", "amp", "in.0");
    CHECK (result.success);

    bool found = false;
    for (const auto& c : controller.getGraph().getConnections())
        if (c.fromNodeId == "svf" && c.toNodeId == "amp" && c.toPortId == "in.0")
            found = true;
    CHECK (found);
}

TEST_CASE ("connectWithAutoAdapt inserts and seeds a real adapt.map node for Unipolar -> a real-quantity port",
           "[plugin][GraphEditController][CanConnect][M16]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    // allocator.velocity is a real Unipolar source (InstanceVoiceNode.h).
    REQUIRE (controller.addNode ("delay.line", "dly", 100.0f, 0.0f).success);

    const auto result = controller.connectWithAutoAdapt ("allocator", "velocity", "dly", "delay.line.samples");
    REQUIRE (result.success);

    const auto& graph = controller.getGraph();
    const bazalt::engine::NodeInstance* mapNode = nullptr;
    for (const auto& n : graph.getNodes())
        if (n.type == "adapt.map")
            mapNode = &n;

    REQUIRE (mapNode != nullptr);

    // Output range seeded from the destination port's own range —
    // delay.line's default maxDelaySamples is 4096 (DelayNode.h's
    // constructor default); input range from the Unipolar source, 0..1.
    REQUIRE (mapNode->parameters.count ("adapt.map.outMin") == 1);
    CHECK (mapNode->parameters.at ("adapt.map.outMin") == 1.0f);
    REQUIRE (mapNode->parameters.count ("adapt.map.outMax") == 1);
    CHECK (mapNode->parameters.at ("adapt.map.outMax") == 4096.0f);
    REQUIRE (mapNode->parameters.count ("adapt.map.inMin") == 1);
    CHECK (mapNode->parameters.at ("adapt.map.inMin") == 0.0f);
    CHECK (mapNode->parameters.at ("adapt.map.inMax") == 1.0f);

    bool sourceToAdapter = false, adapterToDestination = false;
    for (const auto& c : graph.getConnections())
    {
        if (c.fromNodeId == "allocator" && c.fromPortId == "velocity" && c.toNodeId == mapNode->id)
            sourceToAdapter = true;
        if (c.fromNodeId == mapNode->id && c.toNodeId == "dly" && c.toPortId == "delay.line.samples")
            adapterToDestination = true;
    }
    CHECK (sourceToAdapter);
    CHECK (adapterToDestination);
}

TEST_CASE ("connectWithAutoAdapt rejects a connection to an unknown port with no mutation and a descriptive error",
           "[plugin][GraphEditController][CanConnect][M16]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("delay.line", "dly", 0.0f, 0.0f).success);

    const auto nodesBefore = controller.getGraph().getNodes().size();
    const auto connectionsBefore = controller.getGraph().getConnections().size();

    const auto result = controller.connectWithAutoAdapt ("dly", "out", "dly", "no-such-port");
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);
}

TEST_CASE ("connectWithAutoAdapt inserts adapt.pitchToFrequency for Pitch into a filter's Cutoff, not the generic remap",
           "[plugin][GraphEditController][CanConnect][AudioControlBridge]")
{
    // Voice's "pitch" output (Quantity::Pitch, 0-127) into SVF Filter's
    // "cutoff" input (Quantity::Frequency, 20-20000) — pitch-tracking a
    // filter cutoff, a standard synthesis technique. Revised, same session
    // as this test's own original M20 case: Pitch<->Frequency is
    // exponential, not linear, so this pair no longer falls into the
    // generic adapt.remap path (tests/CanConnectTests.cpp covers that path
    // still applying to OTHER real-quantity pairs, e.g. Pitch<->Time) — it
    // gets the exact conversion instead, no seeding needed at all (the
    // formula is fixed, not range-dependent).
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    // Reuses buildVoiceProofGraph()'s own "allocator" node (already wired to
    // osc's pitch) rather than adding a second instance.allocate.voice —
    // DomainRedesign.md Batch 1's runtime is single-origin only for now
    // (multi-origin lands in Batch 2), and this test's own intent is the
    // auto-adapter insertion, not multi-origin behaviour.
    const auto result = controller.connectWithAutoAdapt ("allocator", "pitch", "svf", "filter.svf.cutoff");
    REQUIRE (result.success);

    const auto& graph = controller.getGraph();
    const bazalt::engine::NodeInstance* converterNode = nullptr;
    for (const auto& n : graph.getNodes())
        if (n.type == "adapt.pitchToFrequency")
            converterNode = &n;
    REQUIRE (converterNode != nullptr);

    // Wired directly: allocator.pitch -> converter.pitch, converter.frequency -> svf.cutoff.
    bool sourceToConverter = false, converterToDestination = false;
    for (const auto& c : graph.getConnections())
    {
        if (c.fromNodeId == "allocator" && c.fromPortId == "pitch" && c.toNodeId == converterNode->id && c.toPortId == "pitch")
            sourceToConverter = true;
        if (c.fromNodeId == converterNode->id && c.fromPortId == "frequency" && c.toNodeId == "svf" && c.toPortId == "filter.svf.cutoff")
            converterToDestination = true;
    }
    CHECK (sourceToConverter);
    CHECK (converterToDestination);
}

TEST_CASE ("connectWithAutoAdapt wires a Stereo source straight into a per-channel port",
           "[plugin][GraphEditController][CanConnect][Stereo]")
{
    // wiki/plans/StereoChannels.md: math.multiply's inputs follow the width
    // of its source, so stereo stays stereo — nothing is inserted.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    processor.getNodeFactory().registerType ("test.stereoSource", [] { return std::make_unique<StereoTestSourceNode>(); });
    REQUIRE (controller.addNode ("test.stereoSource", "stereoSrc", 0.0f, 0.0f).success);

    const auto nodesBefore = controller.getGraph().getNodes().size();
    REQUIRE (controller.connectWithAutoAdapt ("stereoSrc", "out", "amp", "in.0").success);
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
}

TEST_CASE ("connectWithAutoAdapt asks before reducing stereo into a mono-only port, then inserts the chosen downmix",
           "[plugin][GraphEditController][CanConnect][Stereo]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    processor.getNodeFactory().registerType ("test.stereoSource", [] { return std::make_unique<StereoTestSourceNode>(); });
    REQUIRE (controller.addNode ("test.stereoSource", "stereoSrc", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("adapt.sampleHold", "follower", 100.0f, 0.0f).success); // its input is one signal

    const auto nodesBefore = controller.getGraph().getNodes().size();

    // No choice: nothing changes, and the result names the options.
    const auto asked = controller.connectWithAutoAdapt ("stereoSrc", "out", "follower", "in");
    CHECK_FALSE (asked.success);
    CHECK (asked.choices.contains ("mid"));
    CHECK (asked.choices.contains ("left"));
    CHECK (asked.choices.contains ("right"));
    CHECK (asked.choices.contains ("side"));
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);

    // With a choice: one visible mix.downmix in that mode.
    REQUIRE (controller.connectWithAutoAdapt ("stereoSrc", "out", "follower", "in", "right").success);
    CHECK (controller.getGraph().getNodes().size() == nodesBefore + 1);

    const auto& nodes = controller.getGraph().getNodes();
    const auto downmixIt = std::find_if (nodes.begin(), nodes.end(), [] (const auto& n) { return n.type == "mix.downmix"; });
    REQUIRE (downmixIt != nodes.end());
    CHECK (downmixIt->parameters.at ("mix.downmix.mode") == 3.0f); // "right"

    const auto& connections = controller.getGraph().getConnections();
    CHECK (std::any_of (connections.begin(), connections.end(), [&] (const auto& c)
                        { return c.fromNodeId == "stereoSrc" && c.toNodeId == downmixIt->id && c.toPortId == "in"; }));
    CHECK (std::any_of (connections.begin(), connections.end(), [&] (const auto& c)
                        { return c.fromNodeId == downmixIt->id && c.toNodeId == "follower" && c.toPortId == "in"; }));
}

// wiki/plans/DataAndWavetable.md D1: Audio and Control are one numeric signal.
// No bridge node any more — a value wires straight in, and a Map appears only
// where the range changes.

namespace
{
    const bazalt::engine::NodeInstance* findNodeOfType (const bazalt::engine::NodeGraph& graph, const juce::String& type)
    {
        for (const auto& n : graph.getNodes())
            if (n.type == type)
                return &n;
        return nullptr;
    }

    bool hasConnection (const bazalt::engine::NodeGraph& graph, const juce::String& from, const juce::String& fromPort,
                        const juce::String& to, const juce::String& toPort)
    {
        const auto& connections = graph.getConnections();
        return std::any_of (connections.begin(), connections.end(), [&] (const auto& c)
                            { return c.fromNodeId == from && c.fromPortId == fromPort && c.toNodeId == to && c.toPortId == toPort; });
    }
}

TEST_CASE ("connectWithAutoAdapt wires raw Audio straight into a modulation port",
           "[plugin][GraphEditController][CanConnect]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    // adapt.map's own "in" is a plain, quantity-less value port.
    REQUIRE (controller.addNode ("adapt.map", "destTest", 0.0f, 0.0f).success);
    const auto nodesBefore = controller.getGraph().getNodes().size();

    REQUIRE (controller.connectWithAutoAdapt ("osc", "out", "destTest", "in").success);
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
    CHECK (hasConnection (controller.getGraph(), "osc", "out", "destTest", "in"));
}

TEST_CASE ("connectWithAutoAdapt inserts one Map for raw Audio into a real-quantity port, reading the audio as ±1",
           "[plugin][GraphEditController][CanConnect]")
{
    // The FM use case: a raw waveform into a filter's cutoff, at audio rate.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.connectWithAutoAdapt ("osc", "out", "svf", "filter.svf.cutoff").success);

    const auto& graph = controller.getGraph();
    const auto* mapNode = findNodeOfType (graph, "adapt.map");
    REQUIRE (mapNode != nullptr);
    CHECK (mapNode->parameters.at ("adapt.map.inMin") == -1.0f);
    CHECK (mapNode->parameters.at ("adapt.map.inMax") == 1.0f);
    CHECK (mapNode->parameters.count ("adapt.map.outMin") == 1);
    CHECK (mapNode->parameters.count ("adapt.map.outMax") == 1);
    CHECK (hasConnection (graph, "osc", "out", mapNode->id, "in"));
    CHECK (hasConnection (graph, mapNode->id, "out", "svf", "filter.svf.cutoff"));
}

TEST_CASE ("connectWithAutoAdapt asks how to reduce a Stereo source into a mono value port",
           "[plugin][GraphEditController][CanConnect][Stereo]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    processor.getNodeFactory().registerType ("test.stereoSource", [] { return std::make_unique<StereoTestSourceNode>(); });
    REQUIRE (controller.addNode ("test.stereoSource", "stereoSrc", 0.0f, 0.0f).success);

    const auto nodesBefore = controller.getGraph().getNodes().size();
    const auto connectionsBefore = controller.getGraph().getConnections().size();

    // Without a choice nothing happens — the user is asked first.
    const auto asked = controller.connectWithAutoAdapt ("stereoSrc", "out", "svf", "filter.svf.cutoff");
    CHECK_FALSE (asked.success);
    CHECK_FALSE (asked.choices.isEmpty());
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);

    // With one: Downmix, then a Map onto the cutoff's range.
    REQUIRE (controller.connectWithAutoAdapt ("stereoSrc", "out", "svf", "filter.svf.cutoff", "mid").success);
    CHECK (findNodeOfType (controller.getGraph(), "mix.downmix") != nullptr);
    CHECK (findNodeOfType (controller.getGraph(), "adapt.map") != nullptr);
}

TEST_CASE ("connectWithAutoAdapt wires a Bipolar modulation source straight into an Audio port",
           "[plugin][GraphEditController][CanConnect]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    // allocator.random1 is a real Bipolar source (InstanceVoiceNode.h).
    REQUIRE (controller.addNode ("filter.dcBlock", "destTest", 0.0f, 0.0f).success);
    const auto nodesBefore = controller.getGraph().getNodes().size();

    REQUIRE (controller.connectWithAutoAdapt ("allocator", "random1", "destTest", "in").success);
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
    CHECK (hasConnection (controller.getGraph(), "allocator", "random1", "destTest", "in"));
}

TEST_CASE ("connectWithAutoAdapt inserts one Map from a real-quantity source onto an Audio port's ±1",
           "[plugin][GraphEditController][CanConnect]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("filter.dcBlock", "destTest", 0.0f, 0.0f).success);
    REQUIRE (controller.connectWithAutoAdapt ("allocator", "pitch", "destTest", "in").success);

    const auto* mapNode = findNodeOfType (controller.getGraph(), "adapt.map");
    REQUIRE (mapNode != nullptr);
    // allocator.pitch declares 0..127 (InstanceVoiceNode.h).
    CHECK (mapNode->parameters.at ("adapt.map.inMin") == 0.0f);
    CHECK (mapNode->parameters.at ("adapt.map.inMax") == 127.0f);
    CHECK (mapNode->parameters.at ("adapt.map.outMin") == -1.0f);
    CHECK (mapNode->parameters.at ("adapt.map.outMax") == 1.0f);
    CHECK (hasConnection (controller.getGraph(), mapNode->id, "out", "destTest", "in"));
}
