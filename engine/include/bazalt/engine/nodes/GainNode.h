#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "amp.vca". Two inputs (Audio, Control), one Audio
        output = audio * control. The "ADSR-gated amp" in the M2 voice
        proof graph (ARCHITECTURE.md §3.4's example path).
    */
    class GainNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "audio", SignalType::Audio }, { "gain", SignalType::Control } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0] * inputs[1];
        }
    };
}
