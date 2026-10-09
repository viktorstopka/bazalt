// The curve document the Factory window edits — the same JSON the engine
// reads (engine/include/bazalt/engine/graph/CurveData.h). Evaluated here only
// to DRAW it (CLAUDE.md rule 1): what you hear is the engine's own rendering
// of this document.

export type SegmentShape = 'curve' | 'smooth' | 'hold'
export type TimeBase = 'cycle' | 'time'

export interface CurvePoint {
  x: number
  y: number
  /** -1..1, `curve` segments only; 0 is a straight line. */
  tension?: number
  shape?: SegmentShape
  /** One letter: A, D, S (where an envelope holds), H, R … */
  marker?: string
}

export interface CurveDoc {
  schema: 'curve'
  version: 1
  timeBase: TimeBase
  /** Time mode: the x range in seconds. */
  length?: number
  points: CurvePoint[]
  loop?: { start: number; end: number }
}

export function domainEnd(doc: CurveDoc): number {
  return doc.timeBase === 'time' ? Math.max(0.001, doc.length ?? 1) : 1
}

/** y range drawn: a cycle is a waveform (-1..1), an envelope 0..1. */
export function valueRange(doc: CurveDoc): [number, number] {
  return doc.timeBase === 'time' ? [0, 1] : [-1, 1]
}

export function sortPoints(points: CurvePoint[]): CurvePoint[] {
  return [...points].sort((a, b) => a.x - b.x)
}

function shapeSegment(from: CurvePoint, toY: number, t: number): number {
  const shape = from.shape ?? 'curve'
  if (shape === 'hold') return from.y
  if (shape === 'smooth') return from.y + (toY - from.y) * 0.5 * (1 - Math.cos(Math.PI * t))
  const c = (from.tension ?? 0) * 6
  const shaped = Math.abs(c) < 1e-4 ? t : (Math.exp(c * t) - 1) / (Math.exp(c) - 1)
  return from.y + (toY - from.y) * shaped
}

/** Mirrors CurveData.h's evaluateCurvePoints exactly. */
export function evaluateCurve(doc: CurveDoc, x: number): number {
  const points = doc.points
  const n = points.length
  if (n === 0) return 0
  if (n === 1) return points[0].y
  if (doc.timeBase === 'cycle') {
    x -= Math.floor(x)
    const first = points[0]
    const last = points[n - 1]
    if (x < first.x || x >= last.x) {
      const start = last.x
      const end = first.x + 1
      const pos = x < first.x ? x + 1 : x
      const width = end - start
      return width <= 1e-9 ? first.y : shapeSegment(last, first.y, (pos - start) / width)
    }
  } else {
    if (x <= points[0].x) return points[0].y
    if (x >= points[n - 1].x) return points[n - 1].y
  }
  let lo = 0
  let hi = n - 1
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1
    if (points[mid].x <= x) lo = mid
    else hi = mid
  }
  const a = points[lo]
  const b = points[hi]
  const width = b.x - a.x
  return width <= 1e-9 ? b.y : shapeSegment(a, b.y, (x - a.x) / width)
}

/** An SVG path through the curve, sampled densely enough for the drawn size. */
export function curvePath(doc: CurveDoc, toX: (x: number) => number, toY: (y: number) => number, samples = 256): string {
  const end = domainEnd(doc)
  let d = ''
  for (let i = 0; i <= samples; i++) {
    const x = (i / samples) * end
    // Read a hair before the end of a cycle, or the wrap lands on the next period's start.
    const value = evaluateCurve(doc, doc.timeBase === 'cycle' && i === samples ? end - 1e-6 : x)
    d += `${i === 0 ? 'M' : 'L'}${toX(x).toFixed(2)},${toY(value).toFixed(2)}`
  }
  return d
}

// ---- presets (CurveData.h's, plus a few) ------------------------------------

export interface CurvePreset {
  name: string
  doc: CurveDoc
}

const cycle = (points: CurvePoint[]): CurveDoc => ({ schema: 'curve', version: 1, timeBase: 'cycle', points })

export function adsrDoc(attack = 0.01, decay = 0.2, sustain = 0.7, release = 0.3, bend = -0.5): CurveDoc {
  return {
    schema: 'curve',
    version: 1,
    timeBase: 'time',
    length: attack + decay + release,
    points: [
      { x: 0, y: 0 },
      { x: attack, y: 1, tension: bend, marker: 'A' },
      { x: attack + decay, y: sustain, tension: bend, marker: 'S' },
      { x: attack + decay + release, y: 0, marker: 'R' },
    ],
  }
}

export const CYCLE_PRESETS: readonly CurvePreset[] = [
  { name: 'Sine', doc: cycle([{ x: 0.25, y: 1, shape: 'smooth' }, { x: 0.75, y: -1, shape: 'smooth' }]) },
  { name: 'Triangle', doc: cycle([{ x: 0.25, y: 1 }, { x: 0.75, y: -1 }]) },
  { name: 'Saw', doc: cycle([{ x: 0, y: -1 }, { x: 1, y: 1 }]) },
  { name: 'Ramp', doc: cycle([{ x: 0, y: 1 }, { x: 1, y: -1 }]) },
  { name: 'Square', doc: cycle([{ x: 0, y: 1, shape: 'hold' }, { x: 0.5, y: -1, shape: 'hold' }]) },
  {
    name: 'Steps',
    doc: cycle([0, 1, 2, 3].map((i) => ({ x: i / 4, y: -1 + (2 * i) / 3, shape: 'hold' as const }))),
  },
]

export const TIME_PRESETS: readonly CurvePreset[] = [
  { name: 'ADSR', doc: adsrDoc() },
  { name: 'Pluck', doc: { schema: 'curve', version: 1, timeBase: 'time', length: 0.6, points: [{ x: 0, y: 1, tension: -0.7 }, { x: 0.6, y: 0 }] } },
  { name: 'Swell', doc: adsrDoc(0.8, 0.1, 0.9, 0.6, 0.3) },
  { name: 'AR', doc: { schema: 'curve', version: 1, timeBase: 'time', length: 0.5, points: [{ x: 0, y: 0 }, { x: 0.05, y: 1, marker: 'S' }, { x: 0.5, y: 0, marker: 'R' }] } },
]

export function defaultCurve(timeBase: TimeBase): CurveDoc {
  return timeBase === 'time' ? adsrDoc() : CYCLE_PRESETS[0].doc
}

/** The node's content as a curve, or its default (the engine's own: a sine, an ADSR). */
export function curveFromContent(content: unknown, timeBase: TimeBase): CurveDoc {
  const c = content as Partial<CurveDoc> | undefined
  if (!c || typeof c !== 'object' || !Array.isArray(c.points)) return defaultCurve(timeBase)
  return {
    schema: 'curve',
    version: 1,
    timeBase: c.timeBase === 'time' || c.timeBase === 'cycle' ? c.timeBase : timeBase,
    length: typeof c.length === 'number' ? c.length : undefined,
    points: sortPoints(c.points.filter((p) => Number.isFinite(p?.x) && Number.isFinite(p?.y))),
    loop: c.loop,
  }
}

/** Which factory editor a node type opens, and its curve's default time base. */
export const CURVE_FACTORY_TYPES: Readonly<Record<string, TimeBase>> = {
  'data.curve': 'cycle',
  'source.oscillator': 'cycle',
  'source.envelope': 'time',
}
