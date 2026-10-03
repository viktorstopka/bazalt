// Curve-aware drag/wheel/fill math for ValueSlider.tsx (wiki/plans/
// PropsAndMacroRedesign.md Batch A3). The engine's Value Contract already
// marks e.g. env.adsr's attack/decay/release Curve::Logarithmic
// (ValueTypes::timeSecondsPort) and has done since M14 - ValueSlider.tsx
// just never read it, so "attack maxes at 10s and 0.05s is unreachable"
// was a wiring gap, not a missing feature.
//
// PortDescriptor.h's own comment is explicit that `curve` is UI-facing
// vocabulary and `skew` (ParameterDescriptor only)/`isLogScale`
// (PortDescriptor only, and NOT consistently set even where `curve` is -
// delay.line.samples' timeSamplesPort declares Curve::Logarithmic but
// never sets isLogScale) are the real mechanism. Reading `curve` directly
// here sidesteps that inconsistency rather than needing to fix it engine-
// side first - `curve` is populated everywhere `isLogScale` would be, plus
// the one place it isn't.
//
// ValueSlider itself stays a dumb, generic numeric widget (matching its
// own "range is the caller's choice" philosophy already documented there):
// it takes one resolved `skew` exponent, not the Curve/Quantity vocabulary.
// This module is where a descriptor's curve/quantity/skew fields become
// that one number - NodeCard.tsx calls resolveSkew() once per row.
import type { Curve, Quantity } from '../graph/descriptorTypes'

/** JUCE's own NormalisableRange power-curve convention (skew < 1 biases
    resolution toward the low end of the range - more drag travel spent on
    short attack times or low frequencies, where it's perceptually needed)
    - the same `skew` field ParameterDescriptor already carries. Prefers a
    genuinely-declared, non-1 `skew` (real per-node tuning:
    frequencyParameter's 0.3 vs. timeSecondsParameter's 0.5) over a generic
    per-Quantity fallback, since a flat single exponent for every curved
    parameter would throw that tuning away.
*/
export function resolveSkew(curve: Curve, quantity: Quantity, declaredSkew?: number): number {
  if (curve === 'linear' || curve === 'custom-ref') return 1
  if (declaredSkew !== undefined && declaredSkew !== 1) return declaredSkew
  if (quantity === 'frequency') return 0.3
  if (quantity === 'time') return 0.45
  return 0.4
}

/** value -> 0..1(ish) drag/wheel position, inverse of fromNormalizedPosition.
    skew === 1 (the overwhelmingly common case - everything that doesn't
    declare a real curve) reduces to plain, UNCLAMPED linear - ValueSlider's
    own prop doc comment is explicit that dragging/scrolling past either
    edge must keep moving the value at the same rate for an unbounded port
    (Remap's own in/out ports; "the slider has max at 10... but you can,
    even via dragging, move it higher"). Only a genuinely curved slider
    clamps to [0,1] first - `Math.pow` with a fractional exponent on a
    negative or >1 base isn't the inverse this function wants, so going
    past a curved slider's own declared range just pins at the nearest end
    instead (a curved range only ever appears on a hard-bounded quantity
    today - Frequency/Time - so this is never user-visible in practice).
*/
export function toNormalizedPosition(value: number, min: number, max: number, skew: number): number {
  const range = max - min || 1
  const linear = (value - min) / range
  if (skew === 1) return linear
  return Math.pow(Math.min(1, Math.max(0, linear)), 1 / skew)
}

/** 0..1(ish) drag/wheel position -> value. Drag/wheel deltas move this
    position linearly (constant feel per pixel/tick, matching every other
    slider in this app); only the position->value warp is curved. See
    toNormalizedPosition's own comment on why only skew!==1 clamps first. */
export function fromNormalizedPosition(position: number, min: number, max: number, skew: number): number {
  if (skew === 1) return min + (max - min) * position
  const warped = Math.pow(Math.min(1, Math.max(0, position)), skew)
  return min + (max - min) * warped
}
