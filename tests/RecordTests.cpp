// wiki/plans/Sampling.md 3a.3: Record — a live Data(sample) a Sampler plays while
// it is being written, in each mode, with no allocation on the audio thread and
// no dependence on the block size.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/RtAllocationTrap.h"
#include "bazalt/engine/nodes/RecordNode.h"
#include "bazalt/engine/nodes/SamplerNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;

namespace
{
    constexpr double rate = 48000.0;
    const float unwired = std::numeric_limits<float>::quiet_NaN();

    struct Moment
    {
        float in = 0.0f, record = unwired, clear = 0.0f;
        float start = 0.0f, position = unwired, speed = unwired;
    };

    /** Record into Sampler, as the compiler runs them: the writer first in each
        block, then the reader. Returns the Sampler's left output. */
    template <typename At>
    std::vector<float> run (nodes::RecordNode& record, nodes::SamplerNode& sampler, int samples, int block, At at,
                            std::vector<float>* positions = nullptr)
    {
        record.prepare ({ rate, block });
        sampler.prepare ({ rate, block });
        sampler.setDataInput ("sample", record.getDataPublisher());
        record.reset();
        sampler.reset();

        std::vector<std::vector<float>> recIn (5, std::vector<float> ((size_t) block)), recOut (3, std::vector<float> ((size_t) block));
        std::vector<std::vector<float>> playIn (9, std::vector<float> ((size_t) block)), playOut (5, std::vector<float> ((size_t) block));
        std::vector<float> out;
        for (int done = 0; done < samples; done += block)
        {
            const auto n = std::min (block, samples - done);
            for (int s = 0; s < n; ++s)
            {
                const auto m = at (done + s);
                const float r[] = { m.in, m.in, m.record, m.clear, unwired };
                for (int i = 0; i < 5; ++i)
                    recIn[(size_t) i][(size_t) s] = r[i];
                const float p[] = { 0.0f, m.start, 0.0f, m.position, m.speed, unwired, unwired, unwired, unwired };
                for (int i = 0; i < 9; ++i)
                    playIn[(size_t) i][(size_t) s] = p[i];
            }
            const float* ri[5];
            float* ro[3];
            const float* pi[9];
            float* po[5];
            for (int i = 0; i < 5; ++i)
                ri[i] = recIn[(size_t) i].data();
            for (int i = 0; i < 3; ++i)
                ro[i] = recOut[(size_t) i].data();
            for (int i = 0; i < 9; ++i)
                pi[i] = playIn[(size_t) i].data();
            for (int i = 0; i < 5; ++i)
                po[i] = playOut[(size_t) i].data();
            {
                ScopedAudioThreadAllocationTrap trap;
                record.processBlock (ri, ro, n);
                sampler.processBlock (pi, po, n);
            }
            out.insert (out.end(), playOut[0].begin(), playOut[0].begin() + n);
            if (positions != nullptr)
                positions->insert (positions->end(), recOut[1].begin(), recOut[1].begin() + n);
        }
        return out;
    }

    float tone (int t) { return (float) std::sin (0.013 * t) * 0.5f + 0.3f * (float) std::sin (0.0021 * t); }
}

TEST_CASE ("A ring keeps the last seconds, and a Sampler plays it live as a steady delay", "[engine][record][3a]")
{
    // A 0.25 s ring (12000 frames), filled four times over; the Sampler starts
    // at its newest end once the ring is full and plays at rate 1.
    for (const auto block : { 64, 512, 97 })
    {
        nodes::RecordNode record;
        record.prepare ({ rate, 512 });
        record.setParameter ("data.record.length", 0.25f);
        record.setParameter ("data.record.channels", 0.0f);
        nodes::SamplerNode sampler;
        sampler.setParameter ("source.sampler.autoStart", 0.0f);
        sampler.setParameter ("source.sampler.interpolation", 0.0f);

        const auto startAt = 15000;
        const auto out = run (record, sampler, 48000, block, [&] (int t)
                              { return Moment { .in = tone (t), .start = t == startAt ? 1.0f : 0.0f, .position = 1.0f }; });

        // A constant delay: the output is the input from `delay` samples ago,
        // everywhere after the start — no glitch at any block boundary.
        int delay = -1;
        for (int d = 0; d < 12000 && delay < 0; ++d)
            if (std::abs (out[(size_t) startAt + 200] - tone (startAt + 200 - d)) < 1.0e-6f
                && std::abs (out[(size_t) startAt + 201] - tone (startAt + 201 - d)) < 1.0e-6f
                && std::abs (out[(size_t) startAt + 900] - tone (startAt + 900 - d)) < 1.0e-6f)
                delay = d;
        INFO ("block " << block << " delay " << delay);
        REQUIRE (delay >= 0);
        CHECK (delay < 600); // "newest" means near the write head (a guard of one block + the interpolation's reach)
        float worst = 0.0f;
        for (int t = startAt + 1; t < 48000; ++t)
            worst = std::max (worst, std::abs (out[(size_t) t] - tone (t - delay)));
        CHECK (worst < 1.0e-6f);
    }
}

TEST_CASE ("A live recording sounds the same at any block size", "[engine][record][3a]")
{
    std::vector<std::vector<float>> renders;
    for (const auto block : { 32, 480, 512 })
    {
        nodes::RecordNode record;
        record.prepare ({ rate, 512 });
        record.setParameter ("data.record.length", 0.2f);
        record.setParameter ("data.record.channels", 0.0f);
        nodes::SamplerNode sampler;
        sampler.setParameter ("source.sampler.autoStart", 0.0f);
        // Starts, scrubs, half speed: every way of reading the ring.
        renders.push_back (run (record, sampler, 40000, block, [] (int t)
                                {
                                    Moment m { .in = tone (t) };
                                    m.start = (t == 11000 || t == 25000) ? 1.0f : 0.0f;
                                    m.position = t < 25000 ? 0.6f : 0.3f;
                                    m.speed = t < 20000 ? 0.5f : (t < 25000 ? 0.0f : 1.0f);
                                    return m;
                                }));
    }
    for (size_t i = 1; i < renders.size(); ++i)
    {
        float worst = 0.0f;
        for (size_t t = 0; t < renders[0].size(); ++t)
            worst = std::max (worst, std::abs (renders[i][t] - renders[0][t]));
        CHECK (worst < 1.0e-5f);
    }
}

TEST_CASE ("Once records one take and stops; Clear starts over", "[engine][record][3a]")
{
    nodes::RecordNode record;
    record.prepare ({ rate, 256 });
    record.setParameter ("data.record.length", 0.1f); // 4800 frames
    record.setParameter ("data.record.channels", 0.0f);
    record.setParameter ("data.record.mode", 1.0f);
    nodes::SamplerNode sampler;
    sampler.setParameter ("source.sampler.autoStart", 0.0f);
    sampler.setParameter ("source.sampler.interpolation", 0.0f);

    std::vector<float> positions;
    // Records 0..4799 (full), plays the take from 6000, clears at 9000 and
    // records again while Record is high from 9500.
    const auto out = run (record, sampler, 16000, 256, [] (int t)
                          {
                              Moment m { .in = (float) t * 1.0e-4f };
                              m.record = t < 9000 || t >= 9500 ? 1.0f : 0.0f;
                              m.clear = t == 9000 ? 1.0f : 0.0f;
                              m.start = t == 6000 ? 1.0f : 0.0f;
                              m.position = 0.0f;
                              return m;
                          }, &positions);
    CHECK (positions[4000] == Catch::Approx (4001.0f / 4800.0f));
    CHECK (positions[5000] == Catch::Approx (1.0f)); // full: stopped
    CHECK (out[6000] == Catch::Approx (0.0f).margin (1.0e-6));
    CHECK (out[6100] == Catch::Approx (100.0e-4f).margin (1.0e-5)); // the take from its start
    CHECK (positions[9200] == Catch::Approx (0.0f));
    CHECK (positions[9600] == Catch::Approx (101.0f / 4800.0f)); // a new take
}

TEST_CASE ("Loop: the first take sets the length, the next passes overdub", "[engine][record][3a]")
{
    nodes::RecordNode record;
    record.prepare ({ rate, 128 });
    record.setParameter ("data.record.channels", 0.0f);
    record.setParameter ("data.record.mode", 2.0f);
    nodes::SamplerNode sampler;
    sampler.setParameter ("source.sampler.autoStart", 0.0f);
    sampler.setParameter ("source.sampler.interpolation", 0.0f);
    sampler.setParameter ("source.sampler.loop", 1.0f);

    // Take 1: 1000 frames of 0.25. Then a gap, then one overdub pass of 0.5.
    const auto out = run (record, sampler, 5000, 128, [] (int t)
                          {
                              Moment m;
                              m.in = t < 1000 ? 0.25f : (t >= 2000 && t < 3000 ? 0.5f : 0.0f);
                              m.record = t < 1000 || (t >= 2000 && t < 3000) ? 1.0f : 0.0f;
                              m.start = t == 3500 ? 1.0f : 0.0f;
                              m.position = 0.0f;
                              return m;
                          });
    // The loop is 1000 frames; the pass from 2000 to 3000 covered it exactly once.
    CHECK (out[3600] == Catch::Approx (0.75f));
    CHECK (out[4700] == Catch::Approx (0.75f)); // looping
}

TEST_CASE ("Record keeps a waveform of what it holds for its body", "[engine][record][3a]")
{
    nodes::RecordNode record;
    record.prepare ({ rate, 256 });
    record.setParameter ("data.record.length", 1.0f);
    record.setParameter ("data.record.channels", 0.0f);
    nodes::SamplerNode sampler;
    // A quarter of a second of +0.5, then nothing recorded.
    run (record, sampler, 12000, 256, [] (int t) { return Moment { .in = 0.5f, .record = t < 12000 ? 1.0f : 0.0f }; });
    const auto view = record.getView();
    REQUIRE (view.peaks.size() == (size_t) nodes::RecordNode::overviewBuckets * 2);
    CHECK (view.filled == Catch::Approx (0.25f));
    CHECK (view.head == Catch::Approx (0.25f));
    CHECK_FALSE (view.wrapped);
    CHECK (view.peaks[1] == Catch::Approx (0.5f));                            // first bucket's max
    CHECK (view.peaks[(size_t) nodes::RecordNode::overviewBuckets] == 0.0f);  // the middle: nothing yet
}
