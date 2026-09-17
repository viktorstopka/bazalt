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

        doc.macroMappings = { { 0, "svf", "filter.svf.cutoff", 200.0f, 8000.0f },
                              { 0, "svf", "filter.svf.resonance", 0.5f, 4.0f },
                              { 3, "env", "env.adsr.release", 0.05f, 2.0f } };

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

    REQUIRE (doc.macroMappings.size() == original.macroMappings.size());
    for (size_t i = 0; i < doc.macroMappings.size(); ++i)
    {
        CHECK (doc.macroMappings[i].macroIndex == original.macroMappings[i].macroIndex);
        CHECK (doc.macroMappings[i].targetNodeId == original.macroMappings[i].targetNodeId);
        CHECK (doc.macroMappings[i].targetParameterId == original.macroMappings[i].targetParameterId);
        CHECK (doc.macroMappings[i].rangeMin == original.macroMappings[i].rangeMin);
        CHECK (doc.macroMappings[i].rangeMax == original.macroMappings[i].rangeMax);
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
            { "id": "osc", "type": "osc.basic", "parameters": {} },
            { "id": "svf", "type": "filter.svf", "parameters": { "filter.svf.cutoff": 3000.0 } },
            { "id": "env", "type": "env.adsr", "parameters": {} },
            { "id": "amp", "type": "amp.vca", "parameters": {} }
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
    CHECK (doc.connections[0].fromPortId == "out"); // osc.basic's only output
    CHECK (doc.connections[0].toPortId == "in");    // filter.svf's only input
    CHECK (doc.connections[1].fromPortId == "out"); // filter.svf's only output
    CHECK (doc.connections[1].toPortId == "audio"); // amp.vca input 0
    CHECK (doc.connections[2].fromPortId == "out"); // env.adsr's only output
    CHECK (doc.connections[2].toPortId == "gain");  // amp.vca input 1

    // The migrated graph must still actually compile — not just parse.
    auto graph = doc.toNodeGraph();
    auto factory = buildDefaultNodeFactory();
    auto compileResult = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (compileResult.success);
}
