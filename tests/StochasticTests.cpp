// wiki/plans/LifeCycle.md, the stochastic batch: each process measured for the
// behaviour it claims — memory, spread, scaling, interval statistics, routes
// to chaos — not just "it moves".
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/ChaosNodes.h"
#include "bazalt/engine/nodes/StochasticNodes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr double sampleRate = 48000.0;
    const float unwired = std::numeric_limits<float>::quiet_NaN();

    /** Runs `node` for `seconds`, returns output `channel` per sample. */
    std::vector<float> run (Node& node, std::vector<float> inputs, double seconds, int channel = 0, double rate = sampleRate)
    {
        node.prepare ({ rate, 64 });
        node.reset();
        std::vector<float> outputs ((size_t) node.getNumOutputPorts());
        std::vector<float> result ((size_t) (seconds * rate));
        for (auto& sample : result)
        {
            node.processSample (inputs.data(), outputs.data());
            sample = outputs[(size_t) channel];
        }
        return result;
    }

    double mean (const std::vector<float>& v, size_t from = 0)
    {
        double sum = 0.0;
        for (size_t i = from; i < v.size(); ++i)
            sum += v[i];
        return sum / (double) (v.size() - from);
    }

    double stddev (const std::vector<float>& v, size_t from = 0)
    {
        const auto m = mean (v, from);
        double sum = 0.0;
        for (size_t i = from; i < v.size(); ++i)
            sum += (v[i] - m) * (v[i] - m);
        return std::sqrt (sum / (double) (v.size() - from));
    }

    double autocorrelation (const std::vector<float>& v, size_t lag)
    {
        const auto m = mean (v);
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i + lag < v.size(); ++i)
            num += (v[i] - m) * (v[i + lag] - m);
        for (auto x : v)
            den += (x - m) * (x - m);
        return num / den;
    }

    /** Mean squared change over `lag` samples (the structure function). */
    double structure (const std::vector<float>& v, size_t lag)
    {
        double sum = 0.0;
        size_t n = 0;
        for (size_t i = 0; i + lag < v.size(); i += 7, ++n)
            sum += (double) (v[i + lag] - v[i]) * (v[i + lag] - v[i]);
        return sum / (double) n;
    }

    std::vector<double> intervals (const std::vector<float>& events)
    {
        std::vector<double> gaps;
        long last = -1;
        for (size_t i = 0; i < events.size(); ++i)
            if (events[i] > 0.5f)
            {
                if (last >= 0)
                    gaps.push_back ((double) ((long) i - last) / sampleRate);
                last = (long) i;
            }
        return gaps;
    }

    double cv (const std::vector<double>& gaps)
    {
        double m = 0.0, s = 0.0;
        for (auto g : gaps)
            m += g;
        m /= (double) gaps.size();
        for (auto g : gaps)
            s += (g - m) * (g - m);
        return std::sqrt (s / (double) gaps.size()) / m;
    }
}

TEST_CASE ("Wander has the spread and the memory it is set to", "[engine][stochastic][wander]")
{
    WanderNode wander;
    wander.setParameter ("random.wander.memory", 0.02f);
    wander.setParameter ("random.wander.spread", 0.4f);
    const auto out = run (wander, { unwired, unwired, unwired }, 20.0);
    CHECK (stddev (out, 4800) == Catch::Approx (0.2).margin (0.02)); // Spread 0.4 → σ = 0.2
    // Ornstein–Uhlenbeck: the correlation after one Memory is e^-1.
    CHECK (autocorrelation (out, (size_t) (0.02 * sampleRate)) == Catch::Approx (std::exp (-1.0)).margin (0.06));
    CHECK (std::abs (mean (out)) < 0.05);

    // Center moves where it is pulled to.
    const auto centred = run (wander, { unwired, unwired, 0.5f }, 5.0);
    CHECK (mean (centred, 4800) == Catch::Approx (0.5).margin (0.05));
}

TEST_CASE ("The same seed replays the same path, another seed another one", "[engine][stochastic][seed]")
{
    WanderNode a, b, c;
    c.setParameter ("random.wander.seed", 7.0f);
    const auto ra = run (a, { unwired, unwired, unwired }, 0.2);
    const auto rb = run (b, { unwired, unwired, unwired }, 0.2);
    const auto rc = run (c, { unwired, unwired, unwired }, 0.2);
    CHECK (ra == rb);
    CHECK (ra != rc);

    LorenzNode la, lb;
    lb.setParameter ("chaos.lorenz.seed", 9.0f);
    CHECK (run (la, { 20.0f, unwired }, 2.0) != run (lb, { 20.0f, unwired }, 2.0)); // chaos: a nudge grows
}

TEST_CASE ("Brownian spreads with the square root of time and stays in range", "[engine][stochastic][brownian]")
{
    BrownianNode brownian;
    const auto out = run (brownian, { 0.2f, unwired }, 60.0);
    for (auto x : out)
        REQUIRE (std::abs (x) <= 1.0f);
    // E[(Δx)²] = speed² · τ: over 0.1 s, 0.004; over 0.4 s, four times as much.
    const auto d1 = structure (out, (size_t) (0.1 * sampleRate));
    const auto d4 = structure (out, (size_t) (0.4 * sampleRate));
    CHECK (d1 == Catch::Approx (0.004).epsilon (0.2));
    CHECK (d4 / d1 == Catch::Approx (4.0).epsilon (0.2));
}

TEST_CASE ("Fractal gets smoother with Smoothness, by its scaling exponent", "[engine][stochastic][fractal]")
{
    // Structure function ∝ τ^(β−1) between its time scales: the ratio between
    // lags 4× apart grows with β = 1 + 2·Smoothness.
    const auto ratio = [] (float smoothness)
    {
        FractalNode fractal;
        const auto out = run (fractal, { smoothness, 10.0f, 1.0f }, 40.0, 0, 12000.0);
        const auto lag = (size_t) (0.05 * 12000.0);
        return structure (out, lag * 4) / structure (out, lag);
    };
    const auto rough = ratio (0.0f), medium = ratio (0.5f), smooth = ratio (1.0f);
    INFO ("ratios " << rough << " " << medium << " " << smooth);
    CHECK (rough < 1.6);                                 // pink: barely grows with lag
    CHECK (medium == Catch::Approx (4.0).epsilon (0.35)); // β = 2: Brownian-like, ∝ τ
    CHECK (smooth > 8.0);                                // β = 3: smooth arcs, ∝ τ²
}

TEST_CASE ("Events: Poisson in the middle, a clock at full Regularity, bursts at none", "[engine][stochastic][events]")
{
    const auto measure = [] (float regularity)
    {
        EventsNode events;
        return intervals (run (events, { 20.0f, regularity, unwired, unwired, unwired }, 100.0));
    };
    const auto poisson = measure (0.5f), clock = measure (1.0f), bursty = measure (0.0f);
    CHECK ((double) poisson.size() == Catch::Approx (2000.0).epsilon (0.1));
    CHECK (cv (poisson) == Catch::Approx (1.0).margin (0.1)); // exponential gaps
    CHECK (cv (clock) < 0.3);
    CHECK (cv (bursty) > 1.5);
}

TEST_CASE ("Events: Clustering multiplies the count, Refractory keeps a gap", "[engine][stochastic][events]")
{
    EventsNode clustered;
    const auto gaps = intervals (run (clustered, { 10.0f, 0.5f, 0.5f, 0.05f, unwired }, 100.0));
    // A Hawkes process with branching 0.5 has 1 / (1 − 0.5) = 2 events per immigrant.
    CHECK ((double) gaps.size() == Catch::Approx (2000.0).epsilon (0.15));
    CHECK (cv (gaps) > 1.2); // clustered, not Poisson

    EventsNode refractory;
    const auto spaced = intervals (run (refractory, { 50.0f, 0.5f, unwired, unwired, 0.05f }, 20.0));
    CHECK (*std::min_element (spaced.begin(), spaced.end()) >= 0.05 - 1.0 / sampleRate);
}

TEST_CASE ("Lorenz visits both lobes at Rho 28 and settles at Rho 10", "[engine][chaos]")
{
    LorenzNode lorenz;
    const auto chaotic = run (lorenz, { 5.0f, 28.0f }, 20.0);
    const auto positive = std::count_if (chaotic.begin(), chaotic.end(), [] (float x) { return x > 0.2f; });
    const auto negative = std::count_if (chaotic.begin(), chaotic.end(), [] (float x) { return x < -0.2f; });
    CHECK (positive > (long) chaotic.size() / 10);
    CHECK (negative > (long) chaotic.size() / 10);

    LorenzNode calm;
    const auto settled = run (calm, { 5.0f, 10.0f }, 20.0);
    CHECK (stddev (std::vector<float> (settled.end() - 48000, settled.end())) < 0.01);
}

TEST_CASE ("Rössler doubles its period as c rises", "[engine][chaos]")
{
    // Distinct heights of the x maxima: one value on a clean cycle, two after
    // the first doubling, many in chaos.
    const auto distinctPeaks = [] (float c)
    {
        RosslerNode rossler;
        const auto x = run (rossler, { 200.0f, c }, 3.0);
        std::set<int> heights;
        for (size_t i = x.size() / 2; i + 1 < x.size(); ++i)
            if (x[i] > x[i - 1] && x[i] >= x[i + 1])
                heights.insert ((int) std::lround (x[i] * 50.0f));
        return heights.size();
    };
    CHECK (distinctPeaks (2.5f) <= 2);
    const auto doubled = distinctPeaks (3.5f);
    CHECK (doubled >= 2);
    CHECK (doubled <= 4);
    CHECK (distinctPeaks (5.7f) > 8);
}

TEST_CASE ("Duffing stays in one well when gently driven and jumps between them when pushed", "[engine][chaos]")
{
    DuffingNode gentle, pushed;
    const auto calm = run (gentle, { 50.0f, 0.1f, 0.25f }, 4.0);
    const auto wild = run (pushed, { 50.0f, 0.4f, 0.25f }, 4.0);
    const auto crossings = [] (const std::vector<float>& x)
    {
        int count = 0;
        for (size_t i = x.size() / 2; i + 1 < x.size(); ++i)
            count += (x[i] < 0.0f) != (x[i + 1] < 0.0f);
        return count;
    };
    CHECK (crossings (calm) == 0);
    CHECK (crossings (wild) > 10);
}

TEST_CASE ("The logistic map settles, alternates, then goes chaotic", "[engine][chaos]")
{
    const auto values = [] (float growth)
    {
        LogisticMapNode map;
        const auto x = run (map, { unwired, 1000.0f, growth }, 2.0);
        std::set<int> seen;
        for (size_t i = x.size() / 2; i < x.size(); ++i)
            seen.insert ((int) std::lround (x[i] * 1000.0f));
        return seen;
    };
    const auto fixed = values (2.8f);
    REQUIRE (fixed.size() == 1);
    CHECK (*fixed.begin() == (int) std::lround ((1.0 - 1.0 / 2.8) * 1000.0));
    CHECK (values (3.2f).size() == 2);
    CHECK (values (3.9f).size() > 50);
}
