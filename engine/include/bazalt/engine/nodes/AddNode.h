#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.add". Two numeric (Control) inputs, one
        output = a + b — spawned by Alt-dragging between two nodes whose
        primary outputs are both numbers (NODE_EDITOR.md §7's Alt-drag
        Mix/Add/Multiply; Mix itself reuses "mix.add2" for the audio case).
        Growable-port behaviour (NODE_EDITOR.md §6.5) is a UI-side concern
        for later — this fixed 2-input version is what M7 needs.
    */
    class AddNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Add"; }
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
            outputs[0] = inputs[0] + inputs[1];
        }
    };
}
