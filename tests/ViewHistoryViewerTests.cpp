#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/ViewScopeNode.h"

// view.scope — the one scrolling-history viewer (wiki/plans/DataAndWavetable.md
// §2, merging view.scope.control, view.scope.modulation and view.gate): a
// pass-through plus ViewHistoryWindow whose ports take on what is wired.

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    class FixedSourceNode : public Node
    {
    public:
        FixedSourceNode (SignalType t, Quantity q) : type (t), quantity (q) {}
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            PortDescriptor port { "out", type };
            port.quantity = quantity;
            return { port };
        }
        void setParameter (const juce::String& id, float value) override
        {
            if (id == "value")
                constantValue = value;
        }
        void processSample (const float*, float* outputs) noexcept override { outputs[0] = constantValue; }

    private:
        SignalType type;
        Quantity quantity;
        float constantValue = 0.0f;
    };

    NodeFactory makeFactory()
    {
        auto factory = buildDefaultNodeFactory();
        factory.registerType ("test.unipolar", [] { return std::make_unique<FixedSourceNode> (SignalType::Signal, Quantity::Unipolar); });
        factory.registerType ("test.bipolar", [] { return std::make_unique<FixedSourceNode> (SignalType::Signal, Quantity::Bipolar); });
        factory.registerType ("test.frequency", [] { return std::make_unique<FixedSourceNode> (SignalType::Signal, Quantity::Frequency); });
        factory.registerType ("test.bool", [] { return std::make_unique<FixedSourceNode> (SignalType::Signal, Quantity::Boolean); });
        return factory;
    }

    float compileAndRun (const juce::String& sourceType, float value, const juce::String& viewerType)
    {
        auto factory = makeFactory();
        NodeGraph graph;
        graph.addNode ({ "src", sourceType, {}, { { "value", value } }, {} });
        graph.addNode ({ "view", viewerType, {}, {}, {} });
        graph.addConnection ({ "src", "out", "view", "in" });
        graph.setOutput ("view", "out");

        auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
        REQUIRE (result.success); // a NeedsAdapters/Reject connection fails compile outright
        result.plan.process (8);
        return result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0)[7];
    }

    template <typename NodeT>
    float previewWindow (const NodeT& node)
    {
        const auto previews = node.getPreviews();
        REQUIRE (previews.size() == 1);
        CHECK (previews[0].kind == PreviewKind::RollingHistory);
        CHECK (previews[0].portId == "out");
        return previews[0].timeWindowSeconds;
    }
}

TEST_CASE ("view.scope is a pass-through whose ports take on the source's type and quantity",
           "[engine][nodes][view.scope]")
{
    ViewScopeNode node;
    REQUIRE (node.hasPolymorphicPorts());
    CHECK (node.getInputPorts()[0].type == SignalType::Signal); // unconnected default

    PortDescriptor boolean { .id = "src", .type = SignalType::Signal, .quantity = Quantity::Boolean };
    node.resolveIncomingPort ("in", boolean);
    CHECK (node.getInputPorts()[0].quantity == Quantity::Boolean);
    CHECK (node.getOutputPorts()[0].quantity == Quantity::Boolean);

    ViewScopeNode modulation;
    PortDescriptor unipolar { .id = "src", .type = SignalType::Signal };
    unipolar.quantity = Quantity::Unipolar;
    modulation.resolveIncomingPort ("in", unipolar);
    CHECK (modulation.getOutputPorts()[0].quantity == Quantity::Unipolar);

    const float inputs[1] = { 0.375f };
    float out = 0.0f;
    node.processSample (inputs, &out);
    CHECK (out == 0.375f);
}

TEST_CASE ("view.scope splices into any value cable without an adapter",
           "[engine][nodes][view.scope][GraphCompiler]")
{
    CHECK (compileAndRun ("test.unipolar", 0.25f, "view.scope") == Catch::Approx (0.25f));
    CHECK (compileAndRun ("test.bipolar", -0.5f, "view.scope") == Catch::Approx (-0.5f));
    CHECK (compileAndRun ("test.frequency", 440.0f, "view.scope") == Catch::Approx (440.0f));
    CHECK (compileAndRun ("test.bool", 1.0f, "view.scope") == Catch::Approx (1.0f));
}

TEST_CASE ("view.scope's RollingHistory preview tracks its structural timeWindow parameter, clamped",
           "[engine][nodes][view.scope]")
{
    ViewScopeNode node;
    const auto parameters = node.getParameters();
    REQUIRE (parameters.size() == 1);
    CHECK (parameters[0].id == "view.scope.timeWindow");
    CHECK (parameters[0].isStructural);
    CHECK (previewWindow (node) == ViewHistoryWindow::defaultSeconds);

    node.setParameter ("view.scope.timeWindow", 4.5f);
    CHECK (previewWindow (node) == 4.5f);
    node.setParameter ("view.scope.timeWindow", 1000.0f);
    CHECK (previewWindow (node) == ViewHistoryWindow::maxSeconds);
    node.setParameter ("view.scope.timeWindow", -5.0f);
    CHECK (previewWindow (node) == ViewHistoryWindow::minSeconds);
}

TEST_CASE ("view.scope replaces the three type-specific scopes in the default factory", "[engine][nodes][view.scope]")
{
    const auto factory = buildDefaultNodeFactory();
    CHECK (factory.isRegistered ("view.scope"));
    CHECK_FALSE (factory.isRegistered ("view.scope.control"));
    CHECK_FALSE (factory.isRegistered ("view.scope.modulation"));
    CHECK_FALSE (factory.isRegistered ("view.gate"));
    CHECK_FALSE (factory.isRegistered ("view.glance")); // removed 2026-10-04 (spliced out of its cable on load)
}
