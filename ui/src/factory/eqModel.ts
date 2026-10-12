// The EQ curve document the Factory window edits — the same JSON the engine
// reads (engine/include/bazalt/engine/graph/EqCurve.h). The response is
// computed here only to DRAW it (CLAUDE.md rule 1): what you hear is the
// engine's own filters playing this document.

export type EqBandType = 'bell' | 'lowShelf' | 'highShelf' | 'lowCut' | 'highCut' | 'notch'

export interface EqBand {
  type: EqBandType
  frequency: number
  /** dB — bells and shelves. */
  gain?: number
  /** Bells, cuts (resonance) and notches. */
  q?: number
  /** Shelves, 0..1. */
  slope?: number
  /** Cuts: 1..4 = 12..48 dB per octave. */
  order?: number
  enabled?: boolean
}

export interface EqDoc {
  schema: 'eq'
  version: 1
  bands: EqBand[]
}

/** Types that read an EQ curve and open the EQ editor. */
export const EQ_FACTORY_TYPES: ReadonlySet<string> = new Set(['filter.eq', 'data.eqCurve'])

export const MIN_FREQUENCY = 10
export const MAX_FREQUENCY = 22000
export const MAX_GAIN_DB = 24
export const MAX_BANDS = 16

export const BAND_TYPES: ReadonlyArray<{ type: EqBandType; label: string }> = [
  { type: 'bell', label: 'Bell' },
  { type: 'lowShelf', label: 'Low Shelf' },
  { type: 'highShelf', label: 'High Shelf' },
  { type: 'lowCut', label: 'Low Cut' },
  { type: 'highCut', label: 'High Cut' },
  { type: 'notch', label: 'Notch' },
]

export const hasGain = (type: EqBandType) => type === 'bell' || type === 'lowShelf' || type === 'highShelf'
export const isShelf = (type: EqBandType) => type === 'lowShelf' || type === 'highShelf'
export const isCut = (type: EqBandType) => type === 'lowCut' || type === 'highCut'

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v))
const finite = (v: unknown, fallback: number) => (typeof v === 'number' && Number.isFinite(v) ? v : fallback)

/** A band with every field filled in and clamped — what the engine plays. */
export function normaliseBand(raw: Partial<EqBand>): Required<EqBand> {
  const type: EqBandType = BAND_TYPES.some((t) => t.type === raw.type) ? (raw.type as EqBandType) : 'bell'
  return {
    type,
    frequency: clamp(finite(raw.frequency, 1000), MIN_FREQUENCY, MAX_FREQUENCY),
    gain: clamp(finite(raw.gain, 0), -MAX_GAIN_DB, MAX_GAIN_DB),
    q: clamp(finite(raw.q, type === 'bell' || type === 'notch' ? 1 : Math.SQRT1_2), 0.1, 30),
    slope: clamp(finite(raw.slope, 0.5), 0, 1),
    order: clamp(Math.round(finite(raw.order, 2)), 1, 4),
    enabled: raw.enabled !== false,
  }
}

/** Mirrors EqCurveDocument::toVar: only the fields a band's type uses. */
export function cleanBand(band: Partial<EqBand>): EqBand {
  const b = normaliseBand(band)
  const out: EqBand = { type: b.type, frequency: b.frequency }
  if (hasGain(b.type)) out.gain = b.gain
  if (isShelf(b.type)) out.slope = b.slope
  else out.q = b.q
  if (isCut(b.type)) out.order = b.order
  if (!b.enabled) out.enabled = false
  return out
}

export function eqFromContent(content: unknown): EqDoc {
  const bands = (content as EqDoc | undefined)?.bands
  return {
    schema: 'eq',
    version: 1,
    bands: Array.isArray(bands) ? bands.slice(0, MAX_BANDS).map((b) => cleanBand(b ?? {})) : [],
  }
}

export const eqDoc = (bands: EqBand[]): EqDoc => ({ schema: 'eq', version: 1, bands: bands.map(cleanBand) })

export const EQ_PRESETS: ReadonlyArray<{ name: string; doc: EqDoc }> = [
  { name: 'Flat', doc: eqDoc([]) },
  { name: 'Clean low end', doc: eqDoc([{ type: 'lowCut', frequency: 80, order: 2, q: Math.SQRT1_2 }]) },
  {
    name: 'Smile',
    doc: eqDoc([
      { type: 'lowShelf', frequency: 120, gain: 4, slope: 0.5 },
      { type: 'bell', frequency: 900, gain: -2, q: 0.7 },
      { type: 'highShelf', frequency: 7000, gain: 4, slope: 0.5 },
    ]),
  },
  {
    name: 'Presence',
    doc: eqDoc([
      { type: 'lowCut', frequency: 100, order: 1, q: Math.SQRT1_2 },
      { type: 'bell', frequency: 300, gain: -3, q: 1.2 },
      { type: 'bell', frequency: 3200, gain: 4, q: 1.5 },
      { type: 'highShelf', frequency: 10000, gain: 2, slope: 0.6 },
    ]),
  },
  {
    name: 'Telephone',
    doc: eqDoc([
      { type: 'lowCut', frequency: 400, order: 3, q: Math.SQRT1_2 },
      { type: 'bell', frequency: 1500, gain: 6, q: 1 },
      { type: 'highCut', frequency: 3200, order: 3, q: Math.SQRT1_2 },
    ]),
  },
  {
    name: 'Air',
    doc: eqDoc([{ type: 'highShelf', frequency: 9000, gain: 5, slope: 0.4 }]),
  },
  {
    name: 'Resonant sweep',
    doc: eqDoc([{ type: 'highCut', frequency: 1200, order: 2, q: 4 }]),
  },
]

// ---- Response (display only) ------------------------------------------------

interface Complex {
  re: number
  im: number
}
const mul = (a: Complex, b: Complex): Complex => ({ re: a.re * b.re - a.im * b.im, im: a.re * b.im + a.im * b.re })
const div = (a: Complex, b: Complex): Complex => {
  const d = b.re * b.re + b.im * b.im
  return { re: (a.re * b.re + a.im * b.im) / d, im: (a.im * b.re - a.re * b.im) / d }
}
const abs = (a: Complex) => Math.hypot(a.re, a.im)

/** |H| of an RBJ biquad (Biquad.h's formulas) at `hz`. */
function biquadMagnitude(b: [number, number, number], a: [number, number], hz: number, sampleRate: number): number {
  const w = (2 * Math.PI * hz) / sampleRate
  const z1: Complex = { re: Math.cos(w), im: -Math.sin(w) }
  const z2 = mul(z1, z1)
  const num: Complex = { re: b[0] + b[1] * z1.re + b[2] * z2.re, im: b[1] * z1.im + b[2] * z2.im }
  const den: Complex = { re: 1 + a[0] * z1.re + a[1] * z2.re, im: a[0] * z1.im + a[1] * z2.im }
  return abs(div(num, den))
}

function angular(hz: number, sampleRate: number) {
  return (2 * Math.PI * clamp(hz, 1, sampleRate * 0.49)) / sampleRate
}

function peakMagnitude(f0: number, q: number, gainLinear: number, hz: number, sr: number) {
  const w0 = angular(f0, sr)
  const cos = Math.cos(w0)
  const A = Math.sqrt(Math.max(1e-6, gainLinear))
  const alpha = Math.sin(w0) / (2 * Math.max(0.01, q))
  const a0 = 1 + alpha / A
  return biquadMagnitude([(1 + alpha * A) / a0, (-2 * cos) / a0, (1 - alpha * A) / a0], [(-2 * cos) / a0, (1 - alpha / A) / a0], hz, sr)
}

function shelfMagnitude(f0: number, gainLinear: number, slope01: number, high: boolean, hz: number, sr: number) {
  const w0 = angular(f0, sr)
  const cos = Math.cos(w0)
  const A = Math.sqrt(Math.max(1e-6, gainLinear))
  const S = 0.1 + clamp(slope01, 0, 1) * 0.9
  const alpha = (Math.sin(w0) / 2) * Math.sqrt((A + 1 / A) * (1 / S - 1) + 2)
  const t = 2 * Math.sqrt(A) * alpha
  if (high) {
    const a0 = A + 1 - (A - 1) * cos + t
    return biquadMagnitude(
      [(A * (A + 1 + (A - 1) * cos + t)) / a0, (-2 * A * (A - 1 + (A + 1) * cos)) / a0, (A * (A + 1 + (A - 1) * cos - t)) / a0],
      [(2 * (A - 1 - (A + 1) * cos)) / a0, (A + 1 - (A - 1) * cos - t) / a0],
      hz,
      sr,
    )
  }
  const a0 = A + 1 + (A - 1) * cos + t
  return biquadMagnitude(
    [(A * (A + 1 - (A - 1) * cos + t)) / a0, (2 * A * (A - 1 - (A + 1) * cos)) / a0, (A * (A + 1 - (A - 1) * cos - t)) / a0],
    [(-2 * (A - 1 + (A + 1) * cos)) / a0, (A + 1 + (A - 1) * cos - t) / a0],
    hz,
    sr,
  )
}

/** The TPT SVF (SvfCore.h) is the bilinear transform of the analog
    prototype prewarped at its cutoff: evaluate the prototype at the
    prewarped frequency. */
function svfMagnitude(kind: 'low' | 'high' | 'notch', f0: number, q: number, hz: number, sr: number) {
  const g = Math.tan((Math.PI * clamp(f0, 1, sr * 0.49)) / sr)
  const k = 1 / clamp(q, 0.01, 100)
  const s: Complex = { re: 0, im: Math.tan((Math.PI * Math.min(hz, sr * 0.4999)) / sr) / g }
  const s2 = mul(s, s)
  const den: Complex = { re: s2.re + k * s.re + 1, im: s2.im + k * s.im }
  const num: Complex = kind === 'low' ? { re: 1, im: 0 } : kind === 'high' ? s2 : { re: s2.re + 1, im: s2.im }
  return abs(div(num, den))
}

/** EqCurveDocument::cascadeQ. */
function cascadeQ(order: number, index: number, q: number) {
  return (1 / (2 * Math.cos((Math.PI * (2 * index + 1)) / (4 * order)))) * (q / Math.SQRT1_2)
}

export const dbToLinear = (db: number) => Math.pow(10, db / 20)

/** One band's |H| at `hz` (1 when disabled). */
export function bandMagnitude(raw: EqBand, hz: number, sampleRate: number): number {
  const b = normaliseBand(raw)
  if (!b.enabled) return 1
  switch (b.type) {
    case 'bell':
      return peakMagnitude(b.frequency, b.q, dbToLinear(b.gain), hz, sampleRate)
    case 'lowShelf':
    case 'highShelf':
      return shelfMagnitude(b.frequency, dbToLinear(b.gain), b.slope, b.type === 'highShelf', hz, sampleRate)
    case 'lowCut':
    case 'highCut': {
      let m = 1
      for (let i = 0; i < b.order; i++) m *= svfMagnitude(b.type === 'lowCut' ? 'high' : 'low', b.frequency, cascadeQ(b.order, i, b.q), hz, sampleRate)
      return m
    }
    case 'notch':
      return svfMagnitude('notch', b.frequency, b.q, hz, sampleRate)
  }
}

export function responseDb(doc: EqDoc, hz: number, sampleRate: number): number {
  let m = 1
  for (const band of doc.bands) m *= bandMagnitude(band, hz, sampleRate)
  return 20 * Math.log10(Math.max(1e-9, m))
}

// ---- The log-frequency axis shared by the EQ editor, its thumbnail and the
// Spectrum view -------------------------------------------------------------

export const AXIS_MIN_HZ = 20
export const AXIS_MAX_HZ = 20000

export const hzToUnit = (hz: number) => Math.log(clamp(hz, AXIS_MIN_HZ, AXIS_MAX_HZ) / AXIS_MIN_HZ) / Math.log(AXIS_MAX_HZ / AXIS_MIN_HZ)
export const unitToHz = (u: number) => AXIS_MIN_HZ * Math.pow(AXIS_MAX_HZ / AXIS_MIN_HZ, clamp(u, 0, 1))

export const FREQUENCY_GRID: ReadonlyArray<{ hz: number; label?: string }> = [
  { hz: 20, label: '20' },
  { hz: 30 },
  { hz: 40 },
  { hz: 50, label: '50' },
  { hz: 60 },
  { hz: 70 },
  { hz: 80 },
  { hz: 90 },
  { hz: 100, label: '100' },
  { hz: 200, label: '200' },
  { hz: 300 },
  { hz: 400 },
  { hz: 500, label: '500' },
  { hz: 600 },
  { hz: 700 },
  { hz: 800 },
  { hz: 900 },
  { hz: 1000, label: '1k' },
  { hz: 2000, label: '2k' },
  { hz: 3000 },
  { hz: 4000 },
  { hz: 5000, label: '5k' },
  { hz: 6000 },
  { hz: 7000 },
  { hz: 8000 },
  { hz: 9000 },
  { hz: 10000, label: '10k' },
  { hz: 20000, label: '20k' },
]

export function formatHz(hz: number): string {
  return hz >= 1000 ? `${(hz / 1000).toFixed(hz >= 10000 ? 1 : 2)} kHz` : `${hz.toFixed(hz >= 100 ? 0 : 1)} Hz`
}

/** Semitones (MIDI note number) of a frequency — pitchReadout's input. */
export const hzToSemitones = (hz: number) => 69 + 12 * Math.log2(hz / 440)

/** The nearest equal-tempered note's frequency — the editor's Snap. */
export const snapHzToNote = (hz: number) => 440 * Math.pow(2, Math.round(hzToSemitones(hz) - 69) / 12)

/** A spectrum frame (linear bins 0..Nyquist) resampled onto `points` log-
    spaced columns of the shared axis, as dB — the peak bin of each column so
    narrow partials survive at high frequencies. */
export function spectrumColumnsDb(payload: Float32Array, sampleRate: number, points: number, out?: Float32Array): Float32Array {
  const result = out && out.length === points ? out : new Float32Array(points)
  const binHz = sampleRate / 2 / Math.max(1, payload.length)
  for (let i = 0; i < points; i++) {
    const lo = unitToHz(i / points) / binHz
    const hi = unitToHz((i + 1) / points) / binHz
    let peak = 0
    const from = Math.floor(lo)
    const to = Math.max(from, Math.min(payload.length - 1, Math.ceil(hi)))
    if (hi - lo < 1) {
      // Fewer than one bin per column: interpolate between the two nearest.
      const x = Math.min(payload.length - 1, (lo + hi) / 2)
      const k = Math.floor(x)
      const t = x - k
      peak = payload[k] * (1 - t) + (payload[Math.min(payload.length - 1, k + 1)] ?? 0) * t
    } else {
      for (let k = from; k <= to; k++) peak = Math.max(peak, payload[k])
    }
    result[i] = 20 * Math.log10(Math.max(1e-9, peak))
  }
  return result
}
