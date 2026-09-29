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

    // Seeded from the destination port's own range — delay.line's default
    // maxDelaySamples is 4096 (DelayNode.h's constructor default).
    REQUIRE (mapNode->parameters.count ("adapt.map.min") == 1);
    CHECK (mapNode->parameters.at ("adapt.map.min") == 1.0f);
    REQUIRE (mapNode->parameters.count ("adapt.map.max") == 1);
    CHECK (mapNode->parameters.at ("adapt.map.max") == 4096.0f);

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

    REQUIRE (controller.addNode ("instance.allocate.voice", "alloc", 0.0f, 0.0f).success);

    const auto result = controller.connectWithAutoAdapt ("alloc", "pitch", "svf", "filter.svf.cutoff");
    REQUIRE (result.success);

    const auto& graph = controller.getGraph();
    const bazalt::engine::NodeInstance* converterNode = nullptr;
    for (const auto& n : graph.getNodes())
        if (n.type == "adapt.pitchToFrequency")
            converterNode = &n;
    REQUIRE (converterNode != nullptr);

    // Wired directly: alloc.pitch -> converter.pitch, converter.frequency -> svf.cutoff.
    bool sourceToConverter = false, converterToDestination = false;
    for (const auto& c : graph.getConnections())
    {
        if (c.fromNodeId == "alloc" && c.fromPortId == "pitch" && c.toNodeId == converterNode->id && c.toPortId == "pitch")
            sourceToConverter = true;
        if (c.fromNodeId == converterNode->id && c.fromPortId == "frequency" && c.toNodeId == "svf" && c.toPortId == "filter.svf.cutoff")
            converterToDestination = true;
    }
    CHECK (sourceToConverter);
    CHECK (converterToDestination);
}

TEST_CASE ("connectWithAutoAdapt auto-inserts mix.downmix for a Stereo source into a mono-only port",
           "[plugin][GraphEditController][CanConnect][Stereo]")
{
    // Real stereo cable redesign (wiki/NODES.System.md §9): mix.downmix
    // became a genuine 1-in-1-out node (one real Channels::Stereo "in", one
    // mono "out"), closing the gap CanConnect.cpp used to flag — before this
    // redesign, downmix's 2-in-1-out shape never fit connectWithAutoAdapt's
    // single-AdapterStep splice mechanism and this exact scenario was
    // rejected outright.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    processor.getNodeFactory().registerType ("test.stereoSource", [] { return std::make_unique<StereoTestSourceNode>(); });

    REQUIRE (controller.addNode ("test.stereoSource", "stereoSrc", 0.0f, 0.0f).success);

    const auto nodesBefore = controller.getGraph().getNodes().size();
    const auto result = controller.connectWithAutoAdapt ("stereoSrc", "out", "amp", "audio");

    REQUIRE (result.success);
    CHECK (controller.getGraph().getNodes().size() == nodesBefore + 1); // exactly one mix.downmix inserted

    const auto& nodes = controller.getGraph().getNodes();
    const auto downmixIt = std::find_if (nodes.begin(), nodes.end(), [] (const auto& n) { return n.type == "mix.downmix"; });
    REQUIRE (downmixIt != nodes.end());

    const auto& connections = controller.getGraph().getConnections();
    const auto feedsDownmixIn = std::any_of (connections.begin(), connections.end(), [&] (const auto& c)
    {
        return c.fromNodeId == "stereoSrc" && c.fromPortId == "out" && c.toNodeId == downmixIt->id && c.toPortId == "in";
    });
    const auto downmixFeedsAmp = std::any_of (connections.begin(), connections.end(), [&] (const auto& c)
    {
        return c.fromNodeId == downmixIt->id && c.fromPortId == "out" && c.toNodeId == "amp" && c.toPortId == "audio";
    });
    CHECK (feedsDownmixIn);
    CHECK (downmixFeedsAmp);
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
    REQUIRE (mapNode->parameters.count ("adapt.map.min") == 1);
    REQUIRE (mapNode->parameters.count ("adapt.map.max") == 1);

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
