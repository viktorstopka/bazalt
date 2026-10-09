// wiki/plans/DataAndWavetable.md 1b: a factory node's content — saved with the
// patch, handed to the node at compile time, and republished on a running node
// without rebuilding it.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/patch/PatchSerializer.h"

using namespace bazalt::engine;

namespace
{
    class ContentProbeNode : public Node
    {
    public:
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .isPrimaryOutput = true } };
        }
        void setContent (const juce::var& content) override
        {
            ++setContentCalls;
            value = (float) content.getProperty ("value", 0.0f);
        }
        void processSample (const float*, float* outputs) noexcept override { outputs[0] = value; }

        int setContentCalls = 0;
        float value = -1.0f;
    };

    NodeFactory makeFactory()
    {
        auto factory = buildDefaultNodeFactory();
        factory.registerType ("test.contentProbe", [] { return std::make_unique<ContentProbeNode>(); });
        return factory;
    }

    juce::var contentWithValue (float value)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("value", value);
        return juce::var (object);
    }

    NodeGraph probeGraph (const juce::var& content)
    {
        NodeGraph graph;
        NodeInstance probe { "probe", "test.contentProbe", {}, {}, {} };
        probe.content = content;
        graph.addNode (probe);
        graph.setOutput ("probe", "out");
        return graph;
    }

    ContentProbeNode* probeIn (ExecutionPlan& plan)
    {
        return dynamic_cast<ContentProbeNode*> (plan.getNodeById ("probe"));
    }
}

TEST_CASE ("Node content round-trips through the patch format", "[engine][content]")
{
    PatchDocument doc;
    doc.nodes.push_back ({ "probe", "test.contentProbe", {}, {}, {} });
    doc.nodes.back().content = contentWithValue (0.25f);
    doc.nodes.push_back ({ "plain", "math.add", {}, {}, {} });

    const auto parsed = parsePatchFromJson (serializePatchToJson (doc));
    REQUIRE (parsed.success);
    CHECK ((float) parsed.document.nodes[0].content.getProperty ("value", 0.0f) == 0.25f);
    CHECK (parsed.document.nodes[1].content.isVoid()); // an ordinary node carries none
}

TEST_CASE ("A content edit keeps the running node and hands it the new content once", "[engine][content][GraphCompiler]")
{
    const auto factory = makeFactory();

    auto first = GraphCompiler::compile (probeGraph (contentWithValue (0.5f)), factory, { 44100.0, 64 }, 1);
    REQUIRE (first.success);
    auto* node = probeIn (first.plan);
    REQUIRE (node != nullptr);
    CHECK (node->setContentCalls == 1);
    CHECK (node->value == 0.5f);

    // Unchanged content: reused, not handed it again.
    auto same = GraphCompiler::compile (probeGraph (contentWithValue (0.5f)), factory, { 44100.0, 64 }, 2, &first.plan);
    REQUIRE (same.success);
    CHECK (probeIn (same.plan) == node);
    CHECK (node->setContentCalls == 1);

    // Changed content: still the same node object, given the new content.
    auto changed = GraphCompiler::compile (probeGraph (contentWithValue (0.75f)), factory, { 44100.0, 64 }, 3, &same.plan);
    REQUIRE (changed.success);
    CHECK (probeIn (changed.plan) == node);
    CHECK (node->setContentCalls == 2);
    CHECK (node->value == 0.75f);
}

TEST_CASE ("A node with no content is never handed any", "[engine][content][GraphCompiler]")
{
    const auto factory = makeFactory();
    auto result = GraphCompiler::compile (probeGraph ({}), factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);
    CHECK (probeIn (result.plan)->setContentCalls == 0);
}
