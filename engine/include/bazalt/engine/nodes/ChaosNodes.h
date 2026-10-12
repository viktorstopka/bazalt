#pragma once

#include "bazalt/engine/nodes/StochasticNodes.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Deterministic chaos (wiki/plans/LifeCycle.md, the stochastic batch):
        not randomness — a few equations with no noise in them whose motion
        never repeats, but keeps a recognisable shape (an attractor). That
        shape is what tells chaos apart from noise by ear: it orbits, it
        almost repeats, it switches; its outputs are related, not independent.
        Each node runs at audio rate as easily as at LFO rate — as a
        modulator it is structured wandering, as a sound it is a living,
        unstable tone.

        Chosen for what they do musically, not to catalogue attractors:
        - **Lorenz**: two lobes, switching unpredictably — "calm, then a
          flip". x, y, z out.
        - **Rössler**: a single spiral whose **c** walks it from a clean cycle
          through period doubling into chaos — the most controllable route
          from tone to chaos; plays at a pitch.
        - **Duffing**: a driven, double-well oscillator — pitched by its
          drive, tipping from tame to chaotic with **Drive**.
        - **Logistic Map**: one stepped value per tick, from fixed through
          cycles of 2, 4, 8 into chaos as **Growth** rises.

        Integrated with RK4 in sub-steps small enough to stay on the
        attractor at any rate (CLAUDE.md rule 6: steps come from the sample
        rate, never the block size). **Seed** nudges the starting point, so
        instances with different seeds run apart while the same seed always
        replays the same path. A state that ever leaves the attractor
        (non-finite, or far out) restarts. */
    namespace chaos
    {
        using Vec3 = std::array<double, 3>;

        template <typename Derivative>
        inline Vec3 rk4 (const Vec3& s, double h, Derivative&& f) noexcept
        {
            const auto add = [] (const Vec3& a, const Vec3& b, double k) { return Vec3 { a[0] + b[0] * k, a[1] + b[1] * k, a[2] + b[2] * k }; };
            const auto k1 = f (s);
            const auto k2 = f (add (s, k1, h * 0.5));
            const auto k3 = f (add (s, k2, h * 0.5));
            const auto k4 = f (add (s, k3, h));
            return { s[0] + h / 6.0 * (k1[0] + 2.0 * k2[0] + 2.0 * k3[0] + k4[0]),
                     s[1] + h / 6.0 * (k1[1] + 2.0 * k2[1] + 2.0 * k3[1] + k4[1]),
                     s[2] + h / 6.0 * (k1[2] + 2.0 * k2[2] + 2.0 * k3[2] + k4[2]) };
        }

        /** Advances `s` by `timeUnits` in sub-steps no longer than `maxStep`. */
        template <typename Derivative>
        inline void advance (Vec3& s, double timeUnits, double maxStep, Derivative&& f) noexcept
        {
            const auto steps = juce::jlimit (1, 64, (int) std::ceil (timeUnits / maxStep));
            const auto h = timeUnits / steps;
            for (int i = 0; i < steps; ++i)
                s = rk4 (s, h, f);
        }

        inline bool escaped (const Vec3& s, double limit) noexcept
        {
            for (auto v : s)
                if (! std::isfinite (v) || std::abs (v) > limit)
                    return true;
            return false;
        }

        inline PortDescriptor hz (const juce::String& id, const char* label, float lo, float hi, float def)
        {
            return PortDescriptor { .id = id, .type = SignalType::Signal, .label = label, .unit = "Hz", .minValue = lo, .maxValue = hi,
                                    .defaultValue = def, .isLogScale = true, .hasFallbackWhenUnconnected = true,
                                    .quantity = Quantity::Frequency, .curve = Curve::Logarithmic };
        }

        inline PortDescriptor plain (const juce::String& id, const char* label, float lo, float hi, float def)
        {
            return PortDescriptor { .id = id, .type = SignalType::Signal, .label = label, .minValue = lo, .maxValue = hi,
                                    .defaultValue = def, .hasFallbackWhenUnconnected = true };
        }
    }

    /** A chaotic flow with three outputs and a seeded starting point. */
    class ChaosFlowNode : public SeededNode
    {
    public:
        ChaosFlowNode (juce::String seedIdIn, chaos::Vec3 originIn) : SeededNode (std::move (seedIdIn)), origin (originIn) { restart(); }

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override
        {
            SeededNode::reset();
            restart();
        }
        int getNumOutputPorts() const noexcept override { return 3; }
        juce::String getCategory() const override { return "Chaos"; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { stochastic::bipolarOut ("out", "X"), stochastic::bipolarOut ("y", "Y"), stochastic::bipolarOut ("z", "Z") };
        }

    protected:
        void restart() noexcept
        {
            state = origin;
            for (auto& v : state)
                v += 0.1 * ((double) random.uniform() - 0.5);
        }

        double sampleRate = 48000.0;
        chaos::Vec3 origin, state {};
    };

    /** Stable type id: "chaos.lorenz" — Lorenz (1963): σ = 10, β = 8/3 and
        **Rho** free. Below ~24.7 it settles into one lobe; around 28 the
        classic butterfly; far above, periodic windows return. **Rate** is
        roughly how many lobe orbits it makes per second. */
    class LorenzNode : public ChaosFlowNode
    {
    public:
        LorenzNode() : ChaosFlowNode ("chaos.lorenz.seed", { 1.0, 1.0, 20.0 }) {}

        int getNumInputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Lorenz"; }
        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { chaos::hz ("chaos.lorenz.rate", "Rate", 0.01f, 2000.0f, 1.0f), chaos::plain ("chaos.lorenz.rho", "Rho", 1.0f, 100.0f, 28.0f) };
        }
        std::vector<ParameterDescriptor> getParameters() const override { return { stochastic::seedParameter (seedId) }; }
        void setParameter (const juce::String& id, float value) override
        {
            if (handleSeed (id, value))
                restart();
            else if (id == "chaos.lorenz.rate")
                storedRate = value;
            else if (id == "chaos.lorenz.rho")
                storedRho = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto rate = juce::jlimit (0.0, 20000.0, (double) stochastic::read (inputs, 0, storedRate));
            const auto rho = juce::jlimit (0.0, 200.0, (double) stochastic::read (inputs, 1, storedRho));
            // ~0.75 time units per lobe orbit at the classic setting
            chaos::advance (state, rate * 0.75 / sampleRate, 0.005, [rho] (const chaos::Vec3& s)
                            { return chaos::Vec3 { 10.0 * (s[1] - s[0]), s[0] * (rho - s[2]) - s[1], s[0] * s[1] - 8.0 / 3.0 * s[2] }; });
            if (chaos::escaped (state, 1.0e3))
                restart();
            outputs[0] = (float) juce::jlimit (-1.0, 1.0, state[0] / 20.0);
            outputs[1] = (float) juce::jlimit (-1.0, 1.0, state[1] / 27.0);
            outputs[2] = (float) juce::jlimit (-1.0, 1.0, (state[2] - rho + 1.0) / 25.0);
        }

    private:
        float storedRate = 1.0f, storedRho = 28.0f;
    };

    /** Stable type id: "chaos.rossler" — Rössler (1976): a = b = 0.2, **c**
        free. c ≈ 2.5: one clean cycle; ≈ 3.5: period 2; ≈ 4: period 4;
        ≈ 5.7: chaos — period doubling you can sweep by ear. Spirals at about
        **Frequency**, so it is also an oscillator. */
    class RosslerNode : public ChaosFlowNode
    {
    public:
        RosslerNode() : ChaosFlowNode ("chaos.rossler.seed", { 1.0, 0.0, 0.0 }) {}

        int getNumInputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Rössler"; }
        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { chaos::hz ("chaos.rossler.frequency", "Frequency", 0.01f, 5000.0f, 1.0f), chaos::plain ("chaos.rossler.c", "c", 2.0f, 9.0f, 5.7f) };
        }
        std::vector<ParameterDescriptor> getParameters() const override { return { stochastic::seedParameter (seedId) }; }
        void setParameter (const juce::String& id, float value) override
        {
            if (handleSeed (id, value))
                restart();
            else if (id == "chaos.rossler.frequency")
                storedFrequency = value;
            else if (id == "chaos.rossler.c")
                storedC = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto frequency = juce::jlimit (0.0, 20000.0, (double) stochastic::read (inputs, 0, storedFrequency));
            const auto c = juce::jlimit (1.0, 20.0, (double) stochastic::read (inputs, 1, storedC));
            // one turn of the spiral ≈ 2π time units
            chaos::advance (state, juce::MathConstants<double>::twoPi * frequency / sampleRate, 0.02, [c] (const chaos::Vec3& s)
                            { return chaos::Vec3 { -s[1] - s[2], s[0] + 0.2 * s[1], 0.2 + s[2] * (s[0] - c) }; });
            if (chaos::escaped (state, 1.0e3))
                restart();
            const auto scale = 1.0 / (c * 1.7);
            outputs[0] = (float) juce::jlimit (-1.0, 1.0, state[0] * scale);
            outputs[1] = (float) juce::jlimit (-1.0, 1.0, state[1] * scale);
            outputs[2] = (float) juce::jlimit (-1.0, 1.0, state[2] / (c * 2.5) * 2.0 - 1.0);
        }

    private:
        float storedFrequency = 1.0f, storedC = 5.7f;
    };

    /** Stable type id: "chaos.duffing" — Duffing (1918), the double-well
        form x'' + δx' − x + x³ = γ·cos(ωt): a ball in two valleys, pushed at
        **Frequency**. Small **Drive** (γ) rocks it in one valley; around 0.3
        with **Damping** 0.25 it starts jumping between them unpredictably —
        pitched chaos, rough and alive. */
    class DuffingNode : public SeededNode
    {
    public:
        DuffingNode() : SeededNode ("chaos.duffing.seed") {}

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override
        {
            SeededNode::reset();
            restart();
        }
        int getNumInputPorts() const noexcept override { return 3; }
        int getNumOutputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Duffing"; }
        juce::String getCategory() const override { return "Chaos"; }
        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { chaos::hz ("chaos.duffing.frequency", "Frequency", 0.01f, 5000.0f, 1.0f),
                     chaos::plain ("chaos.duffing.drive", "Drive", 0.0f, 1.0f, 0.35f),
                     chaos::plain ("chaos.duffing.damping", "Damping", 0.01f, 1.0f, 0.25f) };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { stochastic::bipolarOut ("out", "Position"), stochastic::bipolarOut ("velocity", "Velocity") };
        }
        std::vector<ParameterDescriptor> getParameters() const override { return { stochastic::seedParameter (seedId) }; }
        void setParameter (const juce::String& id, float value) override
        {
            if (handleSeed (id, value))
                restart();
            else if (id == "chaos.duffing.frequency")
                storedFrequency = value;
            else if (id == "chaos.duffing.drive")
                storedDrive = value;
            else if (id == "chaos.duffing.damping")
                storedDamping = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto frequency = juce::jlimit (0.0, 20000.0, (double) stochastic::read (inputs, 0, storedFrequency));
            const auto drive = juce::jlimit (0.0, 2.0, (double) stochastic::read (inputs, 1, storedDrive));
            const auto damping = juce::jlimit (0.0, 2.0, (double) stochastic::read (inputs, 2, storedDamping));
            // State (x, v, θ): θ is the drive's phase, advancing at ω = 1.2 time units⁻¹,
            // so time runs ω-scaled to put the drive at Frequency.
            constexpr double omega = 1.2;
            const auto timeUnits = juce::MathConstants<double>::twoPi * frequency / omega / sampleRate;
            chaos::advance (state, timeUnits, 0.05, [drive, damping] (const chaos::Vec3& s)
                            { return chaos::Vec3 { s[1], -damping * s[1] + s[0] - s[0] * s[0] * s[0] + drive * std::cos (s[2]), omega }; });
            state[2] = std::fmod (state[2], juce::MathConstants<double>::twoPi);
            if (chaos::escaped ({ state[0], state[1], 0.0 }, 1.0e2))
                restart();
            outputs[0] = (float) juce::jlimit (-1.0, 1.0, state[0] / 1.6);
            outputs[1] = (float) juce::jlimit (-1.0, 1.0, state[1] / 1.6);
        }

    private:
        void restart() noexcept { state = { 1.0 + 0.1 * ((double) random.uniform() - 0.5), 0.0, 0.0 }; }

        double sampleRate = 48000.0;
        chaos::Vec3 state { 1.0, 0.0, 0.0 };
        float storedFrequency = 1.0f, storedDrive = 0.35f, storedDamping = 0.25f;
    };

    /** Stable type id: "chaos.logistic" — Logistic Map (May, 1976):
        x ← r·x·(1 − x), one step per tick — at **Rate**, or on each
        **Trigger** when wired. **Growth** (r): below 3 it settles; 3 → 3.45
        alternates two values; then 4, 8, … ; past ~3.57 chaos with windows
        of order (3.83: a cycle of three). Out 0..1 and an event on each step.
        At audio rate it is a crunchy, structured noise. */
    class LogisticMapNode : public SeededNode
    {
    public:
        LogisticMapNode() : SeededNode ("chaos.logistic.seed") {}

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override
        {
            SeededNode::reset();
            x = 0.1 + 0.8 * (double) random.uniform();
            phase = 0.0;
        }
        int getNumInputPorts() const noexcept override { return 3; }
        int getNumOutputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Logistic Map"; }
        juce::String getCategory() const override { return "Chaos"; }
        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger", .hasFallbackWhenUnconnected = true },
                     chaos::hz ("chaos.logistic.rate", "Rate", 0.01f, 20000.0f, 8.0f),
                     chaos::plain ("chaos.logistic.growth", "Growth", 2.5f, 4.0f, 3.7f) };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true,
                                      .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Unipolar },
                     PortDescriptor { .id = "step", .type = SignalType::Event, .label = "Step" } };
        }
        std::vector<ParameterDescriptor> getParameters() const override { return { stochastic::seedParameter (seedId) }; }
        void setParameter (const juce::String& id, float value) override
        {
            if (handleSeed (id, value))
                x = 0.1 + 0.8 * (double) random.uniform();
            else if (id == "chaos.logistic.rate")
                storedRate = value;
            else if (id == "chaos.logistic.growth")
                storedGrowth = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            auto tick = false;
            if (std::isnan (inputs[0]))
            {
                phase += juce::jmax (0.0, (double) stochastic::read (inputs, 1, storedRate)) / sampleRate;
                if (phase >= 1.0)
                {
                    phase -= std::floor (phase);
                    tick = true;
                }
            }
            else
            {
                tick = inputs[0] != 0.0f;
            }
            if (tick)
            {
                const auto r = juce::jlimit (0.0, 4.0, (double) stochastic::read (inputs, 2, storedGrowth));
                x = r * x * (1.0 - x);
                if (! std::isfinite (x) || x <= 0.0 || x >= 1.0)
                    x = 0.1 + 0.8 * (double) random.uniform(); // fell onto a fixed edge: start again
            }
            outputs[0] = (float) x;
            outputs[1] = tick ? 1.0f : 0.0f;
        }

    private:
        double sampleRate = 48000.0, x = 0.4, phase = 0.0;
        float storedRate = 8.0f, storedGrowth = 3.7f;
    };
}
