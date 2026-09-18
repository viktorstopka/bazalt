#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "io.output". One Audio input, one Audio output,
        unity pass-through — the "Master Out" node in the design reference.
        Deliberately just a conventional, visually-meaningful anchor point
        in the graph, not a compiler special case: `NodeGraph::setOutput()`
        designates this node's own "out" port exactly as it would any other
        node's, so the existing final-output mechanism needs no changes to
        support it.
    */
    class OutputNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Master Out"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "in", SignalType::Audio } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }
    };
}
