// wiki/plans/Decorations.md: decorations are canvas-only graph nodes the
// compiler never sees; deco.reroute (once util.reroute) still carries signal;
// patches carry their images as content-addressed assets.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/patch/PatchSerializer.h"

using namespace bazalt::engine;

TEST_CASE ("Decorations are never compiled", "[engine][decorations]")
{
    auto factory = buildDefaultNodeFactory();
    REQUIRE (factory.isDecoration ("deco.header"));
    REQUIRE (factory.isDecoration ("deco.image"));
    REQUIRE_FALSE (factory.isDecoration ("deco.reroute")); // carries signal

    NodeGraph graph;
    graph.addNode ({ "osc", "osc.sine", {}, {}, {} });
    graph.addNode ({ "title", "deco.header", {}, {}, { { "text", "Bass" } } });
    graph.addNode ({ "note", "deco.comment", {}, {}, { { "text", "A comment" } } });
    graph.addNode ({ "frame", "deco.box", {}, {}, {} });
    graph.setOutput ("osc", "out");

    const auto result = GraphCompiler::compile (graph, factory, { 48000.0, 64 }, 1);
    REQUIRE (result.success);
    CHECK (result.plan.nodes.size() == 1);
    CHECK (result.plan.nodeIdToSlot.count ("title") == 0);
}

TEST_CASE ("deco.reroute passes its signal through", "[engine][decorations]")
{
    auto factory = buildDefaultNodeFactory();
    NodeGraph graph;
    graph.addNode ({ "osc", "osc.sine", {}, {}, {} });
    graph.addNode ({ "dot", "deco.reroute", {}, {}, {} });
    graph.addConnection ({ "osc", "out", "dot", "in" });
    graph.setOutput ("dot", "out");
    auto result = GraphCompiler::compile (graph, factory, { 48000.0, 64 }, 1);
    REQUIRE (result.success);
    result.plan.process (64);
    CHECK (result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0)[10] != 0.0f);
}

TEST_CASE ("A v10 patch's util.reroute loads as deco.reroute", "[engine][decorations][PatchSerializer]")
{
    const auto json = R"({ "schemaVersion": 10, "nodes": [ { "id": "r", "type": "util.reroute", "parameters": {}, "properties": {} } ],
                           "connections": [], "outputNodeId": "r", "outputPortId": "out" })";
    const auto parsed = parsePatchFromJson (json);
    REQUIRE (parsed.success);
    CHECK (parsed.document.nodes[0].type == "deco.reroute");
}

TEST_CASE ("Patch assets round-trip, are stored once, and unused ones are dropped", "[engine][decorations][PatchSerializer]")
{
    NodeGraph graph;
    const juce::String data = "aGVsbG8gd29ybGQ=";
    const auto id = NodeGraph::contentIdFor (data);
    graph.setAsset (id, { "image/webp", data });
    graph.setAsset ("unused", { "image/png", "AAAA" });
    graph.addNode ({ "a", "deco.image", {}, {}, { { "asset", id } } });
    graph.addNode ({ "b", "deco.image", {}, {}, { { "asset", id } } });

    const auto json = serializePatchToJson (PatchDocument::fromNodeGraph (graph), false);
    const auto parsed = parsePatchFromJson (json);
    REQUIRE (parsed.success);
    REQUIRE (parsed.document.assets.size() == 1);
    CHECK (parsed.document.assets.at (id).mimeType == "image/webp");
    CHECK (parsed.document.assets.at (id).data == data);
    CHECK (parsed.document.toNodeGraph().referencedAssetBytes() == (size_t) data.length() * 3 / 4);
}
