#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "mix.gain". Two inputs (Audio, Control), one Audio
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

        juce::String getTitle() const override { return "VCA"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "audio", SignalType::Audio }, { "gain", SignalType::Control } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        /** M20 step 8: a default Meter on the VCA's own output — the plan's
            own example of an obvious real-node home for a preview kind. */
        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Meter, .portId = "out" } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0] * inputs[1];
        }
    };
}
