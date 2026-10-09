#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/Biquad.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.peak" (NODE_CATALOG.md's `filter.*` row: a
        bell/peaking EQ band). Built on `Biquad.h`'s RBJ peaking formula.

        `gain` is a genuine LINEAR amplitude multiplier (0.06-16x, matching
        the catalog exactly and VALUE_MODEL.md §3's canonical "gain is
        linear, dB is display-only" rule) — NOT `ValueTypes::gainDbPort`,
        whose own header comment flags it as storing dB directly, a known
        deviation from that rule left in place only because nothing used it
        yet. `mix.gain`'s own "gain" port carries no range/quantity metadata
        at all; this port is more complete, not less compliant.

        Coefficients are recomputed only when `frequency`/`gain`/`q` actually
        changed since the last sample (`CoefficientCache`, `SlewNode.h`'s
        idiom) — cheap in the common case (a static or slowly-modulated
        band), correct but costlier under genuine full audio-rate
        modulation of any of the three, since each recompute involves real
        trig calls (`Biquad::setPeak`).
    */
    class PeakFilterNode : public Node
    {
    public:
        static constexpr float defaultFrequencyHz = 1000.0f;
        static constexpr float defaultGainLinear = 1.0f;
        static constexpr float defaultQ = 1.0f;
        static constexpr int numInputs = 4; // in, frequency, gain, q
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override { biquad.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Peak Filter"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }),
                ValueTypes::frequencyPort ("filter.peak.frequency", "Frequency", defaultFrequencyHz),
                PortDescriptor { .id = "filter.peak.gain", .type = SignalType::Signal, .label = "Gain",
                                  .minValue = 0.06f, .maxValue = 16.0f, .defaultValue = defaultGainLinear,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Gain,
                                  .curve = Curve::Logarithmic },
                PortDescriptor { .id = "filter.peak.q", .type = SignalType::Signal, .label = "Q",
                                  .minValue = 0.1f, .maxValue = 30.0f, .defaultValue = defaultQ,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Ratio,
                                  .curve = Curve::Logarithmic },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ .id = "out", .type = SignalType::Signal, .quantity = Quantity::Audio }) };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "filter.peak.frequency")
                storedFrequencyHz = value;
            else if (parameterId == "filter.peak.gain")
                storedGainLinear = value;
            else if (parameterId == "filter.peak.q")
                storedQ = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto frequency = std::isnan (inputs[1]) ? storedFrequencyHz : inputs[1];
            const auto gain = std::isnan (inputs[2]) ? storedGainLinear : inputs[2];
            const auto q = std::isnan (inputs[3]) ? storedQ : inputs[3];

            if (frequency != cache.frequency || gain != cache.gain || q != cache.q)
            {
                cache = { frequency, gain, q };
                biquad.setPeak (sampleRate, frequency, q, gain);
            }

            outputs[0] = biquad.processSample (inputs[0]);
        }

    private:
        struct Cache
        {
            float frequency = -1.0f, gain = -1.0f, q = -1.0f;
        };

        double sampleRate = 44100.0;
        float storedFrequencyHz = defaultFrequencyHz;
        float storedGainLinear = defaultGainLinear;
        float storedQ = defaultQ;
        Biquad biquad;
        Cache cache;
    };
}
