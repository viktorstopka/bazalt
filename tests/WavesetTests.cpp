// wiki/plans/Wavesets.md W1: Wavesets cuts a sound at its zero crossings into a
// live Data(wavesets); Waveset Play interleaves whole wavesets of several, live.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/RtAllocationTrap.h"
#include "bazalt/engine/nodes/CycleNodes.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;

namespace
{
    constexpr double rate = 48000.0;
    const float unwired = std::numeric_limits<float>::quiet_NaN();

    float sineA (int t) { return (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * t / rate); } // 240-sample cycles
    float sawB (int t) { return (float) (2.0 * std::fmod (t / 150.0, 1.0) - 1.0) * 0.8f; }                   // 150-sample cycles

    struct Rendered
    {
        std::vector<float> out, switches, source;
    };

    /** Two Wavesets into one Play, in blocks of `block`: the captures run first,
        as the compiler orders them. */
    Rendered interleave (int samples, int block, float count = unwired)
    {
        nodes::CycleCutsNode a, b;
        nodes::WeaveNode play;
        play.setGroupPortCount (2);
        for (auto* capture : { &a, &b })
            capture->prepare ({ rate, block });
        play.prepare ({ rate, block });
        play.setDataInput ("in.0", a.getDataPublisher());
        play.setDataInput ("in.1", b.getDataPublisher());
        play.reset();

        Rendered r;
        std::vector<float> inA ((size_t) block), inB ((size_t) block), countIn ((size_t) block, count), zero ((size_t) block, 0.0f);
        std::vector<std::vector<float>> capOut (5, std::vector<float> ((size_t) block)), playOut (3, std::vector<float> ((size_t) block));
        for (int done = 0; done < samples; done += block)
        {
            const auto n = std::min (block, samples - done);
            for (int s = 0; s < n; ++s)
            {
                inA[(size_t) s] = sineA (done + s);
                inB[(size_t) s] = sawB (done + s);
            }
            float* co[5];
            for (int o = 0; o < 5; ++o)
                co[o] = capOut[(size_t) o].data();
            float* po[3];
            for (int o = 0; o < 3; ++o)
                po[o] = playOut[(size_t) o].data();
            const float* ia[] = { inA.data() };
            const float* ib[] = { inB.data() };
            const float* pi[] = { zero.data(), zero.data(), countIn.data() };
            {
                ScopedAudioThreadAllocationTrap trap;
                a.processBlock (ia, co, n);
                b.processBlock (ib, co, n);
                play.processBlock (pi, po, n);
            }
            r.out.insert (r.out.end(), playOut[0].begin(), playOut[0].begin() + n);
            r.switches.insert (r.switches.end(), playOut[1].begin(), playOut[1].begin() + n);
            r.source.insert (r.source.end(), playOut[2].begin(), playOut[2].begin() + n);
        }
        return r;
    }
}

TEST_CASE ("Wavesets finds each cycle at its upward zero crossing", "[engine][wavesets][3a]")
{
    nodes::CycleCutsNode capture;
    capture.prepare ({ rate, 256 });
    std::vector<float> in (256), boundary (256), length (256), peak (256), count (256);
    float* outs[] = { boundary.data(), boundary.data(), length.data(), peak.data(), count.data() };
    int boundaries = 0;
    for (int done = 0; done < 48000; done += 256)
    {
        for (int s = 0; s < 256; ++s)
            in[(size_t) s] = 0.5f * sineA (done + s + 60); // starts at a peak, not a crossing
        const float* ins[] = { in.data() };
        capture.processBlock (ins, outs, 256);
        for (int s = 0; s < 256; ++s)
            boundaries += boundary[(size_t) s] > 0.0f;
    }
    CHECK (boundaries == 200);
    CHECK (length[255] == Catch::Approx (1.0 / 200.0).margin (1.0 / rate));
    CHECK (peak[255] == Catch::Approx (0.5f).margin (1.0e-3));
    CHECK (count[255] == 200.0f);

    // Group 2: two cycles count as one.
    capture.setParameter ("cycle.cuts.group", 2.0f);
    capture.reset();
    boundaries = 0;
    for (int done = 0; done < 48000; done += 256)
    {
        for (int s = 0; s < 256; ++s)
            in[(size_t) s] = 0.5f * sineA (done + s + 60);
        const float* ins[] = { in.data() };
        capture.processBlock (ins, outs, 256);
        for (int s = 0; s < 256; ++s)
            boundaries += boundary[(size_t) s] > 0.0f;
    }
    CHECK (boundaries == 100);
}

TEST_CASE ("Interleave: a sine and a saw alternate as whole cycles, joined at zero crossings", "[engine][wavesets][3a]")
{
    const auto r = interleave (24000, 512);
    std::vector<int> switchesAt;
    for (size_t t = 0; t < r.out.size(); ++t)
        if (r.switches[t] > 0.0f)
            switchesAt.push_back ((int) t);
    REQUIRE (switchesAt.size() > 40);

    // Turns alternate A (240 samples) and B (150): each source plays one whole
    // waveset, then hands over.
    for (size_t i = 2; i + 1 < switchesAt.size(); ++i)
    {
        const auto length = switchesAt[i + 1] - switchesAt[i];
        const auto from = (int) r.source[(size_t) switchesAt[i]];
        INFO ("turn " << i << " source " << from << " length " << length);
        CHECK (std::abs (length - (from == 0 ? 240 : 150)) <= 1);
        CHECK ((int) r.source[(size_t) switchesAt[i + 1]] == 1 - from);
        // Both sides of every join are next to zero.
        CHECK (std::abs (r.out[(size_t) switchesAt[i]]) < 0.04f);
        CHECK (std::abs (r.out[(size_t) switchesAt[i] - 1]) < 0.04f);
    }

    // What plays is the sources themselves, copied whole: an A turn is one sine cycle.
    const auto t = switchesAt[10] + (r.source[(size_t) switchesAt[10]] == 0 ? 0 : switchesAt[11] - switchesAt[10]);
    // (sin(2 pi k) computes as a hair below zero, so a cycle may start one sample late.)
    auto d = 0;
    for (const auto candidate : { 1, -1 })
        if (std::abs (r.out[(size_t) t] - sineA (candidate)) < std::abs (r.out[(size_t) t] - sineA (d)))
            d = candidate;
    for (int i = 0; i < 240; ++i)
        CHECK (r.out[(size_t) (t + i)] == Catch::Approx (sineA (i + d)).margin (1.0e-5));
}

TEST_CASE ("Count plays several wavesets of each source per turn", "[engine][wavesets][3a]")
{
    const auto r = interleave (24000, 256, 3.0f);
    std::vector<int> switchesAt;
    for (size_t t = 0; t < r.out.size(); ++t)
        if (r.switches[t] > 0.0f)
            switchesAt.push_back ((int) t);
    REQUIRE (switchesAt.size() > 10);
    for (size_t i = 2; i + 1 < switchesAt.size(); ++i)
    {
        const auto length = switchesAt[i + 1] - switchesAt[i];
        const auto from = (int) r.source[(size_t) switchesAt[i]];
        CHECK (std::abs (length - 3 * (from == 0 ? 240 : 150)) <= 3);
    }
}

TEST_CASE ("Waveset Play doesn't depend on the block size", "[engine][wavesets][3a]")
{
    const auto reference = interleave (20000, 64);
    for (const auto block : { 512, 97, 480 })
    {
        const auto other = interleave (20000, block);
        float worst = 0.0f;
        for (size_t t = 0; t < reference.out.size(); ++t)
            worst = std::max (worst, std::abs (other.out[t] - reference.out[t]));
        INFO ("block " << block);
        CHECK (worst == 0.0f);
    }
}
