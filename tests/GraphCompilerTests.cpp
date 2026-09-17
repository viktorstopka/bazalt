#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/PlanSwapper.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/Node.h"
#include <algorithm>
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

    NodeFactory buildTestFactory()
    {
        NodeFactory factory;
        factory.registerType ("test.scale", [] { return std::make_unique<ScaleNode>(); });
        factory.registerType ("test.constant", [] { return std::make_unique<ConstantNode>(); });
        factory.registerType ("test.blockonly", [] { return std::make_unique<BlockOnlyNode>(); });
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
