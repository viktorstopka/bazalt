#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/ViewScopeControlNode.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

TEST_CASE ("ViewScopeControlNode is a real Control in/out pass-through", "[engine][nodes][view.scope.control]")
{
    ViewScopeControlNode node;

    const auto inputs = node.getInputPorts();
    REQUIRE (inputs.size() == 1);
    CHECK (inputs[0].id == "in");
    CHECK (inputs[0].type == SignalType::Control);
    CHECK_FALSE (inputs[0].isInteger);

    const auto outputs = node.getOutputPorts();
    REQUIRE (outputs.size() == 1);
    CHECK (outputs[0].id == "out");
    CHECK (outputs[0].type == SignalType::Control);
    CHECK (outputs[0].isPrimaryOutput);
    CHECK_FALSE (outputs[0].isInteger);

    // design/Visualization/Scope1.png: "the value arriving at the input is
    // passed to the output unchanged" — same real, unchanged pass-through
    // every other viewer node (view.ripple/view.count/view.glance) gives.
    float out = -1.0f;
    float in = 0.42f;
    node.processSample (&in, &out);
    CHECK (out == 0.42f);
}

TEST_CASE ("ViewScopeControlNode declares exactly one structural timeWindow parameter",
           "[engine][nodes][view.scope.control]")
{
    ViewScopeControlNode node;

    const auto parameters = node.getParameters();
    REQUIRE (parameters.size() == 1);
    CHECK (parameters[0].id == "view.scope.control.timeWindow");
    CHECK (parameters[0].quantity == Quantity::Time);
    CHECK (parameters[0].isStructural);
    CHECK (parameters[0].minValue == ViewScopeControlNode::minTimeWindowSeconds);
    CHECK (parameters[0].maxValue == ViewScopeControlNode::maxTimeWindowSeconds);
    CHECK (parameters[0].defaultValue == ViewScopeControlNode::defaultTimeWindowSeconds);
}

TEST_CASE ("ViewScopeControlNode's RollingHistory preview tracks the live timeWindow parameter, clamped",
           "[engine][nodes][view.scope.control]")
{
    ViewScopeControlNode node;

    auto previewWindow = [&]
    {
        const auto previews = node.getPreviews();
        REQUIRE (previews.size() == 1);
        CHECK (previews[0].kind == PreviewKind::RollingHistory);
        CHECK (previews[0].portId == "out");
        return previews[0].timeWindowSeconds;
    };

    CHECK (previewWindow() == ViewScopeControlNode::defaultTimeWindowSeconds);

    node.setParameter ("view.scope.control.timeWindow", 4.5f);
    CHECK (previewWindow() == 4.5f);

    // Out-of-range requests clamp rather than taking effect verbatim —
    // AnalysisThread::publishRollingHistory also clamps independently
    // (ViewScopeControlNode.h's own doc comment on why the bounds are
    // duplicated rather than shared), so this is a UI-facing nicety, not
    // the only thing standing between a bad value and a divide-by-zero.
    node.setParameter ("view.scope.control.timeWindow", 1000.0f);
    CHECK (previewWindow() == ViewScopeControlNode::maxTimeWindowSeconds);
    node.setParameter ("view.scope.control.timeWindow", -5.0f);
    CHECK (previewWindow() == ViewScopeControlNode::minTimeWindowSeconds);
}

TEST_CASE ("view.scope.control is registered in the default node factory", "[engine][nodes][view.scope.control]")
{
    auto factory = buildDefaultNodeFactory();
    auto node = factory.create ("view.scope.control");
    REQUIRE (node != nullptr);
    CHECK (node->getTitle() == "Scope");
    CHECK (node->getCategory() == "View");
}
