#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.divide" (NODE_CATALOG.md's `math.*` row: `a`,
        `b`, `safeZero : bool·true` — "division by zero returns 0 rather
        than NaN").

        `safeZero` is a `ParameterDescriptor`, not a port, even though the
        catalog lists it beside `a`/`b`: it's a discrete policy choice
        (same reasoning as `math.round`'s mode), and modelling it as a
        Boolean port with an unwired fallback gave the UI a "dot" glyph
        (meaning "I have a value, connect me") with no control to change
        that value — NodeCard's PortRow has no in-node control for a
        Boolean fallback, and adding a generic one would also put a
        do-nothing control on `env.adsr`'s `gate`, whose NaN sentinel means
        "use internal note gating", not "editable value". A parameter gets
        an ordinary editable row and is never a cable target, which is all
        anything needs from this switch.

        With `safeZero` off, `b == 0` gives the IEEE result (±inf, or NaN
        for 0/0) — an explicit author choice, not something this node
        silently papers over; the plugin's final output NaN guard is what
        still protects the host if that reaches the output bus.
    */
    class DivideNode : public Node
    {
    public:
        static constexpr int numInputs = 2; // a, b
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Divide"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "a", SignalType::Control }, { "b", SignalType::Control } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "math.divide.safeZero",
                                            .minValue = 0.0f,
                                            .maxValue = 1.0f,
                                            .defaultValue = 1.0f,
                                            .displayName = "Safe Zero",
                                            .isInteger = true,
                                            .kind = ValueKind::Bool,
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "math.divide.safeZero")
                safeZero = value > 0.5f;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (inputs[1] == 0.0f && safeZero)
                outputs[0] = 0.0f;
            else
                outputs[0] = inputs[0] / inputs[1];
        }

    private:
        bool safeZero = true; // matches the parameter's own declared defaultValue
    };
}
