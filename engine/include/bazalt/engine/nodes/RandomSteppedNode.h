#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "random.stepped" (NODE_CATALOG.md's `random.*` row).
        On each trigger, draws from a distribution and glides toward it.

        **`trigger` is the first Event port in this codebase to use
        `hasFallbackWhenUnconnected`.** The mechanism (a NaN sentinel for
        "nothing wired here") is normally used for Control-style knob ports,
        but it's type-agnostic at the buffer level, and this node genuinely
        needs to tell "truly unconnected" apart from "connected, not firing
        this sample" — the catalog's own spec ("unconnected, it free-runs at
        `rate`") requires exactly that distinction. Every other Event port
        in this codebase reads plain 0 when unconnected and that's still
        correct here too (an unconnected/NaN trigger is never itself a
        "fired" reading under the ordinary "non-zero = fired" convention);
        this just ALSO lets the node notice the NaN and drive its own
        internal free-running clock instead.

        **`amount`** has no description at all in NODE_CATALOG.md's own
        entry (`spread`/`bias`/`chance`/`smooth` all do) — this node's own
        documented design call: a final output-depth multiplier, applied
        after bias/spread/quantization, distinct from `spread` (which
        shapes the raw DISTRIBUTION's width before biasing). `amount = 0`
        means the output sits at `bias` regardless of what's drawn;
        `amount = 1` (default) is the full range `spread` allows.

        **Distributions** (`gaussian`/`exponential`/`bimodal`) are hand-rolled
        from `juce::Random::nextFloat()` (Box-Muller for Gaussian — JUCE has
        no built-in non-uniform distribution) rather than pulled from
        `<random>`, so `seed` alone determines the whole output stream:
        `juce::Random` is the same seeded generator `NoiseBurstNode.h`
        already uses elsewhere in this codebase, kept consistent rather than
        introducing a second RNG convention.

        **`smooth`** glide time is derived from `rate`: `smooth=1` glides
        over roughly one full cycle at the current rate (`1/rate` seconds) —
        `smooth=0` is an instant jump. Memoised via the same
        `CoefficientCache` idiom `SlewNode.h` established, keyed on the
        `(rate, smooth)` pair since both feed the time constant.
    */
    class RandomSteppedNode : public Node
    {
    public:
        static constexpr float defaultRateHz = 5.0f;
        static constexpr int numInputs = 8; // trigger, rate, amount, smooth, bias, spread, steps, chance
        static constexpr int numOutputs = 2; // out, changed

        enum class Distribution { Uniform, Gaussian, Exponential, Bimodal };
        enum class OutputPolarity { Bipolar, Unipolar };

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override
        {
            phase = 0.0;
            current = 0.0f;
            target = 0.0f;
            random = juce::Random ((juce::int64) seed);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Random"; }
        juce::String getCategory() const override { return "Random"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger",
                                  .hasFallbackWhenUnconnected = true },
                PortDescriptor { .id = "random.stepped.rate", .type = SignalType::Control, .label = "Rate",
                                  .unit = "Hz", .minValue = 0.01f, .maxValue = 1000.0f, .defaultValue = defaultRateHz,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                  .curve = Curve::Logarithmic },
                PortDescriptor { .id = "random.stepped.amount", .type = SignalType::Control, .label = "Amount",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "random.stepped.smooth", .type = SignalType::Control, .label = "Smooth",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "random.stepped.bias", .type = SignalType::Control, .label = "Bias",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Bipolar,
                                  .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "random.stepped.spread", .type = SignalType::Control, .label = "Spread",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "random.stepped.steps", .type = SignalType::Control, .label = "Steps",
                                  .minValue = 0.0f, .maxValue = 64.0f, .defaultValue = 0.0f, .isInteger = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Count },
                PortDescriptor { .id = "random.stepped.chance", .type = SignalType::Control, .label = "Chance",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true,
                                  .minValue = -1.0f, .maxValue = 1.0f, .quantity = Quantity::Bipolar },
                PortDescriptor { .id = "changed", .type = SignalType::Event, .label = "Changed" },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "random.stepped.distribution",
                                       .minValue = 0.0f,
                                       .maxValue = 3.0f,
                                       .defaultValue = 0.0f,
                                       .displayName = "Distribution",
                                       .isInteger = true,
                                       .kind = ValueKind::Enum,
                                       .enumOptions = { { "uniform", "Uniform" }, { "gaussian", "Gaussian" },
                                                         { "exponential", "Exponential" }, { "bimodal", "Bimodal" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "random.stepped.polarity",
                                       .minValue = 0.0f,
                                       .maxValue = 1.0f,
                                       .defaultValue = 0.0f,
                                       .displayName = "Polarity",
                                       .isInteger = true,
                                       .kind = ValueKind::Enum,
                                       .enumOptions = { { "bipolar", "Bipolar" }, { "unipolar", "Unipolar" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "random.stepped.seed",
                                       .minValue = 0.0f,
                                       .maxValue = 999999.0f,
                                       .defaultValue = 1.0f, // deterministic by default - reproducible, test-friendly
                                       .displayName = "Seed",
                                       .isInteger = true,
                                       .quantity = Quantity::Count,
                                       .step = 1.0f,
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "random.stepped.rate")
                storedRateHz = juce::jmax (0.01f, value);
            else if (parameterId == "random.stepped.amount")
                storedAmount = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "random.stepped.smooth")
                storedSmooth = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "random.stepped.bias")
                storedBias = juce::jlimit (-1.0f, 1.0f, value);
            else if (parameterId == "random.stepped.spread")
                storedSpread = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "random.stepped.steps")
                storedSteps = juce::jmax (0.0f, value);
            else if (parameterId == "random.stepped.chance")
                storedChance = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "random.stepped.distribution")
                distribution = (Distribution) juce::jlimit (0, 3, (int) std::lround (value));
            else if (parameterId == "random.stepped.polarity")
                polarity = std::lround (value) == 1 ? OutputPolarity::Unipolar : OutputPolarity::Bipolar;
            else if (parameterId == "random.stepped.seed")
            {
                seed = (int) std::lround (value);
                random = juce::Random ((juce::int64) seed);
            }
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto rate = std::isnan (inputs[1]) ? storedRateHz : juce::jmax (0.01f, inputs[1]);
            const auto amount = std::isnan (inputs[2]) ? storedAmount : juce::jlimit (0.0f, 1.0f, inputs[2]);
            const auto smooth = std::isnan (inputs[3]) ? storedSmooth : juce::jlimit (0.0f, 1.0f, inputs[3]);
            const auto bias = std::isnan (inputs[4]) ? storedBias : juce::jlimit (-1.0f, 1.0f, inputs[4]);
            const auto spread = std::isnan (inputs[5]) ? storedSpread : juce::jlimit (0.0f, 1.0f, inputs[5]);
            const auto steps = std::isnan (inputs[6]) ? storedSteps : juce::jmax (0.0f, inputs[6]);
            const auto chance = std::isnan (inputs[7]) ? storedChance : juce::jlimit (0.0f, 1.0f, inputs[7]);

            auto triggeredThisSample = false;

            if (std::isnan (inputs[0])) // truly unconnected: free-run our own clock
            {
                phase += (double) rate / sampleRate;
                if (phase >= 1.0)
                {
                    phase -= std::floor (phase);
                    triggeredThisSample = true;
                }
            }
            else
            {
                triggeredThisSample = std::fabs (inputs[0]) > 0.0f;
            }

            auto changedThisSample = false;

            if (triggeredThisSample && random.nextFloat() < chance)
            {
                auto raw = drawFromDistribution(); // roughly [-1, 1]
                auto value = bias + amount * spread * raw;

                if (steps > 0.5f)
                {
                    const auto n = (float) std::round (steps);
                    value = std::round ((value + 1.0f) * 0.5f * n) / n * 2.0f - 1.0f;
                }

                target = juce::jlimit (-1.0f, 1.0f, value);
                changedThisSample = true;
            }

            const auto glideTimeSeconds = (double) smooth / (double) rate;
            current += (target - current) * (1.0f - coefficientFor (rate, smooth, glideTimeSeconds));

            outputs[0] = polarity == OutputPolarity::Unipolar ? current * 0.5f + 0.5f : current;
            outputs[1] = changedThisSample ? 1.0f : 0.0f;
        }

    private:
        float drawFromDistribution() noexcept
        {
            switch (distribution)
            {
                case Distribution::Gaussian:
                {
                    // Box-Muller (juce::Random has no built-in Gaussian draw).
                    const auto u1 = juce::jmax (1.0e-6f, random.nextFloat());
                    const auto u2 = random.nextFloat();
                    const auto z0 = std::sqrt (-2.0f * std::log (u1)) * std::cos (juce::MathConstants<float>::twoPi * u2);
                    return juce::jlimit (-1.0f, 1.0f, z0 * 0.35f); // ~99.7% within [-1,1] (3 sigma)
                }
                case Distribution::Exponential:
                {
                    const auto u = juce::jmax (1.0e-6f, random.nextFloat());
                    const auto sign = random.nextBool() ? 1.0f : -1.0f;
                    return juce::jlimit (-1.0f, 1.0f, sign * (1.0f - std::exp (-3.0f * u)));
                }
                case Distribution::Bimodal:
                {
                    const auto sign = random.nextBool() ? 1.0f : -1.0f;
                    return juce::jlimit (-1.0f, 1.0f, sign * (0.6f + 0.4f * random.nextFloat()));
                }
                case Distribution::Uniform:
                default:
                    return random.nextFloat() * 2.0f - 1.0f;
            }
        }

        struct CoefficientCache
        {
            float rate = -1.0f, smooth = -1.0f;
            float coefficient = 0.0f;
        };

        float coefficientFor (float rate, float smooth, double glideTimeSeconds) noexcept
        {
            if (rate != cache.rate || smooth != cache.smooth)
            {
                cache.rate = rate;
                cache.smooth = smooth;
                cache.coefficient = glideTimeSeconds > 0.0 ? (float) std::exp (-1.0 / (glideTimeSeconds * sampleRate)) : 0.0f;
            }
            return cache.coefficient;
        }

        double sampleRate = 44100.0;
        double phase = 0.0;
        float current = 0.0f;
        float target = 0.0f;
        float storedRateHz = defaultRateHz;
        float storedAmount = 1.0f;
        float storedSmooth = 0.0f;
        float storedBias = 0.0f;
        float storedSpread = 1.0f;
        float storedSteps = 0.0f;
        float storedChance = 1.0f;
        Distribution distribution = Distribution::Uniform;
        OutputPolarity polarity = OutputPolarity::Bipolar;
        int seed = 1;
        juce::Random random { (juce::int64) 1 };
        CoefficientCache cache;
    };
}
