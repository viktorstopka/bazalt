#pragma once

#include "bazalt/engine/Stochastic.h"
#include "bazalt/engine/graph/Node.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** The stochastic suite's first nodes (wiki/plans/LifeCycle.md, the
        stochastic batch; research brief: "Stochastic and Natural Suite").
        Each answers one question about randomness and nothing else:

        - **Wander** (Ornstein–Uhlenbeck): moves like something with a memory
          that is pulled back toward a centre — "keep roughly the same
          direction, but don't become periodic".
        - **Brownian**: a random walk with no pull at all, kept inside the
          range by reflecting (or wrapping) at the edges.
        - **Fractal** (1/f, fractional Brownian motion): variation on every
          timescale at once — micro jitter, phrase-length swells and slow
          drift from one source, the multiscale behaviour of natural sound.
        - **Events** (renewal + Hawkes point process): when things happen —
          from clockwork through Poisson to bursty, with self-exciting
          clusters and a refractory gap.

        `random.drift` stays as it was (a leaky walk with a smoothing
        character), next to these.

        All of them run per sample from the sample rate (CLAUDE.md rule 6) and
        can run at audio rate — every modulator is also a sound. **Seed**: the
        same seed replays the same behaviour; 0 means a new one each time the
        patch loads. */
    namespace stochastic
    {
        inline float read (const float* inputs, int index, float stored) noexcept
        {
            return std::isnan (inputs[index]) ? stored : inputs[index];
        }

        inline ParameterDescriptor seedParameter (const juce::String& id)
        {
            return ParameterDescriptor { .id = id, .minValue = 0.0f, .maxValue = 999999.0f, .defaultValue = 1.0f,
                                         .displayName = "Seed", .isInteger = true, .quantity = Quantity::Count,
                                         .step = 1.0f, .isStructural = true };
        }

        inline PortDescriptor bipolarOut (const char* id = "out", const char* label = "Out")
        {
            return PortDescriptor { .id = id, .type = SignalType::Signal, .label = label, .isPrimaryOutput = juce::String (id) == "out",
                                    .minValue = -1.0f, .maxValue = 1.0f, .quantity = Quantity::Bipolar,
                                    .polarity = Polarity::Bipolar };
        }

        inline PortDescriptor seconds (const juce::String& id, const char* label, float lo, float hi, float def)
        {
            return PortDescriptor { .id = id, .type = SignalType::Signal, .label = label, .unit = "s",
                                    .minValue = lo, .maxValue = hi, .defaultValue = def, .isLogScale = true,
                                    .hasFallbackWhenUnconnected = true, .quantity = Quantity::Time,
                                    .curve = Curve::Logarithmic };
        }

        inline PortDescriptor unit (const juce::String& id, const char* label, float def, float lo = 0.0f, float hi = 1.0f)
        {
            return PortDescriptor { .id = id, .type = SignalType::Signal, .label = label, .minValue = lo, .maxValue = hi,
                                    .defaultValue = def, .hasFallbackWhenUnconnected = true,
                                    .quantity = lo < 0.0f ? Quantity::Bipolar : Quantity::Unipolar,
                                    .polarity = lo < 0.0f ? Polarity::Bipolar : Polarity::Unipolar };
        }
    }

    /** A node with a Seed parameter and a StochasticRandom behind it. */
    class SeededNode : public Node
    {
    public:
        explicit SeededNode (juce::String seedIdIn) : seedId (std::move (seedIdIn)) {}

        void reset() override { random.reseed (resolvedSeed); }

    protected:
        bool handleSeed (const juce::String& parameterId, float value) noexcept
        {
            if (parameterId != seedId)
                return false;
            resolvedSeed = StochasticRandom::resolveSeed ((int) std::lround (value));
            random.reseed (resolvedSeed);
            return true;
        }

        juce::String seedId;
        uint64_t resolvedSeed = 1;
        StochasticRandom random { 1 };
    };

    /** Stable type id: "random.wander" — Wander, an Ornstein–Uhlenbeck
        process (Uhlenbeck & Ornstein, 1930): Brownian motion pulled back
        toward a centre. **Memory** is how long it keeps going one way (the
        correlation time); **Spread** how far it strays (its steady-state
        spread); **Center** where it is pulled to — wire a slow signal into it
        and Wander follows it, wandering around it. Updated exactly
        (x ← x·a + σ√(1−a²)·N, a = e^(−dt/Memory)), so the statistics do not
        depend on the sample rate or the block size. */
    class WanderNode : public SeededNode
    {
    public:
        WanderNode() : SeededNode ("random.wander.seed") {}

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override
        {
            SeededNode::reset();
            x = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return 3; }
        int getNumOutputPorts() const noexcept override { return 1; }
        juce::String getTitle() const override { return "Wander"; }
        juce::String getCategory() const override { return "Random"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { stochastic::seconds ("random.wander.memory", "Memory", 0.0005f, 120.0f, 1.0f),
                     stochastic::unit ("random.wander.spread", "Spread", 0.3f),
                     stochastic::unit ("random.wander.center", "Center", 0.0f, -1.0f, 1.0f) };
        }
        std::vector<PortDescriptor> getOutputPorts() const override { return { stochastic::bipolarOut() }; }
        std::vector<ParameterDescriptor> getParameters() const override { return { stochastic::seedParameter (seedId) }; }

        void setParameter (const juce::String& id, float value) override
        {
            if (handleSeed (id, value))
                return;
            if (id == "random.wander.memory")
                storedMemory = value;
            else if (id == "random.wander.spread")
                storedSpread = value;
            else if (id == "random.wander.center")
                storedCenter = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto memory = juce::jmax (1.0e-5f, stochastic::read (inputs, 0, storedMemory));
            const auto sigma = 0.5f * juce::jlimit (0.0f, 1.0f, stochastic::read (inputs, 1, storedSpread));
            const auto center = stochastic::read (inputs, 2, storedCenter);
            if (memory != cachedMemory)
            {
                cachedMemory = memory;
                a = (float) std::exp (-1.0 / ((double) memory * sampleRate));
                noiseScale = std::sqrt (juce::jmax (0.0f, 1.0f - a * a));
            }
            x = x * a + sigma * noiseScale * random.gaussian();
            // a Spread pulled down leaves the old excursion to relax at Memory's pace
            outputs[0] = juce::jlimit (-1.0f, 1.0f, center + x);
        }

    private:
        double sampleRate = 48000.0;
        float x = 0.0f, a = 1.0f, noiseScale = 0.0f, cachedMemory = -1.0f;
        float storedMemory = 1.0f, storedSpread = 0.3f, storedCenter = 0.0f;
    };

    /** Stable type id: "random.brownian" — Brownian motion (a Wiener
        process): every step is independent, nothing pulls it back, so it
        drifts anywhere and stays wherever it got to. **Speed** is how far it
        typically travels in one second; at the edge of −1..1 it reflects or
        wraps (**Edges**). A **Reset** event sends it back to the middle. */
    class BrownianNode : public SeededNode
    {
    public:
        BrownianNode() : SeededNode ("random.brownian.seed") {}

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override
        {
            SeededNode::reset();
            x = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return 2; }
        int getNumOutputPorts() const noexcept override { return 1; }
        juce::String getTitle() const override { return "Brownian"; }
        juce::String getCategory() const override { return "Random"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "random.brownian.speed", .type = SignalType::Signal, .label = "Speed", .unit = juce::String (juce::CharPointer_UTF8 ("/\xe2\x88\x9as")),
                                      .minValue = 0.001f, .maxValue = 100.0f, .defaultValue = 0.3f, .isLogScale = true,
                                      .hasFallbackWhenUnconnected = true, .curve = Curve::Logarithmic },
                     PortDescriptor { .id = "reset", .type = SignalType::Event, .label = "Reset" } };
        }
        std::vector<PortDescriptor> getOutputPorts() const override { return { stochastic::bipolarOut() }; }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "random.brownian.edges", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                           .displayName = "Edges", .isInteger = true, .kind = ValueKind::Enum,
                                           .enumOptions = { { "reflect", "Reflect" }, { "wrap", "Wrap" } } },
                     stochastic::seedParameter (seedId) };
        }

        void setParameter (const juce::String& id, float value) override
        {
            if (handleSeed (id, value))
                return;
            if (id == "random.brownian.speed")
                storedSpeed = value;
            else if (id == "random.brownian.edges")
                wrap = std::lround (value) == 1;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (! std::isnan (inputs[1]) && inputs[1] != 0.0f)
                x = 0.0f;
            const auto speed = juce::jmax (0.0f, stochastic::read (inputs, 0, storedSpeed));
            x += speed * (float) std::sqrt (1.0 / sampleRate) * random.gaussian();
            if (wrap)
                x = x - 2.0f * std::floor ((x + 1.0f) * 0.5f);
            else
            {
                for (int i = 0; i < 4 && (x > 1.0f || x < -1.0f); ++i)
                    x = x > 1.0f ? 2.0f - x : -2.0f - x;
                x = juce::jlimit (-1.0f, 1.0f, x);
            }
            outputs[0] = x;
        }

    private:
        double sampleRate = 48000.0;
        float x = 0.0f, storedSpeed = 0.3f;
        bool wrap = false;
    };

    /** Stable type id: "random.fractal" — Fractal: 1/f-type variation, the
        spectrum natural fluctuations tend to have (Voss & Clarke, 1975),
        built as a sum of smooth random components one octave apart in time
        (each white noise through two one-pole low-passes, so its spectrum
        falls as 1/f⁴ above its corner), weighted so the sum follows 1/f^β —
        the superposition construction of 1/f noise. Plain Ornstein–Uhlenbeck
        components (1/f² tails) cannot get smoother than Brownian, so they
        would cap Smoothness halfway. **Smoothness**
        sets β from 1 (pink: lively, a little nervous) to 3 (fractional
        Brownian motion with Hurst ≈ 1: long smooth arcs); **Slowest** is the
        longest timescale it varies over (below that it is flat), the fastest
        is a few milliseconds. **Amount** scales it. One source gives micro
        jitter, mid swells and slow drift together — and since every scale is
        the same process, they belong together. */
    class FractalNode : public SeededNode
    {
    public:
        static constexpr int maxOctaves = 20;
        static constexpr double fastestSeconds = 0.004;

        FractalNode() : SeededNode ("random.fractal.seed") {}

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            cachedSlowest = cachedSmoothness = -1.0f;
        }
        void reset() override
        {
            SeededNode::reset();
            state.fill (0.0f);
            smooth.fill (0.0f);
        }

        int getNumInputPorts() const noexcept override { return 3; }
        int getNumOutputPorts() const noexcept override { return 1; }
        juce::String getTitle() const override { return "Fractal"; }
        juce::String getCategory() const override { return "Random"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { stochastic::unit ("random.fractal.smoothness", "Smoothness", 0.5f),
                     stochastic::seconds ("random.fractal.slowest", "Slowest", 0.05f, 600.0f, 20.0f),
                     stochastic::unit ("random.fractal.amount", "Amount", 0.5f) };
        }
        std::vector<PortDescriptor> getOutputPorts() const override { return { stochastic::bipolarOut() }; }
        std::vector<ParameterDescriptor> getParameters() const override { return { stochastic::seedParameter (seedId) }; }

        void setParameter (const juce::String& id, float value) override
        {
            if (handleSeed (id, value))
                return;
            if (id == "random.fractal.smoothness")
                storedSmoothness = value;
            else if (id == "random.fractal.slowest")
                storedSlowest = value;
            else if (id == "random.fractal.amount")
                storedAmount = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto smoothness = juce::jlimit (0.0f, 1.0f, stochastic::read (inputs, 0, storedSmoothness));
            const auto slowest = juce::jlimit (0.01f, 3600.0f, stochastic::read (inputs, 1, storedSlowest));
            const auto amount = juce::jlimit (0.0f, 1.0f, stochastic::read (inputs, 2, storedAmount));
            if (slowest != cachedSlowest || smoothness != cachedSmoothness)
                configure (slowest, smoothness);

            float sum = 0.0f;
            for (int k = 0; k < octaves; ++k)
            {
                const auto a = decay[(size_t) k];
                state[(size_t) k] = state[(size_t) k] * a + drive[(size_t) k] * random.gaussian();
                smooth[(size_t) k] = smooth[(size_t) k] * a + (1.0f - a) * state[(size_t) k];
                sum += weight[(size_t) k] * smooth[(size_t) k];
            }
            // normalised to unit standard deviation, then a third of the range per unit Amount
            outputs[0] = juce::jlimit (-1.0f, 1.0f, sum * 0.35f * amount);
        }

    private:
        void configure (float slowest, float smoothness) noexcept
        {
            cachedSlowest = slowest;
            cachedSmoothness = smoothness;
            const auto beta = 1.0 + 2.0 * (double) smoothness;
            octaves = juce::jlimit (1, maxOctaves, (int) std::ceil (std::log2 ((double) slowest / fastestSeconds)) + 1);
            double total = 0.0;
            for (int k = 0; k < octaves; ++k)
            {
                const auto tau = fastestSeconds * std::pow (2.0, k);
                const auto a = std::exp (-1.0 / (tau * sampleRate));
                decay[(size_t) k] = (float) a;
                drive[(size_t) k] = (float) std::sqrt (juce::jmax (0.0, 1.0 - a * a)); // first pole: unit variance
                const auto smoothVariance = (1.0 + a * a) / ((1.0 + a) * (1.0 + a)); // after the second pole
                const auto w2 = std::pow (tau, beta - 1.0); // variance per octave ∝ τ^(β−1) → 1/f^β
                weight[(size_t) k] = (float) std::sqrt (w2 / smoothVariance);
                total += w2;
            }
            const auto norm = 1.0 / std::sqrt (total);
            for (int k = 0; k < octaves; ++k)
                weight[(size_t) k] = (float) (weight[(size_t) k] * norm);
        }

        double sampleRate = 48000.0;
        int octaves = 1;
        std::array<float, maxOctaves> state {}, smooth {}, decay {}, drive {}, weight {};
        float cachedSlowest = -1.0f, cachedSmoothness = -1.0f;
        float storedSmoothness = 0.5f, storedSlowest = 20.0f, storedAmount = 0.5f;
    };

    /** Stable type id: "random.events" — Events: when things happen, as a
        point process.
        - **Rate**: events per second on average.
        - **Regularity**: the spread of the gaps — 0 bursty and irregular,
          0.5 a Poisson process (memoryless: rain, radioactive clicks), 1
          close to a clock. The gaps are Gamma-distributed with mean 1/Rate
          (a renewal process), shape from 0.03 to 30.
        - **Clustering**: each event may set off follow-ups (a Hawkes
          process, Hawkes 1971): 0 none, toward 1 cascades — crackling fire,
          dripping, bursts of chirps. **Cluster Time** is how long the
          follow-ups keep coming.
        - **Refractory**: the shortest allowed gap — nothing fires sooner
          (as a neuron or a cricket's wing can't).
        Out: **Event**, and **Size**, a new random value 0..1 drawn at each
        event (a crackle's loudness, a drop's size). Rate is read every
        sample, so modulating it bends the timing smoothly. */
    class EventsNode : public SeededNode
    {
    public:
        EventsNode() : SeededNode ("random.events.seed") {}

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override
        {
            SeededNode::reset();
            phase = 0.0;
            target = -1.0;
            excitation = 0.0;
            sinceLast = 1.0e9;
            size = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return 5; }
        int getNumOutputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Events"; }
        juce::String getCategory() const override { return "Random"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "random.events.rate", .type = SignalType::Signal, .label = "Rate", .unit = "Hz",
                                      .minValue = 0.01f, .maxValue = 5000.0f, .defaultValue = 4.0f, .isLogScale = true,
                                      .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                      .curve = Curve::Logarithmic },
                     stochastic::unit ("random.events.regularity", "Regularity", 0.5f),
                     stochastic::unit ("random.events.clustering", "Clustering", 0.0f, 0.0f, 0.95f),
                     stochastic::seconds ("random.events.clusterTime", "Cluster Time", 0.001f, 10.0f, 0.08f),
                     PortDescriptor { .id = "random.events.refractory", .type = SignalType::Signal, .label = "Refractory", .unit = "s",
                                      .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f,
                                      .hasFallbackWhenUnconnected = true, .quantity = Quantity::Time } };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "event", .type = SignalType::Event, .label = "Event", .isPrimaryOutput = true },
                     PortDescriptor { .id = "size", .type = SignalType::Signal, .label = "Size", .minValue = 0.0f, .maxValue = 1.0f,
                                      .quantity = Quantity::Unipolar } };
        }
        std::vector<ParameterDescriptor> getParameters() const override { return { stochastic::seedParameter (seedId) }; }

        void setParameter (const juce::String& id, float value) override
        {
            if (handleSeed (id, value))
                return;
            if (id == "random.events.rate")
                storedRate = value;
            else if (id == "random.events.regularity")
                storedRegularity = value;
            else if (id == "random.events.clustering")
                storedClustering = value;
            else if (id == "random.events.clusterTime")
                storedClusterTime = value;
            else if (id == "random.events.refractory")
                storedRefractory = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto dt = 1.0 / sampleRate;
            const auto rate = juce::jmax (0.0, (double) stochastic::read (inputs, 0, storedRate));
            const auto regularity = juce::jlimit (0.0f, 1.0f, stochastic::read (inputs, 1, storedRegularity));
            const auto clustering = (double) juce::jlimit (0.0f, 0.95f, stochastic::read (inputs, 2, storedClustering));
            const auto clusterTime = juce::jmax (1.0e-4, (double) stochastic::read (inputs, 3, storedClusterTime));
            const auto refractory = juce::jmax (0.0, (double) stochastic::read (inputs, 4, storedRefractory));

            // Renewal: a Gamma(k, 1/k) target in units of the mean gap, crossed by
            // a phase advancing at Rate — so a moving Rate bends the timing.
            const auto shape = std::pow (10.0, ((double) regularity - 0.5) * 3.0);
            if (target < 0.0)
                target = random.gamma (shape) / shape;
            phase += rate * dt;
            auto fire = false;
            if (phase >= target)
            {
                phase -= target;
                if (phase > 1.0)
                    phase = 0.0; // a Rate that jumped down from very high: don't fire a backlog
                target = random.gamma (shape) / shape;
                fire = true;
            }

            // Hawkes: follow-ups at an intensity that each event raises.
            excitation *= std::exp (-dt / clusterTime);
            if (! fire && excitation > 0.0 && (double) random.uniform() < excitation * dt)
                fire = true;

            sinceLast += dt;
            if (fire && sinceLast < refractory)
                fire = false;

            if (fire)
            {
                sinceLast = 0.0;
                excitation += clustering / clusterTime; // expected follow-ups per event = Clustering
                size = random.uniform();
            }
            outputs[0] = fire ? 1.0f : 0.0f;
            outputs[1] = size;
        }

    private:
        double sampleRate = 48000.0;
        double phase = 0.0, target = -1.0, excitation = 0.0, sinceLast = 1.0e9;
        float size = 0.0f;
        float storedRate = 4.0f, storedRegularity = 0.5f, storedClustering = 0.0f, storedClusterTime = 0.08f, storedRefractory = 0.0f;
    };
}
