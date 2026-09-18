#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>

namespace bazalt::engine::nodes
{
    /** Stable type id: "adapt.map". Remaps a normalised 0..1 Modulation
        input onto a real-unit Value output — what connecting Modulation to
        Value auto-inserts (NODE_EDITOR.md §5), seeded from the target
        port's own min/max metadata at insertion time (a UI-side concern;
        this node just implements the remap once min/max are set).
    */
    class MapNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Map"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Control, .minValue = 0.0f, .maxValue = 1.0f } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "adapt.map.min", -100000.0f, 100000.0f, 0.0f, 1.0f, "", "Min" },
                     { "adapt.map.max", -100000.0f, 100000.0f, 1.0f, 1.0f, "", "Max" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "adapt.map.min")
                minValue = value;
            else if (parameterId == "adapt.map.max")
                maxValue = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto normalized = std::clamp (inputs[0], 0.0f, 1.0f);
            outputs[0] = minValue + normalized * (maxValue - minValue);
        }

    private:
        float minValue = 0.0f;
        float maxValue = 1.0f;
    };
}
