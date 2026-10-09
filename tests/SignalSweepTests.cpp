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
    CHECK (hasConnection (doc, "src", "out", "gain", "in.0")); // mix.gain is math.multiply since v13
    CHECK (findNode (doc, "plainMod") == nullptr);
    CHECK (hasConnection (doc, "src", "out", "out", "in"));
    CHECK (findNode (doc, "fromBool") == nullptr);

    // To Mod with a depth: a Multiply by that depth, wired the same way.
    const auto* toMod = findNode (doc, "toMod");
    REQUIRE (toMod != nullptr);
    CHECK (toMod->type == "math.multiply");
    CHECK (toMod->parameters.at ("in.1") == 0.5f);
    CHECK (hasConnection (doc, "src", "out", "toMod", "in.0"));
    CHECK (hasConnection (doc, "toMod", "out", "gain", "in.1"));

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

TEST_CASE ("A v12 patch's Gain, Clamp, Clip and Follower load as Multiply, Clip and Level",
           "[engine][PatchSerializer][sweep]")
{
    const auto json = R"({ "schemaVersion": 12,
        "nodes": [
            { "id": "src", "type": "osc.sine", "parameters": {}, "properties": {} },
            { "id": "gain", "type": "mix.gain", "parameters": { "gain": 0.25 }, "properties": {} },
            { "id": "clamp", "type": "math.clamp", "parameters": { "math.clamp.high": 0.5 }, "properties": {} },
            { "id": "clip", "type": "shape.clip", "parameters": { "shape.clip.ceiling": 0.8 }, "properties": {} },
            { "id": "follow", "type": "env.follower", "parameters": { "env.follower.detection": 1, "env.follower.attack": 5 }, "properties": {} },
            { "id": "out", "type": "io.output", "parameters": {}, "properties": {} }
        ],
        "connections": [
            { "fromNodeId": "src", "fromPortId": "out", "toNodeId": "gain", "toPortId": "audio" },
            { "fromNodeId": "follow", "fromPortId": "out", "toNodeId": "gain", "toPortId": "gain" },
            { "fromNodeId": "src", "fromPortId": "out", "toNodeId": "follow", "toPortId": "in" },
            { "fromNodeId": "src", "fromPortId": "out", "toNodeId": "clamp", "toPortId": "math.clamp.low" },
            { "fromNodeId": "gain", "fromPortId": "out", "toNodeId": "clip", "toPortId": "in" },
            { "fromNodeId": "clip", "fromPortId": "out", "toNodeId": "out", "toPortId": "in" }
        ],
        "outputNodeId": "out", "outputPortId": "out" })";

    const auto parsed = parsePatchFromJson (json);
    REQUIRE (parsed.success);
    const auto& doc = parsed.document;
    CHECK (doc.schemaVersion == PatchDocument::currentSchemaVersion);

    const auto* gain = findNode (doc, "gain");
    REQUIRE (gain != nullptr);
    CHECK (gain->type == "math.multiply");
    CHECK (gain->parameters.at ("in.1") == 0.25f);
    CHECK (hasConnection (doc, "src", "out", "gain", "in.0"));
    CHECK (hasConnection (doc, "follow", "level", "gain", "in.1"));

    // Clamp keeps its 0..1 defaults and its instant corners (knee 0).
    const auto* clamp = findNode (doc, "clamp");
    REQUIRE (clamp != nullptr);
    CHECK (clamp->type == "shape.clip");
    CHECK (clamp->parameters.at ("shape.clip.low") == 0.0f);
    CHECK (clamp->parameters.at ("shape.clip.high") == 0.5f);
    CHECK (clamp->parameters.at ("shape.clip.knee") == 0.0f);
    CHECK (hasConnection (doc, "src", "out", "clamp", "shape.clip.low"));

    // The old Clip's ceiling becomes a symmetric range.
    const auto* clip = findNode (doc, "clip");
    REQUIRE (clip != nullptr);
    CHECK (clip->parameters.at ("shape.clip.low") == -0.8f);
    CHECK (clip->parameters.at ("shape.clip.high") == 0.8f);
    CHECK (clip->parameters.find ("shape.clip.ceiling") == clip->parameters.end());

    // Follower RMS (1) is Level RMS (0).
    const auto* follow = findNode (doc, "follow");
    REQUIRE (follow != nullptr);
    CHECK (follow->type == "analysis.level");
    CHECK (follow->parameters.at ("analysis.level.mode") == 0.0f);
    CHECK (follow->parameters.at ("analysis.level.attack") == 5.0f);
}

TEST_CASE ("A v12 patch's Crossfade, Select and history scopes load as Blend and Scope",
           "[engine][PatchSerializer][sweep]")
{
    const auto json = R"({ "schemaVersion": 12,
        "nodes": [
            { "id": "x", "type": "osc.sine", "parameters": {}, "properties": {} },
            { "id": "y", "type": "osc.sine", "parameters": {}, "properties": {} },
            { "id": "cond", "type": "logic.not", "parameters": {}, "properties": {} },
            { "id": "fade", "type": "mix.crossfade", "parameters": { "mix.crossfade.position": 0.25, "mix.crossfade.law": 1 }, "properties": {} },
            { "id": "sel", "type": "logic.select", "parameters": {}, "properties": {} },
            { "id": "mod", "type": "view.scope.modulation", "parameters": { "view.scope.modulation.timeWindow": 4 },
              "properties": { "viewer.center": 0.5 } },
            { "id": "gate", "type": "view.gate", "parameters": {}, "properties": {} }
        ],
        "connections": [
            { "fromNodeId": "x", "fromPortId": "out", "toNodeId": "fade", "toPortId": "a" },
            { "fromNodeId": "y", "fromPortId": "out", "toNodeId": "fade", "toPortId": "b" },
            { "fromNodeId": "x", "fromPortId": "out", "toNodeId": "sel", "toPortId": "whenFalse" },
            { "fromNodeId": "y", "fromPortId": "out", "toNodeId": "sel", "toPortId": "whenTrue" },
            { "fromNodeId": "cond", "fromPortId": "out", "toNodeId": "sel", "toPortId": "condition" },
            { "fromNodeId": "cond", "fromPortId": "out", "toNodeId": "gate", "toPortId": "in" }
        ],
        "outputNodeId": "fade", "outputPortId": "out" })";

    const auto parsed = parsePatchFromJson (json);
    REQUIRE (parsed.success);
    const auto& doc = parsed.document;

    const auto* fade = findNode (doc, "fade");
    REQUIRE (fade != nullptr);
    CHECK (fade->type == "math.blend");
    CHECK (fade->parameters.at ("math.blend.amount") == 0.25f);
    CHECK (fade->parameters.at ("math.blend.law") == 1.0f);
    CHECK (hasConnection (doc, "x", "out", "fade", "a"));

    // Select: whenFalse is A, whenTrue is B, the condition drives Amount, and
    // an unwired condition (false) is Amount 0.
    const auto* sel = findNode (doc, "sel");
    REQUIRE (sel != nullptr);
    CHECK (sel->type == "math.blend");
    CHECK (sel->parameters.at ("math.blend.amount") == 0.0f);
    CHECK (hasConnection (doc, "x", "out", "sel", "a"));
    CHECK (hasConnection (doc, "y", "out", "sel", "b"));
    CHECK (hasConnection (doc, "cond", "out", "sel", "math.blend.amount"));

    const auto* mod = findNode (doc, "mod");
    REQUIRE (mod != nullptr);
    CHECK (mod->type == "view.scope");
    CHECK (mod->parameters.at ("view.scope.timeWindow") == 4.0f);
    CHECK (mod->properties.count ("viewer.center") == 1); // display settings carry over
    CHECK (findNode (doc, "gate")->type == "view.scope");
}
