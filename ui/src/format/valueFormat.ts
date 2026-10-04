// wiki/plans/PropsAndMacroRedesign.md Batch E earmarks this file as the
// shared formatting home for the typed-value system (Macro/Constant) —
// "decimals-by-quantity-or-step, MIDI note-name + cents formatting for
// Pitch/Frequency macros in the top panel". This first cut covers only what
// the Macro node's own in-card rebuild (design/Macro.png) needs: a trimmed
// number format, the Row 2 type-summary chip text, and (rewritten per
// direct correction, 2026-10-03: "Unit should not be a field") the same
// quantity->unit derivation TypedValueNodeBase.h's unitForQuantity uses
// engine-side, for the one caller (MacroKnob.tsx) that has no real
// PortDescriptor to read an already-derived `.unit` off directly. The
// top-bar panel itself stays on its own pre-existing formatting otherwise
// (it's disabled, not rebuilt, in this same pass — see App.tsx's MacroPanel
// comment) — MIDI note-name/cents formatting is real future work, not done
// here.
import type { Quantity } from '../graph/descriptorTypes'
import type { MacroValueType } from '../graph/graphStore'

/** Trims to at most 2 decimals, dropping trailing zeros — "0.50" reads as
    "0.5" in the chip/value fields (design reference: "0.5-1.3", "0.14"),
    but a whole number never grows a spurious ".00". */
export function formatTrimmed(value: number): string {
  if (Number.isInteger(value)) return String(value)
  return value.toFixed(2).replace(/0+$/, '').replace(/\.$/, '')
}

function formatRange(min: number, max: number): string {
  return `${formatTrimmed(min)}-${formatTrimmed(max)}`
}

function capitalize(s: string): string {
  return s.length > 0 ? s.charAt(0).toUpperCase() + s.slice(1) : s
}

/** Mirrors TypedValueNodeBase.h's `unitForQuantity` exactly — same five
    Quantity values that already have an established unit elsewhere in the
    engine (ValueTypes.h's frequencyPort/pitchPort/timeSecondsPort/
    gainDbPort/percentPort factories), everything else unitless. The real
    source of truth for a MACRO's own unit is its descriptor's own
    `outputs[0].unit` (computed engine-side, already correct) — this
    standalone copy exists only for MacroKnob.tsx's top-bar panel, which
    has no PortDescriptor to read, just a bare `quantity` plucked off the
    graph mirror's parameterValues.
*/
export function quantityUnit(quantity: Quantity): string {
  switch (quantity) {
    case 'frequency':
      return 'Hz'
    case 'pitch':
      return 'st'
    case 'time':
      return 's'
    case 'gain':
      return 'dB'
    case 'ratio':
      return '%'
    default:
      return ''
  }
}

/** Row 2's read-only type-summary chip (design/Macro.png): "Ctrl Frequency
    0.5-1.3", "Ctrl Mod Unipolar", "Boolean", "Event", "Int 1-8",
    "Int Enum [4]" — one deterministic string per combination, matching
    every example in the reference image. Rewritten per direct correction,
    2026-10-03: three top-level `type`s (Control/Bool/Trigger), not five —
    Value/Modulation/Int are Control subtypes driven by `isInteger`/
    `quantity`, and `isEnum` is "Int toggled to be Enum", not a parallel
    type value (see graphStore.ts's own `MacroValueType`/
    `classifyMacroControlSubtype` doc comments).
*/
export function macroTypeChipText(type: MacroValueType, isInteger: boolean, isEnum: boolean, quantity: Quantity, min: number, max: number): string {
  if (type === 'bool') return 'Boolean'
  if (type === 'trigger') return 'Event'
  // Control.
  if (isInteger) {
    if (isEnum) return `Int Enum [${Math.max(1, Math.round(max - min) + 1)}]`
    return `Int ${formatRange(min, max)}`
  }
  // Modulation: no range shown at all (design/Macro.png's own "Ctrl Mod
  // Unipolar" carries no numbers — min/max are implicit for this subtype,
  // see TypedValueNodeBase.h's own effectiveMinMax).
  if (quantity === 'unipolar' || quantity === 'bipolar') return `Ctrl Mod ${capitalize(quantity)}`
  // The ±100000 "no real limit" default (util.constant's own, ConstantNode.h)
  // is not a range worth reading: say nothing rather than "-100000-100000".
  const range = min <= -100000 && max >= 100000 ? '' : ` ${formatRange(min, max)}`
  if (quantity === 'dimensionless') return `Ctrl${range}`
  return `Ctrl ${capitalize(quantity)}${range}`
}
