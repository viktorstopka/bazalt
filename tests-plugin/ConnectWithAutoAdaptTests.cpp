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
            bazalt::engine::PortDescriptor port { "out", bazalt::engine::SignalType::Audio };
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

    REQUIRE (controller.disconnect ("svf", "out", "amp", "audio").success);
    const auto result = controller.connectWithAutoAdapt ("svf", "out", "amp", "audio");
    CHECK (result.success);

    bool found = false;
    for (const auto& c : controller.getGraph().getConnections())
        if (c.fromNodeId == "svf" && c.toNodeId == "amp" && c.toPortId == "audio")
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

    REQUIRE (controller.addNode ("adapt.normalise", "norm", 0.0f, 0.0f).success);
    REQUIRE (controller.addNode ("delay.line", "dly", 100.0f, 0.0f).success);

    const auto result = controller.connectWithAutoAdapt ("norm", "out", "dly", "delay.line.samples");
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
        if (c.fromNodeId == "norm" && c.fromPortId == "out" && c.toNodeId == mapNode->id)
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
    // wiki/plans/StereoChannels.md: mix.gain's audio input follows the width
    // of its source, so stereo stays stereo — nothing is inserted.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    processor.getNodeFactory().registerType ("test.stereoSource", [] { return std::make_unique<StereoTestSourceNode>(); });
    REQUIRE (controller.addNode ("test.stereoSource", "stereoSrc", 0.0f, 0.0f).success);

    const auto nodesBefore = controller.getGraph().getNodes().size();
    REQUIRE (controller.connectWithAutoAdapt ("stereoSrc", "out", "amp", "audio").success);
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
    REQUIRE (controller.addNode ("env.follower", "follower", 100.0f, 0.0f).success); // its detector input is one signal

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

TEST_CASE ("connectWithAutoAdapt inserts adapt.audioToControl for raw Audio into a modulation-quantity port",
           "[plugin][GraphEditController][CanConnect][AudioControlBridge]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    // adapt.normalise's own "in" port is a plain Control port with no
    // declared quantity (Dimensionless) - canConnect treats Dimensionless as
    // "not a real quantity" (isRealQuantity() excludes it explicitly, same
    // as connectControl()'s own lenient Dimensionless handling), so this is
    // the one-step case: osc.out -> adapt.audioToControl.in -> destTest.in,
    // no adapt.map involved.
    REQUIRE (controller.addNode ("adapt.normalise", "destTest", 0.0f, 0.0f).success);

    const auto result = controller.connectWithAutoAdapt ("osc", "out", "destTest", "in");
    REQUIRE (result.success);

    const auto& graph = controller.getGraph();
    const bazalt::engine::NodeInstance* bridgeNode = nullptr;
    for (const auto& n : graph.getNodes())
        if (n.type == "adapt.audioToControl")
            bridgeNode = &n;
    REQUIRE (bridgeNode != nullptr);

    bool sourceToBridge = false, bridgeToDestination = false;
    for (const auto& c : graph.getConnections())
    {
        if (c.fromNodeId == "osc" && c.fromPortId == "out" && c.toNodeId == bridgeNode->id && c.toPortId == "in")
            sourceToBridge = true;
        if (c.fromNodeId == bridgeNode->id && c.fromPortId == "out" && c.toNodeId == "destTest" && c.toPortId == "in")
            bridgeToDestination = true;
    }
    CHECK (sourceToBridge);
    CHECK (bridgeToDestination);
}

TEST_CASE ("connectWithAutoAdapt inserts adapt.audioToControl then adapt.map for raw Audio into a real-quantity port",
           "[plugin][GraphEditController][CanConnect][AudioControlBridge]")
{
    // The motivating FM use case: osc.analog's raw waveform ("out", plain
    // Audio) straight into filter.svf's "cutoff" (Quantity::Frequency) -
    // audio-rate filter modulation, not a smoothed envelope-follow.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    const auto result = controller.connectWithAutoAdapt ("osc", "out", "svf", "filter.svf.cutoff");
    REQUIRE (result.success);

    const auto& graph = controller.getGraph();
    const bazalt::engine::NodeInstance* bridgeNode = nullptr;
    const bazalt::engine::NodeInstance* mapNode = nullptr;
    for (const auto& n : graph.getNodes())
    {
        if (n.type == "adapt.audioToControl")
            bridgeNode = &n;
        if (n.type == "adapt.map")
            mapNode = &n;
    }
    REQUIRE (bridgeNode != nullptr);
    REQUIRE (mapNode != nullptr);

    // adapt.map seeded from the destination's own range (filter.svf.cutoff),
    // exactly like every other seedFromDestinationRange step.
    REQUIRE (mapNode->parameters.count ("adapt.map.outMin") == 1);
    REQUIRE (mapNode->parameters.count ("adapt.map.outMax") == 1);
    // The input range comes from adapt.audioToControl's own Bipolar output,
    // not from the raw Audio source.
    REQUIRE (mapNode->parameters.count ("adapt.map.inMin") == 1);
    CHECK (mapNode->parameters.at ("adapt.map.inMin") == -1.0f);
    CHECK (mapNode->parameters.at ("adapt.map.inMax") == 1.0f);

    bool sourceToBridge = false, bridgeToMap = false, mapToDestination = false;
    for (const auto& c : graph.getConnections())
    {
        if (c.fromNodeId == "osc" && c.fromPortId == "out" && c.toNodeId == bridgeNode->id && c.toPortId == "in")
            sourceToBridge = true;
        if (c.fromNodeId == bridgeNode->id && c.fromPortId == "out" && c.toNodeId == mapNode->id && c.toPortId == "in")
            bridgeToMap = true;
        if (c.fromNodeId == mapNode->id && c.fromPortId == "out" && c.toNodeId == "svf" && c.toPortId == "filter.svf.cutoff")
            mapToDestination = true;
    }
    CHECK (sourceToBridge);
    CHECK (bridgeToMap);
    CHECK (mapToDestination);
}

TEST_CASE ("connectWithAutoAdapt rejects a Stereo source into a Control-typed port outright",
           "[plugin][GraphEditController][CanConnect][AudioControlBridge][Stereo]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    processor.getNodeFactory().registerType ("test.stereoSource", [] { return std::make_unique<StereoTestSourceNode>(); });
    REQUIRE (controller.addNode ("test.stereoSource", "stereoSrc", 0.0f, 0.0f).success);

    const auto nodesBefore = controller.getGraph().getNodes().size();
    const auto connectionsBefore = controller.getGraph().getConnections().size();

    const auto result = controller.connectWithAutoAdapt ("stereoSrc", "out", "svf", "filter.svf.cutoff");
    CHECK_FALSE (result.success);
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
    CHECK (controller.getGraph().getConnections().size() == connectionsBefore);
}

TEST_CASE ("connectWithAutoAdapt inserts adapt.controlToAudio for a Bipolar modulation source into an Audio-typed port",
           "[plugin][GraphEditController][CanConnect][ControlToAudioBridge]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    // allocator.random1 is a real Bipolar Control source (InstanceVoiceNode.h).
    // adapt.audioToControl's own "in" is a plain Audio port - a neutral,
    // otherwise-irrelevant destination, same trick the Audio->Control tests
    // above use with adapt.normalise for a neutral Control destination.
    REQUIRE (controller.addNode ("adapt.audioToControl", "destTest", 0.0f, 0.0f).success);

    const auto result = controller.connectWithAutoAdapt ("allocator", "random1", "destTest", "in");
    REQUIRE (result.success);

    const auto& graph = controller.getGraph();
    const bazalt::engine::NodeInstance* bridgeNode = nullptr;
    for (const auto& n : graph.getNodes())
        if (n.type == "adapt.controlToAudio")
            bridgeNode = &n;
    REQUIRE (bridgeNode != nullptr);

    bool sourceToBridge = false, bridgeToDestination = false;
    for (const auto& c : graph.getConnections())
    {
        if (c.fromNodeId == "allocator" && c.fromPortId == "random1" && c.toNodeId == bridgeNode->id && c.toPortId == "in")
            sourceToBridge = true;
        if (c.fromNodeId == bridgeNode->id && c.fromPortId == "out" && c.toNodeId == "destTest" && c.toPortId == "in")
            bridgeToDestination = true;
    }
    CHECK (sourceToBridge);
    CHECK (bridgeToDestination);
}

TEST_CASE ("connectWithAutoAdapt inserts adapt.normalise then adapt.controlToAudio for a real-quantity source into an Audio-typed port",
           "[plugin][GraphEditController][CanConnect][ControlToAudioBridge]")
{
    // allocator.pitch is a real Quantity::Pitch source (InstanceVoiceNode.h) -
    // the symmetric counterpart of the Audio->Control real-quantity test
    // above, just with the real-quantity step on the source side this time.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("adapt.audioToControl", "destTest", 0.0f, 0.0f).success);

    const auto result = controller.connectWithAutoAdapt ("allocator", "pitch", "destTest", "in");
    REQUIRE (result.success);

    const auto& graph = controller.getGraph();
    const bazalt::engine::NodeInstance* normaliseNode = nullptr;
    const bazalt::engine::NodeInstance* bridgeNode = nullptr;
    for (const auto& n : graph.getNodes())
    {
        if (n.type == "adapt.normalise")
            normaliseNode = &n;
        if (n.type == "adapt.controlToAudio")
            bridgeNode = &n;
    }
    REQUIRE (normaliseNode != nullptr);
    REQUIRE (bridgeNode != nullptr);

    // adapt.normalise seeded from the SOURCE's own range (allocator.pitch,
    // 0..127 per InstanceVoiceNode.h's own port descriptor) - the mirror
    // image of the forward bridge's adapt.map, which seeds from the
    // DESTINATION instead.
    REQUIRE (normaliseNode->parameters.count ("adapt.normalise.min") == 1);
    CHECK (normaliseNode->parameters.at ("adapt.normalise.min") == 0.0f);
    REQUIRE (normaliseNode->parameters.count ("adapt.normalise.max") == 1);
    CHECK (normaliseNode->parameters.at ("adapt.normalise.max") == 127.0f);

    bool sourceToNormalise = false, normaliseToBridge = false, bridgeToDestination = false;
    for (const auto& c : graph.getConnections())
    {
        if (c.fromNodeId == "allocator" && c.fromPortId == "pitch" && c.toNodeId == normaliseNode->id && c.toPortId == "in")
            sourceToNormalise = true;
        if (c.fromNodeId == normaliseNode->id && c.fromPortId == "out" && c.toNodeId == bridgeNode->id && c.toPortId == "in")
            normaliseToBridge = true;
        if (c.fromNodeId == bridgeNode->id && c.fromPortId == "out" && c.toNodeId == "destTest" && c.toPortId == "in")
            bridgeToDestination = true;
    }
    CHECK (sourceToNormalise);
    CHECK (normaliseToBridge);
    CHECK (bridgeToDestination);
}
