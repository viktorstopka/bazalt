#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.minmax" (NODE_CATALOG.md's `math.*` row: `a`,
        `b`; structural `mode` min/max). Picks the smaller or larger of two
        values every sample — a hard limiter when one side is a constant
        ceiling/floor, or "whichever modulator is louder" between two live
        ones. `mode` is a discrete algorithmic choice, so it's a
        `ParameterDescriptor` (isStructural), same reasoning as
        `math.round`'s mode, not a port.
    */
    class MinMaxNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Min / Max"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "a", SignalType::Control }, { "b", SignalType::Control } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "math.minmax.mode",
                                            .minValue = 0.0f,
                                            .maxValue = 1.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Mode",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "min", "Min" }, { "max", "Max" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "math.minmax.mode")
                pickMax = (int) std::round (value) == 1;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = pickMax ? std::max (inputs[0], inputs[1]) : std::min (inputs[0], inputs[1]);
        }

    private:
        bool pickMax = false; // matches enumOptions' declaration order (min=0, max=1)
    };
}
