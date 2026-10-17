#pragma once

#include "bazalt/engine/patch/ExactFloat.h"
#include "bazalt/engine/graph/Data.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace bazalt::engine
{
    /** A decoded sample, as the library holds it (wiki/plans/Sampling.md §2):
        interleaved frames, any channel count, its own sample rate. Immutable
        once made; shared by every node and patch that refers to it by `id`
        (a content hash of the file it came from). Decoding happens outside
        the engine (the plugin's SampleLibrary — the engine has no audio file
        formats, CLAUDE.md rule 4); the engine only sees floats. */
    struct SampleSource
    {
        juce::String id;
        juce::String name;
        int channels = 1;
        int frames = 0;
        double sampleRate = 48000.0;
        std::vector<float> data; // interleaved, frames × channels

        float at (int frame, int channel) const noexcept { return data[(size_t) frame * (size_t) channels + (size_t) channel]; }
        double seconds() const noexcept { return sampleRate > 0.0 ? (double) frames / sampleRate : 0.0; }
    };

    /** Every sample loaded in this process, by id. Shared by all plugin
        instances (a sample is immutable, so sharing is safe and saves
        memory). Message / worker threads only — never the audio thread. */
    class SampleStore
    {
    public:
        static SampleStore& instance()
        {
            static SampleStore store;
            return store;
        }

        void add (std::shared_ptr<const SampleSource> source)
        {
            if (source == nullptr)
                return;
            const std::lock_guard<std::mutex> lock (mutex);
            sources[source->id] = std::move (source);
        }

        std::shared_ptr<const SampleSource> find (const juce::String& id) const
        {
            const std::lock_guard<std::mutex> lock (mutex);
            const auto it = sources.find (id);
            return it == sources.end() ? nullptr : it->second;
        }

        bool contains (const juce::String& id) const { return find (id) != nullptr; }

        /** Tests only. */
        void clearForTesting()
        {
            const std::lock_guard<std::mutex> lock (mutex);
            sources.clear();
        }

    private:
        mutable std::mutex mutex;
        std::map<juce::String, std::shared_ptr<const SampleSource>> sources;
    };

    /** FNV-1a 64 over bytes, as 16 hex digits: a sample's id. */
    inline juce::String sampleIdForBytes (const void* bytes, size_t size)
    {
        uint64_t hash = 14695981039346656037ull;
        const auto* p = static_cast<const uint8_t*> (bytes);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= p[i];
            hash *= 1099511628211ull;
        }
        return juce::String::toHexString ((juce::int64) hash).paddedLeft ('0', 16);
    }

    /** What a Sample node does with its sample, non-destructively — the
        node's content. Times are seconds in the source (so a different sample
        rate can't move them). */
    struct SampleRegion
    {
        juce::String sampleId;
        juce::String name;
        double start = 0.0;
        double end = 0.0; // 0: the end of the sample
        bool loop = false;
        double loopStart = 0.0;
        double loopEnd = 0.0; // 0: the end of the region
        float gainDb = 0.0f;
        bool reverse = false;
        bool normalise = false;
        double fadeIn = 0.0, fadeOut = 0.0;
        std::vector<double> slices; // seconds from the region's start
        float rootPitch = std::numeric_limits<float>::quiet_NaN(); // the sample's own note (detected or set)
        int quantity = -1; // what the channels mean (a Quantity ordinal — a captured pitch is a Pitch); -1: audio

        static SampleRegion fromVar (const juce::var& content)
        {
            SampleRegion r;
            if (! content.isObject())
                return r;
            const auto num = [&content] (const char* key, double fallback)
            {
                const auto v = content.getProperty (key, {});
                return v.isDouble() || v.isInt() || v.isInt64() ? (double) v : fallback;
            };
            r.sampleId = content.getProperty ("sample", "").toString();
            r.name = content.getProperty ("name", "").toString();
            r.start = juce::jmax (0.0, num ("start", 0.0));
            r.end = juce::jmax (0.0, num ("end", 0.0));
            r.loop = (bool) content.getProperty ("loop", false);
            r.loopStart = juce::jmax (0.0, num ("loopStart", 0.0));
            r.loopEnd = juce::jmax (0.0, num ("loopEnd", 0.0));
            r.gainDb = (float) num ("gain", 0.0);
            r.reverse = (bool) content.getProperty ("reverse", false);
            r.normalise = (bool) content.getProperty ("normalise", false);
            r.fadeIn = juce::jmax (0.0, num ("fadeIn", 0.0));
            r.fadeOut = juce::jmax (0.0, num ("fadeOut", 0.0));
            if (const auto root = content.getProperty ("root", {}); root.isDouble() || root.isInt())
                r.rootPitch = (float) (double) root;
            if (const auto q = content.getProperty ("quantity", {}); q.isInt() || q.isDouble() || q.isInt64())
                r.quantity = (int) q;
            if (const auto* slices = content.getProperty ("slices", {}).getArray())
                for (const auto& s : *slices)
                    r.slices.push_back ((double) s);
            return r;
        }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("sample", sampleId);
            o->setProperty ("name", name);
            o->setProperty ("start", start);
            o->setProperty ("end", end);
            o->setProperty ("loop", loop);
            o->setProperty ("loopStart", loopStart);
            o->setProperty ("loopEnd", loopEnd);
            o->setProperty ("gain", exactFloatVar (gainDb));
            o->setProperty ("reverse", reverse);
            o->setProperty ("normalise", normalise);
            o->setProperty ("fadeIn", fadeIn);
            o->setProperty ("fadeOut", fadeOut);
            juce::Array<juce::var> s;
            for (const auto v : slices)
                s.add (v);
            o->setProperty ("slices", s);
            if (std::isfinite (rootPitch))
                o->setProperty ("root", exactFloatVar (rootPitch));
            if (quantity >= 0)
                o->setProperty ("quantity", quantity);
            return juce::var (o);
        }
    };

    /** Fills `buffer.sampleMeta.mipLevels`: each level is the one before,
        low-passed at a quarter of its rate (a 31-tap windowed-sinc half-band)
        and decimated by 2. Up to 4 levels — a sample played 16× too fast
        still doesn't alias. Message / worker thread. */
    inline void buildSampleMipLevels (DataBuffer& buffer, int maxLevels = 4)
    {
        constexpr int taps = 31;
        static const auto kernel = []
        {
            std::array<float, taps> k {};
            double sum = 0.0;
            for (int i = 0; i < taps; ++i)
            {
                const auto x = (double) (i - taps / 2);
                const auto sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::halfPi * x) / (juce::MathConstants<double>::halfPi * x);
                const auto window = 0.42 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (taps - 1))
                                    + 0.08 * std::cos (2.0 * juce::MathConstants<double>::twoPi * i / (taps - 1));
                k[(size_t) i] = (float) (sinc * window);
                sum += sinc * window;
            }
            for (auto& v : k)
                v = (float) (v / sum);
            return k;
        }();

        auto& levels = buffer.sampleMeta.mipLevels;
        levels.clear();
        const auto channels = buffer.channels();
        const float* previous = buffer.rawData();
        auto previousFrames = buffer.frames();
        for (int level = 0; level < maxLevels && previousFrames >= 64; ++level)
        {
            const auto frames = previousFrames / 2;
            std::vector<float> next ((size_t) frames * (size_t) channels);
            for (int f = 0; f < frames; ++f)
                for (int c = 0; c < channels; ++c)
                {
                    float sum = 0.0f;
                    for (int t = 0; t < taps; ++t)
                        if (const auto from = f * 2 + t - taps / 2; from >= 0 && from < previousFrames)
                            sum += previous[(size_t) from * (size_t) channels + (size_t) c] * kernel[(size_t) t];
                    next[(size_t) f * (size_t) channels + (size_t) c] = sum;
                }
            levels.push_back (std::move (next));
            previous = levels.back().data();
            previousFrames = frames;
        }
    }

    /** The region of `source` as a published Data(sample) buffer: trimmed,
        reversed, faded, gained, normalised; loop and slices as frame markers
        relative to the region. Every channel is `audioQuantity`, or the
        region's own quantity when it has one (a capture). An empty source (a
        missing sample) gives one silent frame, so a reader never divides by
        zero. Message / worker thread. */
    inline std::unique_ptr<DataBuffer> buildSampleBuffer (const SampleSource* source, const SampleRegion& region,
                                                          uint8_t audioQuantity)
    {
        if (source == nullptr || source->frames <= 0)
        {
            auto silent = std::make_unique<DataBuffer> (DataTag::Sample, std::vector<float> (1, 0.0f), 1, 48000.0);
            silent->sampleMeta.channelQuantities = { audioQuantity };
            return silent;
        }

        const auto sr = source->sampleRate;
        const auto channels = source->channels;
        const auto first = juce::jlimit (0, source->frames - 1, (int) std::lround (region.start * sr));
        auto last = region.end > 0.0 ? (int) std::lround (region.end * sr) : source->frames;
        last = juce::jlimit (first + 1, source->frames, last);
        const auto frames = last - first;

        std::vector<float> values ((size_t) frames * (size_t) channels);
        for (int f = 0; f < frames; ++f)
        {
            const auto from = region.reverse ? last - 1 - f : first + f;
            for (int c = 0; c < channels; ++c)
                values[(size_t) f * (size_t) channels + (size_t) c] = source->at (from, c);
        }

        auto gain = juce::Decibels::decibelsToGain (region.gainDb, -120.0f);
        if (region.normalise)
        {
            float peak = 0.0f;
            for (const auto v : values)
                peak = juce::jmax (peak, std::abs (v));
            if (peak > 1.0e-6f)
                gain /= peak;
        }
        const auto fadeInFrames = juce::jmin (frames, (int) std::lround (region.fadeIn * sr));
        const auto fadeOutFrames = juce::jmin (frames, (int) std::lround (region.fadeOut * sr));
        for (int f = 0; f < frames; ++f)
        {
            auto g = gain;
            if (f < fadeInFrames)
                g *= (float) f / (float) fadeInFrames;
            if (const auto toEnd = frames - 1 - f; toEnd < fadeOutFrames)
                g *= (float) toEnd / (float) fadeOutFrames;
            for (int c = 0; c < channels; ++c)
                values[(size_t) f * (size_t) channels + (size_t) c] *= g;
        }

        auto buffer = std::make_unique<DataBuffer> (DataTag::Sample, std::move (values), channels, sr);
        buffer->sampleMeta.channelQuantities.assign ((size_t) channels, region.quantity >= 0 ? (uint8_t) region.quantity : audioQuantity);
        if (region.loop)
        {
            const auto loopStart = juce::jlimit (0, frames - 1, (int) std::lround (region.loopStart * sr));
            const auto loopEnd = region.loopEnd > 0.0 ? juce::jlimit (loopStart + 1, frames, (int) std::lround (region.loopEnd * sr)) : frames;
            buffer->sampleMeta.loopStart = loopStart;
            buffer->sampleMeta.loopEnd = loopEnd;
        }
        buildSampleMipLevels (*buffer);
        if (std::isfinite (region.rootPitch))
            buffer->sampleMeta.rootPitch = region.rootPitch;
        for (const auto s : region.slices)
            if (const auto frame = (int) std::lround (s * sr); frame >= 0 && frame < frames)
                buffer->sampleMeta.slices.push_back (frame);
        std::sort (buffer->sampleMeta.slices.begin(), buffer->sampleMeta.slices.end());
        return buffer;
    }
}
