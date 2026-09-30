#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/MacroNode.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

TEST_CASE ("MacroNode defaults to slot -1 (unclaimed) and the 0..1 identity range", "[engine][nodes][macro]")
{
    MacroNode node;
    CHECK (node.getSlot() == -1);

    float out = 0.0f;
    node.setParameter ("util.macro.value", 0.5f);
    node.processSample (nullptr, &out);
    CHECK (out == 0.5f); // default min=0/max=1 — an unconfigured macro is the identity
}

TEST_CASE ("MacroNode remaps its stored 0..1 value onto its own declared min/max", "[engine][nodes][macro]")
{
    MacroNode node;
    node.setParameter ("util.macro.min", 200.0f);
    node.setParameter ("util.macro.max", 12000.0f);
    node.setParameter ("util.macro.value", 0.25f);

    float out = 0.0f;
    node.processSample (nullptr, &out);
    CHECK (out == 200.0f + 0.25f * (12000.0f - 200.0f));
}

TEST_CASE ("MacroNode's slot/min/max/isInteger/quantity round-trip through setParameter into getOutputPorts()",
           "[engine][nodes][macro]")
{
    MacroNode node;
    node.setParameter ("util.macro.slot", 7.0f);
    node.setParameter ("util.macro.min", -60.0f);
    node.setParameter ("util.macro.max", 60.0f);
    node.setParameter ("util.macro.isInteger", 1.0f);
    node.setParameter ("util.macro.quantity", 2.0f); // Pitch, per Quantity's own declaration order

    CHECK (node.getSlot() == 7);

    const auto ports = node.getOutputPorts();
    REQUIRE (ports.size() == 1);
    const auto& out = ports[0];
    CHECK (out.id == "out");
    CHECK (out.type == SignalType::Control);
    CHECK (out.isPrimaryOutput);
    REQUIRE (out.minValue.has_value());
    REQUIRE (out.maxValue.has_value());
    CHECK (*out.minValue == -60.0f);
    CHECK (*out.maxValue == 60.0f);
    CHECK (out.isInteger);
    CHECK (out.kind == ValueKind::Int);
    CHECK (out.quantity == Quantity::Pitch);
}

TEST_CASE ("MacroNode's getParameters() never lists util.macro.value - only the direct MacroParameters poke writes it",
           "[engine][nodes][macro]")
{
    // Real, load-bearing: listing it would invite an ordinary NodeCard
    // slider fighting host automation every block (see MacroNode.h's own
    // doc comment). Node::setParameter() has no descriptor-membership
    // check, so the direct poke still works with it absent here — proven
    // by the two tests above already calling setParameter("util.macro.value", ...)
    // successfully.
    MacroNode node;
    for (const auto& p : node.getParameters())
        CHECK (p.id != "util.macro.value");
}

TEST_CASE ("MacroNode's exposed contract is structural - GraphCompiler builds a fresh node on any change, "
           "never mutates a reused one (M17 state pool)",
           "[engine][nodes][macro][GraphCompiler]")
{
    NodeGraph graph;
    graph.addNode ({ "m", "util.macro", {}, { { "util.macro.min", 0.0f }, { "util.macro.max", 1.0f } }, {} });
    graph.setOutput ("m", "out");

    auto factory = buildDefaultNodeFactory();
    auto first = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (first.success);

    // Same graph, unchanged — reused verbatim.
    auto second = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 2, &first.plan);
    REQUIRE (second.success);
    CHECK (second.plan.nodes[(size_t) second.plan.nodeIdToSlot.at ("m")]
           == first.plan.nodes[(size_t) first.plan.nodeIdToSlot.at ("m")]);

    // The contract changed (max: 1.0 -> 10.0) — a fresh node, not a stale
    // reused one carrying the old range forward.
    NodeGraph edited;
    edited.addNode ({ "m", "util.macro", {}, { { "util.macro.min", 0.0f }, { "util.macro.max", 10.0f } }, {} });
    edited.setOutput ("m", "out");

    auto third = GraphCompiler::compile (edited, factory, { 44100.0, 64 }, 3, &first.plan);
    REQUIRE (third.success);
    CHECK (third.plan.nodes[(size_t) third.plan.nodeIdToSlot.at ("m")]
           != first.plan.nodes[(size_t) first.plan.nodeIdToSlot.at ("m")]);
}
