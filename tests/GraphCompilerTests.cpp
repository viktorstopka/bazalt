#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/PlanSwapper.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/nodes/RerouteNode.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace bazalt::engine;

namespace
{
    // A trivial node for hand-built compiler tests: output = input * gain.
    class ScaleNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getInputPorts() const override { return { { "in", SignalType::Audio } }; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Audio } }; }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "gain")
                gain = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0] * gain; }

    private:
        float gain = 1.0f;
    };

    // Outputs a fixed constant every sample (set via parameter).
    class ConstantNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Audio } }; }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "value")
                constantValue = value;
        }

        void processSample (const float*, float* outputs) noexcept override { outputs[0] = constantValue; }

    private:
        float constantValue = 0.0f;
    };

    // Reports it does NOT support per-sample processing — used to prove the
    // compiler rejects placing such a node inside a detected cycle.
    class BlockOnlyNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getInputPorts() const override { return { { "in", SignalType::Audio } }; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Audio } }; }
        bool supportsPerSample() const noexcept override { return false; }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
                outputs[0][i] = inputs[0][i];
        }
    };

    // M17: internal state (a plain counter, standing in for an
    // oscillator's phase, an envelope's stage, a delay line's contents —
    // anything a real node keeps between samples) that only ever grows,
    // never reset by anything the graph does — makes "did this exact
    // object keep running, or did I get a fresh one" trivially observable
    // by reading the counter's value across two separate compiles.
    class CounterNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Audio } }; }
        void processSample (const float*, float* outputs) noexcept override { outputs[0] = (float) count++; }

        int count = 0;
    };

    // M18 (ADR-0024): an Audio self-loop (forces a per-sample region) that
    // also has a Note-typed output — the minimal way to prove GraphCompiler
    // rejects a Note connection whose endpoint lands inside a feedback
    // cycle, since neither real Note-capable node (io.noteIn, has no
    // inputs at all; instance.allocator, whose only input IS its Note
    // port) can actually be wired into a real cycle themselves.
    class NoteProducerWithAudioLoopNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 2; }
        std::vector<PortDescriptor> getInputPorts() const override { return { { "in", SignalType::Audio } }; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio }, { "notes", SignalType::Note } };
        }
        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }
    };

    class NoteSinkNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 0; }
        std::vector<PortDescriptor> getInputPorts() const override { return { { "spawn", SignalType::Note } }; }
        void processSample (const float*, float*) noexcept override {}
    };

    // Control-typed twins of ConstantNode/ScaleNode — RerouteNode's
    // polymorphic-port tests need a non-Audio source and consumer, since
    // canConnect() would reject an Audio-typed Reroute into either.
    class ControlConstantNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Control } }; }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "value")
                constantValue = value;
        }

        void processSample (const float*, float* outputs) noexcept override { outputs[0] = constantValue; }

    private:
        float constantValue = 0.0f;
    };

    class ControlScaleNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getInputPorts() const override { return { { "in", SignalType::Control } }; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Control } }; }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "gain")
                gain = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0] * gain; }

    private:
        float gain = 1.0f;
    };

    // Emits one fixed Note (gate on, pitch 72, velocity 0.8, startEvent on
    // the first sample of each block only) plus a plain Audio output so a
    // graph containing it has something to designate via setOutput().
    class NoteSourceNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 2; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio }, { "notes", SignalType::Note } };
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = 0.0f;
            outputs[1] = 0.0f;
        }

        void produceNoteBlock (NoteEvent* output, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
                output[i] = NoteEvent { .gate = true, .pitch = 72.0f, .velocity = 0.8f, .startEvent = (i == 0), .stopEvent = false };
        }
    };

    // Records whatever Note data reaches its input, for the test to inspect
    // after process() — a fixed array rather than a vector, matching the
    // no-allocation-in-the-audio-path convention even though nothing arms
    // the RT trap in these tests.
    class NoteCaptureNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 0; }
        std::vector<PortDescriptor> getInputPorts() const override { return { { "spawn", SignalType::Note } }; }
        void processSample (const float*, float*) noexcept override {}

        void consumeNoteBlock (const NoteEvent* input, int numSamples) noexcept override
        {
            capturedCount = juce::jmin (numSamples, (int) captured.size());
            for (int i = 0; i < capturedCount; ++i)
                captured[(size_t) i] = input[i];
        }

        std::array<NoteEvent, 64> captured {};
        int capturedCount = 0;
    };

    // Real stereo cable redesign: a synthetic pair proving GraphCompiler's
    // flat-slot allocation directly, before any real node relies on it.
    // Outputs one real `Channels::Stereo` port with independently-settable
    // left/right constants.
    class StereoConstantNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        int getNumOutputChannels() const noexcept override { return 2; } // the one descriptor is Stereo
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Audio, .isPrimaryOutput = true, .channels = Channels::Stereo } };
        }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "left")
                leftValue = value;
            else if (id == "right")
                rightValue = value;
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = leftValue;
            outputs[1] = rightValue;
        }

    private:
        float leftValue = 0.0f;
        float rightValue = 0.0f;
    };

    // Trivial stereo passthrough — one Stereo `in`, one Stereo `out`, exactly
    // the "twin flat slots" shape a real node like PanNode/OutputNode uses.
    class StereoPassthroughNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }
        int getNumInputChannels() const noexcept override { return 2; }
        int getNumOutputChannels() const noexcept override { return 2; }
        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Audio, .channels = Channels::Stereo } };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Audio, .isPrimaryOutput = true, .channels = Channels::Stereo } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0];
            outputs[1] = inputs[1];
        }
    };

    // 17 Stereo-marked input descriptors = 34 flat channels, but only 17
    // descriptors — well under maxPortsPerNode (32) by descriptor count, but
    // over it by flat-channel count. Proves the compile-time channel-count
    // check counts flat channels, not descriptors (a check that compared
    // descriptor count alone would have wrongly let this node through, a
    // real scratch-array overrun at runtime once Stereo counts as two).
    class TooManyStereoChannelsNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 17; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            for (int i = 0; i < 17; ++i)
                ports.push_back (PortDescriptor { .id = "in" + juce::String (i), .type = SignalType::Audio, .channels = Channels::Stereo });
            return ports;
        }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Audio } }; }
        void processSample (const float*, float* outputs) noexcept override { outputs[0] = 0.0f; }
    };

    NodeFactory buildTestFactory()
    {
        NodeFactory factory;
        factory.registerType ("util.reroute", [] { return std::make_unique<nodes::RerouteNode>(); });
        factory.registerType ("test.controlConstant", [] { return std::make_unique<ControlConstantNode>(); });
        factory.registerType ("test.controlScale", [] { return std::make_unique<ControlScaleNode>(); });
        factory.registerType ("test.noteSource", [] { return std::make_unique<NoteSourceNode>(); });
        factory.registerType ("test.noteCapture", [] { return std::make_unique<NoteCaptureNode>(); });
        factory.registerType ("test.scale", [] { return std::make_unique<ScaleNode>(); });
        factory.registerType ("test.constant", [] { return std::make_unique<ConstantNode>(); });
        factory.registerType ("test.blockonly", [] { return std::make_unique<BlockOnlyNode>(); });
        factory.registerType ("test.counter", [] { return std::make_unique<CounterNode>(); });
        factory.registerType ("test.noteProducerWithAudioLoop", [] { return std::make_unique<NoteProducerWithAudioLoopNode>(); });
        factory.registerType ("test.noteSink", [] { return std::make_unique<NoteSinkNode>(); });
        factory.registerType ("test.stereoConstant", [] { return std::make_unique<StereoConstantNode>(); });
        factory.registerType ("test.stereoPassthrough", [] { return std::make_unique<StereoPassthroughNode>(); });
        factory.registerType ("test.tooManyStereoChannels", [] { return std::make_unique<TooManyStereoChannelsNode>(); });
        return factory;
    }
}

TEST_CASE ("GraphCompiler schedules an acyclic chain in dependency order and computes correct output",
           "[engine][GraphCompiler]")
{
    NodeGraph graph;
    graph.addNode ({ "src", "test.constant", {}, { { "value", 2.0f } }, {} });
    graph.addNode ({ "a", "test.scale", {}, { { "gain", 3.0f } }, {} });
    graph.addNode ({ "b", "test.scale", {}, { { "gain", 5.0f } }, {} });
    graph.addConnection ({ "src", "out", "a", "in" });
    graph.addConnection ({ "a", "out", "b", "in" });
    graph.setOutput ("b", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);

    REQUIRE (result.success);
    REQUIRE (result.plan.steps.size() == 3);

    // Topological order: src must be scheduled before a, a before b.
    auto positionOf = [&] (const juce::String& id)
    {
        const auto slot = result.plan.nodeIdToSlot.at (id);
        for (size_t i = 0; i < result.plan.steps.size(); ++i)
            if (result.plan.steps[i].kind == ExecutionPlan::Step::Kind::Block && result.plan.steps[i].block.nodeSlot == slot)
                return (int) i;
        return -1;
    };

    const auto srcPos = positionOf ("src");
    const auto aPos = positionOf ("a");
    const auto bPos = positionOf ("b");

    REQUIRE ((srcPos >= 0 && aPos >= 0 && bPos >= 0));
    CHECK (srcPos < aPos);
    CHECK (aPos < bPos);

    result.plan.process (8);
    const auto* output = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);

    for (int i = 0; i < 8; ++i)
        CHECK (output[i] == 2.0f * 3.0f * 5.0f); // 2 -> *3 -> *5 = 30
}

TEST_CASE ("GraphCompiler routes a feedback cycle into a per-sample region", "[engine][GraphCompiler]")
{
    auto graph = bazalt::engine::buildKarplusStrongProofGraph();
    auto factory = bazalt::engine::buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);

    REQUIRE (result.success);

    const auto regionSteps = std::count_if (result.plan.steps.begin(), result.plan.steps.end(), [] (const auto& step)
    {
        return step.kind == ExecutionPlan::Step::Kind::PerSampleRegion;
    });

    REQUIRE (regionSteps == 1);

    const auto& region = std::find_if (result.plan.steps.begin(), result.plan.steps.end(), [] (const auto& step)
    {
        return step.kind == ExecutionPlan::Step::Kind::PerSampleRegion;
    })->region;

    // mix, delay, damp are the cycle; excite is external and must NOT be
    // scheduled inside the region.
    CHECK (region.nodeSlotsInOrder.size() == 3);
    REQUIRE (region.externalOutputBufferIndex >= 0); // damp's output is the graph's designated output
}

TEST_CASE ("GraphCompiler rejects a cycle containing a node that can't run per-sample", "[engine][GraphCompiler]")
{
    NodeGraph graph;
    graph.addNode ({ "a", "test.blockonly", {}, {}, {} });
    graph.addNode ({ "b", "test.scale", {}, { { "gain", 1.0f } }, {} });
    graph.addConnection ({ "a", "out", "b", "in" });
    graph.addConnection ({ "b", "out", "a", "in" }); // closes the cycle
    graph.setOutput ("b", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);

    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());
}

TEST_CASE ("An invalid recompile never reaches the audio thread - the previous plan stays live",
           "[engine][GraphCompiler][PlanSwapper]")
{
    NodeGraph validGraph;
    validGraph.addNode ({ "src", "test.constant", {}, { { "value", 7.0f } }, {} });
    validGraph.setOutput ("src", "out");

    auto factory = buildTestFactory();
    auto validResult = GraphCompiler::compile (validGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (validResult.success);

    PlanSwapper swapper;
    REQUIRE (swapper.publish (std::make_unique<ExecutionPlan> (std::move (validResult.plan))));

    NodeGraph invalidGraph;
    invalidGraph.addNode ({ "a", "test.blockonly", {}, {}, {} });
    invalidGraph.addNode ({ "b", "test.scale", {}, { { "gain", 1.0f } }, {} });
    invalidGraph.addConnection ({ "a", "out", "b", "in" });
    invalidGraph.addConnection ({ "b", "out", "a", "in" });
    invalidGraph.setOutput ("b", "out");

    auto invalidResult = GraphCompiler::compile (invalidGraph, factory, { 44100.0, 64 }, 2);
    REQUIRE_FALSE (invalidResult.success);

    // Caller never publishes a failed compile — exactly what makes "the
    // previous valid plan stays live" true by construction, not by care.
    auto* live = swapper.getCurrentPlanForAudioThread();
    REQUIRE (live != nullptr);
    CHECK (live->generation == 1);

    live->process (4);
    const auto* output = live->blockBuffers[(size_t) live->finalOutputBufferIndex].getBlock().getChannelPointer (0);
    for (int i = 0; i < 4; ++i)
        CHECK (output[i] == 7.0f);
}

TEST_CASE ("GraphCompiler reuses an unchanged node's exact object across a recompile (M17 state pool)",
           "[engine][GraphCompiler][M17]")
{
    NodeGraph graph;
    graph.addNode ({ "counter", "test.counter", {}, {}, {} });
    graph.setOutput ("counter", "out");

    auto factory = buildTestFactory();

    auto first = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (first.success);
    first.plan.process (10); // counter's internal state advances to 10

    // Recompile the SAME graph (counter's id/type/parameters all unchanged)
    // — passing the first plan as previousPlan should reuse counter's
    // exact object, carrying its state forward instead of restarting at 0.
    auto second = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 2, &first.plan);
    REQUIRE (second.success);

    CHECK (second.plan.nodes[(size_t) second.plan.nodeIdToSlot.at ("counter")]
           == first.plan.nodes[(size_t) first.plan.nodeIdToSlot.at ("counter")]); // literally the same shared_ptr

    second.plan.process (1);
    const auto* output = second.plan.blockBuffers[(size_t) second.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    CHECK (output[0] == 10.0f); // continues from where it left off, not reset to 0
}

TEST_CASE ("GraphCompiler builds a fresh node instead of reusing when that node's own parameters changed",
           "[engine][GraphCompiler][M17]")
{
    NodeGraph graph;
    graph.addNode ({ "counter", "test.counter", {}, {}, {} });
    graph.setOutput ("counter", "out");

    auto factory = buildTestFactory();
    auto first = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (first.success);
    first.plan.process (10);

    // Same id and type, but CounterNode takes no parameters that could
    // differ — simulate "this node's own edit" the way a real stateful
    // node with a real parameter would: add a harmless parameter value
    // that GraphCompiler still sees as a genuine parameter-map difference,
    // proving the safety check is about parameter-map equality, not about
    // whether the node happens to read that particular key.
    NodeGraph edited;
    edited.addNode ({ "counter", "test.counter", {}, { { "unused", 42.0f } }, {} });
    edited.setOutput ("counter", "out");

    auto second = GraphCompiler::compile (edited, factory, { 44100.0, 64 }, 2, &first.plan);
    REQUIRE (second.success);

    CHECK (second.plan.nodes[(size_t) second.plan.nodeIdToSlot.at ("counter")]
           != first.plan.nodes[(size_t) first.plan.nodeIdToSlot.at ("counter")]); // NOT reused — different object

    second.plan.process (1);
    const auto* output = second.plan.blockBuffers[(size_t) second.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    CHECK (output[0] == 0.0f); // fresh object, starts from 0 again — safe, not a state race
}

TEST_CASE ("A bypassed node passes its primary input straight through instead of running processBlock",
           "[engine][GraphCompiler][bypass]")
{
    // docs/CLEANUP.md Priority 1 #1: NodeInstance::properties["bypassed"]
    // used to have zero read sites anywhere in GraphCompiler/ExecutionPlan
    // — toggling it changed only what was stored and displayed, never the
    // compiled audio. ScaleNode (output = input * gain) makes the fix
    // directly observable: bypassed, its "gain" multiply must never run at
    // all, not just happen to produce the same number.
    auto factory = buildTestFactory();

    NodeGraph normalGraph;
    normalGraph.addNode ({ "src", "test.constant", {}, { { "value", 5.0f } }, {} });
    normalGraph.addNode ({ "a", "test.scale", {}, { { "gain", 3.0f } }, {} });
    normalGraph.addConnection ({ "src", "out", "a", "in" });
    normalGraph.setOutput ("a", "out");

    auto normalResult = GraphCompiler::compile (normalGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (normalResult.success);
    normalResult.plan.process (8);
    const auto* normalOutput = normalResult.plan.blockBuffers[(size_t) normalResult.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    for (int i = 0; i < 8; ++i)
        CHECK (normalOutput[i] == 15.0f); // 5 * 3, gain applied as normal

    NodeGraph bypassedGraph;
    bypassedGraph.addNode ({ "src", "test.constant", {}, { { "value", 5.0f } }, {} });
    bypassedGraph.addNode ({ "a", "test.scale", {}, { { "gain", 3.0f } }, { { "bypassed", juce::var (true) } } });
    bypassedGraph.addConnection ({ "src", "out", "a", "in" });
    bypassedGraph.setOutput ("a", "out");

    auto bypassedResult = GraphCompiler::compile (bypassedGraph, factory, { 44100.0, 64 }, 1);
    REQUIRE (bypassedResult.success);
    bypassedResult.plan.process (8);
    const auto* bypassedOutput = bypassedResult.plan.blockBuffers[(size_t) bypassedResult.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    for (int i = 0; i < 8; ++i)
        CHECK (bypassedOutput[i] == 5.0f); // passed straight through — gain=3 never applied
}

TEST_CASE ("GraphCompiler rejects a Note connection whose endpoint is inside a feedback cycle (M18, ADR-0024)",
           "[engine][GraphCompiler][Note][M18]")
{
    NodeGraph graph;
    graph.addNode ({ "loop", "test.noteProducerWithAudioLoop", {}, {}, {} });
    graph.addNode ({ "sink", "test.noteSink", {}, {}, {} });

    graph.addConnection ({ "loop", "out", "loop", "in" }); // self-loop -> per-sample region
    graph.addConnection ({ "loop", "notes", "sink", "spawn" });
    graph.setOutput ("loop", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);

    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());
}

namespace
{
    NoteCaptureNode& captureNodeOf (CompileResult& result, const juce::String& nodeId)
    {
        auto* node = dynamic_cast<NoteCaptureNode*> (result.plan.nodes[(size_t) result.plan.nodeIdToSlot.at (nodeId)].get());
        REQUIRE (node != nullptr);
        return *node;
    }
}

TEST_CASE ("A Reroute takes on a Control signal's type instead of forcing Audio (docs/CLEANUP.md P1 #2)",
           "[engine][GraphCompiler][Reroute]")
{
    // Before the polymorphic-port fix, both Reroute ports were hardcoded
    // Audio, so canConnect() rejected wiring one into any Control chain.
    NodeGraph graph;
    graph.addNode ({ "src", "test.controlConstant", {}, { { "value", 0.25f } }, {} });
    graph.addNode ({ "rr", "util.reroute", {}, {}, {} });
    graph.addNode ({ "scale", "test.controlScale", {}, { { "gain", 4.0f } }, {} });
    graph.addConnection ({ "src", "out", "rr", "in" });
    graph.addConnection ({ "rr", "out", "scale", "in" });
    graph.setOutput ("scale", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    result.plan.process (8);
    const auto* output = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    for (int i = 0; i < 8; ++i)
        CHECK (output[i] == 1.0f); // 0.25 * 4, passed through Reroute unchanged
}

TEST_CASE ("A Reroute carries Note data through intact (docs/CLEANUP.md P1 #2)",
           "[engine][GraphCompiler][Reroute][Note]")
{
    NodeGraph graph;
    graph.addNode ({ "src", "test.noteSource", {}, {}, {} });
    graph.addNode ({ "rr", "util.reroute", {}, {}, {} });
    graph.addNode ({ "cap", "test.noteCapture", {}, {}, {} });
    graph.addConnection ({ "src", "notes", "rr", "in" });
    graph.addConnection ({ "rr", "out", "cap", "spawn" });
    graph.setOutput ("src", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    result.plan.process (8);

    auto& capture = captureNodeOf (result, "cap");
    REQUIRE (capture.capturedCount == 8);
    CHECK (capture.captured[0].gate);
    CHECK (capture.captured[0].pitch == 72.0f);
    CHECK (capture.captured[0].velocity == 0.8f);
    CHECK (capture.captured[0].startEvent);   // only the block's first sample, exactly as the source emitted it
    CHECK_FALSE (capture.captured[1].startEvent);
    CHECK (capture.captured[7].gate);
}

TEST_CASE ("A chain of Reroutes resolves to the source's type regardless of node declaration order",
           "[engine][GraphCompiler][Reroute][Note]")
{
    // Declared sink-first / source-last: a single forward pass over the
    // node list would resolve rr1 too late for rr2 to see it, so this only
    // passes if the compiler genuinely iterates to a fixed point.
    NodeGraph graph;
    graph.addNode ({ "cap", "test.noteCapture", {}, {}, {} });
    graph.addNode ({ "rr2", "util.reroute", {}, {}, {} });
    graph.addNode ({ "rr1", "util.reroute", {}, {}, {} });
    graph.addNode ({ "src", "test.noteSource", {}, {}, {} });
    graph.addConnection ({ "src", "notes", "rr1", "in" });
    graph.addConnection ({ "rr1", "out", "rr2", "in" });
    graph.addConnection ({ "rr2", "out", "cap", "spawn" });
    graph.setOutput ("src", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    result.plan.process (4);
    auto& capture = captureNodeOf (result, "cap");
    REQUIRE (capture.capturedCount == 4);
    CHECK (capture.captured[0].pitch == 72.0f);
    CHECK (capture.captured[0].startEvent);
}

TEST_CASE ("A Reroute fed one type still rejects a downstream port of an incompatible type",
           "[engine][GraphCompiler][Reroute]")
{
    // Polymorphic must not mean "accepts anything": Note -> Reroute -> an
    // Audio input has to fail canConnect() at the Reroute's OUTPUT side.
    NodeGraph graph;
    graph.addNode ({ "src", "test.noteSource", {}, {}, {} });
    graph.addNode ({ "rr", "util.reroute", {}, {}, {} });
    graph.addNode ({ "scale", "test.scale", {}, { { "gain", 1.0f } }, {} });
    graph.addConnection ({ "src", "notes", "rr", "in" });
    graph.addConnection ({ "rr", "out", "scale", "in" });
    graph.setOutput ("scale", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);

    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());
}

TEST_CASE ("A recompile never reuses a Reroute, so it can't keep a stale type after its input is disconnected",
           "[engine][GraphCompiler][Reroute]")
{
    auto factory = buildTestFactory();

    NodeGraph connected;
    connected.addNode ({ "src", "test.controlConstant", {}, { { "value", 1.0f } }, {} });
    connected.addNode ({ "rr", "util.reroute", {}, {}, {} });
    connected.addConnection ({ "src", "out", "rr", "in" });
    connected.setOutput ("rr", "out");

    auto first = GraphCompiler::compile (connected, factory, { 44100.0, 64 }, 1);
    REQUIRE (first.success);
    CHECK (first.plan.nodes[(size_t) first.plan.nodeIdToSlot.at ("rr")]->getInputPorts()[0].type == SignalType::Control);

    // Same node id/type/params, but nothing feeds it any more. A reused
    // node object would still report Control; a fresh compile's default is
    // Audio, and the same graph must compile the same way regardless of
    // what was edited before it.
    NodeGraph disconnected;
    disconnected.addNode ({ "rr", "util.reroute", {}, {}, {} });
    disconnected.setOutput ("rr", "out");

    auto second = GraphCompiler::compile (disconnected, factory, { 44100.0, 64 }, 2, &first.plan);
    REQUIRE (second.success);

    const auto& secondNode = second.plan.nodes[(size_t) second.plan.nodeIdToSlot.at ("rr")];
    CHECK (secondNode != first.plan.nodes[(size_t) first.plan.nodeIdToSlot.at ("rr")]);
    CHECK (secondNode->getInputPorts()[0].type == SignalType::Audio);
}

// ---- Real stereo cable redesign: flat-slot allocation, proven against
// synthetic node types before any real node relies on it ----

TEST_CASE ("A Stereo port occupies two flat buffer slots, resolved pairwise from another Stereo source",
           "[engine][GraphCompiler][Stereo]")
{
    NodeGraph graph;
    graph.addNode ({ "src", "test.stereoConstant", {}, { { "left", 3.0f }, { "right", 7.0f } }, {} });
    graph.setOutput ("src", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    // The Stereo output allocated TWO buffers, not one - and the final
    // output resolution found both without any extra "is it wired" guard
    // (this graph never even connects "out" to anything else).
    REQUIRE (result.plan.finalOutputBufferIndex >= 0);
    REQUIRE (result.plan.finalOutputBufferIndexRight >= 0);
    CHECK (result.plan.finalOutputBufferIndex != result.plan.finalOutputBufferIndexRight);

    result.plan.process (4);
    const auto* left = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    const auto* right = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndexRight].getBlock().getChannelPointer (0);

    for (int i = 0; i < 4; ++i)
    {
        CHECK (left[i] == 3.0f);
        CHECK (right[i] == 7.0f);
    }
}

TEST_CASE ("A Stereo destination fed by a Mono source broadcasts the same buffer to both channels",
           "[engine][GraphCompiler][Stereo]")
{
    // canConnect's existing mono->stereo rule (CanConnect.cpp) is free -
    // this proves the compiler actually backs it with a real second buffer,
    // not just permitting the connection.
    NodeGraph graph;
    graph.addNode ({ "src", "test.constant", {}, { { "value", 5.0f } }, {} });
    graph.addNode ({ "pass", "test.stereoPassthrough", {}, {}, {} });
    graph.addConnection ({ "src", "out", "pass", "in" });
    graph.setOutput ("pass", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);
    REQUIRE (result.plan.finalOutputBufferIndexRight >= 0);

    result.plan.process (4);
    const auto* left = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    const auto* right = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndexRight].getBlock().getChannelPointer (0);

    for (int i = 0; i < 4; ++i)
    {
        CHECK (left[i] == 5.0f);
        CHECK (right[i] == 5.0f); // broadcast, not silence - the same mono source reaches both channels
    }
}

TEST_CASE ("Chaining two Stereo nodes keeps left and right independent all the way through",
           "[engine][GraphCompiler][Stereo]")
{
    NodeGraph graph;
    graph.addNode ({ "src", "test.stereoConstant", {}, { { "left", 1.0f }, { "right", 2.0f } }, {} });
    graph.addNode ({ "pass", "test.stereoPassthrough", {}, {}, {} });
    graph.addConnection ({ "src", "out", "pass", "in" });
    graph.setOutput ("pass", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);
    REQUIRE (result.plan.finalOutputBufferIndexRight >= 0);

    result.plan.process (4);
    const auto* left = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    const auto* right = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndexRight].getBlock().getChannelPointer (0);

    for (int i = 0; i < 4; ++i)
    {
        CHECK (left[i] == 1.0f);
        CHECK (right[i] == 2.0f);
    }
}

TEST_CASE ("A node declaring more flat channels than maxPortsPerNode is rejected, even with few descriptors",
           "[engine][GraphCompiler][Stereo]")
{
    // A real regression this redesign had to guard against: the OLD check
    // compared DESCRIPTOR count to maxPortsPerNode. 17 Stereo descriptors is
    // well under 32 by descriptor count, but 34 flat channels - over the
    // limit ExecutionPlan::process()'s scratch arrays are actually sized
    // for. The old check would have silently passed this and let it
    // overrun those arrays at runtime; the fixed check counts flat channels.
    NodeGraph graph;
    graph.addNode ({ "n", "test.tooManyStereoChannels", {}, {}, {} });
    graph.setOutput ("n", "out");

    auto factory = buildTestFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());
}
