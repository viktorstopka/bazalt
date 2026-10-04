#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/ViewRippleNode.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

TEST_CASE ("ViewRippleNode is a real Event in/out pass-through", "[engine][nodes][view.ripple]")
{
    ViewRippleNode node;

    const auto inputs = node.getInputPorts();
    REQUIRE (inputs.size() == 1);
    CHECK (inputs[0].id == "in");
    CHECK (inputs[0].type == SignalType::Event);

    const auto outputs = node.getOutputPorts();
    REQUIRE (outputs.size() == 1);
    CHECK (outputs[0].id == "out");
    CHECK (outputs[0].type == SignalType::Event);
    CHECK (outputs[0].isPrimaryOutput);

    // The whole point (design/Visualization/Ripple.png): "it can be spliced
    // into an existing wire without altering behaviour" — a real,
    // unchanged pass-through, not a dead end like view.scope/meter.
    float out = -1.0f;
    float in = 0.0f;
    node.processSample (&in, &out);
    CHECK (out == 0.0f);

    in = 1.0f;
    node.processSample (&in, &out);
    CHECK (out == 1.0f);
}

TEST_CASE ("ViewRippleNode declares an EventImpulse preview on its output, and no parameters at all",
           "[engine][nodes][view.ripple]")
{
    ViewRippleNode node;

    const auto previews = node.getPreviews();
    REQUIRE (previews.size() == 1);
    CHECK (previews[0].kind == PreviewKind::EventImpulse);
    CHECK (previews[0].portId == "out");

    // "No title, no labels" (direct instruction) — RippleBody.tsx never
    // renders a parameter row at all; a real empty list, not just an
    // untested assumption.
    CHECK (node.getParameters().empty());
}

TEST_CASE ("view.ripple is registered in the default node factory", "[engine][nodes][view.ripple]")
{
    auto factory = buildDefaultNodeFactory();
    auto node = factory.create ("view.ripple");
    REQUIRE (node != nullptr);
    CHECK (node->getTitle() == "Ripple");
    CHECK (node->getCategory() == "View");
}
