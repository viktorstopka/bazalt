#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "mix.sum" (renamed M14, was "mix.add2"). Two Audio
        inputs, one Audio output = a + b. Generic primitive used by the
        Karplus-Strong proof graph to sum the excitation with the feedback
        loop's return path (ARCHITECTURE.md §3.4). Same deferred-growable-
        ports note as AddNode.h applies — the M14 rename doesn't claim
        this is actually growable yet (`NODE_CATALOG.md`'s `mix.sum` entry
        with a real `PortGroup`-tagged `in.0..in.N` is M21/Batch A's job);
        fixed 2-input `a`/`b` until then.
    */
    class MixNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Mix"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "a", SignalType::Audio }, { "b", SignalType::Audio } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0] + inputs[1];
        }
    };
}
