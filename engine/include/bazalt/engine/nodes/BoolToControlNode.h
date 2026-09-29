#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "adapt.boolToControl". Direct feedback: "bool not
        being pluggable into control and ints... I now have to do it via
        select node... annoying, since the select node doesn't even have
        editable props." Boolean -> Control is exactly as mechanical/
        opinion-free as every other adapter this family auto-inserts
        (`adapt.map`, `adapt.audioToControl`) — a plain two-value lookup,
        never a creative DSP choice — so it earns the same auto-insertion
        treatment `canConnect` already gives those.

        `whenFalse`/`whenTrue` are plain structural parameters (not wireable
        ports), matching `adapt.map`/`adapt.normalise`'s own `min`/`max`
        convention — defaulting to 0/1 covers the "just give me 0 and 1"
        case with zero setup, but editing them makes this a general
        "map false/true to any two numbers" tool too (e.g. -1/1 for a
        Bipolar destination), not a one-trick 0/1 box. `out` stays
        `Quantity::Dimensionless` on purpose — a free pass into literally
        any Control-typed destination (`canConnect`'s own same-or-
        Dimensionless rule), since the two edited values are what actually
        target the destination's real range, not the port's own declared
        quantity.
    */
    class BoolToControlNode : public Node
    {
    public:
        static constexpr float defaultWhenFalse = 0.0f;
        static constexpr float defaultWhenTrue = 1.0f;
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "From Bool"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Boolean, .kind = ValueKind::Bool } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "adapt.boolToControl.whenFalse", -100000.0f, 100000.0f, defaultWhenFalse, 1.0f, "", "When False" },
                     { "adapt.boolToControl.whenTrue", -100000.0f, 100000.0f, defaultWhenTrue, 1.0f, "", "When True" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "adapt.boolToControl.whenFalse")
                whenFalse = value;
            else if (parameterId == "adapt.boolToControl.whenTrue")
                whenTrue = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // Same "> 0.5 = true" convention every Boolean-reading node in
            // this codebase already uses (LogicNotNode.h, env.adsr's gate).
            outputs[0] = inputs[0] > 0.5f ? whenTrue : whenFalse;
        }

    private:
        float whenFalse = defaultWhenFalse;
        float whenTrue = defaultWhenTrue;
    };
}
