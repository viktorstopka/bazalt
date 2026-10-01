#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.macro" (wiki/plans/UtilMacro.md, amending
        archive_docs/decisions/0015-macro-binding-migration.md — see
        archive_docs/decisions/0030-util-macro-is-a-real-wireable-node.md).
        A host-automatable, smoothed version of `util.constant` — the "Macro
        is a Constant that also binds" framing ADR-0015 already adopted,
        landed for real: this node's own output is an ordinary, freely
        wireable `Control` port (unlike ADR-0015's original literal
        mechanism, a `{targetNodeId, targetParameterId}` side-table that
        poked some OTHER node's `setParameter` directly, bypassing wires
        entirely).

        **What doesn't change from ADR-0015/ADR-0004's real, load-bearing
        decision**: the 32 `juce::AudioParameterFloat`s
        (`plugin/source/MacroParameters.h`) are still created once,
        unconditionally, at plugin construction — never grown or shrunk at
        runtime, exactly the VST3 `restartComponent` cross-host mitigation
        ARCHITECTURE.md §4.3 chose. A placed `util.macro` node merely
        CLAIMS one of those 32 pre-existing slots via its own `slot`
        structural parameter; deleting the node frees it. The host
        parameter list's shape never changes.

        **What does change**: `MacroParameters::applyToPlans()`'s existing,
        completely unmodified mechanism (walk every `MacroMapping`, smooth,
        remap into `[rangeMin,rangeMax]`, call `getNodeById(...)->
        setParameter(...)`) now targets THIS node's own id +
        `"util.macro.value"` — the macro becomes its own mapping target,
        instead of the mapping pointing at an arbitrary foreign node/param.
        `GraphEditController::recompileAndPublish()` derives the whole
        `MacroMapping` table fresh, every compile, from whichever
        `util.macro` nodes are actually in the graph and what slot each one
        claims (plugin/source/GraphEditController.cpp) — there is no
        hand-curated mapping list anymore.

        **The exposed contract** (`min`/`max`/`isInteger`/`quantity`) is set
        once via `setParameter()` — typically seeded automatically from
        whatever port this node was drag-created out of (ui/src/graph/
        graphStore.ts's `createMacroFromPort`) — and held fixed until the
        next edit, never re-resolved by the compiler the way a polymorphic
        node's ports are (`AddNode`/`MultiplyNode`/`RerouteNode`'s
        `resolveIncomingPort()`). This node deliberately does NOT declare
        `hasPolymorphicPorts()`: its port shape is config-driven
        (`setParameter`), not wire-driven, the same "store via setParameter,
        publish unchanged until the next edit" shape `DataScaleNode` already
        uses for its own scale/octaveSize — so it stays fully eligible for
        GraphCompiler's ordinary parameter-equality-gated state-pool reuse.

        **`slot` defaults to -1 ("unclaimed"), not a real slot number** — a
        deliberate sentinel (found live while designing this: creating a
        macro is necessarily `addNode` immediately followed by a SEPARATE
        `setParameterValue(slot, N)` command, each its own full recompile
        with its own slot-collision check; if the declared default were a
        real slot number, the `addNode` step alone could collide with an
        already-placed macro and reject the whole gesture before the very
        next command got a chance to fix it up). Any slot outside `[0,31]`
        is excluded from the collision check and from `MacroMapping`
        derivation, both in `GraphEditController.cpp`.
    */
    class MacroNode : public Node
    {
    public:
        static constexpr int numInputs = 0;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Macro"; }
        juce::String getCategory() const override { return "Macro"; }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true,
                                  .minValue = storedMin, .maxValue = storedMax, .isInteger = storedIsInteger,
                                  .kind = storedIsInteger ? ValueKind::Int : ValueKind::Float,
                                  .quantity = storedQuantity },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                // -1 is the "unclaimed" sentinel this class's own doc
                // comment explains — never a real slot number by default.
                ParameterDescriptor { .id = "util.macro.slot",
                                       .minValue = -1.0f, .maxValue = 31.0f, .defaultValue = -1.0f,
                                       .displayName = "Slot", .isInteger = true, .kind = ValueKind::Int,
                                       .isStructural = true },
                ParameterDescriptor { .id = "util.macro.min",
                                       .minValue = -1.0e9f, .maxValue = 1.0e9f, .defaultValue = 0.0f,
                                       .displayName = "Min", .isStructural = true },
                ParameterDescriptor { .id = "util.macro.max",
                                       .minValue = -1.0e9f, .maxValue = 1.0e9f, .defaultValue = 1.0f,
                                       .displayName = "Max", .isStructural = true },
                ParameterDescriptor { .id = "util.macro.isInteger",
                                       .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Integer", .isInteger = true, .kind = ValueKind::Bool,
                                       .isStructural = true },
                // A real Enum, not a bare 0-9 slider — same
                // ValueKind::Enum + enumOptions shape DataScaleNode's own
                // "scale"/"octaveSize" structural params already use.
                // Option `id`s match bazalt::engine::Quantity's own
                // declaration order exactly (PortDescriptor.h) so the
                // selected INDEX is the enum ordinal setParameter() below
                // casts straight to Quantity — reordering these strings
                // would silently misencode every non-Dimensionless macro.
                ParameterDescriptor { .id = "util.macro.quantity",
                                       .minValue = 0.0f, .maxValue = 9.0f, .defaultValue = 0.0f,
                                       .displayName = "Quantity", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "dimensionless", "Dimensionless" }, { "frequency", "Frequency" },
                                                         { "pitch", "Pitch" }, { "time", "Time" }, { "gain", "Gain" },
                                                         { "ratio", "Ratio" }, { "unipolar", "Unipolar" },
                                                         { "bipolar", "Bipolar" }, { "count", "Count" }, { "phase", "Phase" } },
                                       .isStructural = true },
                // Deliberately NOT "util.macro.value" here — that one is
                // written only by MacroParameters::applyToPlans()'s own
                // direct setParameter() poke, every block, never by the
                // ordinary command bridge; listing it would invite a
                // NodeCard slider fighting host automation every block.
                // Node::setParameter() has no descriptor-membership check
                // (ConstantNode/DataScaleNode already rely on exactly
                // this), so the poke still works with it absent here.
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "util.macro.value")
                storedValue = value;
            else if (parameterId == "util.macro.slot")
                storedSlot = (int) std::lround (value);
            else if (parameterId == "util.macro.min")
                storedMin = value;
            else if (parameterId == "util.macro.max")
                storedMax = value;
            else if (parameterId == "util.macro.isInteger")
                storedIsInteger = value >= 0.5f;
            else if (parameterId == "util.macro.quantity")
                storedQuantity = (Quantity) juce::jlimit (0, 9, (int) std::lround (value));
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            const auto remapped = storedMin + storedValue * (storedMax - storedMin);
            // getOutputPorts() advertises isInteger/kind=Int off storedIsInteger -
            // round here so that contract is actually honored, not just declared.
            // Previously this node was the one place in the codebase where
            // isInteger was user-configurable without processSample() respecting
            // it (InstanceVoiceNode's/SeqStepsNode's own integer-contract ports are
            // backed by a real integer internally, never a rounded-at-the-last-
            // moment float).
            outputs[0] = storedIsInteger ? std::round (remapped) : remapped;
        }

        /** Read-only, message-thread-only accessor — GraphEditController
            scans the live graph's util.macro nodes by their own declared
            `slot` PARAMETER (NodeInstance::parameters, before compile),
            not this compiled member, so this exists only for tests/
            debugging, not the real derivation path.
        */
        int getSlot() const noexcept { return storedSlot; }

    private:
        float storedValue = 0.0f;
        float storedMin = 0.0f;
        float storedMax = 1.0f;
        bool storedIsInteger = false;
        Quantity storedQuantity = Quantity::Dimensionless;
        int storedSlot = -1;
    };
}
