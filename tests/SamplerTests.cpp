// wiki/plans/Sampling.md 3a.2: the Sampler — plays a Data(sample) at its own
// rate, at a pitch, by slice, looped, scrubbed, without aliasing when sped up.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/SampleData.h"
#include "bazalt/engine/nodes/SamplerNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;

namespace
{
    constexpr double rate = 48000.0;
    const float unwired = std::numeric_limits<float>::quiet_NaN();

    /** A published sample: mono `values` at `sampleRate`. */
    struct Published
    {
        DataPublisher publisher;
        explicit Published (std::vector<float> values, double sampleRate = rate, DataBuffer::SampleMeta meta = {})
        {
            auto buffer = std::make_unique<DataBuffer> (DataTag::Sample, std::move (values), 1, sampleRate);
            buffer->sampleMeta = std::move (meta);
            buildSampleMipLevels (*buffer);
            publisher.publish (std::move (buffer));
        }
    };

    struct Inputs
    {
        float start = 0.0f, stop = 0.0f, position = unwired, speed = unwired, pitch = unwired, slice = unwired, channel = unwired, gate = unwired;
    };

    /** Renders `samples` frames in blocks of `block`; `at` changes inputs per sample. */
    template <typename At>
    std::vector<float> render (nodes::SamplerNode& node, int samples, int block, At at)
    {
        node.prepare ({ rate, block });
        node.reset();
        std::vector<float> out;
        std::vector<std::vector<float>> ins (9, std::vector<float> ((size_t) block));
        std::vector<std::vector<float>> outs (5, std::vector<float> ((size_t) block));
        for (int done = 0; done < samples; done += block)
        {
            const auto n = std::min (block, samples - done);
            for (int s = 0; s < n; ++s)
            {
                const auto v = at (done + s);
                const float values[] = { 0.0f, v.start, v.stop, v.position, v.speed, v.pitch, v.slice, v.channel, v.gate };
                for (int i = 0; i < 9; ++i)
                    ins[(size_t) i][(size_t) s] = values[i];
            }
            const float* inPtrs[9];
            float* outPtrs[5];
            for (int i = 0; i < 9; ++i)
                inPtrs[i] = ins[(size_t) i].data();
            for (int o = 0; o < 5; ++o)
                outPtrs[o] = outs[(size_t) o].data();
            node.processBlock (inPtrs, outPtrs, n);
            out.insert (out.end(), outs[0].begin(), outs[0].begin() + n);
        }
        return out;
    }

    std::vector<float> ramp (int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) i / (float) n;
        return v;
    }

    std::vector<float> sine (double hz, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate);
        return v;
    }

    int crossings (const std::vector<float>& v, size_t from, size_t to)
    {
        int count = 0;
        for (size_t i = from + 1; i < to; ++i)
            count += (v[i - 1] < 0.0f) != (v[i] < 0.0f);
        return count;
    }

    double rms (const std::vector<float>& v, size_t from, size_t to)
    {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i)
            sum += (double) v[i] * v[i];
        return std::sqrt (sum / (double) (to - from));
    }
}

TEST_CASE ("The Sampler plays a sample at its own rate, then stops", "[engine][sampler][3a]")
{
    Published sample (ramp (1000));
    nodes::SamplerNode node;
    node.setParameter ("source.sampler.interpolation", 0.0f);
    node.setDataInput ("sample", &sample.publisher);
    const auto out = render (node, 1200, 64, [] (int) { return Inputs {}; });
    for (int i = 0; i < 1000; i += 97)
        CHECK (out[(size_t) i] == Catch::Approx ((float) i / 1000.0f).margin (1.0e-6));
    CHECK (out[1100] == 0.0f); // ended
}

TEST_CASE ("The Sampler follows Pitch against the root, reads slices, and loops", "[engine][sampler][3a]")
{
    Published tone (sine (441.0, 48000));
    nodes::SamplerNode node;
    node.setDataInput ("sample", &tone.publisher);
    auto out = render (node, 24000, 128, [] (int) { Inputs in; in.pitch = 72.0f; return in; });
    CHECK (crossings (out, 0, 24000) == Catch::Approx (882).margin (3)); // an octave up: 882 Hz over 0.5 s

    DataBuffer::SampleMeta meta;
    meta.slices = { 250, 500, 750 };
    Published sliced (ramp (1000), rate, meta);
    nodes::SamplerNode slicer;
    slicer.setParameter ("source.sampler.interpolation", 0.0f);
    slicer.setDataInput ("sample", &sliced.publisher);
    out = render (slicer, 400, 64, [] (int s) { Inputs in; in.slice = 2.0f; in.start = s == 0 ? 1.0f : 0.0f; return in; });
    CHECK (out[0] == Catch::Approx (0.5f));    // slice 2 starts at 500
    CHECK (out[249] == Catch::Approx (0.749f));
    CHECK (out[260] == 0.0f);                   // and ends at the next slice

    DataBuffer::SampleMeta looped;
    looped.loopStart = 100;
    looped.loopEnd = 200;
    Published loop (ramp (1000), rate, looped);
    nodes::SamplerNode looper;
    looper.setParameter ("source.sampler.interpolation", 0.0f);
    looper.setParameter ("source.sampler.loop", 1.0f);
    looper.setDataInput ("sample", &loop.publisher);
    out = render (looper, 3000, 64, [] (int) { return Inputs {}; });
    CHECK (out[150] == Catch::Approx (0.15f));
    CHECK (out[250] == Catch::Approx (0.15f)); // 250 → 150 inside the loop
    CHECK (out[2950] != 0.0f);                  // still playing
}

TEST_CASE ("At Rate 0 the Sampler scrubs: Position is the read head", "[engine][sampler][3a]")
{
    Published sample (ramp (1000));
    nodes::SamplerNode node;
    node.setParameter ("source.sampler.interpolation", 0.0f);
    node.setDataInput ("sample", &sample.publisher);
    const auto out = render (node, 100, 32, [] (int s) { Inputs in; in.speed = 0.0f; in.position = (float) s / 200.0f; return in; });
    CHECK (out[0] == Catch::Approx (0.0f));
    CHECK (out[50] == Catch::Approx (0.25f).margin (1.0e-3));
    CHECK (out[99] == Catch::Approx (0.495f).margin (1.0e-3));
}

TEST_CASE ("Played four times too fast, a sample doesn't alias", "[engine][sampler][3a]")
{
    // 9 kHz played 4× is 36 kHz — above Nyquist. Without the band-limited copy
    // it folds back loud (to 12 kHz); with it, it's nearly gone.
    Published tone (sine (9000.0, 48000));
    nodes::SamplerNode node;
    node.setDataInput ("sample", &tone.publisher);
    const auto out = render (node, 10000, 256, [] (int) { Inputs in; in.speed = 4.0f; return in; });
    CHECK (rms (out, 1000, 10000) < 0.03); // vs. 0.707 if it aliased

    // A 1 kHz tone played 4× (4 kHz) passes untouched.
    Published low (sine (1000.0, 48000));
    nodes::SamplerNode clean;
    clean.setDataInput ("sample", &low.publisher);
    const auto pass = render (clean, 10000, 256, [] (int) { Inputs in; in.speed = 4.0f; return in; });
    CHECK (rms (pass, 1000, 10000) > 0.6);
}

TEST_CASE ("The Sampler doesn't depend on the block size", "[engine][sampler][3a]")
{
    Published tone (sine (300.0, 20000));
    const auto play = [&tone] (int block)
    {
        nodes::SamplerNode node;
        node.setParameter ("source.sampler.interpolation", 2.0f);
        node.setDataInput ("sample", &tone.publisher);
        return render (node, 20000, block, [] (int s) { Inputs in; in.speed = 1.3f; in.start = s == 3000 ? 1.0f : 0.0f; return in; });
    };
    CHECK (play (64) == play (37));
}
