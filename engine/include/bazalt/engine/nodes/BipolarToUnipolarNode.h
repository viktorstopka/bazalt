#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.bipolarToUnipolar" (wiki/plans/
        PropsAndMacroRedesign.md Batch D). A thin, self-labeled wrapper
        over what `adapt.remap` can already do (`inMin=-1,inMax=1,
        outMin=0,outMax=1`) — same shape as `adapt.normalise`/`adapt.map`/
        `adapt.pitchToFrequency`: a generic mechanism already covers the
        capability, this exists purely so "Bipolar -> Unipolar" reads
        instantly in the Add-menu search instead of a user configuring four
        Remap ports correctly every time for one of its most common uses.

        Deliberately NOT auto-inserted by `GraphEditController::
        connectWithAutoAdapt` — a Unipolar<->Bipolar quantity mismatch
        already resolves today via the generic `adapt.remap` fallback in
        `CanConnect.cpp`, so this node adds readability, not connectivity.
        Manual placement only, per direct instruction.
    */
    class BipolarToUnipolarNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Bipolar to Unipolar"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Control, .label = "In",
                                       .minValue = -1.0f, .maxValue = 1.0f,
                                       .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true,
                                       .minValue = 0.0f, .maxValue = 1.0f,
                                       .quantity = Quantity::Unipolar, .polarity = Polarity::Unipolar } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = std::clamp (inputs[0] * 0.5f + 0.5f, 0.0f, 1.0f);
        }
    };
}
