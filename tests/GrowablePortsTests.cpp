#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/PortGroups.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include "bazalt/engine/nodes/AddNode.h"
#include <unordered_map>

using namespace bazalt::engine;

namespace
{
    // An Audio-typed constant, so a polymorphic node's Audio inputs (math.add
    // resolved to Audio, since DomainRedesign.md Batch 3 folded mix.sum into it)
    // have a legal source.
    class AudioConstantNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { .id = "out", .type = SignalType::Signal, .quantity = Quantity::Audio } }; }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "value")
                constantValue = value;
        }

        void processSample (const float*, float* outputs) noexcept override { outputs[0] = constantValue; }

    private:
        float constantValue = 0.0f;
    };

    // A Boolean-typed constant — logic.and/or/xor's own inputs are fixed
    // Boolean (never polymorphic, unlike math.add/math.multiply since
    // DomainRedesign.md Batch 3), so it needs a plain source of that type.
    class BoolConstantNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { .id = "out", .type = SignalType::Signal, .quantity = Quantity::Boolean } }; }

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
                ports.push_back ({ .id = "in" + juce::String (i), .type = SignalType::Signal, .quantity = Quantity::Audio });
            return ports;
        }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { .id = "out", .type = SignalType::Signal, .quantity = Quantity::Audio } }; }
        void processSample (const float*, float* outputs) noexcept override { outputs[0] = 0.0f; }
    };

    NodeFactory makeFactory()
    {
        auto factory = buildDefaultNodeFactory();
        factory.registerType ("test.audioConstant", [] { return std::make_unique<AudioConstantNode>(); });
        factory.registerType ("test.boolConstant", [] { return std::make_unique<BoolConstantNode>(); });
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

    for (const auto* typeId : { "math.add", "math.multiply", "logic.and", "logic.or", "logic.xor", "logic.eventGroup" })
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

TEST_CASE ("math.add sums a growable group of Audio inputs, holes and all - DomainRedesign.md Batch 3",
           "[engine][PortGroups][GraphCompiler]")
{
    // wiki/plans/DomainRedesign.md Batch 3: mix.sum (Audio-only, no
    // stored-value fallback) folded straight into math.add outright - the
    // two were almost line-for-line the same node once Schema v4
    // (wiki/NODES_Gaps.md's `redundant-composable-param` finding) removed
    // mix.sum's own per-input level.N gain (that's a real mix.gain node's
    // job now, spliced in by hand or, for an old patch, by PatchSerializer's
    // v3->v4 migration - see PatchSerializerMigrationTests.cpp). math.add
    // now resolves Audio dynamically (PortPolymorphism::SignalAndQuantity)
    // instead of mix.sum declaring it statically.
    const auto factory = makeFactory();

    auto mixWith = [&] (int highestInput)
    {
        NodeGraph graph;
        graph.addNode ({ "a", "test.audioConstant", {}, { { "value", 0.5f } }, {} });
        graph.addNode ({ "b", "test.audioConstant", {}, { { "value", 0.25f } }, {} });
        graph.addNode ({ "mix", "math.add", {}, {}, {} });
        graph.addConnection ({ "a", "out", "mix", "in.0" });
        graph.addConnection ({ "b", "out", "mix", "in." + juce::String (highestInput) });
        graph.setOutput ("mix", "out");

        auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
        REQUIRE (result.success);
        REQUIRE (nodeOf (result, "mix").getNumInputPorts() == highestInput + 1); // in.N per member, no level.N any more
        return finalOutput (result);
    };

    // The original a + b, bit for bit.
    CHECK (mixWith (1) == 0.75f);

    // in.2 with a hole at in.1: the hole reads silence, the identity for a sum.
    CHECK (mixWith (2) == Catch::Approx (0.75f)); // 0.5 + 0 + 0.25
}

TEST_CASE ("math.add's ports report Audio + the source's quantity once an Audio source resolves them, "
           "the same PortPolymorphism::SignalAndQuantity mechanism deco.reroute already uses",
           "[engine][PortGroups][inheriting][DomainRedesign]")
{
    const auto factory = makeFactory();

    NodeGraph graph;
    graph.addNode ({ "a", "test.audioConstant", {}, { { "value", 0.5f } }, {} });
    graph.addNode ({ "b", "test.audioConstant", {}, { { "value", 0.25f } }, {} });
    graph.addNode ({ "add", "math.add", {}, {}, {} });
    graph.addConnection ({ "a", "out", "add", "in.0" });
    graph.addConnection ({ "b", "out", "add", "in.1" });
    graph.setOutput ("add", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    const auto& node = nodeOf (result, "add");
    for (const auto& port : node.getInputPorts())
    {
        CHECK (port.quantity == Quantity::Audio);
        CHECK (port.polymorphism == PortPolymorphism::SignalAndQuantity);
    }
    REQUIRE (node.getOutputPorts().size() == 1);
    CHECK (node.getOutputPorts()[0].quantity == Quantity::Audio);
    CHECK (node.getOutputPorts()[0].polymorphism == PortPolymorphism::SignalAndQuantity);
}

TEST_CASE ("math.add's SIGNAL TYPE still resolves by declaration order when wired members disagree, "
           "same rule InheritingPortsNode::offer() already establishes",
           "[engine][PortGroups][inheriting][DomainRedesign]")
{
    // Calls resolveIncomingPort() directly, the same way
    // InheritingPortsTests.cpp's own "logic.select resolves by port
    // priority..." test does — GraphCompiler's real canConnect validation
    // would (correctly) reject two DIFFERENT SignalTypes disagreeing on one
    // node's single resolved type), so this tests the resolution rule in
    // isolation. SignalType keeps the
    // strict priority rule (mixing Audio and Control on one sum is rare,
    // and if it happens one of them IS the odd one out) — QUANTITY does
    // NOT, see the sibling test below for why.
    for (const auto in1First : { true, false })
    {
        DYNAMIC_SECTION ("in.1 offered " << (in1First ? "first" : "second"))
        {
            nodes::AddNode node;
            const PortDescriptor audioSource { .id = "src", .type = SignalType::Signal, .quantity = Quantity::Audio };
            const PortDescriptor controlSource { .id = "src", .type = SignalType::Signal };

            if (in1First)
            {
                node.resolveIncomingPort ("in.1", controlSource);
                node.resolveIncomingPort ("in.0", audioSource);
            }
            else
            {
                node.resolveIncomingPort ("in.0", audioSource);
                node.resolveIncomingPort ("in.1", controlSource);
            }

            // in.0 declared first wins, regardless of offer order.
            for (const auto& port : node.getInputPorts())
                CHECK (port.quantity == Quantity::Audio);
            CHECK (node.getOutputPorts()[0].quantity == Quantity::Audio);
        }
    }
}

TEST_CASE ("math.add's QUANTITY only resolves when every wired member unanimously agrees; any real "
           "disagreement falls back to Dimensionless, canConnect's own universal wildcard",
           "[engine][PortGroups][inheriting][DomainRedesign]")
{
    // Real, found-live design bug (HostInputTests.cpp's own "combine
    // io.control + io.transport through math.add" case, a pre-existing,
    // legitimate graph): io.control's Unipolar "value" and io.transport's
    // Time "position" have never had any reason to agree, and summing them
    // through math.add always worked, because math.add never declared a
    // real quantity before this redesign. A first cut of this fix used
    // InheritingPortsNode's own "lowest priority wins" rule for quantity
    // too — that made the LOSING port's own real source spuriously fail
    // canConnect's strict real-quantity matching against the node's now-
    // different resolved quantity, breaking exactly that graph. Unlike
    // SignalType, quantity has no "one of them is obviously the odd one
    // out" reading for a sum: it needs unanimous agreement, not priority.
    nodes::AddNode node;
    PortDescriptor frequencySource { .id = "src", .type = SignalType::Signal };
    frequencySource.quantity = Quantity::Frequency;
    PortDescriptor pitchSource { .id = "src", .type = SignalType::Signal };
    pitchSource.quantity = Quantity::Pitch;
    PortDescriptor dimensionlessSource { .id = "src", .type = SignalType::Signal };

    // Disagreement (Frequency vs Pitch) -> Dimensionless, not either one.
    node.resolveIncomingPort ("in.0", frequencySource);
    node.resolveIncomingPort ("in.1", pitchSource);
    for (const auto& port : node.getInputPorts())
        CHECK (port.quantity == Quantity::Dimensionless);

    // A THIRD input, unwired/Dimensionless, never counts as "disagreement" —
    // matches canConnect's own two-sided Dimensionless wildcard.
    nodes::AddNode agreeing;
    agreeing.resolveIncomingPort ("in.0", frequencySource);
    agreeing.resolveIncomingPort ("in.1", dimensionlessSource);
    for (const auto& port : agreeing.getInputPorts())
        CHECK (port.quantity == Quantity::Frequency);

    // A later re-offer that resolves the disagreement (a Reroute chain
    // upstream settling on a fixed point) un-sticks it — recomputed fresh
    // from every port's current state, not accumulated permanently.
    node.resolveIncomingPort ("in.1", frequencySource);
    for (const auto& port : node.getInputPorts())
        CHECK (port.quantity == Quantity::Frequency);
}

TEST_CASE ("math.multiply resolves Audio dynamically too - the real ring-mod case, "
           "correcting wiki/NODES.md's documentation-level claim this already worked",
           "[engine][PortGroups][inheriting][DomainRedesign]")
{
    // wiki/plans/DomainRedesign.md §10 (the plan-mode Explore pass): "wiki/
    // NODES.md's claim that math.multiply already doubles as ring-mod for
    // audio-rate signals was documentation-level, not real - its ports were
    // fixed Control." Verified and fixed here directly against the real node.
    const auto factory = makeFactory();

    NodeGraph graph;
    graph.addNode ({ "a", "test.audioConstant", {}, { { "value", 2.0f } }, {} });
    graph.addNode ({ "b", "test.audioConstant", {}, { { "value", 3.0f } }, {} });
    graph.addNode ({ "mul", "math.multiply", {}, {}, {} });
    graph.addConnection ({ "a", "out", "mul", "in.0" });
    graph.addConnection ({ "b", "out", "mul", "in.1" });
    graph.setOutput ("mul", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    const auto& node = nodeOf (result, "mul");
    CHECK (node.getInputPorts()[0].quantity == Quantity::Audio);
    CHECK (node.getOutputPorts()[0].quantity == Quantity::Audio);
    CHECK (finalOutput (result) == 6.0f);
}

TEST_CASE ("Recompiling reuses a growable node while its size is unchanged and replaces it once it grows",
           "[engine][PortGroups][GraphCompiler]")
{
    // logic.or, not math.add: DomainRedesign.md Batch 3 made math.add
    // genuinely polymorphic (PortPolymorphism::SignalAndQuantity), and
    // GraphCompiler's own reuse check already excludes ANY
    // hasPolymorphicPorts() node from reuse outright, regardless of group
    // size (GraphCompiler.cpp's own comment: "such a node holds no DSP
    // state worth carrying forward") — the exact same rule deco.reroute's
    // node has always lived under. math.add now lives under it too; a
    // dedicated test for that is below. logic.or is a growable node
    // that stays fixed-Boolean (never polymorphic), so it's what still
    // demonstrates the group-size-driven half of this rule in isolation.
    const auto factory = makeFactory();

    NodeGraph graph;
    graph.addNode ({ "c0", "test.boolConstant", {}, { { "value", 1.0f } }, {} });
    graph.addNode ({ "c1", "test.boolConstant", {}, { { "value", 1.0f } }, {} });
    graph.addNode ({ "c2", "test.boolConstant", {}, { { "value", 1.0f } }, {} });
    graph.addNode ({ "add", "logic.or", {}, {}, {} });
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
    CHECK (finalOutput (third) == 1.0f); // AND of three trues
}

TEST_CASE ("math.add is never reused across a recompile, even at an unchanged group size - "
           "PortPolymorphism::SignalAndQuantity makes it a polymorphic node like deco.reroute",
           "[engine][PortGroups][GraphCompiler][DomainRedesign]")
{
    const auto factory = makeFactory();

    NodeGraph graph;
    addConstant (graph, "c0", 1.0f);
    addConstant (graph, "c1", 2.0f);
    graph.addNode ({ "add", "math.add", {}, {}, {} });
    graph.addConnection ({ "c0", "out", "add", "in.0" });
    graph.addConnection ({ "c1", "out", "add", "in.1" });
    graph.setOutput ("add", "out");

    auto first = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (first.success);

    auto second = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 2, &first.plan);
    REQUIRE (second.success);
    CHECK (&nodeOf (second, "add") != &nodeOf (first, "add")); // same group size, still a fresh node
    CHECK (finalOutput (second) == 3.0f); // harmless: math.add holds no DSP state worth preserving anyway
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
    CHECK (result.document.schemaVersion == PatchDocument::currentSchemaVersion);

    // wiki/plans/DomainRedesign.md Batch 3 deleted "mix.sum" outright
    // (folded into math.add) — CLAUDE.md's Rule 3 suspension means this
    // node's own type id, unlike its ports here, is NOT migrated forward:
    // "nothing real depends on today's ids yet... no migration required".
    // A patch old enough to still say "mix.sum" no longer compiles at all
    // (a real, accepted, documented consequence of the suspension, not a
    // silent drop) — the port-id migration this test actually exists to
    // verify (toPortIndex -> in.0/in.1) still ran correctly above; only the
    // OLD type name itself doesn't resolve any more.
    const auto factory = makeFactory();
    NodeGraph graph;
    for (const auto& node : result.document.nodes)
        graph.addNode (node);
    for (const auto& connection : result.document.connections)
        graph.addConnection (connection);
    graph.setOutput (result.document.outputNodeId, result.document.outputPortId);
    const auto compiled = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    CHECK_FALSE (compiled.success);
    CHECK (compiled.errorMessage.contains ("mix.sum"));
}

TEST_CASE ("Schema v3 -> v4 preserves mix.sum's level.N as a spliced mix.gain node, or drops it if it was a no-op",
           "[engine][patch][migration][PortGroups]")
{
    // mix.sum here has three inputs: in.0/level.0 left at the default (1.0,
    // unconnected — nothing to preserve), in.1/level.1 with a non-default
    // CONSTANT level, and in.2/level.2 with level.2 fed by a CONNECTION
    // (a modulated gain) — the three real cases the v3->v4 migration has
    // to tell apart.
    const juce::String v3Json = R"json({
        "schemaVersion": 3,
        "nodes": [
            { "id": "a", "type": "test.audioConstant", "position": { "x": 0, "y": 0 }, "parameters": { "value": 1.0 }, "properties": {} },
            { "id": "b", "type": "test.audioConstant", "position": { "x": 0, "y": 0 }, "parameters": { "value": 1.0 }, "properties": {} },
            { "id": "c", "type": "test.audioConstant", "position": { "x": 0, "y": 0 }, "parameters": { "value": 1.0 }, "properties": {} },
            { "id": "lfo", "type": "util.constant", "position": { "x": 0, "y": 0 }, "parameters": { "util.constant.value": 0.5 }, "properties": {} },
            { "id": "mix", "type": "mix.sum", "position": { "x": 500, "y": 100 }, "parameters": { "level.0": 1.0, "level.1": 2.0 }, "properties": {} }
        ],
        "connections": [
            { "fromNodeId": "a", "fromPortId": "out", "toNodeId": "mix", "toPortId": "in.0" },
            { "fromNodeId": "b", "fromPortId": "out", "toNodeId": "mix", "toPortId": "in.1" },
            { "fromNodeId": "c", "fromPortId": "out", "toNodeId": "mix", "toPortId": "in.2" },
            { "fromNodeId": "lfo", "fromPortId": "out", "toNodeId": "mix", "toPortId": "level.2" }
        ],
        "outputNodeId": "mix",
        "outputPortId": "out",
        "macroMappings": [],
        "macroValues": [],
        "view": { "panX": 0, "panY": 0, "zoom": 1 },
        "meta": { "name": "Legacy v3 mix.sum", "author": "", "createdAtMs": 0, "modifiedAtMs": 0 }
    })json";

    const auto result = parsePatchFromJson (v3Json);
    REQUIRE (result.success);
    CHECK (result.document.schemaVersion == PatchDocument::currentSchemaVersion);

    auto findConnection = [&] (const juce::String& fromNodeId, const juce::String& fromPortId,
                                const juce::String& toNodeId, const juce::String& toPortId)
    {
        for (const auto& c : result.document.connections)
            if (c.fromNodeId == fromNodeId && c.fromPortId == fromPortId && c.toNodeId == toNodeId && c.toPortId == toPortId)
                return true;
        return false;
    };

    // level.0 was already the identity, unconnected: no gain node, in.0
    // stays wired straight to mix, and no stray "level.0" survives anywhere.
    CHECK (findConnection ("a", "out", "mix", "in.0"));
    for (const auto& node : result.document.nodes)
        if (node.id == "mix")
        {
            CHECK (node.parameters.find ("level.0") == node.parameters.end());
            CHECK (node.parameters.find ("level.1") == node.parameters.end());
        }

    // level.1 = 2.0, a plain constant: a real gain node (math.multiply since
    // schema v13) now sits between
    // b and mix, holding that value as its own "gain" parameter — and the
    // old direct b -> mix.in.1 connection is gone (replaced, not duplicated).
    juce::String gain1Id;
    for (const auto& node : result.document.nodes)
        if (node.type == "math.multiply" && findConnection ("b", "out", node.id, "in.0"))
            gain1Id = node.id;
    REQUIRE (gain1Id.isNotEmpty());
    CHECK (findConnection (gain1Id, "out", "mix", "in.1"));
    CHECK_FALSE (findConnection ("b", "out", "mix", "in.1"));
    for (const auto& node : result.document.nodes)
        if (node.id == gain1Id)
            CHECK (node.parameters.at ("in.1") == Catch::Approx (2.0f));

    // level.2 was itself fed by a connection (lfo), not a constant: the
    // spliced mix.gain node's own "gain" INPUT is wired from lfo instead of
    // holding a stored value, and the old lfo -> mix.level.2 connection
    // is gone (its port doesn't exist any more).
    juce::String gain2Id;
    for (const auto& node : result.document.nodes)
        if (node.type == "math.multiply" && findConnection ("c", "out", node.id, "in.0"))
            gain2Id = node.id;
    REQUIRE (gain2Id.isNotEmpty());
    CHECK (findConnection (gain2Id, "out", "mix", "in.2"));
    CHECK (findConnection ("lfo", "out", gain2Id, "in.1"));
    CHECK_FALSE (findConnection ("lfo", "out", "mix", "level.2"));
    CHECK_FALSE (findConnection ("c", "out", "mix", "in.2")); // replaced, not left dangling alongside the new one

    // The structural migration this test exists to verify (level.N ->
    // a real, spliced mix.gain node) is fully checked above. It no longer
    // compiles past that: wiki/plans/DomainRedesign.md Batch 3 deleted
    // "mix.sum" outright (folded into math.add), and CLAUDE.md's Rule 3
    // suspension deliberately does NOT migrate a node's own type id
    // forward ("nothing real depends on today's ids yet... no migration
    // required") — only its ports/parameters, which the checks above
    // already confirm landed correctly. Replaces the old "1 (a, no gain) +
    // 1*2 (b through gain1) + 0.5*1 (c through gain2) = 3.5" sum proof,
    // which needed a real compile to run at all.
    const auto factory = makeFactory(); // registers test.audioConstant, same as the sibling test above
    NodeGraph graph;
    for (const auto& node : result.document.nodes)
        graph.addNode (node);
    for (const auto& connection : result.document.connections)
        graph.addConnection (connection);
    graph.setOutput (result.document.outputNodeId, result.document.outputPortId);

    const auto compiled = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    CHECK_FALSE (compiled.success);
    CHECK (compiled.errorMessage.contains ("mix.sum"));
}
