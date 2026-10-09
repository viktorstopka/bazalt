#pragma once

#include "bazalt/engine/nodes/TypedValueNodeBase.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.macro" (wiki/plans/UtilMacro.md, amending
        archive_docs/decisions/0015-macro-binding-migration.md — see
        archive_docs/decisions/0030-util-macro-is-a-real-wireable-node.md).
        A host-automatable, smoothed version of `util.constant` — the "Macro
        is a Constant that also binds" framing ADR-0015 already adopted,
        landed for real: this node's own output is an ordinary, freely
        wireable port (unlike ADR-0015's original literal mechanism, a
        `{targetNodeId, targetParameterId}` side-table that poked some OTHER
        node's `setParameter` directly, bypassing wires entirely).

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

        **wiki/plans/PropsAndMacroRedesign.md Batch E**: the exposed
        contract is `type`/`isInteger`/`isEnum`/`min`/`max`/`quantity`
        (`TypedValueNodeBase.h`) — `type` is `Control`/`Bool`/`Trigger`
        (three, not five: Value/Modulation/Int are `Control` "subtypes"
        that fall out of `quantity`/`isInteger` instead of being their own
        parallel type values, and `isEnum` is literally "Int toggled to be
        Enum" — direct instruction, 2026-10-03, correcting this contract's
        first cut). `type` decides this node's own output `SignalType`/
        `ValueKind` (a Bool macro's output genuinely IS
        `SignalType::Boolean`; a Control macro's `isInteger`/`isEnum`/
        `min`/`max` describe a plain range or an option-index range). Set
        once via `setParameter()`, typically
        seeded automatically from whatever port this node was drag-created
        out of (`ui/src/graph/graphStore.ts`'s `createMacroFromPort`) — and
        held fixed until the next edit, never re-resolved by the compiler
        the way a polymorphic node's ports are. This node deliberately does
        NOT declare `hasPolymorphicPorts()`: its port shape is config-driven
        (`setParameter`), not wire-driven, the same "store via setParameter,
        publish unchanged until the next edit" shape `DataScaleNode` already
        uses for its own scale/octaveSize — so it stays fully eligible for
        GraphCompiler's ordinary parameter-equality-gated state-pool reuse.

        **`Trigger` is edge-detected, not level-following, and needs no
        change to `MacroParameters.h`/.cpp at all**: that file keeps doing
        exactly what it always did — smooth the host-automatable 0..1 level
        and poke it into `util.macro.value` every block. This node's own
        `processSample`, when `type == Trigger`, watches `storedValue` for a
        rising edge (crossing above 0.5) and emits a single-block pulse
        (`SignalType::Event`, 1.0 for one block, back to 0.0) — the
        continuous host-parameter level and the one-shot node output are
        deliberately decoupled, so a UI "button" press (set the relay to 1,
        then back to 0 shortly after) just looks like a brief, ordinary
        parameter move from `MacroParameters`' own point of view.

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
    class MacroNode : public TypedValueNodeBase
    {
    public:
        static constexpr int numInputs = 0;
        static constexpr int numOutputs = 1;

        void reset() override { previousTriggerLevelHigh = false; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Macro"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { buildTypedOutputPort ("out") };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            auto parameters = commonParameters ("util.macro", /* includeTrigger */ true);
            // -1 is the "unclaimed" sentinel this class's own doc comment
            // explains — never a real slot number by default. Appended
            // after the shared type/min/max/quantity block so "Slot" reads
            // as this node's own extra, not a generic typed-value field.
            parameters.push_back (ParameterDescriptor { .id = "util.macro.slot",
                                                          .minValue = -1.0f, .maxValue = 31.0f, .defaultValue = -1.0f,
                                                          .displayName = "Slot", .isInteger = true, .kind = ValueKind::Int,
                                                          .isStructural = true });
            // Deliberately NOT "util.macro.value" here — that one is
            // written only by MacroParameters::applyToPlans()'s own
            // direct setParameter() poke, every block, never by the
            // ordinary command bridge; listing it would invite a
            // NodeCard slider fighting host automation every block.
            // Node::setParameter() has no descriptor-membership check
            // (ConstantNode/DataScaleNode already rely on exactly
            // this), so the poke still works with it absent here.
            return parameters;
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "util.macro.value")
                storedValue = value;
            else if (parameterId == "util.macro.slot")
                storedSlot = (int) std::lround (value);
            else
                setCommonParameter (parameterId, value, "util.macro", /* triggerAllowed */ true);
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            if (storedType == TypedValueType::Trigger)
            {
                // Rising-edge detection on the smoothed host-parameter
                // level, not level-following — see this class's own header
                // comment. min/max don't apply to a Trigger's pulse.
                const auto levelHigh = storedValue >= 0.5f;
                const auto pulse = levelHigh && ! previousTriggerLevelHigh;
                previousTriggerLevelHigh = levelHigh;
                outputs[0] = pulse ? 1.0f : 0.0f;
                return;
            }

            if (storedType == TypedValueType::Bool)
            {
                // Read directly off the raw 0..1 relay fraction, not
                // remapped through min/max first — those two parameters
                // can hold a stale leftover range from before a Control ->
                // Bool type switch (the UI hides them for a Bool macro,
                // but doesn't reset them), and remapping through e.g. a
                // leftover [5,10] range would make this always read true.
                outputs[0] = storedValue >= 0.5f ? 1.0f : 0.0f;
                return;
            }

            const auto [min, max] = effectiveMinMax (storedQuantity, storedMin, storedMax);
            const auto remapped = min + storedValue * (max - min);
            outputs[0] = typedOutputFor (remapped, storedType, storedIsInteger);
        }

        /** Read-only, message-thread-only accessor — GraphEditController
            scans the live graph's util.macro nodes by their own declared
            `slot` PARAMETER (NodeInstance::parameters, before compile),
            not this compiled member, so this exists only for tests/
            debugging, not the real derivation path.
        */
        int getSlot() const noexcept { return storedSlot; }

    private:
        bool previousTriggerLevelHigh = false;
        int storedSlot = -1;
    };
}
