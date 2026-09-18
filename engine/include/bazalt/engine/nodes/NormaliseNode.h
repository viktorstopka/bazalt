#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>

namespace bazalt::engine::nodes
{
    /** Stable type id: "adapt.normalise" (M16). The inverse of `adapt.map`
        (`MapNode.h`) and `SIGNAL_TYPES.md` §5's real-quantity ->
        `Unipolar` adapter — `canConnect` (`CanConnect.h`) auto-inserts
        this when a real-quantity output feeds a `Unipolar`/`Bipolar`-
        tagged input, seeded from the *source* port's own declared range
        (`AdapterStep::seedFromSourceRange`). One Control input, one
        Control output tagged `Quantity::Unipolar` — the one place in this
        node's own descriptor a quantity is hardcoded rather than
        inherited, since normalising is specifically "become Unipolar."
    */
    class NormaliseNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Normalise"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "in", SignalType::Control } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out",
                                       .type = SignalType::Control,
                                       .isPrimaryOutput = true,
                                       .quantity = Quantity::Unipolar,
                                       .polarity = Polarity::Unipolar } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "adapt.normalise.min", -100000.0f, 100000.0f, 0.0f, 1.0f, "", "Min" },
                     { "adapt.normalise.max", -100000.0f, 100000.0f, 1.0f, 1.0f, "", "Max" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "adapt.normalise.min")
                minValue = value;
            else if (parameterId == "adapt.normalise.max")
                maxValue = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto range = maxValue - minValue;
            const auto normalized = range != 0.0f ? (inputs[0] - minValue) / range : 0.0f;
            outputs[0] = std::clamp (normalized, 0.0f, 1.0f);
        }

    private:
        float minValue = 0.0f;
        float maxValue = 1.0f;
    };
}
