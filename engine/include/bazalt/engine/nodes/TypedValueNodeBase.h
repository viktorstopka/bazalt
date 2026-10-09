#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>
#include <utility>
#include <vector>

namespace bazalt::engine::nodes
{
    /** wiki/plans/PropsAndMacroRedesign.md Batch E (rewritten per direct
        correction, 2026-10-03 — the first cut's five parallel types
        Float/Int/Bool/Enum/Trigger was NOT the system actually asked for,
        and bolted on a free-text "unit" field that shouldn't have existed
        either): the shared contract `util.macro` and `util.constant` both
        implement. Direct instruction, verbatim: "controls (with
        subtypes), int (can be toggled to be enum), mod (uni/bi), trigger,
        bool. That is the system. The only one that makes sense." — three
        top-level kinds (`Control`/`Bool`/`Trigger`), with Control's own
        "subtypes" (Value/Modulation/Int) falling out of the ALREADY-
        orthogonal `quantity`/`isInteger` axes every other Control port in
        this codebase already uses, rather than being yet more parallel
        enum values:
          - Value:      Control, !isInteger, quantity is a real unit
                        (Dimensionless/Frequency/Pitch/Time/Gain/Ratio/
                        Count/Phase).
          - Modulation: Control, !isInteger, quantity is Unipolar/Bipolar —
                        min/max are IMPLICIT (0..1 / -1..1), never
                        separately stored (see `effectiveMinMax` below),
                        matching design/Macro.png's own "Ctrl Mod Unipolar"
                        chip, which shows no numbers at all.
          - Int:        Control, isInteger — `isEnum` is the "can be
                        toggled to be enum" flag: false is a plain integer
                        range, true is a named-option index range (the UI
                        supplies the option LABELS as a cosmetic property
                        overlay, same mechanism `util.macro.unit` used to —
                        see MacroBody.tsx; this engine side only needs the
                        numeric 0..(N-1) contract, same as before).
        This exactly matches the port-colour palette `portUiKind.ts`
        already classifies every ordinary Control port into (value/
        modulation/integer) — a Macro/Constant's own configurable type now
        follows that same three-way split instead of a disconnected,
        Macro-only fourth and fifth.

        `Bool`/`Trigger` are unchanged in spirit from the first cut:
        `Bool` genuinely outputs a Boolean (`Quantity::Boolean`, not a
        disguised plain 0/1) and `Trigger` genuinely outputs `SignalType::Event` —
        see this class's own method doc comments below for why.

        `unit` is no longer a stored/settable field at all ("Unit should
        not be a field", direct instruction) — `unitForQuantity` below
        derives it from `quantity`, matching whatever a real node using
        that same Quantity already shows (ValueTypes.h's own frequencyPort/
        pitchPort/timeSecondsPort/gainDbPort/percentPort factories).
    */
    enum class TypedValueType { Control, Bool, Trigger };
    // ^ Trigger stays LAST (highest ordinal) deliberately — util.constant
    // excludes it (ConstantNode.h's own comment on why a Constant has no
    // Trigger type), and typedValueTypeFromOrdinal's clamp below only
    // works as "cap one below the top" because whatever's excluded is the
    // ordinal maximum.

    class TypedValueNodeBase : public Node
    {
    protected:
        static SignalType outputSignalTypeFor (TypedValueType type) noexcept
        {
            if (type == TypedValueType::Trigger) return SignalType::Event;
            return SignalType::Signal; // a Bool is a Signal whose quantity is Boolean (buildTypedOutputPort)
        }

        /** `isInteger`/`isEnum` only mean anything for `Control` — both
            ignored for Bool/Trigger, matching `buildTypedOutputPort`'s own
            min/max-only-for-Control convention below. */
        static ValueKind outputValueKindFor (TypedValueType type, bool isInteger, bool isEnum) noexcept
        {
            if (type == TypedValueType::Bool) return ValueKind::Bool;
            if (type == TypedValueType::Trigger) return ValueKind::Int; // no dedicated Trigger ValueKind exists
            if (isEnum) return ValueKind::Enum;
            return isInteger ? ValueKind::Int : ValueKind::Float;
        }

        /** Trigger reads as integer-like (a 0/1 pulse) for PortDescriptor
            purposes even though it has no real `isInteger` of its own to
            toggle — same behaviour the first cut's `effectiveIsInteger`
            already gave Trigger, unchanged here. */
        static bool portIsIntegerFor (TypedValueType type, bool isInteger) noexcept
        {
            return type == TypedValueType::Trigger || (type == TypedValueType::Control && isInteger);
        }

        static TypedValueType typedValueTypeFromOrdinal (float value, bool triggerAllowed) noexcept
        {
            const auto maxOrdinal = triggerAllowed ? 2 : 1;
            return (TypedValueType) juce::jlimit (0, maxOrdinal, (int) std::lround (value));
        }

        /** No established unit convention exists for Dimensionless/
            Unipolar/Bipolar/Count/Phase (ValueTypes.h's own factories never
            set one for these either) — only the five Quantity values that
            already have a real one elsewhere in this codebase get one
            here, so a macro's own unit always matches what a real node
            using the same Quantity already shows. */
        static juce::String unitForQuantity (Quantity quantity) noexcept
        {
            switch (quantity)
            {
                case Quantity::Frequency: return "Hz";
                case Quantity::Pitch: return "st";
                case Quantity::Time: return "s";
                case Quantity::Gain: return "dB";
                case Quantity::Ratio: return "%";
                default: return {};
            }
        }

        /** Modulation's own min/max are implicit, never separately stored
            (this class's own header comment) — Unipolar is 0..1, Bipolar
            is -1..1, matching `Polarity`'s own two values exactly.
            Anything else (a real-quantity Value, or Int/Enum) uses
            whatever min/max were actually set. */
        static std::pair<float, float> effectiveMinMax (Quantity quantity, float storedMin, float storedMax) noexcept
        {
            if (quantity == Quantity::Unipolar) return { 0.0f, 1.0f };
            if (quantity == Quantity::Bipolar) return { -1.0f, 1.0f };
            return { storedMin, storedMax };
        }

        /** `type`/`isInteger`/`isEnum`/`min`/`max`/`quantity`, under
            `idPrefix` (e.g. "util.macro"). `includeTrigger` is false for
            `util.constant` (see ConstantNode.h's own comment on why a
            Constant doesn't get a Trigger type).
        */
        static std::vector<ParameterDescriptor> commonParameters (const juce::String& idPrefix, bool includeTrigger)
        {
            std::vector<EnumOption> typeOptions = {
                { "control", "Control" }, { "bool", "Bool" },
            };
            if (includeTrigger)
                typeOptions.push_back ({ "trigger", "Trigger" });

            return {
                ParameterDescriptor { .id = idPrefix + ".type",
                                       .minValue = 0.0f, .maxValue = (float) (typeOptions.size() - 1), .defaultValue = 0.0f,
                                       .displayName = "Type", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = typeOptions, .isStructural = true },
                ParameterDescriptor { .id = idPrefix + ".isInteger",
                                       .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Integer", .kind = ValueKind::Bool, .isStructural = true },
                // "can be toggled to be enum" (direct instruction) — only
                // meaningful when isInteger is also true; the UI hides this
                // otherwise (same "UI hides, doesn't reset" convention the
                // Bool-type min/max case below already follows).
                ParameterDescriptor { .id = idPrefix + ".isEnum",
                                       .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Enum", .kind = ValueKind::Bool, .isStructural = true },
                ParameterDescriptor { .id = idPrefix + ".min",
                                       .minValue = -1.0e9f, .maxValue = 1.0e9f, .defaultValue = 0.0f,
                                       .displayName = "Min", .isStructural = true },
                ParameterDescriptor { .id = idPrefix + ".max",
                                       .minValue = -1.0e9f, .maxValue = 1.0e9f, .defaultValue = 1.0f,
                                       .displayName = "Max", .isStructural = true },
                // Option ids match bazalt::engine::Quantity's own declaration
                // order exactly (PortDescriptor.h) — reordering these
                // strings would silently misencode every non-Dimensionless
                // value.
                ParameterDescriptor { .id = idPrefix + ".quantity",
                                       .minValue = 0.0f, .maxValue = 9.0f, .defaultValue = 0.0f,
                                       .displayName = "Quantity", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "dimensionless", "Dimensionless" }, { "frequency", "Frequency" },
                                                         { "pitch", "Pitch" }, { "time", "Time" }, { "gain", "Gain" },
                                                         { "ratio", "Ratio" }, { "unipolar", "Unipolar" },
                                                         { "bipolar", "Bipolar" }, { "count", "Count" }, { "phase", "Phase" } },
                                       .isStructural = true },
            };
        }

        /** Handles `idPrefix + ".type"/".isInteger"/".isEnum"/".min"/
            ".max"/".quantity"` — returns false (nothing stored) for any
            other id, so a subclass's own `setParameter` can fall through
            to its own extra ids (`slot`, `value`, ...) with a single
            `else`.
        */
        bool setCommonParameter (const juce::String& parameterId, float value, const juce::String& idPrefix, bool triggerAllowed) noexcept
        {
            if (parameterId == idPrefix + ".type")
                storedType = typedValueTypeFromOrdinal (value, triggerAllowed);
            else if (parameterId == idPrefix + ".isInteger")
                storedIsInteger = value >= 0.5f;
            else if (parameterId == idPrefix + ".isEnum")
                storedIsEnum = value >= 0.5f;
            else if (parameterId == idPrefix + ".min")
                storedMin = value;
            else if (parameterId == idPrefix + ".max")
                storedMax = value;
            else if (parameterId == idPrefix + ".quantity")
                storedQuantity = (Quantity) juce::jlimit (0, 9, (int) std::lround (value));
            else
                return false;
            return true;
        }

        /** The node's typed output port, built from the current `type`/
            `isInteger`/`isEnum`/`min`/`max`/`quantity` — shared between
            Macro's `getOutputPorts()` and (via `buildTypedValueParameter`
            below) Constant's own value parameter shape.
        */
        PortDescriptor buildTypedOutputPort (const juce::String& id) const
        {
            PortDescriptor port;
            port.id = id;
            port.type = outputSignalTypeFor (storedType);
            port.isPrimaryOutput = true;
            port.kind = outputValueKindFor (storedType, storedIsInteger, storedIsEnum);
            port.quantity = storedType == TypedValueType::Bool ? Quantity::Boolean : storedQuantity;
            port.isInteger = portIsIntegerFor (storedType, storedIsInteger);
            if (storedType == TypedValueType::Control)
            {
                const auto [min, max] = effectiveMinMax (storedQuantity, storedMin, storedMax);
                port.minValue = min;
                port.maxValue = max;
                port.unit = unitForQuantity (storedQuantity);
            }
            return port;
        }

        /** Constant's own "value" `ParameterDescriptor` — a Bool-typed
            value is always a plain 0/1 regardless of whatever `min`/`max`
            happen to be left at (the UI hides those fields for a Bool
            type, same reasoning `buildTypedOutputPort` already applies).
        */
        ParameterDescriptor buildTypedValueParameter (const juce::String& id, const juce::String& displayName) const
        {
            ParameterDescriptor parameter;
            parameter.id = id;
            parameter.displayName = displayName;
            parameter.kind = outputValueKindFor (storedType, storedIsInteger, storedIsEnum);
            parameter.quantity = storedQuantity;
            parameter.isInteger = portIsIntegerFor (storedType, storedIsInteger);
            if (storedType == TypedValueType::Bool)
            {
                parameter.minValue = 0.0f;
                parameter.maxValue = 1.0f;
            }
            else
            {
                const auto [min, max] = effectiveMinMax (storedQuantity, storedMin, storedMax);
                parameter.minValue = min;
                parameter.maxValue = max;
                parameter.unit = unitForQuantity (storedQuantity);
            }
            return parameter;
        }

        /** A real value (already in its own effective min..max range —
            Constant's own `storedValue` IS this directly; Macro's own
            `processSample` remaps its raw 0..1 relay fraction into one
            first) rounded/clamped to whatever `type`/`isInteger` actually
            are. NOT used for `Trigger` — that's stateful edge-detection,
            MacroNode's own job (the only place `Trigger` is reachable;
            Constant never offers it).
        */
        static float typedOutputFor (float realValue, TypedValueType type, bool isInteger) noexcept
        {
            if (type == TypedValueType::Bool)
                return realValue >= 0.5f ? 1.0f : 0.0f;
            return isInteger ? std::round (realValue) : realValue;
        }

        TypedValueType storedType = TypedValueType::Control;
        bool storedIsInteger = false;
        bool storedIsEnum = false;
        float storedMin = 0.0f;
        float storedMax = 1.0f;
        Quantity storedQuantity = Quantity::Dimensionless;
        float storedValue = 0.0f;
    };
}
