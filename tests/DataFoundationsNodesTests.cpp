// Data Foundations batch (wiki/NODES.Status.md's own build-next order, step
// 2): data.scale, data.table, data.lookup - the first real Data-producing/
// consuming nodes, and the first real exercise of GraphCompiler.cpp's new
// Data-connection wiring (Node::getDataPublisher()/setDataInput()).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/DataScaleNode.h"
#include "bazalt/engine/graph/CurveData.h"
#include "bazalt/engine/nodes/DataLookupNode.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/RtAllocationTrap.h"
#include <cmath>
#include <limits>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    std::vector<float> currentValues (DataPublisher& publisher)
    {
        const auto* buffer = publisher.getCurrentForAudioThread();
        REQUIRE (buffer != nullptr);
        std::vector<float> values;
        for (int i = 0; i < buffer->length(); ++i)
            values.push_back (buffer->at (i));
        return values;
    }

    // Runs DataLookupNode for a single sample and returns its output - the
    // node's own currentA/currentB are refreshed by processBlock() every
    // call, matching how it's actually driven in a real compiled plan.
    float lookupOnce (DataLookupNode& node, float in, float morph = kNaN)
    {
        const float inputs0[1] = { in };
        const float inputs1[1] = { 0.0f }; // "data" - unused directly, wiring is via setDataInput()
        const float inputs2[1] = { 0.0f }; // "dataB" - same
        const float inputs3[1] = { morph };
        const float* const inputs[4] = { inputs0, inputs1, inputs2, inputs3 };
        float outputs0[1] = {};
        float* const outputs[1] = { outputs0 };
        node.processBlock (inputs, outputs, 1);
        return outputs0[0];
    }
}

// ---- data.scale ----

TEST_CASE ("DataScaleNode publishes the major scale by default", "[engine][nodes][DataScaleNode][DataFoundations]")
{
    DataScaleNode node;
    node.prepare ({ 44100.0, 512 });
    CHECK (currentValues (*node.getDataPublisher()) == std::vector<float> { 0, 2, 4, 5, 7, 9, 11 });
}

TEST_CASE ("DataScaleNode's root rotates the pattern, sorted and octave-wrapped",
           "[engine][nodes][DataScaleNode][DataFoundations]")
{
    DataScaleNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("data.scale.root", 61.0f); // pitch class 1
    // Major {0,2,4,5,7,9,11} + 1 = {1,3,5,6,8,10,12->0}, sorted.
    CHECK (currentValues (*node.getDataPublisher()) == std::vector<float> { 0, 1, 3, 5, 6, 8, 10 });
}

TEST_CASE ("DataScaleNode's scale parameter selects a different pattern",
           "[engine][nodes][DataScaleNode][DataFoundations]")
{
    DataScaleNode node;
    node.prepare ({ 44100.0, 512 });

    node.setParameter ("data.scale.scale", 8.0f); // minorPentatonic
    CHECK (currentValues (*node.getDataPublisher()) == std::vector<float> { 0, 3, 5, 7, 10 });

    node.setParameter ("data.scale.scale", 10.0f); // wholeTone
    CHECK (currentValues (*node.getDataPublisher()) == std::vector<float> { 0, 2, 4, 6, 8, 10 });

    node.setParameter ("data.scale.scale", 11.0f); // chromatic
    CHECK (currentValues (*node.getDataPublisher()) == std::vector<float> { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });
}

TEST_CASE ("DataScaleNode's octaveSize rescales the named 12-tone pattern proportionally",
           "[engine][nodes][DataScaleNode][DataFoundations]")
{
    DataScaleNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("data.scale.root", 0.0f); // pitch class 0, isolating octaveSize's own effect from root's rotation
    node.setParameter ("data.scale.octaveSize", 24.0f);
    // Major {0,2,4,5,7,9,11} scaled by 24/12=2: {0,4,8,10,14,18,22}.
    CHECK (currentValues (*node.getDataPublisher()) == std::vector<float> { 0, 4, 8, 10, 14, 18, 22 });
}

TEST_CASE ("DataScaleNode republishes (a new generation) on every relevant setParameter call",
           "[engine][nodes][DataScaleNode][DataFoundations]")
{
    DataScaleNode node;
    node.prepare ({ 44100.0, 512 });
    const auto firstGeneration = node.getDataPublisher()->getCurrentForAudioThread()->generation;

    node.setParameter ("data.scale.root", 62.0f);
    const auto secondGeneration = node.getDataPublisher()->getCurrentForAudioThread()->generation;

    CHECK (secondGeneration > firstGeneration);
}

TEST_CASE ("DataScaleNode's read side (getCurrentForAudioThread) is allocation-free",
           "[engine][nodes][DataScaleNode][DataFoundations]")
{
    DataScaleNode node;
    node.prepare ({ 44100.0, 512 });

    const DataBuffer* buffer = nullptr;
    {
        ScopedAudioThreadAllocationTrap trap;
        buffer = node.getDataPublisher()->getCurrentForAudioThread();
    }
    REQUIRE (buffer != nullptr);
    CHECK (buffer->tag() == DataTag::Scale);
}

// ---- data.lookup ----

TEST_CASE ("DataLookupNode nearest/interpolate modes read a bipolar -1..1 position across the buffer",
           "[engine][nodes][DataLookupNode][DataFoundations]")
{
    // wiki/plans/PropsAndMacroRedesign.md Batch D: the old per-node
    // Unipolar/Bipolar "polarity" selector is gone - `in` is always
    // remapped from -1..1 now ("mod values are always bipolar"), replacing
    // this test's old 0..1-direct inputs and the separate "polarity remaps"
    // test case that used to exercise the non-default setting.
    DataPublisher publisher;
    publisher.publish (std::make_unique<DataBuffer> (DataTag::Curve, std::vector<float> { 10, 20, 30, 40 }, 1));

    DataLookupNode node;
    node.setDataInput ("data", &publisher);
    node.setParameter ("data.lookup.mode", 1.0f); // interpolate (also the default)

    CHECK (lookupOnce (node, -1.0f) == Catch::Approx (10.0f)); // pos01=0.0
    CHECK (lookupOnce (node, 1.0f) == Catch::Approx (40.0f));  // pos01=1.0
    CHECK (lookupOnce (node, 0.0f) == Catch::Approx (25.0f));  // pos01=0.5, halfway between index 1 (20) and 2 (30)

    node.setParameter ("data.lookup.mode", 0.0f); // nearest
    CHECK (lookupOnce (node, 0.2f) == Catch::Approx (30.0f)); // pos01=0.6, raw = 0.6*3 = 1.8 -> rounds to index 2
}

TEST_CASE ("DataLookupNode's index/wrapIndex modes read a literal index, no position remap applied",
           "[engine][nodes][DataLookupNode][DataFoundations]")
{
    DataPublisher publisher;
    publisher.publish (std::make_unique<DataBuffer> (DataTag::Curve, std::vector<float> { 10, 20, 30, 40 }, 1));

    DataLookupNode node;
    node.setDataInput ("data", &publisher);
    node.setParameter ("data.lookup.mode", 2.0f); // index

    CHECK (lookupOnce (node, 2.0f) == Catch::Approx (30.0f));

    node.setParameter ("data.lookup.edgeMode", 0.0f); // clamp
    CHECK (lookupOnce (node, 5.0f) == Catch::Approx (40.0f)); // clamped to the last index

    node.setParameter ("data.lookup.edgeMode", 1.0f); // wrap
    CHECK (lookupOnce (node, 5.0f) == Catch::Approx (20.0f)); // 5 % 4 = 1

    // wrapIndex always wraps, regardless of edgeMode.
    node.setParameter ("data.lookup.edgeMode", 0.0f); // clamp - should be overridden by wrapIndex
    node.setParameter ("data.lookup.mode", 3.0f); // wrapIndex
    CHECK (lookupOnce (node, 5.0f) == Catch::Approx (20.0f));
}

TEST_CASE ("DataLookupNode blends data/dataB by morph when their tags match",
           "[engine][nodes][DataLookupNode][DataFoundations]")
{
    DataPublisher publisherA;
    publisherA.publish (std::make_unique<DataBuffer> (DataTag::Curve, std::vector<float> { 0.0f }, 1));
    DataPublisher publisherB;
    publisherB.publish (std::make_unique<DataBuffer> (DataTag::Curve, std::vector<float> { 100.0f }, 1));

    DataLookupNode node;
    node.setDataInput ("data", &publisherA);
    node.setDataInput ("dataB", &publisherB);

    CHECK (lookupOnce (node, 0.0f, 0.0f) == Catch::Approx (0.0f));
    CHECK (lookupOnce (node, 0.0f, 1.0f) == Catch::Approx (100.0f));
    CHECK (lookupOnce (node, 0.0f, 0.5f) == Catch::Approx (50.0f));
}

TEST_CASE ("DataLookupNode falls back to data alone when dataB's tag doesn't match",
           "[engine][nodes][DataLookupNode][DataFoundations]")
{
    DataPublisher publisherA;
    publisherA.publish (std::make_unique<DataBuffer> (DataTag::Curve, std::vector<float> { 5.0f }, 1));
    DataPublisher publisherB;
    publisherB.publish (std::make_unique<DataBuffer> (DataTag::Scale, std::vector<float> { 0.0f, 7.0f }, 1));

    DataLookupNode node;
    node.setDataInput ("data", &publisherA);
    node.setDataInput ("dataB", &publisherB);

    CHECK (lookupOnce (node, 0.0f, 1.0f) == Catch::Approx (5.0f)); // morph=1 would normally favour B entirely
}

TEST_CASE ("DataLookupNode outputs silence, not a crash, when nothing is wired to data",
           "[engine][nodes][DataLookupNode][DataFoundations]")
{
    DataLookupNode node;
    CHECK (lookupOnce (node, 0.5f) == 0.0f);
}

// ---- End-to-end: the real GraphCompiler wiring, not just direct C++ calls ----

TEST_CASE ("A real compiled graph wires data.scale's publisher into data.lookup via GraphCompiler",
           "[engine][GraphCompiler][DataFoundations]")
{
    auto factory = buildDefaultNodeFactory();

    NodeGraph graph;
    graph.addNode ({ "scale", "data.scale", {}, {}, {} }); // default: major, root 60 -> {0,2,4,5,7,9,11}
    graph.addNode ({ "index", "util.constant", {}, { { "util.constant.value", 2.0f } }, {} }); // reads scale degree 2 -> 4
    graph.addNode ({ "lookup", "data.lookup", {}, { { "data.lookup.mode", 2.0f } }, {} }); // index mode
    graph.addConnection ({ "scale", "data", "lookup", "data" });
    graph.addConnection ({ "index", "out", "lookup", "in" });
    graph.setOutput ("lookup", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 8 }, 1);
    REQUIRE (result.success);

    result.plan.process (8);
    const auto output = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex]
                             .getBlock().getChannelPointer (0)[7];
    CHECK (output == Catch::Approx (4.0f)); // major scale's own degree index 2 is semitone 4
}

TEST_CASE ("A real compiled graph wires data.curve's publisher into data.lookup via GraphCompiler",
           "[engine][GraphCompiler][DataFoundations]")
{
    auto factory = buildDefaultNodeFactory();

    NodeGraph graph;
    CurveDocument ramp; // what a two-point data.table was (schema v16 migrates it the same way)
    ramp.timeBase = CurveDocument::TimeBase::Time;
    ramp.points = { { 0.0f, -1.0f }, { 1.0f, 1.0f } };
    graph.addNode (withContent ({ "table", "data.curve", {}, {}, {} }, ramp));
    // position is bipolar (-1..1 -> 0..1, wiki/plans/PropsAndMacroRedesign.md
    // Batch D) - 0.0 is the midpoint now, not 0.5.
    graph.addNode ({ "position", "util.constant", {}, { { "util.constant.value", 0.0f } }, {} }); // midpoint
    graph.addNode ({ "lookup", "data.lookup", {}, {}, {} }); // interpolate mode (default)
    graph.addConnection ({ "table", "curve", "lookup", "data" });
    graph.addConnection ({ "position", "out", "lookup", "in" });
    graph.setOutput ("lookup", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 8 }, 1);
    REQUIRE (result.success);

    result.plan.process (8);
    const auto output = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex]
                             .getBlock().getChannelPointer (0)[7];
    CHECK (output == Catch::Approx (0.0f)); // halfway between -1 and 1
}

TEST_CASE ("Wiring two Data producers into the same data.lookup input is rejected, same as any other port",
           "[engine][GraphCompiler][DataFoundations]")
{
    auto factory = buildDefaultNodeFactory();

    NodeGraph graph;
    graph.addNode ({ "scaleA", "data.scale", {}, {}, {} });
    graph.addNode ({ "scaleB", "data.scale", {}, {}, {} });
    graph.addNode ({ "lookup", "data.lookup", {}, {}, {} });
    graph.addConnection ({ "scaleA", "data", "lookup", "data" });
    graph.addConnection ({ "scaleB", "data", "lookup", "data" });
    graph.setOutput ("lookup", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 8 }, 1);
    CHECK_FALSE (result.success);
}
