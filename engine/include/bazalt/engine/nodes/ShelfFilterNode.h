#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/Biquad.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.shelf" (NODE_CATALOG.md's `filter.*` row).
        Built on `Biquad.h`'s RBJ low/high shelf formula — see `PeakFilterNode.h`
        for the shared `gain`-is-linear-not-dB reasoning (identical here) and
        `Biquad::setShelf`'s own comment for how `slope` (0-1) maps onto RBJ's
        shelf-slope parameter `S`, this node's own documented design call
        where the catalog leaves the mapping unspecified.

        `type` (structural: low/high) picks which shelf `Biquad::setShelf`
        computes — switching it is a coefficient recompute, not a different
        code path at `processSample` time.
    */
    class ShelfFilterNode : public Node
    {
    public:
        static constexpr float defaultFrequencyHz = 1000.0f;
        static constexpr float defaultGainLinear = 1.0f;
        static constexpr float defaultSlope = 0.5f;
        static constexpr int numInputs = 3; // in, frequency, gain
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override { biquad.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Shelf Filter"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ "in", SignalType::Audio }),
                ValueTypes::frequencyPort ("filter.shelf.frequency", "Frequency", defaultFrequencyHz),
                PortDescriptor { .id = "filter.shelf.gain", .type = SignalType::Control, .label = "Gain",
                                  .minValue = 0.06f, .maxValue = 16.0f, .defaultValue = defaultGainLinear,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Gain,
                                  .curve = Curve::Logarithmic },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ "out", SignalType::Audio }) };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "filter.shelf.type",
                                       .minValue = 0.0f,
                                       .maxValue = 1.0f,
                                       .defaultValue = 0.0f,
                                       .displayName = "Type",
                                       .isInteger = true,
                                       .kind = ValueKind::Enum,
                                       .enumOptions = { { "low", "Low" }, { "high", "High" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "filter.shelf.slope",
                                       .minValue = 0.0f,
                                       .maxValue = 1.0f,
                                       .defaultValue = defaultSlope,
                                       .displayName = "Slope",
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "filter.shelf.frequency")
                storedFrequencyHz = value;
            else if (parameterId == "filter.shelf.gain")
                storedGainLinear = value;
            else if (parameterId == "filter.shelf.type")
                isHighShelf = std::lround (value) == 1;
            else if (parameterId == "filter.shelf.slope")
                slope = juce::jlimit (0.0f, 1.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto frequency = std::isnan (inputs[1]) ? storedFrequencyHz : inputs[1];
            const auto gain = std::isnan (inputs[2]) ? storedGainLinear : inputs[2];

            if (frequency != cache.frequency || gain != cache.gain || slope != cache.slope || isHighShelf != cache.isHighShelf)
            {
                cache = { frequency, gain, slope, isHighShelf };
                biquad.setShelf (sampleRate, frequency, gain, slope, isHighShelf);
            }

            outputs[0] = biquad.processSample (inputs[0]);
        }

    private:
        struct Cache
        {
            float frequency = -1.0f, gain = -1.0f, slope = -1.0f;
            bool isHighShelf = false;
        };

        double sampleRate = 44100.0;
        float storedFrequencyHz = defaultFrequencyHz;
        float storedGainLinear = defaultGainLinear;
        float slope = defaultSlope;
        bool isHighShelf = false;
        Biquad biquad;
        Cache cache;
    };
}
