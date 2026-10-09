#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/patch/PatchSerializer.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/GraphCompiler.h"

using namespace bazalt::engine;

namespace
{
    PatchDocument buildSamplePatch()
    {
        auto doc = PatchDocument::fromNodeGraph (buildVoiceProofGraph());

        // Exercise position/properties round-tripping (M7) on a couple of
        // nodes — buildVoiceProofGraph() itself leaves these at defaults.
        for (auto& node : doc.nodes)
        {
            if (node.id == "osc")
            {
                node.position = { 120.5f, -40.25f };
                node.properties["waveformLabel"] = "Saw";
            }
            else if (node.id == "svf")
            {
                node.position = { 340.0f, -40.25f };
            }
        }

        doc.view = { 12.5f, -8.0f, 1.75f };

        doc.macroValues.assign (32, 0.0f);
        doc.macroValues[0] = 0.35f;
        doc.macroValues[3] = 0.812345f;

        doc.meta.name = "Init Voice";
        doc.meta.author = "Bazalt";
        doc.meta.createdAtMs = 1758000000123;
        doc.meta.modifiedAtMs = 1758000012456;

        return doc;
    }
}

TEST_CASE ("Patch save/reload round-trips exactly", "[engine][patch]")
{
    const auto original = buildSamplePatch();
    const auto json = serializePatchToJson (original);

    const auto parsed = parsePatchFromJson (json);
    REQUIRE (parsed.success);
    REQUIRE (parsed.errorMessage.isEmpty());

    const auto& doc = parsed.document;

    CHECK (doc.schemaVersion == original.schemaVersion);
    CHECK (doc.outputNodeId == original.outputNodeId);
    CHECK (doc.outputPortId == original.outputPortId);
    CHECK (doc.view.panX == original.view.panX);
    CHECK (doc.view.panY == original.view.panY);
    CHECK (doc.view.zoom == original.view.zoom);

    REQUIRE (doc.nodes.size() == original.nodes.size());
    for (size_t i = 0; i < doc.nodes.size(); ++i)
    {
        CHECK (doc.nodes[i].id == original.nodes[i].id);
        CHECK (doc.nodes[i].type == original.nodes[i].type);
        CHECK (doc.nodes[i].position.x == original.nodes[i].position.x);
        CHECK (doc.nodes[i].position.y == original.nodes[i].position.y);
        REQUIRE (doc.nodes[i].parameters.size() == original.nodes[i].parameters.size());

        for (const auto& [paramId, value] : original.nodes[i].parameters)
        {
            REQUIRE (doc.nodes[i].parameters.count (paramId) == 1);
            CHECK (doc.nodes[i].parameters.at (paramId) == value); // bit-identical, not approximate
        }

        REQUIRE (doc.nodes[i].properties.size() == original.nodes[i].properties.size());
        for (const auto& [key, value] : original.nodes[i].properties)
        {
            REQUIRE (doc.nodes[i].properties.count (key) == 1);
            CHECK (doc.nodes[i].properties.at (key).toString() == value.toString());
        }
    }

    REQUIRE (doc.connections.size() == original.connections.size());
    for (size_t i = 0; i < doc.connections.size(); ++i)
    {
        CHECK (doc.connections[i].fromNodeId == original.connections[i].fromNodeId);
        CHECK (doc.connections[i].fromPortId == original.connections[i].fromPortId);
        CHECK (doc.connections[i].toNodeId == original.connections[i].toNodeId);
        CHECK (doc.connections[i].toPortId == original.connections[i].toPortId);
    }

    REQUIRE (doc.macroValues.size() == original.macroValues.size());
    for (size_t i = 0; i < doc.macroValues.size(); ++i)
        CHECK (doc.macroValues[i] == original.macroValues[i]);

    CHECK (doc.meta.name == original.meta.name);
    CHECK (doc.meta.author == original.meta.author);
    CHECK (doc.meta.createdAtMs == original.meta.createdAtMs);
    CHECK (doc.meta.modifiedAtMs == original.meta.modifiedAtMs);
}

TEST_CASE ("A round-tripped patch compiles back into an equivalent, working NodeGraph", "[engine][patch]")
{
    const auto original = buildSamplePatch();
    const auto json = serializePatchToJson (original);
    const auto parsed = parsePatchFromJson (json);
    REQUIRE (parsed.success);

    auto graph = parsed.document.toNodeGraph();
    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);

    REQUIRE (result.success);
    CHECK (result.plan.finalOutputBufferIndex >= 0);
}

TEST_CASE ("Patch parsing rejects malformed JSON with a clear error, not a crash", "[engine][patch]")
{
    const auto result = parsePatchFromJson ("{ this is not valid json");

    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.isNotEmpty());
}

TEST_CASE ("Patch parsing rejects a schema version newer than this build supports", "[engine][patch]")
{
    const juce::String json = "{ \"schemaVersion\": 999, \"nodes\": [], \"connections\": [] }";
    const auto result = parsePatchFromJson (json);

    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("999"));
}

TEST_CASE ("A hand-written schema v1 patch migrates to v2 with resolved port ids", "[engine][patch][migration]")
{
    // Hand-written, not produced by this build: v1 (M0-M6) addressed ports
    // by integer index (osc.out=0 -> svf.in=0, svf.out=0 -> amp.audio=0,
    // env.out=0 -> amp.gain=1), matching ProofGraphs.h's pre-M7 shape.
    const juce::String v1Json = R"json({
        "schemaVersion": 1,
        "nodes": [
            { "id": "osc", "type": "osc.analog", "parameters": {} },
            { "id": "svf", "type": "filter.svf", "parameters": { "filter.svf.cutoff": 3000.0 } },
            { "id": "env", "type": "env.adsr", "parameters": {} },
            { "id": "amp", "type": "mix.gain", "parameters": {} }
        ],
        "connections": [
            { "fromNodeId": "osc", "fromPortIndex": 0, "toNodeId": "svf", "toPortIndex": 0 },
            { "fromNodeId": "svf", "fromPortIndex": 0, "toNodeId": "amp", "toPortIndex": 0 },
            { "fromNodeId": "env", "fromPortIndex": 0, "toNodeId": "amp", "toPortIndex": 1 }
        ],
        "outputNodeId": "amp",
        "outputPortIndex": 0,
        "macroMappings": [],
        "macroValues": [],
        "meta": { "name": "Legacy", "author": "", "createdAtMs": 0, "modifiedAtMs": 0 }
    })json";

    const auto result = parsePatchFromJson (v1Json);
    REQUIRE (result.success);
    REQUIRE (result.errorMessage.isEmpty());

    const auto& doc = result.document;
    CHECK (doc.schemaVersion == PatchDocument::currentSchemaVersion);
    CHECK (doc.outputNodeId == "amp");
    CHECK (doc.outputPortId == "out");
    CHECK (doc.view.zoom == 1.0f); // v1 had no view state — migration fills a sane default

    REQUIRE (doc.connections.size() == 3);
    CHECK (doc.connections[0].fromPortId == "out"); // osc.analog's only output
    CHECK (doc.connections[0].toPortId == "in");    // filter.svf's only input
    CHECK (doc.connections[1].fromPortId == "out"); // filter.svf's only output
    CHECK (doc.connections[1].toPortId == "in.0"); // mix.gain input 0, math.multiply since v13
    CHECK (doc.connections[2].fromPortId == "out"); // env.adsr's only output
    CHECK (doc.connections[2].toPortId == "in.1"); // mix.gain input 1, math.multiply since v13

    // The migrated graph must still actually compile — not just parse.
    auto graph = doc.toNodeGraph();
    auto factory = buildDefaultNodeFactory();
    auto compileResult = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (compileResult.success);
}

TEST_CASE ("A hand-written schema v6 patch's macroMappings content migrates away cleanly, not a parse failure",
           "[engine][patch][migration]")
{
    // wiki/plans/UtilMacro.md: macroMappings is dropped from PatchDocument
    // entirely (a real util.macro node derives it from the live graph now,
    // never persists it) — a v6 file that still has real macroMappings
    // content (the only shape any v6 file could ever have, since nothing
    // before this ever wrote it any other way) must still parse and
    // compile, with that content simply not carried forward.
    const juce::String v6Json = R"json({
        "schemaVersion": 6,
        "nodes": [
            { "id": "osc", "type": "osc.analog", "position": { "x": 0.0, "y": 0.0 }, "parameters": {}, "properties": {} }
        ],
        "connections": [],
        "outputNodeId": "osc",
        "outputPortId": "out",
        "macroMappings": [ { "macroIndex": 0, "targetNodeId": "osc", "targetParameterId": "osc.analog.shape", "rangeMin": 0.0, "rangeMax": 3.0 } ],
        "macroValues": [],
        "view": { "panX": 0.0, "panY": 0.0, "zoom": 1.0 },
        "meta": { "name": "", "author": "", "createdAtMs": 0, "modifiedAtMs": 0 }
    })json";

    const auto result = parsePatchFromJson (v6Json);
    REQUIRE (result.success);
    REQUIRE (result.errorMessage.isEmpty());
    CHECK (result.document.schemaVersion == PatchDocument::currentSchemaVersion);
    CHECK (result.document.outputNodeId == "osc");

    auto graph = result.document.toNodeGraph();
    auto factory = buildDefaultNodeFactory();
    auto compileResult = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (compileResult.success);
}

TEST_CASE ("v7 -> v8 migration rewrites adapt.remap into adapt.map and the old two-parameter adapt.map into the four-range one",
           "[engine][patch][adapt.map]")
{
    // design/Map.png: adapt.remap became adapt.map and the old Map stopped
    // existing. patches/CatPurr.json has a real adapt.remap (node73) — this
    // is the shape it and the user's other saved patches arrive in.
    const juce::String json = R"({
        "schemaVersion": 7,
        "nodes": [
            { "id": "oldMap", "type": "adapt.map", "parameters": { "adapt.map.min": 200.0, "adapt.map.max": 8000.0 } },
            { "id": "remap", "type": "adapt.remap", "parameters": { "adapt.remap.outMin": 1.0, "adapt.remap.outMax": 0.0 } },
            { "id": "src", "type": "util.constant", "parameters": {} }
        ],
        "connections": [ { "fromNodeId": "src", "fromPortId": "out", "toNodeId": "remap", "toPortId": "adapt.remap.inMax" } ]
    })";

    const auto result = parsePatchFromJson (json);
    REQUIRE (result.success);
    CHECK (result.document.schemaVersion == PatchDocument::currentSchemaVersion);

    auto find = [&] (const juce::String& id) -> const NodeInstance&
    {
        for (const auto& node : result.document.nodes)
            if (node.id == id)
                return node;
        FAIL ("no node " << id);
        return result.document.nodes.front();
    };

    const auto& oldMap = find ("oldMap");
    CHECK (oldMap.type == "adapt.map");
    CHECK (oldMap.parameters.count ("adapt.map.min") == 0);
    CHECK (oldMap.parameters.at ("adapt.map.inMin") == 0.0f);
    CHECK (oldMap.parameters.at ("adapt.map.inMax") == 1.0f);
    CHECK (oldMap.parameters.at ("adapt.map.outMin") == 200.0f);
    CHECK (oldMap.parameters.at ("adapt.map.outMax") == 8000.0f);

    const auto& remap = find ("remap");
    CHECK (remap.type == "adapt.map");
    CHECK (remap.parameters.at ("adapt.map.outMin") == 1.0f);
    CHECK (remap.parameters.at ("adapt.map.outMax") == 0.0f);
    CHECK (remap.parameters.count ("adapt.remap.outMin") == 0);

    REQUIRE (result.document.connections.size() == 1);
    CHECK (result.document.connections[0].toPortId == "adapt.map.inMax");
}

TEST_CASE ("v8 -> v9 migration drops view.scope and splices view.glance out of its cable",
           "[engine][patch][view.cycle]")
{
    const juce::String json = R"({
        "schemaVersion": 8,
        "nodes": [
            { "id": "osc", "type": "osc.sine", "parameters": {} },
            { "id": "glance", "type": "view.glance", "parameters": {} },
            { "id": "scope", "type": "view.scope", "parameters": {} },
            { "id": "gain", "type": "mix.gain", "parameters": {} }
        ],
        "connections": [
            { "fromNodeId": "osc", "fromPortId": "out", "toNodeId": "glance", "toPortId": "in" },
            { "fromNodeId": "glance", "fromPortId": "out", "toNodeId": "gain", "toPortId": "audio" },
            { "fromNodeId": "osc", "fromPortId": "out", "toNodeId": "scope", "toPortId": "in" }
        ],
        "outputNodeId": "glance", "outputPortId": "out"
    })";

    const auto result = parsePatchFromJson (json);
    REQUIRE (result.success);
    REQUIRE (result.document.nodes.size() == 2);
    for (const auto& node : result.document.nodes)
        CHECK ((node.type == "osc.sine" || node.type == "math.multiply")); // mix.gain became math.multiply in v13

    REQUIRE (result.document.connections.size() == 1);
    const auto& c = result.document.connections[0];
    CHECK (c.fromNodeId == "osc");
    CHECK (c.toNodeId == "gain");
    CHECK (c.toPortId == "in.0");
    CHECK (result.document.outputNodeId == "osc");
}

TEST_CASE ("v9 -> v10 migration turns logic.boolean's Op into logic.and / logic.or / logic.xor (+ Invert)",
           "[engine][patch][logic]")
{
    const juce::String json = R"({
        "schemaVersion": 9,
        "nodes": [
            { "id": "a", "type": "logic.boolean", "parameters": {} },
            { "id": "o", "type": "logic.boolean", "parameters": { "logic.boolean.op": 1 } },
            { "id": "x", "type": "logic.boolean", "parameters": { "logic.boolean.op": 2 } },
            { "id": "na", "type": "logic.boolean", "parameters": { "logic.boolean.op": 3 } },
            { "id": "no", "type": "logic.boolean", "parameters": { "logic.boolean.op": 4 } }
        ],
        "connections": []
    })";
    const auto result = parsePatchFromJson (json);
    REQUIRE (result.success);

    auto check = [&] (const juce::String& id, const juce::String& type, bool inverted)
    {
        for (const auto& node : result.document.nodes)
            if (node.id == id)
            {
                CHECK (node.type == type);
                CHECK (node.parameters.count (type + ".invert") == (inverted ? 1u : 0u));
                CHECK (node.parameters.count ("logic.boolean.op") == 0);
            }
    };
    check ("a", "logic.and", false);
    check ("o", "logic.or", false);
    check ("x", "logic.xor", false);
    check ("na", "logic.and", true);
    check ("no", "logic.or", true);
}
