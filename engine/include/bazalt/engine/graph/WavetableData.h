#pragma once

#include "bazalt/engine/graph/CurveData.h"
#include <list>
#include <mutex>
#include <utility>

namespace bazalt::engine
{
    /** A wavetable (wiki/plans/DataAndWavetable.md 1c, D7): the content of
        `data.wavetable`. Keyframes at positions 0..1 along the table, each
        one cycle, either
        - **curve**: drawn exactly like a data.curve in Cycle mode, or
        - **harmonics**: a spectrum — amplitude (0..1) and phase (cycles) per
          harmonic, harmonic 1 first; scaled down if its peak would pass 1.
        Between keyframes the table **morphs** (a crossfade between the two
        around the frame) or **steps** (the keyframe at or below the frame,
        the classic hard-switching wavetable).

        The published `Data(wavetable)` buffer holds every keyframe as the
        same band-limited mipmap a curve gets (CurveView's levels), so the
        Oscillator reads it with the level its frequency can play without
        aliasing. The frame itself is not in the buffer — it is a live signal
        (the node's Frame input) riding beside it as the publisher's companion.
    */
    struct WavetableKeyframe
    {
        enum class Kind { Curve, Harmonics };

        float position = 0.0f;
        Kind kind = Kind::Curve;
        CurveDocument curve = CurveDocument::sine();
        std::vector<float> amplitudes; // harmonics 1..N
        std::vector<float> phases;     // cycles, same length or shorter (missing: 0)

        /** One cycle, CurveView::tableSize samples, before band-limiting. */
        std::vector<float> renderCycle() const
        {
            constexpr auto n = CurveView::tableSize;
            std::vector<float> raw ((size_t) n, 0.0f);
            if (kind == Kind::Curve)
            {
                for (int i = 0; i < n; ++i)
                    raw[(size_t) i] = evaluateCurve (curve, (float) i / (float) n);
                return raw;
            }

            const auto count = juce::jmin ((int) amplitudes.size(), n / 2);
            for (int h = 0; h < count; ++h)
            {
                const auto amplitude = amplitudes[(size_t) h];
                if (std::abs (amplitude) < 1.0e-6f)
                    continue;
                const auto phase = h < (int) phases.size() ? (double) phases[(size_t) h] : 0.0;
                const auto harmonic = (double) (h + 1);
                for (int i = 0; i < n; ++i)
                    raw[(size_t) i] += amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * (harmonic * (double) i / (double) n + phase));
            }
            float peak = 0.0f;
            for (const auto v : raw)
                peak = juce::jmax (peak, std::abs (v));
            if (peak > 1.0f)
                for (auto& v : raw)
                    v /= peak;
            return raw;
        }

        juce::var toVar() const
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("position", position);
            object->setProperty ("kind", kind == Kind::Curve ? "curve" : "harmonics");
            if (kind == Kind::Curve)
                object->setProperty ("curve", curve.toVar());
            else
            {
                juce::Array<juce::var> a, p;
                for (const auto v : amplitudes)
                    a.add (v);
                for (const auto v : phases)
                    p.add (v);
                object->setProperty ("amplitudes", a);
                object->setProperty ("phases", p);
            }
            return juce::var (object);
        }

        static WavetableKeyframe fromVar (const juce::var& v)
        {
            WavetableKeyframe frame;
            frame.position = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("position", 0.0));
            if (! std::isfinite (frame.position))
                frame.position = 0.0f;
            if (v.getProperty ("kind", "curve").toString() == "harmonics")
            {
                frame.kind = Kind::Harmonics;
                auto readArray = [] (const juce::var& array, float lo, float hi)
                {
                    std::vector<float> out;
                    if (auto* items = array.getArray())
                        for (const auto& item : *items)
                        {
                            const auto value = (float) (double) item;
                            out.push_back (std::isfinite (value) ? juce::jlimit (lo, hi, value) : 0.0f);
                        }
                    return out;
                };
                frame.amplitudes = readArray (v.getProperty ("amplitudes", {}), -1.0f, 1.0f);
                frame.phases = readArray (v.getProperty ("phases", {}), -1.0f, 1.0f);
            }
            else
            {
                auto curve = v.getProperty ("curve", {});
                frame.curve = curve.isObject() ? CurveDocument::fromVar (curve, CurveDocument::TimeBase::Cycle) : CurveDocument::sine();
                frame.curve.timeBase = CurveDocument::TimeBase::Cycle; // a keyframe is always one cycle
                frame.curve.normalise();
            }
            return frame;
        }

        static WavetableKeyframe ofCurve (float position, CurveDocument curve)
        {
            WavetableKeyframe frame;
            frame.position = position;
            frame.curve = std::move (curve);
            return frame;
        }

        static WavetableKeyframe ofHarmonics (float position, std::vector<float> amplitudes, std::vector<float> phases = {})
        {
            WavetableKeyframe frame;
            frame.position = position;
            frame.kind = Kind::Harmonics;
            frame.amplitudes = std::move (amplitudes);
            frame.phases = std::move (phases);
            return frame;
        }
    };

    struct WavetableDocument
    {
        enum class Interpolation { Morph, Step };

        Interpolation interpolation = Interpolation::Morph;
        std::vector<WavetableKeyframe> keyframes; // sorted by position, never empty after normalise()

        /** The default table: a sine morphing into a saw. */
        static WavetableDocument sineToSaw()
        {
            WavetableDocument doc;
            doc.keyframes = { WavetableKeyframe::ofCurve (0.0f, CurveDocument::sine()), WavetableKeyframe::ofCurve (1.0f, CurveDocument::saw()) };
            return doc;
        }

        static WavetableDocument fromVar (const juce::var& content)
        {
            if (! content.isObject())
                return sineToSaw();
            WavetableDocument doc;
            doc.interpolation = content.getProperty ("interpolation", "morph").toString() == "step" ? Interpolation::Step : Interpolation::Morph;
            if (auto* array = content.getProperty ("keyframes", {}).getArray())
                for (const auto& frame : *array)
                    if (frame.isObject())
                        doc.keyframes.push_back (WavetableKeyframe::fromVar (frame));
            doc.normalise();
            return doc;
        }

        juce::var toVar() const
        {
            auto* root = new juce::DynamicObject();
            root->setProperty ("schema", "wavetable");
            root->setProperty ("version", 1);
            root->setProperty ("interpolation", interpolation == Interpolation::Step ? "step" : "morph");
            juce::Array<juce::var> array;
            for (const auto& frame : keyframes)
                array.add (frame.toVar());
            root->setProperty ("keyframes", array);
            return juce::var (root);
        }

        void normalise()
        {
            std::stable_sort (keyframes.begin(), keyframes.end(), [] (const auto& a, const auto& b) { return a.position < b.position; });
            if (keyframes.empty())
                keyframes.push_back (WavetableKeyframe::ofCurve (0.0f, CurveDocument::sine()));
        }
    };

    /** The published `Data(wavetable)` buffer, read on the audio thread.
        Layout (stride 1): a header, the keyframe positions, then per
        keyframe `CurveView::numLevels` tables of `CurveView::tableSize`. */
    class WavetableView
    {
    public:
        static constexpr float magic = 27182.0f;
        static constexpr int headerSize = 4;
        static constexpr int levelsPerFrame = CurveView::numLevels * CurveView::tableSize;

        explicit WavetableView (const DataBuffer* bufferIn) noexcept
        {
            if (bufferIn == nullptr || bufferIn->tag() != DataTag::Wavetable || bufferIn->rawSize() < (size_t) headerSize
                || bufferIn->at (0) != magic)
                return;
            data = bufferIn->rawData();
            numFrames = (int) data[1];
            step = data[2] > 0.5f;
            if (numFrames < 1 || bufferIn->rawSize() < (size_t) (headerSize + numFrames + numFrames * levelsPerFrame))
                data = nullptr;
        }

        bool isValid() const noexcept { return data != nullptr; }
        int getNumKeyframes() const noexcept { return numFrames; }
        bool isStepped() const noexcept { return step; }
        float position (int frame) const noexcept { return data[headerSize + frame]; }

        const float* table (int frame, int level) const noexcept
        {
            return data + headerSize + numFrames + frame * levelsPerFrame + juce::jlimit (0, CurveView::numLevels - 1, level) * CurveView::tableSize;
        }

        /** The two keyframes around `frame` (0..1) and the mix between them. */
        void locate (float frame, int& lower, int& upper, float& mix) const noexcept
        {
            frame = std::isfinite (frame) ? juce::jlimit (0.0f, 1.0f, frame) : 0.0f;
            lower = 0;
            while (lower + 1 < numFrames && position (lower + 1) <= frame)
                ++lower;
            upper = juce::jmin (lower + 1, numFrames - 1);
            const auto from = position (lower), to = position (upper);
            mix = (upper == lower || step || frame <= from) ? 0.0f : juce::jlimit (0.0f, 1.0f, (frame - from) / juce::jmax (1.0e-6f, to - from));
        }

        /** Linear read at `phase` (cycles) and `frame` (0..1), band-limited
            to `level` (CurveView::levelFor). */
        float read (int level, double phase, float frame) const noexcept
        {
            int lower = 0, upper = 0;
            float mix = 0.0f;
            locate (frame, lower, upper, mix);
            phase -= std::floor (phase);
            const auto positionInTable = phase * (double) CurveView::tableSize;
            const auto index = (int) positionInTable;
            const auto frac = (float) (positionInTable - (double) index);
            auto sample = [&] (int keyframe)
            {
                const auto* t = table (keyframe, level);
                const auto a = t[index & (CurveView::tableSize - 1)];
                const auto b = t[(index + 1) & (CurveView::tableSize - 1)];
                return a + (b - a) * frac;
            };
            const auto a = sample (lower);
            return mix <= 0.0f ? a : a + (sample (upper) - a) * mix;
        }

        /** The table as drawn (level 0) at `phase`, without interpolating
            across the cycle's wrap — what a finished one-shot holds. */
        float readHeld (double phase, float frame) const noexcept
        {
            int lower = 0, upper = 0;
            float mix = 0.0f;
            locate (frame, lower, upper, mix);
            phase -= std::floor (phase);
            const auto index = juce::jmin (CurveView::tableSize - 1, (int) (phase * (double) CurveView::tableSize));
            const auto a = table (lower, 0)[index];
            return mix <= 0.0f ? a : a + (table (upper, 0)[index] - a) * mix;
        }

    private:
        const float* data = nullptr;
        int numFrames = 0;
        bool step = false;
    };

    /** Message thread: one keyframe's mipmap, cached by its content. Every
        voice copy of a wavetable builds the same table, and a live edit
        changes one keyframe at a time — the cache makes both a copy. */
    inline std::shared_ptr<const std::vector<float>> keyframeMipmap (const WavetableKeyframe& frame)
    {
        static std::mutex mutex;
        static std::list<std::pair<juce::String, std::shared_ptr<const std::vector<float>>>> cache; // most recent first
        constexpr size_t capacity = 64;

        auto key = juce::JSON::toString (frame.toVar(), true);
        key = key.fromFirstOccurrenceOf ("\"kind\"", true, false); // the position does not change the table
        {
            const std::lock_guard<std::mutex> lock (mutex);
            for (auto it = cache.begin(); it != cache.end(); ++it)
                if (it->first == key)
                {
                    cache.splice (cache.begin(), cache, it);
                    return cache.front().second;
                }
        }

        auto levels = std::make_shared<std::vector<float>>();
        levels->reserve ((size_t) WavetableView::levelsPerFrame);
        appendMipmapLevels (frame.renderCycle(), *levels);
        std::shared_ptr<const std::vector<float>> built = std::move (levels);

        const std::lock_guard<std::mutex> lock (mutex);
        cache.emplace_front (std::move (key), built);
        if (cache.size() > capacity)
            cache.pop_back();
        return built;
    }

    /** Message thread: the buffer every consumer of `doc` reads. */
    inline std::unique_ptr<DataBuffer> buildWavetableBuffer (const WavetableDocument& doc)
    {
        const auto count = (int) doc.keyframes.size();
        std::vector<float> values;
        values.reserve ((size_t) (WavetableView::headerSize + count + count * WavetableView::levelsPerFrame));
        values.push_back (WavetableView::magic);
        values.push_back ((float) count);
        values.push_back (doc.interpolation == WavetableDocument::Interpolation::Step ? 1.0f : 0.0f);
        values.push_back (0.0f);
        for (const auto& frame : doc.keyframes)
            values.push_back (frame.position);
        for (const auto& frame : doc.keyframes)
        {
            const auto levels = keyframeMipmap (frame);
            values.insert (values.end(), levels->begin(), levels->end());
        }
        return std::make_unique<DataBuffer> (DataTag::Wavetable, std::move (values), 1);
    }
}
