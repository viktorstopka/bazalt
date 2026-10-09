// wiki/plans/DataAndWavetable.md 1c: the wavetable model, data.wavetable and
// the Oscillator playing it at a live Frame.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/WavetableData.h"
#include "bazalt/engine/nodes/CurvePlayerNode.h"
#include "bazalt/engine/nodes/DataWavetableNode.h"
#include <cmath>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr double sampleRate = 48000.0;

    WavetableDocument sineToSquare (WavetableDocument::Interpolation interpolation = WavetableDocument::Interpolation::Morph)
    {
        WavetableDocument doc;
        doc.interpolation = interpolation;
        doc.keyframes = { WavetableKeyframe::ofCurve (0.0f, CurveDocument::sine()),
                          WavetableKeyframe::ofCurve (1.0f, CurveDocument::square()) };
        return doc;
    }

    float level0 (const WavetableView& view, double phase, float frame) { return view.read (0, phase, frame); }
}

TEST_CASE ("A wavetable document round-trips through JSON", "[engine][wavetable]")
{
    WavetableDocument doc;
    doc.interpolation = WavetableDocument::Interpolation::Step;
    doc.keyframes = { WavetableKeyframe::ofCurve (0.0f, CurveDocument::triangle()),
                      WavetableKeyframe::ofHarmonics (0.5f, { 1.0f, 0.0f, 0.33f }, { 0.0f, 0.0f, 0.25f }),
                      WavetableKeyframe::ofCurve (1.0f, CurveDocument::saw()) };

    const auto back = WavetableDocument::fromVar (juce::JSON::parse (juce::JSON::toString (doc.toVar())));
    REQUIRE (back.keyframes.size() == 3);
    CHECK (back.interpolation == WavetableDocument::Interpolation::Step);
    CHECK (back.keyframes[1].kind == WavetableKeyframe::Kind::Harmonics);
    CHECK (back.keyframes[1].position == 0.5f);
    CHECK (back.keyframes[1].amplitudes == std::vector<float> { 1.0f, 0.0f, 0.33f });
    CHECK (back.keyframes[1].phases[2] == 0.25f);
    CHECK (back.keyframes[2].curve.points.size() == CurveDocument::saw().points.size());

    CHECK (WavetableDocument::fromVar ({}).keyframes.size() == 2); // the default: sine into saw
    CHECK (WavetableDocument::fromVar (juce::JSON::parse ("{\"keyframes\":[]}")).keyframes.size() == 1); // never empty
}

TEST_CASE ("A harmonics keyframe is the sum of its sines, scaled to fit", "[engine][wavetable]")
{
    const auto single = WavetableKeyframe::ofHarmonics (0.0f, { 0.5f }).renderCycle();
    for (int i = 0; i < CurveView::tableSize; i += 97)
        CHECK (single[(size_t) i] == Catch::Approx (0.5 * std::sin (juce::MathConstants<double>::twoPi * i / CurveView::tableSize)).margin (1.0e-5));

    const auto loud = WavetableKeyframe::ofHarmonics (0.0f, { 1.0f, 1.0f, 1.0f }).renderCycle();
    float peak = 0.0f;
    for (const auto v : loud)
        peak = std::max (peak, std::abs (v));
    CHECK (peak == Catch::Approx (1.0f).margin (1.0e-5));
}

TEST_CASE ("Frames morph between keyframes, or step", "[engine][wavetable]")
{
    const auto morphBuffer = buildWavetableBuffer (sineToSquare());
    const WavetableView morph (morphBuffer.get());
    REQUIRE (morph.isValid());
    REQUIRE (morph.getNumKeyframes() == 2);

    const auto phase = 0.6; // sine ~ -0.588, square -1
    const auto sine = level0 (morph, phase, 0.0f);
    const auto square = level0 (morph, phase, 1.0f);
    CHECK (sine == Catch::Approx (std::sin (juce::MathConstants<double>::twoPi * phase)).margin (2.0e-3));
    CHECK (square == Catch::Approx (-1.0f).margin (1.0e-3));
    CHECK (level0 (morph, phase, 0.5f) == Catch::Approx (0.5f * (sine + square)).margin (1.0e-4));

    const auto stepBuffer = buildWavetableBuffer (sineToSquare (WavetableDocument::Interpolation::Step));
    const WavetableView step (stepBuffer.get());
    CHECK (level0 (step, phase, 0.5f) == Catch::Approx (sine).margin (1.0e-4));
    CHECK (level0 (step, phase, 1.0f) == Catch::Approx (square).margin (1.0e-4));

    // A curve buffer is not a wavetable, and the other way round.
    const auto curve = buildCurveBuffer (CurveDocument::saw());
    CHECK_FALSE (WavetableView (curve.get()).isValid());
    CHECK_FALSE (CurveView (morphBuffer.get()).isValid());
}

TEST_CASE ("Every keyframe is band-limited like a curve", "[engine][wavetable]")
{
    const auto buffer = buildWavetableBuffer (sineToSquare());
    const WavetableView view (buffer.get());
    // The top level keeps the fundamental only: the square's is a 4/pi sine.
    const auto top = CurveView::numLevels - 1;
    CHECK (view.read (top, 0.25, 1.0f) == Catch::Approx (4.0 / juce::MathConstants<double>::pi).margin (1.0e-3));
}

TEST_CASE ("The Oscillator plays a wired wavetable at the Frame it is sent", "[engine][wavetable][GraphCompiler]")
{
    const auto factory = buildDefaultNodeFactory();
    const auto render = [&] (bool modulated)
    {
        NodeGraph graph;
        NodeInstance table { "table", "data.wavetable", {}, {}, {} };
        table.content = sineToSquare().toVar();
        graph.addNode (table);
        // A 1 Hz frame sweep (0..1) from a second oscillator, or a fixed frame.
        graph.addNode ({ "sweep", "source.oscillator", {}, { { "source.oscillator.frequency", 2.0f } }, {} });
        graph.addNode ({ "osc", "source.oscillator", {}, { { "source.oscillator.frequency", 100.0f } }, {} });
        graph.addConnection ({ "table", "table", "osc", "shape" });
        if (modulated)
            graph.addConnection ({ "sweep", "out", "table", "data.wavetable.frame" });
        graph.setOutput ("osc", "out");
        auto result = GraphCompiler::compile (graph, factory, { sampleRate, 64 }, 1);
        REQUIRE (result.success);
        std::vector<float> out;
        for (int block = 0; block < 375; ++block) // 0.5 s
        {
            result.plan.process (64);
            const auto* samples = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
            out.insert (out.end(), samples, samples + 64);
        }
        return out;
    };

    // Unmodulated: frame 0, the sine keyframe, at 100 Hz.
    const auto still = render (false);
    for (size_t i = 0; i < still.size(); i += 101)
        REQUIRE (still[i] == Catch::Approx (std::sin (juce::MathConstants<double>::twoPi * 100.0 * (double) i / sampleRate)).margin (3.0e-3));

    // Modulated: the sweep's sine (2 Hz) clamped to 0..1 by Frame — at a
    // quarter of its cycle (125 ms) the frame is 1, the square.
    const auto swept = render (true);
    const auto at = (size_t) 6288; // 131 ms: the sweep is near 1, and 100 Hz is a tenth into a cycle (sine 0.59, square high)
    CHECK (swept[at] > 0.95f);
    CHECK (still[at] < 0.7f);
}

TEST_CASE ("data.wavetable's live edits never run out of buffers, and copies share a cache", "[engine][wavetable]")
{
    DataWavetableNode node;
    node.prepare ({ sampleRate, 64 });
    for (int i = 0; i < 200; ++i)
    {
        auto doc = sineToSquare();
        doc.keyframes[1].curve.points[1].x = 0.2f + 0.003f * (float) i;
        REQUIRE (node.setContent (doc.toVar()));
        node.getDataPublisher()->getCurrentForAudioThread(); // the audio thread moves on
    }

    const auto a = keyframeMipmap (WavetableKeyframe::ofCurve (0.0f, CurveDocument::triangle()));
    const auto b = keyframeMipmap (WavetableKeyframe::ofCurve (0.7f, CurveDocument::triangle()));
    CHECK (a == b); // the same table, built once
}
