#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/ViewScopeModulationNode.h"
#include "bazalt/engine/nodes/ViewGateNode.h"
#include "bazalt/engine/nodes/ViewScopeControlNode.h"

// design/Visualization/ScopeMod.png and Gate.png: the second and third of
// the three scrolling-history viewers. Their engine sides are deliberately
// view.scope.control's own (a pass-through plus ViewHistoryWindow), so these
// tests cover only what actually differs: the ports, the parameter ids, and
// that each splices into its own signal type without an adapter.

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
        factory.registerType ("test.unipolar", [] { return std::make_unique<FixedSourceNode> (SignalType::Control, Quantity::Unipolar); });
        factory.registerType ("test.bipolar", [] { return std::make_unique<FixedSourceNode> (SignalType::Control, Quantity::Bipolar); });
        factory.registerType ("test.bool", [] { return std::make_unique<FixedSourceNode> (SignalType::Boolean, Quantity::Dimensionless); });
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

TEST_CASE ("view.scope.modulation is a Control pass-through that defaults to Bipolar and adopts its source's quantity",
           "[engine][nodes][view.scope.modulation]")
{
    ViewScopeModulationNode node;
    CHECK (node.hasPolymorphicPorts());

    const auto in = node.getInputPorts();
    const auto out = node.getOutputPorts();
    REQUIRE (in.size() == 1);
    REQUIRE (out.size() == 1);
    CHECK (in[0].type == SignalType::Control);
    CHECK (in[0].quantity == Quantity::Bipolar); // unconnected: the signed case the panel is drawn for
    CHECK (in[0].polymorphism == PortPolymorphism::Quantity);
    CHECK (out[0].isPrimaryOutput);
    CHECK (out[0].quantity == Quantity::Bipolar);

    PortDescriptor unipolar { "src", SignalType::Control };
    unipolar.quantity = Quantity::Unipolar;
    node.resolveIncomingPort ("in", unipolar);
    CHECK (node.getInputPorts()[0].quantity == Quantity::Unipolar);
    CHECK (node.getOutputPorts()[0].quantity == Quantity::Unipolar); // the pass-through stays transparent downstream
    CHECK (node.getInputPorts()[0].type == SignalType::Control);     // only the quantity is inherited, never the type

    float value = -0.7f, result = 0.0f;
    node.processSample (&value, &result);
    CHECK (result == -0.7f);
}

TEST_CASE ("view.scope.modulation splices directly into both a unipolar and a bipolar cable, no adapter",
           "[engine][nodes][view.scope.modulation][GraphCompiler]")
{
    CHECK (compileAndRun ("test.unipolar", 0.25f, "view.scope.modulation") == Catch::Approx (0.25f));
    CHECK (compileAndRun ("test.bipolar", -0.5f, "view.scope.modulation") == Catch::Approx (-0.5f));
}

TEST_CASE ("view.gate is a Boolean pass-through", "[engine][nodes][view.gate]")
{
    ViewGateNode node;
    CHECK_FALSE (node.hasPolymorphicPorts());

    const auto in = node.getInputPorts();
    const auto out = node.getOutputPorts();
    REQUIRE (in.size() == 1);
    REQUIRE (out.size() == 1);
    CHECK (in[0].type == SignalType::Boolean);
    CHECK (in[0].kind == ValueKind::Bool);
    CHECK (out[0].type == SignalType::Boolean);
    CHECK (out[0].isPrimaryOutput);

    float value = 1.0f, result = 0.0f;
    node.processSample (&value, &result);
    CHECK (result == 1.0f);

    CHECK (compileAndRun ("test.bool", 1.0f, "view.gate") == Catch::Approx (1.0f));
}

TEST_CASE ("every history viewer exposes the same clamped time window under its own parameter id",
           "[engine][nodes][view.scope.modulation][view.gate][view.scope.control]")
{
    auto check = [] (auto node, const juce::String& parameterId)
    {
        const auto parameters = node.getParameters();
        REQUIRE (parameters.size() == 1);
        CHECK (parameters[0].id == parameterId);
        CHECK (parameters[0].isStructural);
        CHECK (parameters[0].minValue == ViewHistoryWindow::minSeconds);
        CHECK (parameters[0].maxValue == ViewHistoryWindow::maxSeconds);
        CHECK (previewWindow (node) == ViewHistoryWindow::defaultSeconds);

        node.setParameter (parameterId, 7.5f);
        CHECK (previewWindow (node) == 7.5f);
        node.setParameter (parameterId, 1000.0f);
        CHECK (previewWindow (node) == ViewHistoryWindow::maxSeconds);
        node.setParameter (parameterId, 0.0f);
        CHECK (previewWindow (node) == ViewHistoryWindow::minSeconds);
        node.setParameter ("some.other.parameter", 5.0f);
        CHECK (previewWindow (node) == ViewHistoryWindow::minSeconds);
    };

    check (ViewScopeControlNode {}, "view.scope.control.timeWindow");
    check (ViewScopeModulationNode {}, "view.scope.modulation.timeWindow");
    check (ViewGateNode {}, "view.gate.timeWindow");
}

TEST_CASE ("view.scope and view.glance are gone; view.cycle replaces them; nothing registered is deprecated",
           "[engine][nodes][deprecated]")
{
    const auto factory = buildDefaultNodeFactory();
    REQUIRE (factory.isRegistered ("view.scope.modulation"));
    REQUIRE (factory.isRegistered ("view.gate"));
    REQUIRE (factory.isRegistered ("view.cycle"));
    CHECK_FALSE (factory.isRegistered ("view.scope"));  // removed 2026-10-04 (PatchSerializer v8 -> v9 drops it)
    CHECK_FALSE (factory.isRegistered ("view.glance")); // removed 2026-10-04 (spliced out of its cable on load)

    for (const auto& descriptor : factory.describeAll())
    {
        INFO (descriptor.typeId);
        CHECK_FALSE (descriptor.deprecated);
    }
}
