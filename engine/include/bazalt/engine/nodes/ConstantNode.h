#pragma once

#include "bazalt/engine/nodes/TypedValueNodeBase.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.constant". No inputs, one output holding a
        fixed value set via parameter — what Unwrapping a plain slider value
        creates (NODE_EDITOR.md §4), and what a placeholder dropdown value
        becomes once Unwrapped into a real node.

        **wiki/plans/PropsAndMacroRedesign.md Batch E**: adopts the same
        `type`/`isInteger`/`isEnum`/`min`/`max`/`quantity` contract
        `util.macro` does (`TypedValueNodeBase.h`) — "the components and
        some logic should be shared with Macro, no hardcoding for each,"
        direct instruction. Replaces the old hardcoded single float
        parameter, range always -100000..100000 regardless of what the
        value actually meant ("completely uncontrollable," direct
        feedback). Unlike Macro, `util.constant.value` stays an ordinary,
        always-listed `ParameterDescriptor` — there's no host-automation
        relay here to fight over it, so no reason to exclude it from
        `getParameters()` the way Macro must.

        **No `Trigger` type** (unlike Macro's three) — a momentary "button"
        gesture only stays cheap when there's a relay to flip twice with no
        recompile (Macro's 32 slots always exist, independent of the
        graph). Constant has no relay: a manual push-button here would cost
        a full `GraphEditController::setParameterValue` recompile per press
        (CLAUDE.md's documented "every single command recompiles" cost) —
        a real, avoidable cost for something that should be instant. A
        dedicated manual-trigger node is the right home for that gesture if
        it's ever wanted standalone, not bolted onto Constant.

        **Default range, before the user picks a real Quantity**:
        `ParameterDescriptor::minValue`/`maxValue` are plain, always-concrete
        `float`s (unlike `PortDescriptor`'s `std::optional<float>` ones) —
        a parameter can never be genuinely unbounded the way a port can, so
        the fix for "-100000..100000 makes it completely uncontrollable"
        (direct feedback) isn't removing the hard bound, it's giving the
        already-existing `softMin`/`softMax` mechanism (`ValueSlider.tsx`'s
        own "a comfortable default range for a value with no real hard
        limit") a sane default: the hard range stays a generous ±100000 (so
        typing a genuinely large value still works), but the default VISUAL/
        drag range (and hence the fill bar and drag sensitivity) is now
        ±10 instead of the full ±100000, while still untouched — setting a
        real `min`/`max` (with or without a real `type`/`quantity`) is a
        deliberate choice and fully overrides both, same as before.
    */
    class ConstantNode : public TypedValueNodeBase
    {
    public:
        static constexpr int numInputs = 0;
        static constexpr int numOutputs = 1;

        // TypedValueNodeBase's own storedMin/storedMax default to 0/1 — the
        // right default for Macro (whose raw relay value is always 0..1
        // anyway) but a real regression for Constant on its own: a fresh,
        // never-configured Constant's own VALUE would be hard-clamped to
        // [0,1], unable to even represent a negative bias. Constant keeps
        // its own historical, generous ±100000 default instead.
        ConstantNode()
        {
            storedMin = -100000.0f;
            storedMax = 100000.0f;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Constant"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { buildTypedOutputPort ("out") };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            auto parameters = commonParameters ("util.constant", /* includeTrigger */ false);
            // commonParameters() declares "min"/"max" with a generic
            // defaultValue of 0/1 (the right choice for Macro) — patched
            // here to match this class's own ±100000 default so the UI's
            // displayed/double-click-reset value agrees with what a fresh
            // node's own C++ member actually starts at. Indices are safe:
            // commonParameters()'s own fixed return shape is {type,
            // isInteger, isEnum, min, max, quantity}, in that order.
            parameters[3].defaultValue = -100000.0f; // "min"
            parameters[4].defaultValue = 100000.0f;  // "max"

            auto valueParameter = buildTypedValueParameter ("util.constant.value", "Value");
            // "completely uncontrollable" (direct feedback) was never
            // really about the HARD bound — ParameterDescriptor's
            // minValue/maxValue are plain floats, never genuinely
            // unbounded the way a PortDescriptor's can be — it was the
            // default VISUAL/drag range being the full ±100000 with no
            // comfortable default. softMin/softMax (ValueSlider.tsx's own
            // "comfortable range for a value with no real hard limit")
            // fixes exactly that, only while still at this class's own
            // untouched default — once the user sets a real min/max (even
            // without picking a Quantity), that's a deliberate choice and
            // this stops overriding it.
            if (storedType == TypedValueType::Control && !storedIsInteger && storedMin == -100000.0f && storedMax == 100000.0f)
            {
                valueParameter.softMin = -10.0f;
                valueParameter.softMax = 10.0f;
            }
            parameters.push_back (valueParameter);
            return parameters;
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "util.constant.value")
                storedValue = value;
            else
                setCommonParameter (parameterId, value, "util.constant", /* triggerAllowed */ false);
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = typedOutputFor (storedValue, storedType, storedIsInteger);
        }
    };
}
