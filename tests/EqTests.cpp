// wiki/plans/DataAndWavetable.md 1d: the EQ curve, the EQ, the EQ Curve node,
// and Unwrap into Peak / Shelf / SVF — bit for bit.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/Unwrap.h"
#include "bazalt/engine/nodes/EqNodes.h"
#include <cmath>
#include <complex>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int block = 64;

    EqBand band (EqBandType type, float frequency, float gainDb = 0.0f, float q = 1.0f, int order = 2, float slope = 0.5f)
    {
        EqBand b;
        b.type = type;
        b.frequency = frequency;
        b.gainDb = gainDb;
        b.q = q;
        b.order = order;
        b.slope = slope;
        return b;
    }

    juce::var curveOf (std::vector<EqBand> bands)
    {
        EqCurveDocument doc;
        doc.bands = std::move (bands);
        return doc.toVar();
    }

    /** Steady-state gain of a sine at `hz` through a fresh EQ, in dB. */
    double gainDbAt (const juce::var& content, double hz)
    {
        EqFilterNode eq;
        eq.prepare ({ sampleRate, block });
        REQUIRE (eq.setContent (content));
        const auto total = (int) (sampleRate * 0.5);
        double inPower = 0.0, outPower = 0.0;
        std::vector<float> in ((size_t) block), out ((size_t) block);
        for (int start = 0; start < total; start += block)
        {
            for (int i = 0; i < block; ++i)
                in[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (start + i) / sampleRate);
            const float* ins[2] = { in.data(), nullptr };
            float* outs[1] = { out.data() };
            eq.processBlock (ins, outs, block);
            if (start > total / 2)
                for (int i = 0; i < block; ++i)
                {
                    inPower += (double) in[(size_t) i] * in[(size_t) i];
                    outPower += (double) out[(size_t) i] * out[(size_t) i];
                }
        }
        return 10.0 * std::log10 (outPower / inPower);
    }

    std::vector<float> render (const NodeGraph& graph, int blocks = 200)
    {
        const auto factory = buildDefaultNodeFactory();
        auto result = GraphCompiler::compile (graph, factory, { sampleRate, block }, 1);
        REQUIRE (result.success);
        std::vector<float> out;
        for (int b = 0; b < blocks; ++b)
        {
            result.plan.process (block);
            const auto* samples = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
            out.insert (out.end(), samples, samples + block);
        }
        return out;
    }

    /** saw -> EQ (every band type) -> output. */
    NodeGraph eqGraph (const juce::var& content, bool viaCurveNode)
    {
        NodeGraph graph;
        graph.addNode ({ "saw", "source.saw", { 0.0f, 0.0f }, { { "source.saw.frequency", 110.0f } }, {} });
        NodeInstance eq { "eq", "filter.eq", { 200.0f, 0.0f }, {}, {} };
        if (viaCurveNode)
        {
            NodeInstance curve { "curve", "data.eqCurve", { 200.0f, 200.0f }, {}, {} };
            curve.content = content;
            graph.addNode (curve);
            graph.addConnection ({ "curve", "eq", "eq", "curve" });
        }
        else
        {
            eq.content = content;
        }
        graph.addNode (eq);
        graph.addNode ({ "out", "io.output", { 400.0f, 0.0f }, {}, {} });
        graph.addConnection ({ "saw", "out", "eq", "in" });
        graph.addConnection ({ "eq", "out", "out", "in" });
        graph.setOutput ("out", "out");
        return graph;
    }

    const auto everyType = []
    {
        return curveOf ({ band (EqBandType::LowCut, 60.0f, 0.0f, 0.9f, 3),
                          band (EqBandType::LowShelf, 200.0f, -4.0f, 1.0f, 2, 0.7f),
                          band (EqBandType::Bell, 900.0f, 9.0f, 2.5f),
                          band (EqBandType::Notch, 2400.0f, 0.0f, 4.0f),
                          band (EqBandType::HighShelf, 6000.0f, 5.0f, 1.0f, 2, 0.3f),
                          band (EqBandType::HighCut, 12000.0f, 0.0f, 0.7071f, 2) });
    };
}

TEST_CASE ("An EQ bell lifts its band and leaves the rest alone", "[engine][eq]")
{
    const auto content = curveOf ({ band (EqBandType::Bell, 1000.0f, 12.0f, 2.0f) });
    CHECK (gainDbAt (content, 1000.0) == Catch::Approx (12.0).margin (0.2));
    CHECK (std::abs (gainDbAt (content, 100.0)) < 0.3);
    CHECK (std::abs (gainDbAt (content, 10000.0)) < 0.3);
}

TEST_CASE ("A Low Cut's order sets its slope", "[engine][eq]")
{
    const auto at = [] (int order, double hz) { return gainDbAt (curveOf ({ band (EqBandType::LowCut, 400.0f, 0.0f, 0.70710678f, order) }), hz); };
    CHECK (at (1, 400.0) == Catch::Approx (-3.0).margin (0.3)); // Butterworth: -3 dB at the corner
    CHECK (at (4, 400.0) == Catch::Approx (-3.0).margin (0.3));
    CHECK (at (1, 100.0) == Catch::Approx (-24.0).margin (1.5)); // two octaves down, 12 dB/oct
    CHECK (at (4, 100.0) < -80.0);                                // 48 dB/oct
    CHECK (std::abs (at (4, 4000.0)) < 0.1);
}

TEST_CASE ("A flat or empty EQ passes its input through unchanged", "[engine][eq]")
{
    CHECK (std::abs (gainDbAt (curveOf ({}), 1000.0)) < 1.0e-6);
    CHECK (std::abs (gainDbAt (curveOf ({ band (EqBandType::Bell, 1000.0f, 0.0f) }), 1000.0)) < 1.0e-4);
}

TEST_CASE ("An EQ curve survives JSON, clamped to sane values", "[engine][eq]")
{
    const auto doc = EqCurveDocument::fromVar (curveOf ({ band (EqBandType::HighCut, 5000.0f, 0.0f, 0.9f, 3),
                                                          band (EqBandType::Bell, 99999.0f, 60.0f, 2.0f) }));
    REQUIRE (doc.bands.size() == 2);
    CHECK (doc.bands[0].type == EqBandType::HighCut);
    CHECK (doc.bands[0].order == 3);
    CHECK (doc.bands[0].q == Catch::Approx (0.9f));
    CHECK (doc.bands[1].frequency == EqCurveDocument::maxFrequency);
    CHECK (doc.bands[1].gainDb == EqCurveDocument::maxGainDb);
    CHECK (EqCurveDocument::fromVar (juce::var()).bands.empty());
}

TEST_CASE ("Unwrap turns an EQ into Peak, Shelf and SVF nodes that sound bit-identical", "[engine][eq][unwrap]")
{
    for (const auto viaCurveNode : { false, true })
    {
        INFO ("via an EQ Curve node: " << viaCurveNode);
        const auto graph = eqGraph (everyType(), viaCurveNode);
        auto unwrapped = graph;
        const auto result = unwrapNode (unwrapped, "eq");
        REQUIRE (result.success);
        CHECK (unwrapped.findNode ("eq") == nullptr);
        CHECK (result.newNodeIds.size() == 3 + 1 + 1 + 1 + 1 + 2); // a 3rd-order cut is three SVF stages, a 2nd-order two

        int peaks = 0, shelves = 0, svfs = 0;
        for (const auto& id : result.newNodeIds)
        {
            const auto type = unwrapped.findNode (id)->type;
            peaks += type == "filter.peak";
            shelves += type == "filter.shelf";
            svfs += type == "filter.svf";
        }
        CHECK (peaks == 1);
        CHECK (shelves == 2);
        CHECK (svfs == 6);

        const auto before = render (graph);
        const auto after = render (unwrapped);
        REQUIRE (before.size() == after.size());
        size_t mismatches = 0;
        for (size_t i = 0; i < before.size(); ++i)
            mismatches += before[i] != after[i];
        CHECK (mismatches == 0);
        CHECK (std::abs (before[before.size() / 2]) > 0.0f); // it really played
    }
}

TEST_CASE ("A flat EQ unwraps into a wire", "[engine][eq][unwrap]")
{
    auto graph = eqGraph (curveOf ({}), false);
    const auto result = unwrapNode (graph, "eq");
    REQUIRE (result.success);
    CHECK (result.newNodeIds.empty());
    REQUIRE (graph.getConnections().size() == 1);
    CHECK (graph.getConnections()[0].fromNodeId == "saw");
    CHECK (graph.getConnections()[0].toNodeId == "out");
}

TEST_CASE ("Only factories unwrap", "[engine][eq][unwrap]")
{
    auto graph = eqGraph (curveOf ({}), false);
    CHECK_FALSE (unwrapNode (graph, "saw").success);
    CHECK_FALSE (unwrapNode (graph, "nope").success);
    CHECK (graph.findNode ("eq") != nullptr);
}

TEST_CASE ("A stereo EQ plays its curve on both channels", "[engine][eq][stereo]")
{
    // Lane copies of a content node used to start without the content.
    const auto content = curveOf ({ band (EqBandType::HighCut, 300.0f, 0.0f, 0.70710678f, 4) });
    NodeGraph stereo;
    stereo.addNode ({ "l", "source.saw", {}, { { "source.saw.frequency", 220.0f } }, {} });
    stereo.addNode ({ "r", "source.saw", {}, { { "source.saw.frequency", 330.0f } }, {} });
    stereo.addNode ({ "combine", "channels.combine", {}, {}, {} });
    NodeInstance eq { "eq", "filter.eq", {}, {}, {} };
    eq.content = content;
    stereo.addNode (eq);
    stereo.addNode ({ "split", "channels.split", {}, {}, {} });
    stereo.addConnection ({ "l", "out", "combine", "left" });
    stereo.addConnection ({ "r", "out", "combine", "right" });
    stereo.addConnection ({ "combine", "out", "eq", "in" });
    stereo.addConnection ({ "eq", "out", "split", "in" });
    stereo.setOutput ("split", "right");

    NodeGraph mono;
    mono.addNode ({ "r", "source.saw", {}, { { "source.saw.frequency", 330.0f } }, {} });
    mono.addNode (eq);
    mono.addConnection ({ "r", "out", "eq", "in" });
    mono.setOutput ("eq", "out");

    const auto a = render (stereo, 50), b = render (mono, 50);
    for (size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i] == Catch::Approx (b[i]).margin (1.0e-6));
}

TEST_CASE ("An EQ edit glides instead of jumping", "[engine][eq]")
{
    EqFilterNode eq;
    eq.prepare ({ sampleRate, block });
    REQUIRE (eq.setContent (curveOf ({ band (EqBandType::Bell, 1000.0f, 0.0f, 1.0f) })));
    std::vector<float> in ((size_t) block, 0.0f), out ((size_t) block);
    const float* ins[2] = { in.data(), nullptr };
    float* outs[1] = { out.data() };
    std::vector<float> rendered;
    double phase = 0.0;
    for (int b = 0; b < 300; ++b)
    {
        if (b == 100)
            REQUIRE (eq.setContent (curveOf ({ band (EqBandType::Bell, 1000.0f, 18.0f, 1.0f) })));
        for (auto& s : in)
        {
            s = (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
        }
        eq.processBlock (ins, outs, block);
        rendered.insert (rendered.end(), out.begin(), out.end());
    }
    // The 18 dB bell reaches full height over ~10 ms, not in one sample:
    // no single step between neighbouring samples is far bigger than a
    // steady sine's own.
    float largestStep = 0.0f;
    for (size_t i = 100 * block; i < 140 * block; ++i)
        largestStep = std::max (largestStep, std::abs (rendered[i] - rendered[i - 1]));
    CHECK (largestStep < 8.0f * 0.131f * 1.05f); // 8x amplitude, sine step 2*pi*1000/48000
    const auto peak = *std::max_element (rendered.end() - block * 20, rendered.end());
    CHECK (peak == Catch::Approx (std::pow (10.0, 18.0 / 20.0)).margin (0.1));
}
