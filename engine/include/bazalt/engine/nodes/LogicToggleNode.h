#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "logic.toggle" (NODE_CATALOG.md's `logic.*` row:
        `trigger : Event`, `reset : Event` -> `out` bool). A flip-flop: each
        `trigger` event flips the output; a `reset` event forces it to
        `initialState`, not unconditionally false (see that parameter's own
        comment). If both fire on the same sample, reset wins (a reset that
        can be overridden by a coincident trigger isn't a reset).

        Events are "non-zero = fired this sample" (ThresholdNode's own
        output convention), so every non-zero sample counts as one event —
        this node does no edge detection of its own, and a source that
        holds an Event high for several samples will flip several times.

        `reset()` (voice restart, not the `reset` INPUT PORT above — same
        name, unrelated mechanisms) also returns the output to
        `initialState`.
    */
    class LogicToggleNode : public Node
    {
    public:
        static constexpr int numInputs = 2; // trigger, reset
        static constexpr int numOutputs = 1;

        void reset() override { on = initialState; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Toggle"; }
        juce::String getCategory() const override { return "Logic"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "trigger", SignalType::Event }, { "reset", SignalType::Event } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Boolean, .isPrimaryOutput = true, .kind = ValueKind::Bool } };
        }

        /** wiki/plans/PropsAndMacroRedesign.md Batch B: the concrete "an
            initial state in a toggle" prop that motivated adding a real
            Boolean structural-parameter rendering path in the first place
            — this node previously had no way to configure it at all,
            always hardcoding `on = false` both as the member initializer
            and in `reset()`. Structural (NODES.System.md §3's own test: a
            one-time startup condition, not something to modulate over a
            cable) — changing it needs a fresh node instance, same as every
            other structural parameter.
        */
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "logic.toggle.initialState",
                                       .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Initial State", .isInteger = true, .kind = ValueKind::Bool,
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "logic.toggle.initialState")
            {
                initialState = value >= 0.5f;
                on = initialState;
            }
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // fabs(x) > 0 is false for NaN, so a stray NaN is never an event.
            const auto triggered = std::fabs (inputs[0]) > 0.0f;
            const auto resetFired = std::fabs (inputs[1]) > 0.0f;

            if (resetFired)
                on = initialState;
            else if (triggered)
                on = ! on;

            outputs[0] = on ? 1.0f : 0.0f;
        }

    private:
        bool initialState = false;
        bool on = false;
    };
}
