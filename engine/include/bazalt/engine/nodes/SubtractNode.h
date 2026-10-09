#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.subtract" (NODE_CATALOG.md's `math.*` row:
        `a`, `b`). `out = a - b` — order matters, unlike `math.add`, so the
        two ports are deliberately named `a`/`b` rather than a growable
        group. Ports are plain (undeclared quantity) exactly like
        `math.add`/`math.multiply`, so any Control-typed value connects
        directly.
    */
    class SubtractNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Subtract"; }
        juce::String getCategory() const override { return "Math"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { .id = "a", .type = SignalType::Signal }, { .id = "b", .type = SignalType::Signal } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .isPrimaryOutput = true } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0] - inputs[1];
        }
    };
}
