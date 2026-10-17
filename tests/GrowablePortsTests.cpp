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
