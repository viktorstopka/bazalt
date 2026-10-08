// wiki/plans/DataAndWavetable.md §2: nodes removed or merged by the stage 1
// sweep load from older patches as their replacements.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/patch/PatchSerializer.h"
#include <algorithm>

using namespace bazalt::engine;

namespace
{
    const NodeInstance* findNode (const PatchDocument& doc, const juce::String& id)
    {
        const auto it = std::find_if (doc.nodes.begin(), doc.nodes.end(), [&] (const auto& n) { return n.id == id; });
        return it == doc.nodes.end() ? nullptr : &*it;
    }

    bool hasConnection (const PatchDocument& doc, const juce::String& from, const juce::String& fromPort,
                        const juce::String& to, const juce::String& toPort)
    {
        return std::any_of (doc.connections.begin(), doc.connections.end(), [&] (const auto& c)
                            { return c.fromNodeId == from && c.fromPortId == fromPort && c.toNodeId == to && c.toPortId == toPort; });
    }
}

TEST_CASE ("A v11 patch's bridge adapters are spliced out or rewritten as Multiply / Map",
           "[engine][PatchSerializer][sweep]")
{
    const auto json = R"({ "schemaVersion": 11,
        "nodes": [
            { "id": "src", "type": "osc.sine", "parameters": {}, "properties": {} },
            { "id": "toAudio", "type": "adapt.controlToAudio", "parameters": {}, "properties": {} },
            { "id": "gain", "type": "mix.gain", "parameters": {}, "properties": {} },
            { "id": "toMod", "type": "adapt.audioToControl", "parameters": { "depth": 0.5 }, "properties": {} },
            { "id": "plainMod", "type": "adapt.audioToControl", "parameters": {}, "properties": {} },
            { "id": "fromBool", "type": "adapt.boolToControl", "parameters": {}, "properties": {} },
            { "id": "fromBoolCustom", "type": "adapt.boolToControl",
              "parameters": { "adapt.boolToControl.whenFalse": -1.0, "adapt.boolToControl.whenTrue": 5.0 }, "properties": {} },
            { "id": "norm", "type": "adapt.normalise", "parameters": { "adapt.normalise.min": 20.0, "adapt.normalise.max": 2000.0 }, "properties": {} },
            { "id": "uniToBi", "type": "util.unipolarToBipolar", "parameters": {}, "properties": {} },
            { "id": "out", "type": "io.output", "parameters": {}, "properties": {} }
        ],
        "connections": [
            { "fromNodeId": "src", "fromPortId": "out", "toNodeId": "toAudio", "toPortId": "in" },
            { "fromNodeId": "toAudio", "fromPortId": "out", "toNodeId": "gain", "toPortId": "audio" },
            { "fromNodeId": "src", "fromPortId": "out", "toNodeId": "toMod", "toPortId": "in" },
            { "fromNodeId": "toMod", "fromPortId": "out", "toNodeId": "gain", "toPortId": "gain" },
            { "fromNodeId": "src", "fromPortId": "out", "toNodeId": "plainMod", "toPortId": "in" },
            { "fromNodeId": "plainMod", "fromPortId": "out", "toNodeId": "out", "toPortId": "in" }
        ],
        "outputNodeId": "out", "outputPortId": "out" })";

    const auto parsed = parsePatchFromJson (json);
    REQUIRE (parsed.success);
    const auto& doc = parsed.document;
    CHECK (doc.schemaVersion == PatchDocument::currentSchemaVersion);

    // Spliced out: the wire now runs straight through.
    CHECK (findNode (doc, "toAudio") == nullptr);
    CHECK (hasConnection (doc, "src", "out", "gain", "audio"));
    CHECK (findNode (doc, "plainMod") == nullptr);
    CHECK (hasConnection (doc, "src", "out", "out", "in"));
    CHECK (findNode (doc, "fromBool") == nullptr);

    // To Mod with a depth: a Multiply by that depth, wired the same way.
    const auto* toMod = findNode (doc, "toMod");
    REQUIRE (toMod != nullptr);
    CHECK (toMod->type == "math.multiply");
    CHECK (toMod->parameters.at ("in.1") == 0.5f);
    CHECK (hasConnection (doc, "src", "out", "toMod", "in.0"));
    CHECK (hasConnection (doc, "toMod", "out", "gain", "gain"));

    // Map over the same ranges.
    const auto* custom = findNode (doc, "fromBoolCustom");
    REQUIRE (custom != nullptr);
    CHECK (custom->type == "adapt.map");
    CHECK (custom->parameters.at ("adapt.map.outMin") == -1.0f);
    CHECK (custom->parameters.at ("adapt.map.outMax") == 5.0f);

    const auto* norm = findNode (doc, "norm");
    REQUIRE (norm != nullptr);
    CHECK (norm->type == "adapt.map");
    CHECK (norm->parameters.at ("adapt.map.inMin") == 20.0f);
    CHECK (norm->parameters.at ("adapt.map.inMax") == 2000.0f);
    CHECK (norm->parameters.at ("adapt.map.outMin") == 0.0f);
    CHECK (norm->parameters.at ("adapt.map.outMax") == 1.0f);

    const auto* uniToBi = findNode (doc, "uniToBi");
    REQUIRE (uniToBi != nullptr);
    CHECK (uniToBi->type == "adapt.map");
    CHECK (uniToBi->parameters.at ("adapt.map.outMin") == -1.0f);
}
