#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.multiply". Two numeric (Control) inputs, one
        output = a * b — the other half of Alt-drag's number+number case
        (NODE_EDITOR.md §7), alongside AddNode.
    */
    class MultiplyNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Multiply"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "a", SignalType::Control }, { "b", SignalType::Control } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0] * inputs[1];
        }
    };
}
