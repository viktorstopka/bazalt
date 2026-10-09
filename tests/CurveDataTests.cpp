// wiki/plans/DataAndWavetable.md 1b: the curve model, its band-limited
// tables, and the data.curve factory node.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/CurveData.h"
#include "bazalt/engine/nodes/DataCurveNode.h"
#include "bazalt/engine/nodes/DataLookupNode.h"
#include <cmath>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
}

TEST_CASE ("The Sine preset is an exact sine, and the other presets land on their corners", "[engine][curve]")
{
    const auto sine = CurveDocument::sine();
    for (int i = 0; i <= 64; ++i)
    {
        const auto x = (float) i / 64.0f;
        CHECK (evaluateCurve (sine, x) == Catch::Approx (std::sin (twoPi * x)).margin (1.0e-5));
    }

    const auto triangle = CurveDocument::triangle();
    CHECK (evaluateCurve (triangle, 0.0f) == Catch::Approx (0.0f).margin (1.0e-6));
    CHECK (evaluateCurve (triangle, 0.25f) == Catch::Approx (1.0f));
    CHECK (evaluateCurve (triangle, 0.5f) == Catch::Approx (0.0f).margin (1.0e-6));
    CHECK (evaluateCurve (triangle, 0.75f) == Catch::Approx (-1.0f));

    const auto square = CurveDocument::square();
    CHECK (evaluateCurve (square, 0.1f) == 1.0f);
    CHECK (evaluateCurve (square, 0.6f) == -1.0f);

    const auto saw = CurveDocument::saw();
    CHECK (evaluateCurve (saw, 0.0f) == Catch::Approx (-1.0f));
    CHECK (evaluateCurve (saw, 0.5f) == Catch::Approx (0.0f).margin (1.0e-6));
    CHECK (evaluateCurve (saw, 0.999f) == Catch::Approx (1.0f).margin (0.01));
}

TEST_CASE ("Tension bends a segment; Time mode holds its ends", "[engine][curve]")
{
    CurveDocument doc;
    doc.timeBase = CurveDocument::TimeBase::Time;
    doc.lengthSeconds = 2.0f;
    doc.points = { { 0.5f, 0.0f, 0.0f }, { 1.5f, 1.0f } };
    CHECK (evaluateCurve (doc, 1.0f) == Catch::Approx (0.5f));
    CHECK (evaluateCurve (doc, 0.0f) == 0.0f); // before the first point
    CHECK (evaluateCurve (doc, 2.0f) == 1.0f); // after the last

    doc.points[0].tension = 0.8f; // slow start
    CHECK (evaluateCurve (doc, 1.0f) < 0.4f);
    doc.points[0].tension = -0.8f; // fast start
    CHECK (evaluateCurve (doc, 1.0f) > 0.6f);
}

TEST_CASE ("A curve round-trips through its JSON", "[engine][curve]")
{
    auto doc = CurveDocument::adsr (0.01f, 0.2f, 0.6f, 0.4f);
    doc.points[1].tension = 0.3f;
    const auto back = CurveDocument::fromVar (juce::JSON::parse (juce::JSON::toString (doc.toVar())));
    REQUIRE (back.points.size() == doc.points.size());
    CHECK (back.timeBase == CurveDocument::TimeBase::Time);
    CHECK (back.indexOfMarker ('S') == 2);
    CHECK (back.points[1].tension == Catch::Approx (0.3f));
    CHECK (back.points[2].y == Catch::Approx (0.6f));
    CHECK (back.lengthSeconds == Catch::Approx (0.61f));

    // No content: the default shape for the time base.
    CHECK (CurveDocument::fromVar ({}).points.size() == CurveDocument::sine().points.size());
}

TEST_CASE ("The published buffer's levels are band-limited copies of the curve", "[engine][curve]")
{
    const auto sineBuffer = buildCurveBuffer (CurveDocument::sine());
    const CurveView sine (sineBuffer.get());
    REQUIRE (sine.isValid());
    REQUIRE (sine.hasBandLimitedTables());
    // A sine has one harmonic: every level is the same sine.
    for (const auto level : { 0, 5, CurveView::numLevels - 1 })
        for (const auto x : { 0.1, 0.3, 0.77 })
            CHECK (sine.read (level, x) == Catch::Approx (std::sin (twoPi * x)).margin (1.0e-3));

    // A saw at the top level keeps only its fundamental, of amplitude 2/pi.
    const auto sawBuffer = buildCurveBuffer (CurveDocument::saw());
    const CurveView saw (sawBuffer.get());
    auto peak = 0.0f;
    for (int i = 0; i < 256; ++i)
        peak = juce::jmax (peak, std::abs (saw.read (CurveView::numLevels - 1, (double) i / 256.0)));
    CHECK (peak == Catch::Approx (2.0f / juce::MathConstants<float>::pi).margin (0.01));
    // Level 0 is the saw exactly as drawn.
    CHECK (saw.read (0, 0.25) == Catch::Approx (-0.5f).margin (0.002));
}

TEST_CASE ("The level for a frequency keeps every harmonic below Nyquist", "[engine][curve]")
{
    constexpr double sampleRate = 48000.0;
    CHECK (CurveView::levelFor (5.0 / sampleRate) == 0); // an LFO: exactly as drawn
    for (const auto frequency : { 30.0, 220.0, 1000.0, 5000.0, 12000.0 })
    {
        const auto level = CurveView::levelFor (frequency / sampleRate);
        const auto harmonics = (CurveView::tableSize / 2) >> level;
        CHECK ((double) harmonics * frequency <= sampleRate / 2.0);
        if (level > 0)
            CHECK ((double) (harmonics * 2) * frequency > sampleRate / 2.0); // and no darker than it needs to be
    }
}

TEST_CASE ("data.curve publishes its content, and a long live drag never runs out of buffers", "[engine][curve][data.curve]")
{
    DataCurveNode node;
    node.prepare ({ 48000.0, 64 });
    auto* publisher = node.getDataPublisher();
    REQUIRE (publisher != nullptr);
    CHECK (CurveView (publisher->getCurrentForAudioThread()).isValid()); // the default sine

    for (int i = 0; i < 100; ++i)
    {
        auto doc = CurveDocument::square (0.1f + 0.008f * (float) i);
        node.setContent (doc.toVar());
        const CurveView view (publisher->getCurrentForAudioThread()); // the audio thread reads between edits
        REQUIRE (view.isValid());
        CHECK (view.point (1).x == Catch::Approx (doc.points[1].x));
    }
}

TEST_CASE ("data.lookup reads a drawn curve exactly", "[engine][curve][data.lookup]")
{
    DataCurveNode curve;
    curve.prepare ({ 48000.0, 64 });
    curve.setContent (CurveDocument::triangle().toVar());

    DataLookupNode lookup;
    lookup.setDataInput ("data", curve.getDataPublisher());
    const float in[4] = { -0.5f, 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN() }; // position 0.25 of the cycle
    float out[1] = {};
    const float* inputs[4] = { &in[0], &in[1], &in[2], &in[3] };
    float* outputs[1] = { out };
    lookup.processBlock (inputs, outputs, 1);
    CHECK (out[0] == Catch::Approx (1.0f));
}
