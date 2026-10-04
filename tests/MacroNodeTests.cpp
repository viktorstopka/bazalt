#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/MacroNode.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include <algorithm>
#include <cmath>

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

TEST_CASE ("MacroNode rounds its output when isInteger is set, matching its own declared port contract",
           "[engine][nodes][macro]")
{
    // getOutputPorts() advertises isInteger/kind=Int off util.macro.isInteger
    // (Control type + isInteger=true — wiki/plans/PropsAndMacroRedesign.md
    // Batch E, rewritten per direct correction, 2026-10-03: "int (can be
    // toggled to be enum)" is a Control SUBTYPE, not a parallel type value,
    // so there's no separate "Int" ordinal to set anymore, just this flag)
    // — this confirms processSample() actually honors it, not just
    // getOutputPorts() alone.
    MacroNode node;
    node.setParameter ("util.macro.min", 0.0f);
    node.setParameter ("util.macro.max", 7.0f);
    node.setParameter ("util.macro.isInteger", 1.0f);

    float out = 0.0f;
    for (const float raw : { 0.0f, 0.1f, 0.49f, 0.5f, 0.51f, 0.9f, 1.0f })
    {
        node.setParameter ("util.macro.value", raw);
        node.processSample (nullptr, &out);
        CHECK (out == std::round (out)); // always a whole number
        CHECK (out == std::round (raw * 7.0f)); // and the RIGHT whole number
    }

    // Not rounded when isInteger is false (the default) - confirms this is a real
    // conditional, not an always-on rounding regression on the Float path.
    MacroNode floatNode;
    floatNode.setParameter ("util.macro.min", 0.0f);
    floatNode.setParameter ("util.macro.max", 7.0f);
    floatNode.setParameter ("util.macro.value", 0.1f);
    floatNode.processSample (nullptr, &out);
    CHECK (out == 0.1f * 7.0f);
    CHECK (out != std::round (out));
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

// ---- wiki/plans/PropsAndMacroRedesign.md Batch E: type-aware Macro ----
// Rewritten per direct correction, 2026-10-03: the real system is three
// top-level types (Control/Bool/Trigger), not five — Value/Modulation/Int
// are Control "subtypes" that fall out of quantity/isInteger, and Enum is
// "Int toggled to be Enum" (isEnum), not a parallel type value. Ordinals
// below: Control=0, Bool=1, Trigger=2 (Trigger stays last deliberately —
// see TypedValueNodeBase.h's own comment on why).

TEST_CASE ("MacroNode's type=Bool genuinely declares SignalType::Boolean, not a disguised Control 0/1",
           "[engine][nodes][macro][props-edit]")
{
    // The whole point of a real Bool output type: it connects straight
    // into a real Boolean port (env.adsr's own "gate") with no adapter at
    // all, closing the gap graphStore.ts's isMacroablePort() comment
    // documents (an adapter exists for Boolean->Control, none for the
    // reverse, so a macro whose output was ALWAYS Control could never feed
    // a Boolean port).
    MacroNode node;
    node.setParameter ("util.macro.type", 1.0f); // Bool

    const auto ports = node.getOutputPorts();
    REQUIRE (ports.size() == 1);
    CHECK (ports[0].type == SignalType::Boolean);
    CHECK (ports[0].kind == ValueKind::Bool);
    CHECK_FALSE (ports[0].minValue.has_value()); // no range on a Boolean port, matching every other one in this codebase

    float out = -1.0f;
    node.setParameter ("util.macro.value", 0.2f);
    node.processSample (nullptr, &out);
    CHECK (out == 0.0f);

    node.setParameter ("util.macro.value", 0.8f);
    node.processSample (nullptr, &out);
    CHECK (out == 1.0f);
}

TEST_CASE ("MacroNode's type=Bool ignores a stale min/max range left over from a previous Control type",
           "[engine][nodes][macro][props-edit]")
{
    // A real bug caught while building this: processSample() used to remap
    // storedValue through storedMin/storedMax BEFORE checking the 0.5
    // threshold, so a macro that had e.g. min=5/max=10 while it was still
    // Control (the UI hides those fields once switched to Bool, but doesn't
    // reset them) would read as "always true" after switching to Bool,
    // regardless of the real raw value.
    MacroNode node;
    node.setParameter ("util.macro.min", 5.0f);
    node.setParameter ("util.macro.max", 10.0f);
    node.setParameter ("util.macro.type", 1.0f); // Bool

    float out = -1.0f;
    node.setParameter ("util.macro.value", 0.2f); // raw fraction, not a 5..10 value
    node.processSample (nullptr, &out);
    CHECK (out == 0.0f); // would wrongly read 1.0f (true) under the bug
}

TEST_CASE ("MacroNode's isInteger+isEnum declares an integer-kind Control output clamped to min..max",
           "[engine][nodes][macro][props-edit]")
{
    // Custom option LABELS are a UI-only property overlay (same mechanism
    // util.macro.unit used to be, before "Unit should not be a field"
    // removed it outright) — the engine side only needs the numeric
    // contract: 0..(N-1), integer, so Compare/etc. all work on it exactly
    // like a plain Int, just clamped so the index can't go out of range
    // (direct instruction).
    MacroNode node;
    node.setParameter ("util.macro.isInteger", 1.0f);
    node.setParameter ("util.macro.isEnum", 1.0f);
    node.setParameter ("util.macro.min", 0.0f);
    node.setParameter ("util.macro.max", 3.0f); // 4 options, indices 0..3

    const auto ports = node.getOutputPorts();
    REQUIRE (ports.size() == 1);
    CHECK (ports[0].type == SignalType::Control);
    CHECK (ports[0].kind == ValueKind::Enum);
    CHECK (ports[0].isInteger);
    REQUIRE (ports[0].minValue.has_value());
    REQUIRE (ports[0].maxValue.has_value());
    CHECK (*ports[0].minValue == 0.0f);
    CHECK (*ports[0].maxValue == 3.0f);

    float out = -1.0f;
    node.setParameter ("util.macro.value", 2.0f / 3.0f); // raw fraction landing on index 2
    node.processSample (nullptr, &out);
    CHECK (out == 2.0f);
}

TEST_CASE ("MacroNode's type=Trigger emits a one-shot Event pulse on a rising edge, not a level",
           "[engine][nodes][macro][props-edit]")
{
    // Deliberately decoupled from MacroParameters' own smoothed 0..1 level
    // (see this node's own header comment) — this node does its own
    // edge-detection on storedValue, so a UI "button" (set the relay to 1,
    // then back to 0 shortly after) produces exactly one pulse, not a
    // sustained high level.
    MacroNode node;
    node.setParameter ("util.macro.type", 2.0f); // Trigger

    const auto ports = node.getOutputPorts();
    REQUIRE (ports.size() == 1);
    CHECK (ports[0].type == SignalType::Event);

    float out = -1.0f;
    auto step = [&] (float value)
    {
        node.setParameter ("util.macro.value", value);
        node.processSample (nullptr, &out);
        return out;
    };

    CHECK (step (0.0f) == 0.0f);
    CHECK (step (1.0f) == 1.0f); // rising edge: exactly one pulse
    CHECK (step (1.0f) == 0.0f); // held high: no repeated pulse
    CHECK (step (0.0f) == 0.0f);
    CHECK (step (1.0f) == 1.0f); // a second rising edge: another pulse

    node.reset();
    CHECK (step (1.0f) == 1.0f); // reset() clears the edge memory - this reads as a fresh rising edge, not "already high"
}

TEST_CASE ("MacroNode's type parameter caps at Trigger (2), the full 3-type range", "[engine][nodes][macro][props-edit]")
{
    MacroNode node;
    const auto parameters = node.getParameters();
    const auto typeParam = std::find_if (parameters.begin(), parameters.end(),
                                          [] (const ParameterDescriptor& p) { return p.id == "util.macro.type"; });
    REQUIRE (typeParam != parameters.end());
    CHECK (typeParam->maxValue == 2.0f);
    CHECK (typeParam->enumOptions.size() == 3);
    CHECK (typeParam->kind == ValueKind::Enum);
}

TEST_CASE ("MacroNode's Modulation quantity (Unipolar/Bipolar) implies its own min/max, ignoring whatever "
           "min/max were separately set",
           "[engine][nodes][macro][props-edit]")
{
    // design/Macro.png's own "Ctrl Mod Unipolar" chip shows no numbers at
    // all — Modulation's range is implicit, not a leftover/stale min/max
    // from before the quantity was switched (same "UI hides, doesn't
    // reset" convention the Bool-type case above already relies on).
    MacroNode node;
    node.setParameter ("util.macro.min", 20.0f); // stale/irrelevant once quantity is Unipolar
    node.setParameter ("util.macro.max", 200.0f);
    node.setParameter ("util.macro.quantity", 6.0f); // Unipolar

    const auto ports = node.getOutputPorts();
    REQUIRE (ports.size() == 1);
    REQUIRE (ports[0].minValue.has_value());
    REQUIRE (ports[0].maxValue.has_value());
    CHECK (*ports[0].minValue == 0.0f);
    CHECK (*ports[0].maxValue == 1.0f);

    float out = -1.0f;
    node.setParameter ("util.macro.value", 0.5f);
    node.processSample (nullptr, &out);
    CHECK (out == 0.5f); // 0..1 identity, NOT remapped through the stale 20..200

    node.setParameter ("util.macro.quantity", 7.0f); // Bipolar
    const auto bipolarPorts = node.getOutputPorts();
    CHECK (*bipolarPorts[0].minValue == -1.0f);
    CHECK (*bipolarPorts[0].maxValue == 1.0f);
}

TEST_CASE ("MacroNode's output port unit is derived from quantity, never a separately-set field",
           "[engine][nodes][macro][props-edit]")
{
    // Direct instruction, 2026-10-03: "Unit should not be a field" — there
    // is no util.macro.unit parameter anymore; TypedValueNodeBase.h's
    // unitForQuantity() derives it, matching the same unit a real node
    // using that Quantity already shows (ValueTypes.h's frequencyPort's
    // own "Hz", etc.).
    MacroNode node;
    node.setParameter ("util.macro.quantity", 1.0f); // Frequency
    CHECK (node.getOutputPorts()[0].unit == "Hz");

    node.setParameter ("util.macro.quantity", 0.0f); // Dimensionless
    CHECK (node.getOutputPorts()[0].unit == "");
}
