// Non-React telemetry client (ARCHITECTURE.md §7: "high-rate rendering must
// run outside the framework's render cycle" — this module has no React
// dependency at all, on purpose). Polls every currently-subscribed
// (tapName, frameType) pair via the resource-provider fetch() transport
// (ADR-0005) in one shared rAF loop, and exposes a plain module-level store
// that canvas/preview renderers read from directly each of their own rAF
// ticks, interpolating between the last two received frames
// (ARCHITECTURE.md §6.4) so motion stays smooth at display refresh even
// though telemetry itself arrives at a lower, non-fixed rate.
//
// M20: what to poll is no longer a hardcoded 5-tap/3-type matrix — it's a
// live Map that grows and shrinks as previews subscribe/unsubscribe
// (NODE_EDITOR.md §9's "many small, dynamic taps... driven by what is
// visible in the viewport"). The M4 baseline (main + 4 aux buses, all 3
// frame types) is seeded once at startup so the M5 analysis panel keeps
// working completely unchanged — those taps are already subscribed
// engine-side automatically (PluginProcessor::prepareToPlay), so seeding
// here only needs to start polling them, not call any native function.
import { getBackendResourceAddress } from '@juce-framework/webview'
import { parseTelemetryFrame, TelemetryFrameType, type TelemetryFrame } from './parseTelemetryFrame'

export type TapName = string

const BASELINE_TAP_NAMES: readonly TapName[] = ['main', 'aux1', 'aux2', 'aux3', 'aux4']
const ALL_FRAME_TYPES: readonly TelemetryFrameType[] = [
  TelemetryFrameType.Oscilloscope,
  TelemetryFrameType.Spectrum,
  TelemetryFrameType.Meter,
]

const FRAME_TYPE_PATH: Record<TelemetryFrameType, string> = {
  [TelemetryFrameType.Oscilloscope]: 'scope',
  [TelemetryFrameType.Spectrum]: 'spectrum',
  [TelemetryFrameType.Meter]: 'meter',
}

interface TapState {
  previous: TelemetryFrame | null
  latest: TelemetryFrame | null
  previousReceivedAtMs: number
  latestReceivedAtMs: number
}

const store = new Map<string, TapState>()

function tapKey(tap: TapName, frameType: TelemetryFrameType): string {
  return `${tap}:${frameType}`
}

// The set of (tapName, frameType) pairs the shared loop below actually
// polls each frame — everything else about this module (the fetch
// transport, interpolation, the M4 baseline seed) is unchanged from before
// M20; only this "what to poll" set became dynamic instead of a fixed
// nested loop over TAP_NAMES x FRAME_TYPES.
const activePolls = new Map<string, { tap: TapName; frameType: TelemetryFrameType }>()

/** Starts polling one (tap, frameType) pair — idempotent, safe to call for
    a pair that's already being polled. This only manages the LOCAL polling
    set; it never calls a native function itself. For a dynamic per-node
    preview, call this together with the engine-side subscribe command
    (see ui/src/graph/previewSubscriptions.ts, C2) — for a tap the engine
    already subscribes on its own (M4's baseline main/aux buses), this is
    the only thing needed.
*/
export function pollTap(tap: TapName, frameType: TelemetryFrameType): void {
  activePolls.set(tapKey(tap, frameType), { tap, frameType })
}

/** Stops polling one (tap, frameType) pair — idempotent. Leaves whatever
    frame was last received in the store (a late reader still gets the last
    real value, not a sudden gap) until that tap starts polling again, at
    which point it naturally gets overwritten by fresh data.
*/
export function stopPollingTap(tap: TapName, frameType: TelemetryFrameType): void {
  activePolls.delete(tapKey(tap, frameType))
}

function getOrCreateState(key: string): TapState {
  let state = store.get(key)
  if (!state) {
    state = { previous: null, latest: null, previousReceivedAtMs: 0, latestReceivedAtMs: 0 }
    store.set(key, state)
  }
  return state
}

async function pollOne(tap: TapName, frameType: TelemetryFrameType): Promise<void> {
  const path = `tap/${tap}/${FRAME_TYPE_PATH[frameType]}`
  try {
    const response = await fetch(getBackendResourceAddress(path))
    const buffer = await response.arrayBuffer()
    const frame = parseTelemetryFrame(buffer)
    if (!frame) return

    const state = getOrCreateState(tapKey(tap, frameType))
    if (state.latest && state.latest.sequenceNumber === frame.sequenceNumber) return // nothing new

    state.previous = state.latest
    state.previousReceivedAtMs = state.latestReceivedAtMs
    state.latest = frame
    state.latestReceivedAtMs = performance.now()
  } catch {
    // Transient fetch failure (editor tearing down, etc.) — the next poll retries.
  }
}

let running = false
let rafHandle: number | null = null

export function startTelemetryPolling(): void {
  if (running) return
  running = true

  for (const tap of BASELINE_TAP_NAMES)
    for (const frameType of ALL_FRAME_TYPES)
      pollTap(tap, frameType)

  const loop = () => {
    if (!running) return
    for (const { tap, frameType } of activePolls.values()) void pollOne(tap, frameType)
    rafHandle = requestAnimationFrame(loop)
  }

  rafHandle = requestAnimationFrame(loop)
}

export function stopTelemetryPolling(): void {
  running = false
  if (rafHandle !== null) cancelAnimationFrame(rafHandle)
  rafHandle = null
}

export interface InterpolatedTap {
  payload: Float32Array
  sampleRate: number
}

/** Reads the current best estimate for a tap's payload, linearly
    interpolating between the last two received frames based on how far
    "now" is into the interval between their arrival times. Returns null
    until at least one frame has arrived.
*/
export function getInterpolatedTap(tap: TapName, frameType: TelemetryFrameType): InterpolatedTap | null {
  const state = store.get(tapKey(tap, frameType))
  if (!state || !state.latest) return null

  if (!state.previous || state.previous.payload.length !== state.latest.payload.length)
    return { payload: state.latest.payload, sampleRate: state.latest.sampleRate }

  const interval = state.latestReceivedAtMs - state.previousReceivedAtMs
  const alpha = interval > 0 ? Math.max(0, Math.min(1, (performance.now() - state.previousReceivedAtMs) / interval)) : 1

  const previous = state.previous
  const latest = state.latest
  const out = new Float32Array(latest.payload.length)
  for (let i = 0; i < out.length; i++) out[i] = previous.payload[i] * (1 - alpha) + latest.payload[i] * alpha

  return { payload: out, sampleRate: latest.sampleRate }
}
