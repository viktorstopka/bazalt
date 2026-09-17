#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.reroute". The Knob decoration (NODE_EDITOR.md
        §6.6): one input, passes it straight through. Fan-out to multiple
        destinations needs no special support here — any node's output can
        already feed multiple downstream inputs (GraphCompiler resolves
        each consumer's input independently against the same producer
        location), so Reroute's only job is to give that fan-out point a
        place to sit and be moved/renamed on the canvas.

        Declared Audio-typed: the real "take on the source's connected
        type" behaviour the design reference shows (grey until connected,
        then the source's colour) is a UI-side rendering behaviour, not an
        engine one — the engine doesn't type-check connections at all
        today (NODE_EDITOR.md §5's classification is UI-side). Revisit if a
        future node genuinely needs a type-polymorphic port.
    */
    class RerouteNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Reroute"; }
        juce::String getCategory() const override { return "Utility"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Decoration; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "in", SignalType::Audio } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Audio, .isPrimaryOutput = true } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }
    };
}
