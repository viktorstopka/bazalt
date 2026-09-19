#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"
#include <unordered_map>

using namespace bazalt::engine;

namespace
{
    NodeGraph buildNoteInToAllocatorGraph()
    {
        NodeGraph graph;
        graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
        graph.addNode ({ "allocator", "instance.allocator", {}, {}, {} });
        graph.addConnection ({ "noteIn", "notes", "allocator", "spawn" });
        graph.setOutput ("allocator", "gate"); // arbitrary — bufferIndexFor() below reads any port directly
        return graph;
    }

    // Finds an arbitrary (nodeId, portId)'s block buffer index, regardless
    // of whether it's the graph's own designated output — mirrors
    // GraphCompilerTests.cpp's positionOf() scanning idiom.
    int bufferIndexFor (const ExecutionPlan& plan, const juce::String& nodeId, const juce::String& portId,
                         const NodeFactory& factory, const NodeGraph& graph)
    {
        const auto slot = plan.nodeIdToSlot.at (nodeId);

        int portIndex = -1;
        for (const auto& n : graph.getNodes())
            if (n.id == nodeId)
            {
                auto node = factory.create (n.type);
                const auto outputs = node->getOutputPorts();
                for (int i = 0; i < (int) outputs.size(); ++i)
                    if (outputs[(size_t) i].id == portId)
                        portIndex = i;
            }

        for (const auto& step : plan.steps)
            if (step.kind == ExecutionPlan::Step::Kind::Block && step.block.nodeSlot == slot)
                return step.block.outputBufferIndices[(size_t) portIndex];

        return -1;
    }
}

TEST_CASE ("io.noteIn -> instance.allocator delivers a real Note-typed connection (M18, ADR-0024)",
           "[engine][GraphCompiler][Note][M18]")
{
    auto graph = buildNoteInToAllocatorGraph();
    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    auto& plan = result.plan;
    auto* noteIn = dynamic_cast<nodes::IoNoteInNode*> (plan.getNodeById ("noteIn"));
    REQUIRE (noteIn != nullptr);

    const auto gateIdx = bufferIndexFor (plan, "allocator", "gate", factory, graph);
    const auto pitchIdx = bufferIndexFor (plan, "allocator", "pitch", factory, graph);
    const auto velocityIdx = bufferIndexFor (plan, "allocator", "velocity", factory, graph);
    const auto startIdx = bufferIndexFor (plan, "allocator", "start", factory, graph);
    const auto stopIdx = bufferIndexFor (plan, "allocator", "stop", factory, graph);
    REQUIRE ((gateIdx >= 0 && pitchIdx >= 0 && velocityIdx >= 0 && startIdx >= 0 && stopIdx >= 0));

    auto sampleAt = [&] (int bufferIndex, int sample)
    { return plan.blockBuffers[(size_t) bufferIndex].getBlock().getChannelPointer (0)[sample]; };

    // Before any note: allocator's default state.
    plan.process (8);
    CHECK (sampleAt (gateIdx, 0) == 0.0f);
    CHECK (sampleAt (startIdx, 0) == 0.0f);

    // A real note-on, injected at io.noteIn, must reach allocator's outputs
    // through the compiled Note connection — not a direct poke on allocator
    // itself.
    noteIn->injectNoteOn (72.0f, 0.8f);
    plan.process (8);

    CHECK (sampleAt (gateIdx, 0) == 1.0f);
    CHECK (sampleAt (pitchIdx, 0) == 72.0f);
    CHECK (sampleAt (velocityIdx, 0) == 0.8f);
    CHECK (sampleAt (startIdx, 0) == 1.0f);   // the start pulse lands on sample 0...
    CHECK (sampleAt (startIdx, 1) == 0.0f);   // ...and only that one sample.
    CHECK (sampleAt (gateIdx, 7) == 1.0f);    // gate stays held for the rest of the block

    noteIn->injectNoteOff();
    plan.process (8);

    CHECK (sampleAt (gateIdx, 0) == 0.0f);
    CHECK (sampleAt (stopIdx, 0) == 1.0f);
    CHECK (sampleAt (stopIdx, 1) == 0.0f);
}
