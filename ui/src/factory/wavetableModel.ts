// The wavetable document the Factory window edits — the same JSON the engine
// reads (engine/include/bazalt/engine/graph/WavetableData.h). Evaluated here
// only to DRAW it (CLAUDE.md rule 1): what you hear is the engine's own
// band-limited rendering of this document.
import { type CurveDoc, CYCLE_PRESETS, curveFromContent, evaluateCurve } from './curveModel'

export type Interpolation = 'morph' | 'step'

export type Keyframe =
  | { position: number; kind: 'curve'; curve: CurveDoc }
  | { position: number; kind: 'harmonics'; amplitudes: number[]; phases?: number[] }

export interface WavetableDoc {
  schema: 'wavetable'
  version: 1
  interpolation: Interpolation
  keyframes: Keyframe[]
}

/** Bars shown in the harmonics editor (the engine accepts up to 1024). */
export const HARMONIC_BARS = 64

const curveOf = (name: string): CurveDoc => CYCLE_PRESETS.find((p) => p.name === name)!.doc

export function curveKeyframe(position: number, curve: CurveDoc): Keyframe {
  return { position, kind: 'curve', curve }
}

export function harmonicsKeyframe(position: number, amplitudes: number[], phases?: number[]): Keyframe {
  return { position, kind: 'harmonics', amplitudes, phases }
}

function table(keyframes: Keyframe[], interpolation: Interpolation = 'morph'): WavetableDoc {
  return { schema: 'wavetable', version: 1, interpolation, keyframes }
}

/** The engine's default: a sine morphing into a saw. */
export function defaultWavetable(): WavetableDoc {
  return table([curveKeyframe(0, curveOf('Sine')), curveKeyframe(1, curveOf('Saw'))])
}

const range = (n: number, f: (i: number) => number) => Array.from({ length: n }, (_, i) => f(i))

export interface WavetablePreset {
  name: string
  doc: WavetableDoc
}

export const WAVETABLE_PRESETS: readonly WavetablePreset[] = [
  { name: 'Sine → Saw', doc: defaultWavetable() },
  { name: 'Sine → Square', doc: table([curveKeyframe(0, curveOf('Sine')), curveKeyframe(1, curveOf('Square'))]) },
  {
    name: 'Organ drawbars',
    doc: table([
      harmonicsKeyframe(0, [1]),
      harmonicsKeyframe(0.5, [1, 0.5, 0, 0.35]),
      harmonicsKeyframe(1, [1, 0.7, 0.5, 0.5, 0, 0.35, 0, 0.3]),
    ]),
  },
  {
    name: 'Brightening saw',
    doc: table([
      harmonicsKeyframe(0, range(2, (i) => 1 / (i + 1))),
      harmonicsKeyframe(0.5, range(8, (i) => 1 / (i + 1))),
      harmonicsKeyframe(1, range(HARMONIC_BARS, (i) => 1 / (i + 1))),
    ]),
  },
  {
    name: 'Odd to even',
    doc: table([
      harmonicsKeyframe(0, range(16, (i) => (i % 2 === 0 ? 1 / (i + 1) : 0))),
      harmonicsKeyframe(1, range(16, (i) => (i === 0 ? 1 : i % 2 === 1 ? 1 / (i + 1) : 0))),
    ]),
  },
  {
    name: 'Stepped shapes',
    doc: table(
      ['Sine', 'Triangle', 'Saw', 'Square'].map((name, i) => curveKeyframe(i / 3, curveOf(name))),
      'step',
    ),
  },
]

function finite(v: unknown, fallback = 0): number {
  return typeof v === 'number' && Number.isFinite(v) ? v : fallback
}

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v))

/** The node's content as a wavetable, or the default. */
export function wavetableFromContent(content: unknown): WavetableDoc {
  const c = content as Partial<WavetableDoc> | undefined
  if (!c || typeof c !== 'object' || !Array.isArray(c.keyframes)) return defaultWavetable()
  const keyframes: Keyframe[] = c.keyframes
    .filter((k) => k && typeof k === 'object')
    .map((k) => {
      const position = clamp(finite(k.position), 0, 1)
      if (k.kind === 'harmonics')
        return harmonicsKeyframe(
          position,
          (Array.isArray(k.amplitudes) ? k.amplitudes : []).map((a) => clamp(finite(a), -1, 1)),
          Array.isArray(k.phases) ? k.phases.map((p) => clamp(finite(p), -1, 1)) : undefined,
        )
      const curve = curveFromContent((k as { curve?: unknown }).curve, 'cycle')
      return curveKeyframe(position, { ...curve, timeBase: 'cycle' })
    })
    .sort((a, b) => a.position - b.position)
  return table(keyframes.length > 0 ? keyframes : [curveKeyframe(0, curveOf('Sine'))], c.interpolation === 'step' ? 'step' : 'morph')
}

/** One cycle of a keyframe, `samples` long — WavetableKeyframe::renderCycle's
    shape (a harmonics frame scaled down when its peak passes 1). */
export function keyframeCycle(keyframe: Keyframe, samples: number): number[] {
  if (keyframe.kind === 'curve') return range(samples, (i) => evaluateCurve(keyframe.curve, i / samples))
  const out = new Array<number>(samples).fill(0)
  keyframe.amplitudes.forEach((amplitude, h) => {
    if (Math.abs(amplitude) < 1e-6) return
    const phase = keyframe.phases?.[h] ?? 0
    for (let i = 0; i < samples; i++) out[i] += amplitude * Math.sin(2 * Math.PI * ((h + 1) * (i / samples) + phase))
  })
  const peak = out.reduce((m, v) => Math.max(m, Math.abs(v)), 0)
  return peak > 1 ? out.map((v) => v / peak) : out
}

/** The two keyframes around `frame` and the mix — WavetableView::locate. */
export function locateFrame(doc: WavetableDoc, frame: number): { lower: number; upper: number; mix: number } {
  const f = clamp(frame, 0, 1)
  const n = doc.keyframes.length
  let lower = 0
  while (lower + 1 < n && doc.keyframes[lower + 1].position <= f) lower++
  const upper = Math.min(lower + 1, n - 1)
  const from = doc.keyframes[lower].position
  const to = doc.keyframes[upper].position
  const mix = upper === lower || doc.interpolation === 'step' || f <= from ? 0 : clamp((f - from) / Math.max(1e-6, to - from), 0, 1)
  return { lower, upper, mix }
}

/** One cycle at a frame position, as the Oscillator plays it (before band-limiting). */
export function frameCycle(doc: WavetableDoc, frame: number, samples: number, cache?: Map<Keyframe, number[]>): number[] {
  const cycleOf = (k: Keyframe) => {
    let c = cache?.get(k)
    if (!c) {
      c = keyframeCycle(k, samples)
      cache?.set(k, c)
    }
    return c
  }
  const { lower, upper, mix } = locateFrame(doc, frame)
  const a = cycleOf(doc.keyframes[lower])
  if (mix <= 0) return a
  const b = cycleOf(doc.keyframes[upper])
  return a.map((v, i) => v + (b[i] - v) * mix)
}

/** A keyframe's harmonics, read from its drawn cycle — so a curve keyframe
    can be switched to bars and edited as a spectrum (a plain DFT, editor-side
    only: the engine builds the sound from the result). */
export function toHarmonics(keyframe: Keyframe, count = HARMONIC_BARS): Keyframe {
  if (keyframe.kind === 'harmonics') return keyframe
  const n = 1024
  const cycle = keyframeCycle(keyframe, n)
  const amplitudes: number[] = []
  const phases: number[] = []
  for (let h = 1; h <= count; h++) {
    let re = 0
    let im = 0
    for (let i = 0; i < n; i++) {
      const angle = (2 * Math.PI * h * i) / n
      re += cycle[i] * Math.cos(angle)
      im += cycle[i] * Math.sin(angle)
    }
    re *= 2 / n
    im *= 2 / n
    // x = A sin(2π(h t + p)) = A sin(2πp) cos + A cos(2πp) sin  →  re = A sin 2πp, im = A cos 2πp
    const amplitude = Math.hypot(re, im)
    amplitudes.push(clamp(Math.round(amplitude * 1000) / 1000, 0, 1))
    phases.push(amplitude < 1e-4 ? 0 : Math.round((Math.atan2(re, im) / (2 * Math.PI)) * 1000) / 1000)
  }
  return harmonicsKeyframe(keyframe.position, amplitudes, phases)
}

/** A harmonics keyframe drawn as a curve of `count` straight segments. */
export function toCurve(keyframe: Keyframe, count = 64): Keyframe {
  if (keyframe.kind === 'curve') return keyframe
  const cycle = keyframeCycle(keyframe, count)
  return curveKeyframe(keyframe.position, {
    schema: 'curve',
    version: 1,
    timeBase: 'cycle',
    points: cycle.map((y, i) => ({ x: i / count, y: Math.round(y * 1000) / 1000 })),
  })
}

export const WAVETABLE_FACTORY_TYPES: ReadonlySet<string> = new Set(['data.wavetable'])
