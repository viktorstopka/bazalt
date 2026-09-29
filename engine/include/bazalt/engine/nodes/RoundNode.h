#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.round" (NODE_CATALOG.md's `math.*` row).
        Quantizes `in` to the nearest multiple of `step` — `step = 1`
        (the default) gives plain integer rounding, but any step size
        works ("quantising any value, not just pitch," per the catalog's
        own note): 0.5 rounds to the nearest half, a scale-step value
        quantizes onto a musical grid, etc. `mode` picks which direction
        ties/fractions resolve: nearest (round), floor, or ceil — a
        discrete algorithmic choice, so it's a plain ParameterDescriptor
        (isStructural), same reasoning as osc.analog's shape or
        instance.sum's mode, not a port.
    */
    class RoundNode : public Node
    {
    public:
        static constexpr int numInputs = 2; // in, step
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Round"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "in", SignalType::Control },
                PortDescriptor { .id = "math.round.step", .type = SignalType::Control, .label = "Step",
                                  .minValue = 0.0f, .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "math.round.mode",
                                            .minValue = 0.0f,
                                            .maxValue = 2.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Mode",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "nearest", "Nearest" }, { "floor", "Floor" }, { "ceil", "Ceil" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "math.round.mode")
                mode = modeForValue (value);
            else if (parameterId == "math.round.step")
                stepValue = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto in = inputs[0];
            const auto step = std::isnan (inputs[1]) ? stepValue : inputs[1];

            if (step <= 0.0f)
            {
                outputs[0] = in;
                return;
            }

            const auto scaled = in / step;
            float quantized;
            switch (mode)
            {
                case Mode::Floor: quantized = std::floor (scaled); break;
                case Mode::Ceil:  quantized = std::ceil (scaled); break;
                default:          quantized = std::round (scaled); break;
            }
            outputs[0] = quantized * step;
        }

    private:
        enum class Mode
        {
            Nearest,
            Floor,
            Ceil
        };

        static Mode modeForValue (float value) noexcept
        {
            const auto index = (int) std::round (value);
            if (index == 1) return Mode::Floor;
            if (index == 2) return Mode::Ceil;
            return Mode::Nearest;
        }

        Mode mode = Mode::Nearest;
        float stepValue = 1.0f;
    };
}
