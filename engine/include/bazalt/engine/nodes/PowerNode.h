#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.power" (NODE_CATALOG.md's `math.*` row: `in`,
        `exponent : float·Ratio·0.01–10·log·1`, "curve shaping for
        modulation"). `exponent` is a live-modulatable port, log-scaled so
        the useful sub-1 (convex) and super-1 (concave) ranges get equal
        slider travel; 1 is the identity.

        Sign-preserving: `out = sign(in) * |in|^exponent`. A plain `pow()`
        of a negative base with a fractional exponent is NaN, and this node
        is routinely fed a bipolar LFO, so the odd-symmetric extension is
        the only definition that stays finite for every input (and is
        exactly `pow` for the 0..1 unipolar signals it's mostly used on).
    */
    class PowerNode : public Node
    {
    public:
        static constexpr int numInputs = 2; // in, exponent
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Power"; }
        juce::String getCategory() const override { return "Math"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { .id = "in", .type = SignalType::Signal },
                PortDescriptor { .id = "math.power.exponent", .type = SignalType::Signal, .label = "Exponent",
                                  .minValue = 0.01f, .maxValue = 10.0f, .defaultValue = 1.0f, .isLogScale = true,
                                  .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Ratio, .curve = Curve::Logarithmic },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "math.power.exponent")
                storedExponent = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto in = inputs[0];
            const auto exponent = std::isnan (inputs[1]) ? storedExponent : inputs[1];

            outputs[0] = std::copysign (std::pow (std::fabs (in), exponent), in);
        }

    private:
        float storedExponent = 1.0f; // matches the port's own declared defaultValue
    };
}
