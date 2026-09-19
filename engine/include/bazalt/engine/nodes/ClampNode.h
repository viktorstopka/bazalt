#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.clamp" (NODE_CATALOG.md's `math.*` row:
        `in`, `low`, `high`). `low`/`high` are real ports, not parameters
        — clamping to a live-modulated range (a sidechain envelope
        limiting how far another signal can swing, say) is exactly the
        kind of thing this whole node exists for. Swapped rather than
        rejected if `low > high` (a live modulator isn't bound by any
        author-time ordering guarantee).
    */
    class ClampNode : public Node
    {
    public:
        static constexpr int numInputs = 3; // in, low, high
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Clamp"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "in", SignalType::Control },
                PortDescriptor { .id = "math.clamp.low", .type = SignalType::Control, .label = "Low",
                                  .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true },
                PortDescriptor { .id = "math.clamp.high", .type = SignalType::Control, .label = "High",
                                  .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "math.clamp.low")
                lowValue = value;
            else if (parameterId == "math.clamp.high")
                highValue = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto low = std::isnan (inputs[1]) ? lowValue : inputs[1];
            const auto high = std::isnan (inputs[2]) ? highValue : inputs[2];
            outputs[0] = std::clamp (inputs[0], std::min (low, high), std::max (low, high));
        }

    private:
        float lowValue = 0.0f;
        float highValue = 1.0f;
    };
}
