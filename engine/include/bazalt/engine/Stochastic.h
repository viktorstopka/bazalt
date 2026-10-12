#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>

namespace bazalt::engine
{
    /** The random source of the stochastic suite (wiki/plans/LifeCycle.md,
        stochastic batch): a small, fast, deterministic generator
        (xoshiro128+ seeded through SplitMix64) with the draws the processes
        need — uniform, Gaussian (Box–Muller, the second value kept), Gamma
        (Marsaglia–Tsang) — all allocation-free and safe on the audio thread.

        Seeds: the same seed always replays the same stream. Seed 0 means "a
        new one each time the patch loads" (`resolveSeed`), so determinism is
        a setting rather than a rule. */
    class StochasticRandom
    {
    public:
        explicit StochasticRandom (uint64_t seed = 1) noexcept { reseed (seed); }

        /** 0 becomes a fresh seed from the system (message thread only). */
        static uint64_t resolveSeed (int seed) noexcept
        {
            return seed == 0 ? (uint64_t) juce::Random::getSystemRandom().nextInt64() | 1u : (uint64_t) seed;
        }

        void reseed (uint64_t seed) noexcept
        {
            auto splitMix = [&seed]
            {
                auto z = (seed += 0x9E3779B97F4A7C15ull);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                return z ^ (z >> 31);
            };
            for (auto& word : state)
                word = (uint32_t) splitMix();
            if ((state[0] | state[1] | state[2] | state[3]) == 0)
                state[0] = 1;
            hasSpareGaussian = false;
        }

        /** Uniform in [0, 1). */
        float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }

        /** Uniform in (0, 1] — safe under a log. */
        double uniformOpen() noexcept { return ((double) (next() >> 8) + 1.0) * (1.0 / 16777216.0); }

        /** Standard normal. */
        float gaussian() noexcept
        {
            if (hasSpareGaussian)
            {
                hasSpareGaussian = false;
                return spareGaussian;
            }
            const auto radius = std::sqrt (-2.0 * std::log (uniformOpen()));
            const auto angle = juce::MathConstants<double>::twoPi * (double) uniform();
            spareGaussian = (float) (radius * std::sin (angle));
            hasSpareGaussian = true;
            return (float) (radius * std::cos (angle));
        }

        /** Gamma(shape k, scale 1): mean k. Marsaglia & Tsang (2000); a shape
            below 1 is drawn as Gamma(k + 1) · U^(1/k). */
        double gamma (double k) noexcept
        {
            if (k < 1.0)
                return gamma (k + 1.0) * std::pow (uniformOpen(), 1.0 / k);
            const auto d = k - 1.0 / 3.0;
            const auto c = 1.0 / std::sqrt (9.0 * d);
            for (;;)
            {
                double x, v;
                do
                {
                    x = (double) gaussian();
                    v = 1.0 + c * x;
                } while (v <= 0.0);
                v = v * v * v;
                const auto u = uniformOpen();
                if (u < 1.0 - 0.0331 * x * x * x * x || std::log (u) < 0.5 * x * x + d * (1.0 - v + std::log (v)))
                    return d * v;
            }
        }

    private:
        uint32_t next() noexcept
        {
            const auto result = state[0] + state[3];
            const auto t = state[1] << 9;
            state[2] ^= state[0];
            state[3] ^= state[1];
            state[1] ^= state[2];
            state[0] ^= state[3];
            state[2] ^= t;
            state[3] = (state[3] << 11) | (state[3] >> 21);
            return result;
        }

        uint32_t state[4] {};
        float spareGaussian = 0.0f;
        bool hasSpareGaussian = false;
    };
}
