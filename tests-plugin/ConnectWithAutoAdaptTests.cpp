#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/Node.h"
#include <cmath>

using namespace bazalt;

namespace
{
    // A minimal synthetic node with a Stereo-channels Audio output, just to
    // exercise canConnect's channels rule end to end through the real
    // command path — no real catalog node produces this yet (M16's own
    // scope note: no multi-channel-per-port buffer plumbing exists), so
    // this is registered only for this test, not part of the production
    // NodeFactory (ProofGraphs.h).
    class StereoTestSourceNode : public bazalt::engine::Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }

        std::vector<bazalt::engine::PortDescriptor> getOutputPorts() const override
        {
            bazalt::engine::PortDescriptor port { "out", bazalt::engine::SignalType::Audio };
            port.channels = bazalt::engine::Channels::Stereo;
            return { port };
        }

        void processSample (const float*, float* outputs) noexcept override { outputs[0] = 0.0f; }
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

TEST_CASE ("connectWithAutoAdapt inserts adapt.remap for two different real quantities (M20: Pitch into a filter's Cutoff)",
           "[plugin][GraphEditController][CanConnect][M20]")
{
    // Instance Allocator's "pitch" output (Quantity::Pitch, 0-127) into SVF
    // Filter's "cutoff" input (Quantity::Frequency, 20-20000) — pitch-
    // tracking a filter cutoff, a standard synthesis technique, and exactly
    // the pair that used to be a bare Reject before M20 (adapt.remap, the
    // MVP of NODE_CATALOG.md's own remap node) fixed it.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.addNode ("instance.allocator", "alloc", 0.0f, 0.0f).success);

    const auto result = controller.connectWithAutoAdapt ("alloc", "pitch", "svf", "filter.svf.cutoff");
    REQUIRE (result.success);

    const auto& graph = controller.getGraph();
    const bazalt::engine::NodeInstance* remapNode = nullptr;
    for (const auto& n : graph.getNodes())
        if (n.type == "adapt.remap")
            remapNode = &n;
    REQUIRE (remapNode != nullptr);

    // Seeded from BOTH ends at once: inMin/inMax from the source's range
    // (pitch: 0-127), outMin/outMax from the destination's (cutoff: 20-20000).
    REQUIRE (remapNode->parameters.count ("adapt.remap.inMin") == 1);
    CHECK (remapNode->parameters.at ("adapt.remap.inMin") == 0.0f);
    REQUIRE (remapNode->parameters.count ("adapt.remap.inMax") == 1);
    CHECK (remapNode->parameters.at ("adapt.remap.inMax") == 127.0f);
    REQUIRE (remapNode->parameters.count ("adapt.remap.outMin") == 1);
    CHECK (remapNode->parameters.at ("adapt.remap.outMin") == 20.0f);
    REQUIRE (remapNode->parameters.count ("adapt.remap.outMax") == 1);
    CHECK (remapNode->parameters.at ("adapt.remap.outMax") == 20000.0f);

    // Wired directly: alloc.pitch -> remap.in, remap.out -> svf.cutoff.
    bool sourceToRemap = false, remapToDestination = false;
    for (const auto& c : graph.getConnections())
    {
        if (c.fromNodeId == "alloc" && c.fromPortId == "pitch" && c.toNodeId == remapNode->id && c.toPortId == "in")
            sourceToRemap = true;
        if (c.fromNodeId == remapNode->id && c.fromPortId == "out" && c.toNodeId == "svf" && c.toPortId == "filter.svf.cutoff")
            remapToDestination = true;
    }
    CHECK (sourceToRemap);
    CHECK (remapToDestination);
}

TEST_CASE ("connectWithAutoAdapt does not attempt to auto-insert mix.downmix (2-in-1-out doesn't fit a single splice)",
           "[plugin][GraphEditController][CanConnect][M16]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    processor.getNodeFactory().registerType ("test.stereoSource", [] { return std::make_unique<StereoTestSourceNode>(); });

    REQUIRE (controller.addNode ("test.stereoSource", "stereoSrc", 0.0f, 0.0f).success);

    const auto nodesBefore = controller.getGraph().getNodes().size();
    const auto result = controller.connectWithAutoAdapt ("stereoSrc", "out", "amp", "audio");

    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());
    // No mix.downmix (or anything else) was silently inserted.
    CHECK (controller.getGraph().getNodes().size() == nodesBefore);
}
