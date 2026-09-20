#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/PortGroups.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include <unordered_map>

using namespace bazalt::engine;

namespace
{
    // An Audio-typed constant, so mix.sum's Audio inputs have a legal source.
    class AudioConstantNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Audio } }; }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "value")
                constantValue = value;
        }

        void processSample (const float*, float* outputs) noexcept override { outputs[0] = constantValue; }

    private:
        float constantValue = 0.0f;
    };

    // Declares one more input port than ExecutionPlan can scratch-buffer.
    class TooManyPortsNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return ExecutionPlan::maxPortsPerNode + 1; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            for (int i = 0; i < getNumInputPorts(); ++i)
                ports.push_back ({ "in" + juce::String (i), SignalType::Audio });
            return ports;
        }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Audio } }; }
        void processSample (const float*, float* outputs) noexcept override { outputs[0] = 0.0f; }
    };

    NodeFactory makeFactory()
    {
        auto factory = buildDefaultNodeFactory();
        factory.registerType ("test.audioConstant", [] { return std::make_unique<AudioConstantNode>(); });
        factory.registerType ("test.tooManyPorts", [] { return std::make_unique<TooManyPortsNode>(); });
        return factory;
    }

    void addConstant (NodeGraph& graph, const juce::String& id, float value)
    {
        graph.addNode ({ id, "util.constant", {}, { { "util.constant.value", value } }, {} });
    }

    // The last sample of the plan's final output after one 8-sample block.
    float finalOutput (CompileResult& result)
    {
        result.plan.process (8);
        return result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0)[7];
    }

    const Node& nodeOf (const CompileResult& result, const juce::String& nodeId)
    {
        return *result.plan.nodes[(size_t) result.plan.nodeIdToSlot.at (nodeId)];
    }
}

TEST_CASE ("parsePortGroupIndex accepts only canonical decimal indices after the exact prefix", "[engine][PortGroups]")
{
    CHECK (parsePortGroupIndex ("in.0", "in.") == 0);
    CHECK (parsePortGroupIndex ("in.15", "in.") == 15);
    CHECK (parsePortGroupIndex ("level.3", "level.") == 3);

    CHECK (parsePortGroupIndex ("in.01", "in.") == -1); // non-canonical: would alias "in.1"
    CHECK (parsePortGroupIndex ("in.", "in.") == -1);
    CHECK (parsePortGroupIndex ("in.x", "in.") == -1);
    CHECK (parsePortGroupIndex ("in.-1", "in.") == -1);
    CHECK (parsePortGroupIndex ("in.12345", "in.") == -1);
    CHECK (parsePortGroupIndex ("out.1", "in.") == -1);
    CHECK (parsePortGroupIndex ("in.1", "") == -1);
}

TEST_CASE ("Growable nodes start at their minimum and describe their group on every port",
           "[engine][PortGroups]")
{
    const auto factory = makeFactory();

    for (const auto* typeId : { "math.add", "math.multiply", "mix.sum", "logic.boolean" })
    {
        DYNAMIC_SECTION (typeId)
        {
            const auto node = factory.create (typeId);
            REQUIRE (node != nullptr);
            CHECK (node->getGroupPortCount() == 2);

            const auto groups = collectPortGroups (node->getInputPorts());
            REQUIRE_FALSE (groups.empty());
            for (const auto& group : groups)
            {
                CHECK (group.minCount == 2);
                CHECK (group.maxCount == 16);
            }

            // A fixed node reports no group at all.
            CHECK (factory.create ("math.subtract")->getGroupPortCount() == -1);
        }
    }
}

TEST_CASE ("math.add's group size is derived from its connections, so wiring in.2 makes in.0..in.2 exist",
           "[engine][PortGroups][GraphCompiler]")
{
    const auto factory = makeFactory();

    NodeGraph graph;
    addConstant (graph, "c0", 1.0f);
    addConstant (graph, "c1", 2.0f);
    addConstant (graph, "c2", 4.0f);
    graph.addNode ({ "add", "math.add", {}, {}, {} });
    graph.addConnection ({ "c0", "out", "add", "in.0" });
    graph.addConnection ({ "c1", "out", "add", "in.1" });
    graph.addConnection ({ "c2", "out", "add", "in.2" });
    graph.setOutput ("add", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);
    CHECK (nodeOf (result, "add").getGroupPortCount() == 3);
    CHECK (finalOutput (result) == 7.0f);
}

TEST_CASE ("A hole in a group reads the port's stored value, and never renumbers its neighbours",
           "[engine][PortGroups][GraphCompiler]")
{
    const auto factory = makeFactory();

    auto sumWith = [&] (const std::unordered_map<juce::String, float>& parameters)
    {
        NodeGraph graph;
        addConstant (graph, "c0", 1.0f);
        addConstant (graph, "c2", 3.0f);
        graph.addNode ({ "add", "math.add", {}, parameters, {} });
        graph.addConnection ({ "c0", "out", "add", "in.0" });
        graph.addConnection ({ "c2", "out", "add", "in.2" }); // in.1 is the hole
        graph.setOutput ("add", "out");

        auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
        REQUIRE (result.success);
        REQUIRE (nodeOf (result, "add").getGroupPortCount() == 3);
        return finalOutput (result);
    };

    CHECK (sumWith ({}) == 4.0f);                    // hole is the identity, 0
    CHECK (sumWith ({ { "in.1", 5.0f } }) == 9.0f);  // ... until its in-node value is set
}

TEST_CASE ("math.multiply's unwired ports are the identity 1, so a spare or a hole never zeroes the product",
           "[engine][PortGroups][GraphCompiler]")
{
    const auto factory = makeFactory();

    auto productWith = [&] (const std::unordered_map<juce::String, float>& parameters)
    {
        NodeGraph graph;
        addConstant (graph, "c0", 2.0f);
        addConstant (graph, "c3", 5.0f);
        graph.addNode ({ "mul", "math.multiply", {}, parameters, {} });
        graph.addConnection ({ "c0", "out", "mul", "in.0" });
        graph.addConnection ({ "c3", "out", "mul", "in.3" }); // in.1 and in.2 are holes
        graph.setOutput ("mul", "out");

        auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
        REQUIRE (result.success);
        return finalOutput (result);
    };

    CHECK (productWith ({}) == 10.0f);
    CHECK (productWith ({ { "in.1", 3.0f } }) == 30.0f);
}

TEST_CASE ("The last valid group index is accepted and one past the maximum is rejected",
           "[engine][PortGroups][GraphCompiler]")
{
    const auto factory = makeFactory();

    auto compileWithPort = [&] (const juce::String& portId)
    {
        NodeGraph graph;
        addConstant (graph, "c", 1.0f);
        graph.addNode ({ "add", "math.add", {}, {}, {} });
        graph.addConnection ({ "c", "out", "add", portId });
        graph.setOutput ("add", "out");
        return GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    };

    auto atMax = compileWithPort ("in.15");
    REQUIRE (atMax.success);
    CHECK (nodeOf (atMax, "add").getGroupPortCount() == 16);
    CHECK (finalOutput (atMax) == 1.0f);

    const auto pastMax = compileWithPort ("in.16");
    CHECK_FALSE (pastMax.success);
    CHECK (pastMax.errorMessage.isNotEmpty());

    CHECK_FALSE (compileWithPort ("in.01").success); // non-canonical ids name no port
}

TEST_CASE ("mix.sum sums a growable group with per-input levels, holes and all",
           "[engine][PortGroups][GraphCompiler]")
{
    const auto factory = makeFactory();

    auto mixWith = [&] (const std::unordered_map<juce::String, float>& parameters, int highestInput)
    {
        NodeGraph graph;
        graph.addNode ({ "a", "test.audioConstant", {}, { { "value", 0.5f } }, {} });
        graph.addNode ({ "b", "test.audioConstant", {}, { { "value", 0.25f } }, {} });
        graph.addNode ({ "mix", "mix.sum", {}, parameters, {} });
        graph.addConnection ({ "a", "out", "mix", "in.0" });
        graph.addConnection ({ "b", "out", "mix", "in." + juce::String (highestInput) });
        graph.setOutput ("mix", "out");

        auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
        REQUIRE (result.success);
        REQUIRE (nodeOf (result, "mix").getNumInputPorts() == 2 * (highestInput + 1)); // in.N + level.N per member
        return finalOutput (result);
    };

    // The original a + b, bit for bit: an unwired level is 1.
    CHECK (mixWith ({}, 1) == 0.75f);

    // in.2 with a hole at in.1, and a level of 2 on the far input only.
    CHECK (mixWith ({ { "level.2", 2.0f } }, 2) == Catch::Approx (1.0f)); // 0.5 + 0 + 0.25*2
    CHECK (mixWith ({ { "level.0", 0.0f } }, 1) == Catch::Approx (0.25f)); // first input muted
}

TEST_CASE ("Recompiling reuses a growable node while its size is unchanged and replaces it once it grows",
           "[engine][PortGroups][GraphCompiler]")
{
    const auto factory = makeFactory();

    NodeGraph graph;
    addConstant (graph, "c0", 1.0f);
    addConstant (graph, "c1", 2.0f);
    addConstant (graph, "c2", 4.0f);
    graph.addNode ({ "add", "math.add", {}, {}, {} });
    graph.addConnection ({ "c0", "out", "add", "in.0" });
    graph.addConnection ({ "c1", "out", "add", "in.1" });
    graph.setOutput ("add", "out");

    auto first = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (first.success);

    auto second = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 2, &first.plan);
    REQUIRE (second.success);
    CHECK (&nodeOf (second, "add") == &nodeOf (first, "add")); // unchanged size: state-preserving reuse still applies

    // Wiring the spare port grows the group. That would mutate a node the
    // audio thread may still be running, so the compiler must build a new one.
    graph.addConnection ({ "c2", "out", "add", "in.2" });
    auto third = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 3, &second.plan);
    REQUIRE (third.success);
    CHECK (&nodeOf (third, "add") != &nodeOf (second, "add"));
    CHECK (nodeOf (third, "add").getGroupPortCount() == 3);
    CHECK (nodeOf (second, "add").getGroupPortCount() == 2); // the live plan's node was not touched
    CHECK (finalOutput (third) == 7.0f);
}

TEST_CASE ("A node declaring more than maxPortsPerNode ports is a compile error, not a stack overrun",
           "[engine][GraphCompiler]")
{
    const auto factory = makeFactory();

    NodeGraph graph;
    graph.addNode ({ "many", "test.tooManyPorts", {}, {}, {} });
    graph.setOutput ("many", "out");

    const auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    CHECK_FALSE (result.success);
    CHECK (result.errorMessage.contains ("many"));
}

TEST_CASE ("Schema v2 -> v3 renames a/b to in.0/in.1 on math.add, math.multiply and mix.sum, and only those",
           "[engine][patch][migration][PortGroups]")
{
    const juce::String v2Json = R"json({
        "schemaVersion": 2,
        "nodes": [
            { "id": "c1", "type": "util.constant", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
            { "id": "c2", "type": "util.constant", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
            { "id": "add", "type": "math.add", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
            { "id": "mul", "type": "math.multiply", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
            { "id": "mix", "type": "mix.sum", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
            { "id": "sub", "type": "math.subtract", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} }
        ],
        "connections": [
            { "fromNodeId": "c1", "fromPortId": "out", "toNodeId": "add", "toPortId": "a" },
            { "fromNodeId": "c2", "fromPortId": "out", "toNodeId": "add", "toPortId": "b" },
            { "fromNodeId": "c1", "fromPortId": "out", "toNodeId": "mul", "toPortId": "a" },
            { "fromNodeId": "c2", "fromPortId": "out", "toNodeId": "mul", "toPortId": "b" },
            { "fromNodeId": "c1", "fromPortId": "out", "toNodeId": "mix", "toPortId": "a" },
            { "fromNodeId": "c2", "fromPortId": "out", "toNodeId": "mix", "toPortId": "b" },
            { "fromNodeId": "c1", "fromPortId": "out", "toNodeId": "sub", "toPortId": "a" },
            { "fromNodeId": "c2", "fromPortId": "out", "toNodeId": "sub", "toPortId": "b" }
        ],
        "outputNodeId": "add",
        "outputPortId": "out",
        "macroMappings": [],
        "macroValues": [],
        "view": { "panX": 0, "panY": 0, "zoom": 1 },
        "meta": { "name": "Legacy v2", "author": "", "createdAtMs": 0, "modifiedAtMs": 0 }
    })json";

    const auto result = parsePatchFromJson (v2Json);
    REQUIRE (result.success);
    CHECK (result.document.schemaVersion == PatchDocument::currentSchemaVersion);

    auto toPortsOf = [&] (const juce::String& nodeId)
    {
        std::vector<juce::String> ports;
        for (const auto& connection : result.document.connections)
            if (connection.toNodeId == nodeId)
                ports.push_back (connection.toPortId);
        return ports;
    };

    const std::vector<juce::String> renamed { "in.0", "in.1" };
    CHECK (toPortsOf ("add") == renamed);
    CHECK (toPortsOf ("mul") == renamed);
    CHECK (toPortsOf ("mix") == renamed);

    // math.subtract is not a growable node: a/b are its real, shipped port ids.
    const std::vector<juce::String> untouched { "a", "b" };
    CHECK (toPortsOf ("sub") == untouched);
}

TEST_CASE ("A schema v1 patch migrates through v2 to v3, landing mix.sum's index-addressed inputs on in.0/in.1",
           "[engine][patch][migration][PortGroups]")
{
    const juce::String v1Json = R"json({
        "schemaVersion": 1,
        "nodes": [
            { "id": "excite", "type": "excite.burst", "parameters": {} },
            { "id": "damp", "type": "filter.onepole", "parameters": {} },
            { "id": "mix", "type": "mix.sum", "parameters": {} }
        ],
        "connections": [
            { "fromNodeId": "excite", "fromPortIndex": 0, "toNodeId": "mix", "toPortIndex": 0 },
            { "fromNodeId": "damp", "fromPortIndex": 0, "toNodeId": "mix", "toPortIndex": 1 }
        ],
        "outputNodeId": "mix",
        "outputPortIndex": 0,
        "macroMappings": [],
        "macroValues": [],
        "meta": { "name": "Legacy v1", "author": "", "createdAtMs": 0, "modifiedAtMs": 0 }
    })json";

    const auto result = parsePatchFromJson (v1Json);
    REQUIRE (result.success);
    REQUIRE (result.document.connections.size() == 2);
    CHECK (result.document.connections[0].toPortId == "in.0");
    CHECK (result.document.connections[1].toPortId == "in.1");
    CHECK (result.document.schemaVersion == 3);

    // ...and the migrated document really compiles against the current node.
    const auto factory = makeFactory();
    NodeGraph graph;
    for (const auto& node : result.document.nodes)
        graph.addNode (node);
    for (const auto& connection : result.document.connections)
        graph.addConnection (connection);
    graph.setOutput (result.document.outputNodeId, result.document.outputPortId);
    CHECK (GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1).success);
}
