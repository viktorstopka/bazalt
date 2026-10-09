#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/ViewCountNode.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

TEST_CASE ("ViewCountNode is a real integer Control in/out pass-through", "[engine][nodes][view.count]")
{
    ViewCountNode node;

    const auto inputs = node.getInputPorts();
    REQUIRE (inputs.size() == 1);
    CHECK (inputs[0].id == "in");
    CHECK (inputs[0].type == SignalType::Signal);
    CHECK (inputs[0].isInteger);

    const auto outputs = node.getOutputPorts();
    REQUIRE (outputs.size() == 1);
    CHECK (outputs[0].id == "out");
    CHECK (outputs[0].type == SignalType::Signal);
    CHECK (outputs[0].isInteger);
    CHECK (outputs[0].isPrimaryOutput);

    // design/Visualization/Count.png: "the value arriving at the input is
    // passed to the output unchanged" — a real, unchanged pass-through,
    // same reasoning view.ripple/view.glance give for their own.
    float out = -1.0f;
    float in = 3.0f;
    node.processSample (&in, &out);
    CHECK (out == 3.0f);

    in = 43.0f;
    node.processSample (&in, &out);
    CHECK (out == 43.0f);
}

TEST_CASE ("ViewCountNode declares a Waveform preview on its output, and no parameters at all",
           "[engine][nodes][view.count]")
{
    ViewCountNode node;

    const auto previews = node.getPreviews();
    REQUIRE (previews.size() == 1);
    CHECK (previews[0].kind == PreviewKind::Waveform);
    CHECK (previews[0].portId == "out");

    // "No title" (design/Visualization/Count.png) — CountBody.tsx never
    // renders a parameter row at all; a real empty list, not just an
    // untested assumption (ViewRippleNode's own test makes the same check).
    CHECK (node.getParameters().empty());
}

TEST_CASE ("view.count is registered in the default node factory", "[engine][nodes][view.count]")
{
    auto factory = buildDefaultNodeFactory();
    auto node = factory.create ("view.count");
    REQUIRE (node != nullptr);
    CHECK (node->getTitle() == "Count");
    CHECK (node->getCategory() == "View");
}
