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
// it takes one resolved `logarithmic` flag, not the Curve/Quantity
// vocabulary - NodeCard.tsx calls isLogarithmicCurve() once per row.
//
// 2026-10-05: the old power curve here, value = pos^skew with skew < 1,
// pushed resolution toward the TOP of the range (half the travel already
// reached ~16 kHz) - JUCE's convention is pos^(1/skew). Replaced with a
// real exponential mapping derived from the range itself.
import type { Curve } from '../graph/descriptorTypes'

/** Whether a descriptor's slider moves exponentially rather than
    linearly. The engine's Value Contract marks it with
    `curve: 'logarithmic'` (frequencies, times); a declared `skew` is the
    host parameter's own JUCE tuning and isn't needed here — the
    exponential mapping below is derived from the range itself.
*/
export function isLogarithmicCurve(curve: Curve): boolean {
  return curve === 'logarithmic'
}

// A range that starts at zero (attack 0-10 s) has no ratio of its own: its
// curve spans this many decades below the maximum (10 s -> ~1 ms reaches
// the first tenth of the slider), with the very start still exactly 0.
const ZERO_BASED_DECADES = 4

/** The value ratio the curve spans: max/min for a positive range — a true
    logarithmic slider, every decade the same drag travel (0.01-20000 Hz:
    20 Hz sits around the middle, not in the first pixel) — otherwise
    ZERO_BASED_DECADES. 1 means linear. */
function curveRatio(min: number, max: number): number {
  if (!(max > min)) return 1
  if (min > 0) return max / min
  return Math.pow(10, ZERO_BASED_DECADES)
}

/** value -> 0..1(ish) drag/wheel position, inverse of fromNormalizedPosition.
    Linear (the overwhelmingly common case) stays plain and UNCLAMPED —
    ValueSlider's own prop doc comment is explicit that dragging past
    either edge must keep moving the value for an unbounded port (Map's
    in/out ranges). A logarithmic slider clamps to its range: it only ever
    appears on a hard-bounded quantity (Frequency/Time).

    The curve is v = min + (max - min) * (r^p - 1) / (r - 1): for a
    positive range with r = max/min that is exactly min * r^p (true log);
    for a zero-based one it is the same exponential, offset so p = 0 is 0.
*/
export function toNormalizedPosition(value: number, min: number, max: number, logarithmic: boolean): number {
  const range = max - min || 1
  const linear = (value - min) / range
  if (!logarithmic) return linear
  const r = curveRatio(min, max)
  if (r <= 1) return Math.min(1, Math.max(0, linear))
  const clamped = Math.min(1, Math.max(0, linear))
  return Math.log(1 + clamped * (r - 1)) / Math.log(r)
}

/** 0..1(ish) drag/wheel position -> value. Drag/wheel deltas move this
    position linearly (constant feel per pixel/tick); only the
    position->value mapping is curved. */
export function fromNormalizedPosition(position: number, min: number, max: number, logarithmic: boolean): number {
  if (!logarithmic) return min + (max - min) * position
  const p = Math.min(1, Math.max(0, position))
  const r = curveRatio(min, max)
  if (r <= 1) return min + (max - min) * p
  return min + ((max - min) * (Math.pow(r, p) - 1)) / (r - 1)
}
