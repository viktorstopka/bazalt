// design/Visualization/Scope1.png: "the scope derives a range from the
// signal it actually sees, rounds it to a readable number, and then stops
// adjusting." The classic chart-axis "nice numbers" algorithm (Paul Heckbert,
// Graphics Gems I) — pick a round STEP near span/10 (1, 2, or 5 times a power
// of ten) and floor/ceil the observed bounds out to a multiple of it, rather
// than trying to round each bound independently (which needs sign-aware
// "round away from zero" logic that's easy to get backwards for a negative
// bound; this is naturally sign-agnostic since floor/ceil already do the
// right thing on either side of zero).

/** The nearest "nice" value (1, 2, or 5 times a power of ten) to `roughStep`,
    rounding up — e.g. 0.34 -> 0.5, 3 -> 5, 12 -> 20. Degenerates to 1 for a
    non-positive input (a flat/degenerate observed range) rather than
    producing a zero or negative step, which floor/ceil below can't use.
*/
function niceStep(roughStep: number): number {
  if (!(roughStep > 0)) return 1
  const exponent = Math.floor(Math.log10(roughStep))
  const base = 10 ** exponent
  const fraction = roughStep / base
  const niceFraction = fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10
  return niceFraction * base
}

export interface NiceRange {
  min: number
  max: number
}

/** Expands [min, max] outward to a "readable" bound on each side — a
    multiple of a nice step sized for roughly 10 divisions across the span.
    A degenerate or non-finite input (min===max, or either not finite: no
    real signal observed yet) is centred and given a span of 2 rather than
    returning a zero-width or NaN range, so a caller never has to special-
    case "nothing to show a range for yet".
*/
export function niceRange(min: number, max: number): NiceRange {
  let lo = min
  let hi = max
  if (!Number.isFinite(lo) || !Number.isFinite(hi) || lo === hi) {
    const center = Number.isFinite(lo) ? lo : Number.isFinite(hi) ? hi : 0
    lo = center - 1
    hi = center + 1
  }
  const step = niceStep((hi - lo) / 10)
  return { min: Math.floor(lo / step) * step, max: Math.ceil(hi / step) * step }
}
