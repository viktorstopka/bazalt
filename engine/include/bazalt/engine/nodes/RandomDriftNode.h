#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "random.drift" (NODE_CATALOG.md's `random.*` row:
        "slow, correlated, natural wander... bounded 1/f-style noise,
        guaranteed to stay in range").

        A per-sample bounded random walk: `value += push*noise -
        leak*value`, both `push` and `leak` derived from `rate`, with
        `centering` setting the ratio between them (`centering=0`: no leak
        at all, a free walk exactly as the catalog names it; `centering=1`:
        a strong pull toward 0 alongside the push). A hard `[-1,1]` clamp is
        the safety net underneath that, not the normal-operation mechanism —
        `centering=0` is genuinely meant to wander freely, including toward
        the edges, per the catalog's own wording.

        **`spectrum`** (structural: brown/pink/white-filtered) is an
        honestly-approximate character knob, not a precise 1/f filter design
        — real colored-noise shaping is `noise.colored`'s job (M23), a
        different node with a different purpose. Here it's a FIXED one-pole
        smoothing coefficient applied to the raw noise increment before
        integration (`white-filtered`: none, the fastest/least-correlated
        option this node offers; `pink`: moderate; `brown`, the default:
        heaviest, the slowest/smoothest). Every mode's OUTPUT still trends
        toward brown/red character regardless, since leaky-integrating ANY
        white noise does that by construction — `spectrum` only changes how
        much EXTRA smoothing sits on top, giving three clearly different,
        clearly documented characters rather than claiming exact spectral
        accuracy this node was never meant to provide.

        `seed` behaves exactly as `random.stepped.seed` does — deterministic
        by default (1, not time-based), same `juce::Random` convention.
    */
    class RandomDriftNode : public Node
    {
    public:
        static constexpr float defaultRateHz = 0.2f;
        static constexpr float defaultAmount = 0.3f;
        static constexpr float defaultCentering = 0.5f;
        static constexpr int numInputs = 3; // rate, amount, centering
        static constexpr int numOutputs = 1;

        enum class Spectrum { Brown, Pink, WhiteFiltered };

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override
        {
            value = 0.0f;
            filteredNoise = 0.0f;
            random = juce::Random ((juce::int64) seed);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Drift"; }
        juce::String getCategory() const override { return "Random"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "random.drift.rate", .type = SignalType::Control, .label = "Rate",
                                  .unit = "Hz", .minValue = 0.001f, .maxValue = 20.0f, .defaultValue = defaultRateHz,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                  .curve = Curve::Logarithmic },
                PortDescriptor { .id = "random.drift.amount", .type = SignalType::Control, .label = "Amount",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultAmount,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "random.drift.centering", .type = SignalType::Control, .label = "Centering",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultCentering,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true,
                                       .minValue = -1.0f, .maxValue = 1.0f, .quantity = Quantity::Bipolar,
                                       .polarity = Polarity::Bipolar } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "random.drift.spectrum",
                                       .minValue = 0.0f,
                                       .maxValue = 2.0f,
                                       .defaultValue = 0.0f,
                                       .displayName = "Spectrum",
                                       .isInteger = true,
                                       .kind = ValueKind::Enum,
                                       .enumOptions = { { "brown", "Brown" }, { "pink", "Pink" }, { "whiteFiltered", "White (filtered)" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "random.drift.seed",
                                       .minValue = 0.0f,
                                       .maxValue = 999999.0f,
                                       .defaultValue = 1.0f,
                                       .displayName = "Seed",
                                       .isInteger = true,
                                       .quantity = Quantity::Count,
                                       .step = 1.0f,
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value_) override
        {
            if (parameterId == "random.drift.rate")
                storedRateHz = juce::jmax (0.001f, value_);
            else if (parameterId == "random.drift.amount")
                storedAmount = juce::jlimit (0.0f, 1.0f, value_);
            else if (parameterId == "random.drift.centering")
                storedCentering = juce::jlimit (0.0f, 1.0f, value_);
            else if (parameterId == "random.drift.spectrum")
                spectrum = (Spectrum) juce::jlimit (0, 2, (int) std::lround (value_));
            else if (parameterId == "random.drift.seed")
            {
                seed = (int) std::lround (value_);
                random = juce::Random ((juce::int64) seed);
            }
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto rate = std::isnan (inputs[0]) ? storedRateHz : juce::jmax (0.001f, inputs[0]);
            const auto amount = std::isnan (inputs[1]) ? storedAmount : juce::jlimit (0.0f, 1.0f, inputs[1]);
            const auto centering = std::isnan (inputs[2]) ? storedCentering : juce::jlimit (0.0f, 1.0f, inputs[2]);

            const auto smoothing = spectrum == Spectrum::Brown ? 0.9f : spectrum == Spectrum::Pink ? 0.5f : 0.0f;
            const auto rawNoise = random.nextFloat() * 2.0f - 1.0f;
            filteredNoise = smoothing * filteredNoise + (1.0f - smoothing) * rawNoise;

            const auto ratePerSample = (float) ((double) rate / sampleRate);
            const auto push = ratePerSample * amount * pushScale;
            const auto leak = ratePerSample * centering * leakScale;

            value += push * filteredNoise - leak * value;
            value = juce::jlimit (-1.0f, 1.0f, value);

            outputs[0] = value;
        }

    private:
        // Empirically calibrated (a throwaway probe, not committed) so the
        // default rate/amount/centering produce an audibly slow, bounded
        // wander rather than either near-silence or a walk that pins to the
        // clamp within a second.
        static constexpr float pushScale = 800.0f;
        static constexpr float leakScale = 160.0f;

        double sampleRate = 44100.0;
        float value = 0.0f;
        float filteredNoise = 0.0f;
        float storedRateHz = defaultRateHz;
        float storedAmount = defaultAmount;
        float storedCentering = defaultCentering;
        Spectrum spectrum = Spectrum::Brown;
        int seed = 1;
        juce::Random random { (juce::int64) 1 };
    };
}
