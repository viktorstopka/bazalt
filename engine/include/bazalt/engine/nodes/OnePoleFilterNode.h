#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.onepole". One Audio input, one Audio
        output: y[n] = c*x[n] + (1-c)*y[n-1]. The Karplus-Strong proof
        graph's damping filter — lower `coefficient` values decay/mellow
        the plucked string faster (ARCHITECTURE.md §3.4).
    */
    class OnePoleFilterNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        void reset() override { state = 0.0f; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "in", SignalType::Audio } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "filter.onepole.coefficient", 0.0f, 1.0f, 0.5f, 1.0f, "", "Damping" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "filter.onepole.coefficient")
                coefficient = juce::jlimit (0.0f, 1.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            state = coefficient * inputs[0] + (1.0f - coefficient) * state;
            outputs[0] = state;
        }

    private:
        float coefficient = 0.5f;
        float state = 0.0f;
    };
}
