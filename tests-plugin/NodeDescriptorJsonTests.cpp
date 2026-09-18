#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "NodeDescriptorJson.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/ConstantNode.h"
#include "bazalt/engine/nodes/OscillatorNode.h"
#include "bazalt/engine/nodes/RerouteNode.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"

using namespace bazalt;
using namespace bazalt::engine;

// Verifies the JSON shape ui/src/graph/descriptorTypes.ts expects
// (NODE_EDITOR.md §3) — this is a pure data transform (no WebView, no
// PluginEditor construction needed), so it's tested directly at the
// var level rather than through a live native-function round trip.

TEST_CASE ("nodeDescriptorToVar serializes a node with an unbounded numeric output port",
           "[plugin][NodeDescriptorJson][NODE_EDITOR]")
{
    const auto descriptor = describeNode ("util.constant", nodes::ConstantNode {});
    const auto var = nodeDescriptorToVar (descriptor);

    REQUIRE (var.isObject());
    CHECK (var["typeId"].toString() == "util.constant");
    CHECK (var["title"].toString() == "Constant");
    CHECK (var["category"].toString() == "Utility");
    CHECK (var["layoutVariant"].toString() == "standard");

    const auto* outputs = var["outputs"].getArray();
    REQUIRE (outputs != nullptr);
    REQUIRE (outputs->size() == 1);
    const auto& out = (*outputs)[0];
    CHECK (out["id"].toString() == "out");
    CHECK (out["type"].toString() == "control");
    CHECK ((bool) out["isPrimaryOutput"]);
    // Unset optional<float> must serialize to a JS-visible null (isVoid),
    // never 0 — 0 is a legitimate bound, absence isn't the same fact.
    CHECK (out["minValue"].isVoid());
    CHECK (out["maxValue"].isVoid());

    const auto* parameters = var["parameters"].getArray();
    REQUIRE (parameters != nullptr);
    REQUIRE (parameters->size() == 1);
    CHECK ((*parameters)[0]["id"].toString() == "util.constant.value");
    CHECK ((float) (*parameters)[0]["minValue"] == -100000.0f);
}

TEST_CASE ("nodeDescriptorToVar reports the Decoration layout variant",
           "[plugin][NodeDescriptorJson][NODE_EDITOR]")
{
    const auto descriptor = describeNode ("util.reroute", nodes::RerouteNode {});
    const auto var = nodeDescriptorToVar (descriptor);
    CHECK (var["layoutVariant"].toString() == "decoration");
}

TEST_CASE ("nodeDescriptorToVar serializes InstanceMixNode's port metadata intact",
           "[plugin][NodeDescriptorJson][M17]")
{
    const auto descriptor = describeNode ("instance.mix", nodes::InstanceMixNode {});
    const auto var = nodeDescriptorToVar (descriptor);
    CHECK (var["typeId"].toString() == "instance.mix");

    const auto* inputs = var["inputs"].getArray();
    REQUIRE (inputs != nullptr);
    REQUIRE (! inputs->isEmpty());
}

TEST_CASE ("nodeDescriptorToVar serializes osc.analog.shape's M14 enum metadata end to end",
           "[plugin][NodeDescriptorJson][M14]")
{
    const auto descriptor = describeNode ("osc.analog", nodes::OscillatorNode {});
    const auto var = nodeDescriptorToVar (descriptor);

    const auto* parameters = var["parameters"].getArray();
    REQUIRE (parameters != nullptr);
    const auto it = std::find_if (parameters->begin(), parameters->end(), [] (const juce::var& p)
                                   { return p["id"].toString() == "osc.analog.shape"; });
    REQUIRE (it != parameters->end());

    CHECK ((*it)["kind"].toString() == "enum");
    CHECK ((bool) (*it)["isStructural"]);
    const auto* options = (*it)["enumOptions"].getArray();
    REQUIRE (options != nullptr);
    REQUIRE (options->size() == 4);
    CHECK ((*options)[0]["id"].toString() == "sine");
    CHECK ((*options)[0]["label"].toString() == "Sine");
    CHECK ((*options)[3]["id"].toString() == "triangle");
}

TEST_CASE ("nodeDescriptorsToVar serializes every registered type exactly once",
           "[plugin][NodeDescriptorJson][NODE_EDITOR]")
{
    const auto factory = buildDefaultNodeFactory();
    const auto descriptors = factory.describeAll();
    const auto var = nodeDescriptorsToVar (descriptors);
    const auto* array = var.getArray();

    REQUIRE (array != nullptr);
    REQUIRE ((size_t) array->size() == descriptors.size());

    // Every entry must round-trip a real, non-empty typeId — a blank
    // typeId reaching the UI would render as an untitled Add-menu entry.
    for (const auto& entry : *array)
        CHECK (entry["typeId"].toString().isNotEmpty());
}
