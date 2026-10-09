#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "logic.not" (NODE_CATALOG.md's `logic.*` row: `in`
        -> `out`, both bool). Inverts a `SignalType::Boolean` signal. A
        Boolean is a plain 0/1 float on the wire and "true" means `> 0.5`
        (the same threshold `env.adsr`'s `gate` uses), so an unconnected
        input reads as false and this node's output as true.
    */
    class LogicNotNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Not"; }
        juce::String getCategory() const override { return "Logic"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Signal, .kind = ValueKind::Bool, .quantity = Quantity::Boolean } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .isPrimaryOutput = true, .kind = ValueKind::Bool, .quantity = Quantity::Boolean } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0] > 0.5f ? 0.0f : 1.0f;
        }
    };
}
