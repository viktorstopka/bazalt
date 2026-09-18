#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.add" (renamed M14, was "util.add"). Two
        numeric (Control) inputs, one output = a + b — spawned by
        Alt-dragging between two nodes whose primary outputs are both
        numbers (NODE_EDITOR.md §7's Alt-drag Mix/Add/Multiply; Mix
        itself reuses "mix.sum" for the audio case).

        Growable-port behaviour (SIGNAL_TYPES.md §6, `PortGroup` in
        PortDescriptor.h since M14) is still deferred — this fixed
        2-input version is what M7 needs and what M14 leaves unchanged.
        `NODE_CATALOG.md`'s `math.add` entry is the real, growable
        version this node becomes in M21 (Batch A): a `PortGroup`-tagged
        `in.0..in.N`, `processSample` summing however many are actually
        wired, `numInputs` no longer a compile-time constant. Don't treat
        the M14 schema addition as having already done that work — the
        struct exists, nothing here uses it yet.
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
