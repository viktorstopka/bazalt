#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.constant". No inputs, one output holding a
        fixed value set via parameter — what Unwrapping a plain slider value
        creates (NODE_EDITOR.md §4), and what a placeholder dropdown value
        becomes once Unwrapped into a real node.
    */
    class ConstantNode : public Node
    {
    public:
        static constexpr int numInputs = 0;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Constant"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "util.constant.value", -100000.0f, 100000.0f, 0.0f, 1.0f, "", "Value" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "util.constant.value")
                storedValue = value;
        }

        void processSample (const float*, float* outputs) noexcept override { outputs[0] = storedValue; }

    private:
        float storedValue = 0.0f;
    };
}
