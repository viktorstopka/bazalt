#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "logic.toggle" (NODE_CATALOG.md's `logic.*` row:
        `trigger : Event`, `reset : Event` -> `out` bool). A flip-flop: each
        `trigger` event flips the output; a `reset` event forces it false.
        If both fire on the same sample, reset wins (a reset that can be
        overridden by a coincident trigger isn't a reset).

        Events are "non-zero = fired this sample" (ThresholdNode's own
        output convention), so every non-zero sample counts as one event —
        this node does no edge detection of its own, and a source that
        holds an Event high for several samples will flip several times.
        Starts false, and `reset()` (voice restart) returns it to false.
    */
    class LogicToggleNode : public Node
    {
    public:
        static constexpr int numInputs = 2; // trigger, reset
        static constexpr int numOutputs = 1;

        void reset() override { on = false; }

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

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // fabs(x) > 0 is false for NaN, so a stray NaN is never an event.
            const auto triggered = std::fabs (inputs[0]) > 0.0f;
            const auto resetFired = std::fabs (inputs[1]) > 0.0f;

            if (resetFired)
                on = false;
            else if (triggered)
                on = ! on;

            outputs[0] = on ? 1.0f : 0.0f;
        }

    private:
        bool on = false;
    };
}
