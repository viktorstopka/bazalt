#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.unipolarToBipolar" (wiki/plans/
        PropsAndMacroRedesign.md Batch D) — the inverse of
        `BipolarToUnipolarNode.h`; see that file's own header comment for
        the full reasoning (a thin, self-labeled wrapper over what
        `adapt.map` already does, manual placement only, never
        auto-inserted).
    */
    class UnipolarToBipolarNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Unipolar to Bipolar"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Control, .label = "In",
                                       .minValue = 0.0f, .maxValue = 1.0f,
                                       .quantity = Quantity::Unipolar, .polarity = Polarity::Unipolar } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true,
                                       .minValue = -1.0f, .maxValue = 1.0f,
                                       .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = std::clamp (inputs[0] * 2.0f - 1.0f, -1.0f, 1.0f);
        }
    };
}
