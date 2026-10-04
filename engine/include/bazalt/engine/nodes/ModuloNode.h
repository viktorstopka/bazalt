#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.modulo" (NODE_CATALOG.md's `math.*` row: `in`,
        `divisor` — "wrapping phase, cycling indices"). Floored modulo: the
        result always takes the divisor's sign, so a negative input wraps
        up into `[0, divisor)` instead of mirroring like C's `fmod` (which
        is what makes a phase or an index wrap correctly when it goes
        negative). A divisor of 0 gives 0 rather than NaN — same
        never-poison-the-graph stance as `math.divide`'s default.
    */
    class ModuloNode : public Node
    {
    public:
        static constexpr int numInputs = 2; // in, divisor
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Modulo"; }
        juce::String getCategory() const override { return "Math"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "in", SignalType::Control },
                PortDescriptor { .id = "math.modulo.divisor", .type = SignalType::Control, .label = "Divisor",
                                  .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "math.modulo.divisor")
                storedDivisor = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto divisor = std::isnan (inputs[1]) ? storedDivisor : inputs[1];

            if (divisor == 0.0f || ! std::isfinite (inputs[0]))
            {
                outputs[0] = 0.0f;
                return;
            }

            auto result = std::fmod (inputs[0], divisor);
            if (result != 0.0f && (result < 0.0f) != (divisor < 0.0f))
                result += divisor;

            // Float rounding can land exactly on the divisor (a tiny negative
            // input + divisor rounds up to it) — that's the wrapped-around
            // zero, not a value outside the half-open range.
            outputs[0] = (result == divisor) ? 0.0f : result;
        }

    private:
        float storedDivisor = 1.0f; // matches the port's own declared defaultValue
    };
}
