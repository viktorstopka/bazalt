#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "logic.select" (NODE_CATALOG.md's `logic.*` row:
        `condition` (bool), `whenTrue`, `whenFalse` -> `out` — "one node for
        every signal type: input types must match, output quantity
        inherited"). `out = condition ? whenTrue : whenFalse`, per sample.

        `whenTrue`, `whenFalse` and `out` share one type and quantity, taken
        from whatever is wired (InheritingPortsNode.h); `whenTrue` outranks
        `whenFalse` if they disagree. `condition` stays a plain Boolean and
        never influences that. It works for Audio, Control, Boolean and Event
        values. It does NOT carry Note: the compiler supports one Note input
        per node and this has two data inputs, so a Note cable is left
        un-adopted and rejected as an ordinary type mismatch.

        "True" is `> 0.5` (the threshold `env.adsr`'s gate uses); an unwired
        `condition` reads false, so an unwired select passes `whenFalse`.
        Switching is hard, per-sample — no crossfade; use `mix.crossfade` when
        a smooth blend is what's wanted.
    */
    class LogicSelectNode : public InheritingPortsNode
    {
    public:
        static constexpr int numInputs = 3; // condition, whenTrue, whenFalse
        static constexpr int numOutputs = 1;

        LogicSelectNode() noexcept : InheritingPortsNode (SignalType::Control) {}

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Select"; }
        juce::String getCategory() const override { return "Logic"; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "whenTrue")
                offer (0, source, true);
            else if (toPortId == "whenFalse")
                offer (1, source, true);
            // "condition" is a fixed Boolean, not a source of type or quantity.
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "condition", .type = SignalType::Boolean, .label = "Condition", .kind = ValueKind::Bool },
                PortDescriptor { .id = "whenTrue", .type = resolvedType, .label = "When True",
                                  .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::SignalAndQuantity },
                PortDescriptor { .id = "whenFalse", .type = resolvedType, .label = "When False",
                                  .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::SignalAndQuantity },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = resolvedType, .isPrimaryOutput = true,
                                       .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0] > 0.5f ? inputs[1] : inputs[2];
        }
    };
}
