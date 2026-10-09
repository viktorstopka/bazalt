#pragma once

#include "bazalt/engine/graph/Data.h"
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace bazalt::engine
{
    /** A curve (wiki/plans/DataAndWavetable.md D5/D6, Factories.md §5): the
        content of `data.curve`, `source.oscillator` and `source.envelope`.

        Points, each starting a segment to the next one with its own shape:
        - `curve`: tension -1..1 bends it (0 is a straight line);
        - `smooth`: a cosine S — two `smooth` points at the peaks of a cycle
          are an exact sine, which is how the Sine preset needs no
          approximation;
        - `hold`: stays at the point's value until the next one (steps, a
          square).
        A point may carry a one-letter marker (A, D, S, H, R …); `S` is where
        an envelope holds while its gate is high.

        Time base: **cycle** — x runs over one period, 0..1, and wraps (the
        segment from the last point continues into the first one of the next
        period); **time** — x is seconds, 0..length, held at either end.

        The editor writes this as JSON (NodeInstance::content); the engine
        turns it into one immutable `Data(curve)` buffer (`buildCurveBuffer`)
        that every consumer reads through `CurveView` on the audio thread.
        Building happens on the message thread, never per sample.
    */
    struct CurvePoint
    {
        enum class Shape { Curve, Smooth, Hold };

        float x = 0.0f;
        float y = 0.0f;
        float tension = 0.0f;
        Shape shape = Shape::Curve;
        char marker = 0; // 0: none
    };

    struct CurveDocument
    {
        enum class TimeBase { Cycle, Time };

        TimeBase timeBase = TimeBase::Cycle;
        float lengthSeconds = 1.0f;     // Time mode: the x range
        std::vector<CurvePoint> points; // sorted by x
        float loopStart = -1.0f;        // Time mode: a loop range, or < 0 for none
        float loopEnd = -1.0f;

        float domainEnd() const noexcept { return timeBase == TimeBase::Cycle ? 1.0f : lengthSeconds; }

        int indexOfMarker (char marker) const noexcept
        {
            for (size_t i = 0; i < points.size(); ++i)
                if (points[i].marker == marker)
                    return (int) i;
            return -1;
        }

        // ---- presets -------------------------------------------------------

        static CurveDocument sine()
        {
            CurveDocument doc;
            doc.points = { { 0.25f, 1.0f, 0.0f, CurvePoint::Shape::Smooth }, { 0.75f, -1.0f, 0.0f, CurvePoint::Shape::Smooth } };
            return doc;
        }
        static CurveDocument triangle()
        {
            CurveDocument doc;
            doc.points = { { 0.25f, 1.0f }, { 0.75f, -1.0f } };
            return doc;
        }
        static CurveDocument saw()
        {
            CurveDocument doc;
            doc.points = { { 0.0f, -1.0f }, { 1.0f, 1.0f } };
            return doc;
        }
        static CurveDocument square (float duty = 0.5f)
        {
            CurveDocument doc;
            doc.points = { { 0.0f, 1.0f, 0.0f, CurvePoint::Shape::Hold }, { juce::jlimit (0.01f, 0.99f, duty), -1.0f, 0.0f, CurvePoint::Shape::Hold } };
            return doc;
        }
        static CurveDocument ramp()
        {
            CurveDocument doc;
            doc.points = { { 0.0f, 1.0f }, { 1.0f, -1.0f } };
            return doc;
        }
        static CurveDocument steps (int count = 4)
        {
            CurveDocument doc;
            for (int i = 0; i < count; ++i)
                doc.points.push_back ({ (float) i / (float) count, -1.0f + 2.0f * (float) i / (float) juce::jmax (1, count - 1), 0.0f,
                                        CurvePoint::Shape::Hold });
            return doc;
        }
        /** An ADSR in Time mode: attack to 1, decay to `sustain` (held while
            the gate is high, marker S), release to 0. Tension bends attack
            up and decay/release down, the classic shape. */
        static CurveDocument adsr (float attack, float decay, float sustain, float release)
        {
            CurveDocument doc;
            doc.timeBase = TimeBase::Time;
            attack = juce::jmax (0.0f, attack);
            decay = juce::jmax (0.0f, decay);
            release = juce::jmax (0.0f, release);
            doc.points = { { 0.0f, 0.0f, 0.0f, CurvePoint::Shape::Curve, 0 },
                           { attack, 1.0f, -0.5f, CurvePoint::Shape::Curve, 'A' },
                           { attack + decay, juce::jlimit (0.0f, 1.0f, sustain), -0.5f, CurvePoint::Shape::Curve, 'S' },
                           { attack + decay + release, 0.0f, 0.0f, CurvePoint::Shape::Curve, 'R' } };
            doc.lengthSeconds = juce::jmax (0.001f, attack + decay + release);
            return doc;
        }

        // ---- JSON ----------------------------------------------------------

        static CurveDocument fromVar (const juce::var& content, TimeBase defaultTimeBase = TimeBase::Cycle)
        {
            if (! content.isObject())
                return defaultTimeBase == TimeBase::Cycle ? sine() : adsr (0.01f, 0.2f, 0.7f, 0.3f);

            CurveDocument doc;
            doc.timeBase = content.getProperty ("timeBase", "").toString() == "time" ? TimeBase::Time
                         : content.getProperty ("timeBase", "").toString() == "cycle" ? TimeBase::Cycle
                                                                                         : defaultTimeBase;
            doc.lengthSeconds = juce::jmax (0.001f, (float) (double) content.getProperty ("length", 1.0));
            if (auto* array = content.getProperty ("points", {}).getArray())
            {
                for (const auto& p : *array)
                {
                    CurvePoint point;
                    point.x = (float) (double) p.getProperty ("x", 0.0);
                    point.y = (float) (double) p.getProperty ("y", 0.0);
                    point.tension = juce::jlimit (-1.0f, 1.0f, (float) (double) p.getProperty ("tension", 0.0));
                    const auto shape = p.getProperty ("shape", "curve").toString();
                    point.shape = shape == "smooth" ? CurvePoint::Shape::Smooth : shape == "hold" ? CurvePoint::Shape::Hold : CurvePoint::Shape::Curve;
                    const auto marker = p.getProperty ("marker", "").toString();
                    point.marker = marker.isNotEmpty() ? (char) marker[0] : 0;
                    if (std::isfinite (point.x) && std::isfinite (point.y))
                        doc.points.push_back (point);
                }
            }
            if (auto loop = content.getProperty ("loop", {}); loop.isObject())
            {
                doc.loopStart = (float) (double) loop.getProperty ("start", -1.0);
                doc.loopEnd = (float) (double) loop.getProperty ("end", -1.0);
            }
            doc.normalise();
            return doc;
        }

        juce::var toVar() const
        {
            auto* root = new juce::DynamicObject();
            root->setProperty ("schema", "curve");
            root->setProperty ("version", 1);
            root->setProperty ("timeBase", timeBase == TimeBase::Time ? "time" : "cycle");
            if (timeBase == TimeBase::Time)
                root->setProperty ("length", lengthSeconds);
            juce::Array<juce::var> array;
            for (const auto& point : points)
            {
                auto* p = new juce::DynamicObject();
                p->setProperty ("x", point.x);
                p->setProperty ("y", point.y);
                if (point.tension != 0.0f)
                    p->setProperty ("tension", point.tension);
                if (point.shape != CurvePoint::Shape::Curve)
                    p->setProperty ("shape", point.shape == CurvePoint::Shape::Smooth ? "smooth" : "hold");
                if (point.marker != 0)
                    p->setProperty ("marker", juce::String::charToString ((juce::juce_wchar) point.marker));
                array.add (juce::var (p));
            }
            root->setProperty ("points", array);
            if (loopStart >= 0.0f && loopEnd > loopStart)
            {
                auto* loop = new juce::DynamicObject();
                loop->setProperty ("start", loopStart);
                loop->setProperty ("end", loopEnd);
                root->setProperty ("loop", juce::var (loop));
            }
            return juce::var (root);
        }

        /** Sorted, clamped to the domain, never empty. */
        void normalise()
        {
            const auto end = domainEnd();
            for (auto& point : points)
                point.x = juce::jlimit (0.0f, end, point.x);
            std::stable_sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.x < b.x; });
            if (points.empty())
                points.push_back ({ 0.0f, 0.0f });
        }
    };

    /** The value along one segment, t in 0..1. */
    inline float shapeSegment (const CurvePoint& from, float toY, float t) noexcept
    {
        switch (from.shape)
        {
            case CurvePoint::Shape::Hold:
                return from.y;
            case CurvePoint::Shape::Smooth:
                return from.y + (toY - from.y) * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * t));
            case CurvePoint::Shape::Curve:
            default:
            {
                const auto c = from.tension * 6.0f;
                const auto shaped = std::abs (c) < 1.0e-4f ? t : (std::exp (c * t) - 1.0f) / (std::exp (c) - 1.0f);
                return from.y + (toY - from.y) * shaped;
            }
        }
    }

    /** Evaluate sorted points at x. Cycle mode wraps (x in 0..1); Time mode
        holds the first/last value outside the points. Works on any point
        array, so the audio thread can call it on a CurveView's points. */
    template <typename PointAt>
    float evaluateCurvePoints (int count, PointAt pointAt, bool cycle, float x) noexcept
    {
        if (count <= 0)
            return 0.0f;
        if (count == 1)
            return pointAt (0).y;

        if (cycle)
        {
            x -= std::floor (x);
            const auto first = pointAt (0);
            const auto last = pointAt (count - 1);
            // The wrap segment runs from the last point into the next period's first.
            if (x < first.x || x >= last.x)
            {
                const auto start = last.x;
                const auto end = first.x + 1.0f;
                const auto pos = x < first.x ? x + 1.0f : x;
                const auto width = end - start;
                return width <= 1.0e-9f ? first.y : shapeSegment (last, first.y, (pos - start) / width);
            }
        }
        else
        {
            if (x <= pointAt (0).x)
                return pointAt (0).y;
            if (x >= pointAt (count - 1).x)
                return pointAt (count - 1).y;
        }

        // Binary search for the segment [i, i+1] holding x (right-continuous).
        int lo = 0, hi = count - 1;
        while (hi - lo > 1)
        {
            const auto mid = (lo + hi) / 2;
            if (pointAt (mid).x <= x)
                lo = mid;
            else
                hi = mid;
        }
        const auto a = pointAt (lo);
        const auto b = pointAt (hi);
        const auto width = b.x - a.x;
        return width <= 1.0e-9f ? b.y : shapeSegment (a, b.y, (x - a.x) / width);
    }

    inline float evaluateCurve (const CurveDocument& doc, float x) noexcept
    {
        return evaluateCurvePoints ((int) doc.points.size(), [&] (int i) { return doc.points[(size_t) i]; },
                                    doc.timeBase == CurveDocument::TimeBase::Cycle, x);
    }

    /** The published `Data(curve)` buffer, read on the audio thread. Layout
        (stride 1): a header, the points (5 floats each: x, y, tension,
        shape, marker), then — Cycle mode only — `numLevels` tables of
        `tableSize` samples: level 0 is the curve exactly as drawn, level l
        keeps only the harmonics up to tableSize/2 >> l, so an oscillator
        picks the level its frequency can play without aliasing (one per
        octave). */
    class CurveView
    {
    public:
        static constexpr float magic = 31415.0f;
        static constexpr int headerSize = 8;
        static constexpr int floatsPerPoint = 5;
        static constexpr int tableOrder = 11;
        static constexpr int tableSize = 1 << tableOrder; // 2048
        static constexpr int numLevels = tableOrder;      // harmonic limits 1024 … 1

        explicit CurveView (const DataBuffer* bufferIn) noexcept : buffer (bufferIn)
        {
            if (buffer == nullptr || buffer->tag() != DataTag::Curve || buffer->rawSize() < (size_t) headerSize
                || buffer->at (0) != magic)
            {
                buffer = nullptr;
                return;
            }
            data = buffer->rawData();
            numPoints = (int) data[2];
            hasTables = data[3] > 0.5f;
        }

        bool isValid() const noexcept { return buffer != nullptr; }
        bool isCycle() const noexcept { return data[1] < 0.5f; }
        float lengthSeconds() const noexcept { return data[4]; }
        float loopStart() const noexcept { return data[5]; }
        float loopEnd() const noexcept { return data[6]; }
        int sustainIndex() const noexcept { return (int) data[7]; }
        int getNumPoints() const noexcept { return numPoints; }
        bool hasBandLimitedTables() const noexcept { return hasTables; }

        CurvePoint point (int index) const noexcept
        {
            const auto* p = data + headerSize + index * floatsPerPoint;
            return { p[0], p[1], p[2], (CurvePoint::Shape) (int) p[3], (char) (int) p[4] };
        }

        /** The curve exactly as drawn. */
        float evaluate (float x) const noexcept
        {
            return evaluateCurvePoints (numPoints, [this] (int i) { return point (i); }, isCycle(), x);
        }

        const float* table (int level) const noexcept
        {
            return data + headerSize + numPoints * floatsPerPoint + juce::jlimit (0, numLevels - 1, level) * tableSize;
        }

        /** The table level for a phase increment (cycles per sample): the
            richest one whose harmonics all stay below Nyquist. */
        static int levelFor (double phaseIncrement) noexcept
        {
            const auto allowed = 0.5 / juce::jmax (1.0e-12, std::abs (phaseIncrement)); // harmonics below Nyquist
            if (allowed >= (double) (tableSize / 2))
                return 0;
            const auto level = (int) std::ceil (std::log2 ((double) (tableSize / 2) / allowed));
            return juce::jlimit (0, numLevels - 1, level);
        }

        /** Linear read of a level at a phase in cycles. */
        float read (int level, double phase) const noexcept
        {
            const auto* t = table (level);
            phase -= std::floor (phase);
            const auto position = phase * (double) tableSize;
            const auto index = (int) position;
            const auto frac = (float) (position - (double) index);
            const auto a = t[index & (tableSize - 1)];
            const auto b = t[(index + 1) & (tableSize - 1)];
            return a + (b - a) * frac;
        }

    private:
        const DataBuffer* buffer = nullptr;
        const float* data = nullptr;
        int numPoints = 0;
        bool hasTables = false;
    };

    /** Message thread: the buffer every consumer of `doc` reads. */
    inline std::unique_ptr<DataBuffer> buildCurveBuffer (const CurveDocument& doc)
    {
        const auto cycle = doc.timeBase == CurveDocument::TimeBase::Cycle;
        const auto numPoints = (int) doc.points.size();
        std::vector<float> values;
        values.reserve ((size_t) (CurveView::headerSize + numPoints * CurveView::floatsPerPoint
                                  + (cycle ? CurveView::numLevels * CurveView::tableSize : 0)));
        values.push_back (CurveView::magic);
        values.push_back (cycle ? 0.0f : 1.0f);
        values.push_back ((float) numPoints);
        values.push_back (cycle ? 1.0f : 0.0f);
        values.push_back (doc.lengthSeconds);
        values.push_back (doc.loopStart);
        values.push_back (doc.loopEnd);
        values.push_back ((float) doc.indexOfMarker ('S'));
        for (const auto& point : doc.points)
        {
            values.push_back (point.x);
            values.push_back (point.y);
            values.push_back (point.tension);
            values.push_back ((float) (int) point.shape);
            values.push_back ((float) (int) point.marker);
        }

        if (cycle)
        {
            constexpr auto n = CurveView::tableSize;
            std::vector<float> raw ((size_t) n);
            for (int i = 0; i < n; ++i)
                raw[(size_t) i] = evaluateCurve (doc, (float) i / (float) n);

            juce::dsp::FFT fft (CurveView::tableOrder);
            std::vector<float> spectrum ((size_t) (2 * n), 0.0f);
            std::copy (raw.begin(), raw.end(), spectrum.begin());
            fft.performRealOnlyForwardTransform (spectrum.data()); // the full spectrum, n (re, im) pairs

            values.insert (values.end(), raw.begin(), raw.end()); // level 0: exactly as drawn
            std::vector<float> work ((size_t) (2 * n));
            for (int level = 1; level < CurveView::numLevels; ++level)
            {
                const auto keep = (n / 2) >> level;
                std::fill (work.begin(), work.end(), 0.0f);
                // Harmonics 0..keep and their negative-frequency mirrors.
                for (int bin = 0; bin < n; ++bin)
                {
                    if (bin > keep && bin < n - keep)
                        continue;
                    work[(size_t) (2 * bin)] = spectrum[(size_t) (2 * bin)];
                    work[(size_t) (2 * bin + 1)] = spectrum[(size_t) (2 * bin + 1)];
                }
                fft.performRealOnlyInverseTransform (work.data());
                values.insert (values.end(), work.begin(), work.begin() + n);
            }
        }

        return std::make_unique<DataBuffer> (DataTag::Curve, std::move (values), 1);
    }
}
