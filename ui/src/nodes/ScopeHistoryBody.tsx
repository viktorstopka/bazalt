// design/Visualization/Scope1.png: the shared body behind every long-window
// scrolling-history viewer — "Scope for Control values, Scope for
// Modulation, and Gate... variants of one panel with different vertical
// scales and different trace styles, so build the shared pieces here and
// reuse them for the other two." ScopeControlBody.tsx is the thin
// typeId-specific wrapper that currently uses this (trace colour + which
// parameter id carries the time window); a future view.scope.modulation
// reuses this file unchanged, passing its own colour/parameter id.
//
// Driven entirely by real engine telemetry (PreviewKind::RollingHistory,
// AnalysisThread::publishRollingHistory via view.scope.control's own
// ParameterDescriptor-backed timeWindow — ViewScopeControlNode.h) —
// "Driven by engine telemetry at display rate and interpolated between
// frames; never reconstructed UI-side" (direct instruction) is why this
// reads getInterpolatedTap, unlike CountBody.tsx/RippleBody.tsx's own
// getLatestTapFrame: THOSE exist specifically to avoid animating between
// discrete values/events, which is exactly the opposite of what a
// continuous scrolling trace wants.
import { useEffect, useRef, useState } from 'react'
import type { NodeDescriptor } from '../graph/descriptorTypes'
import { type NodeCardState, PortGlyph } from './NodeCard'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getInterpolatedTap, getLatestTapFrame } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { findWireAtInput, getEndpoint } from '../graph/graphStore'
import { niceRange } from '../format/niceRange'
import { EditableStat } from './EditableStat'
import { getCamera } from '../canvas/interactionStore'
import './ScopeHistoryBody.css'

const VISIBILITY_ROOT_MARGIN = '200px'

// design/Visualization/Scope1.png: "On connection they autofill... derives
// a range... and then stops adjusting" + "[the time window's] default
// should suit what is connected... State plainly in the code which rule
// you chose." Both share ONE 1-second real-time observation phase after a
// (re)connection, rather than each inventing its own timing — simpler, and
// it's how the spec itself talks about them (one breath, one "on
// connection" moment).
const OBSERVATION_MS = 1000
// How many estimated cycles of the observed signal the auto-window default
// shows at once — enough to read it as a repeating shape without cramming
// the panel into either a single flat segment or a illegibly dense scribble.
// A deliberately chosen constant, not derived from anything else.
const CYCLES_TO_SHOW = 6

/** `(lo+hi)/2` midpoint-crossing counting estimates the observed signal's
    period from a short real sample, the same technique
    AnalysisThread::publishOscilloscope's own RisingEdge trigger mode uses
    for a single crossing (ADR-0029) — here counted repeatedly over the
    whole observed span instead of finding just the latest one. A `payload`
    of (lo, hi) pairs, oldest first; NaN (never-written) columns are
    skipped. Returns null if fewer than 2 real columns exist yet (nothing
    to estimate a period from) or if genuinely nothing crossed the
    midpoint (a flat/DC signal, or one that hasn't started moving) — the
    caller treats either as "assume slow", matching the stated problem's
    own bias (the wrong default being too SHORT, not too long).
*/
function estimatePeriodSeconds(payload: Float32Array, columnDurationSeconds: number): number | null {
  const numColumns = payload.length / 2
  let min = Infinity
  let max = -Infinity
  let filled = 0
  for (let c = 0; c < numColumns; c++) {
    const lo = payload[c * 2]
    if (Number.isNaN(lo)) continue
    const hi = payload[c * 2 + 1]
    min = Math.min(min, lo)
    max = Math.max(max, hi)
    filled++
  }
  if (filled < 2) return null

  const threshold = (min + max) / 2
  let crossings = 0
  let lastSign: number | null = null
  for (let c = 0; c < numColumns; c++) {
    const lo = payload[c * 2]
    if (Number.isNaN(lo)) continue
    const mid = (lo + payload[c * 2 + 1]) / 2
    const sign = mid >= threshold ? 1 : -1
    if (lastSign !== null && sign !== lastSign) crossings++
    lastSign = sign
  }
  if (crossings === 0) return null

  const observedSeconds = filled * columnDurationSeconds
  return (observedSeconds * 2) / crossings // each crossing is one half-period
}

export interface ScopeHistoryBodyProps {
  descriptor: NodeDescriptor
  state: NodeCardState
  instanceId?: string
  /** The port colour (fixed, not live-resolved — these nodes have a fixed
      SignalType per variant, same reasoning CountBody/RippleBody give for
      their own fixed glyph colour) and the trace stroke colour. Always the
      same value today (the panel's whole visual language is "this type's
      colour"), kept as one prop since nothing has ever needed them to
      differ.
  */
  traceColor: string
  /** The node's own ParameterDescriptor id for its time window — committed
      through the ordinary state.onParameterCommit path (real engine
      parameter, not a cosmetic property: it changes what AnalysisThread
      computes, ViewScopeControlNode.h's own doc comment has the full
      reasoning), both for manual edits and for this component's own
      auto-window default.
  */
  timeWindowParameterId: string
  minTimeWindowSeconds: number
  maxTimeWindowSeconds: number
  defaultTimeWindowSeconds: number
}

export function ScopeHistoryBody({
  descriptor,
  state,
  instanceId,
  traceColor,
  timeWindowParameterId,
  minTimeWindowSeconds,
  maxTimeWindowSeconds,
  defaultTimeWindowSeconds,
}: ScopeHistoryBodyProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const rootRef = useRef<HTMLDivElement | null>(null)
  const [visible, setVisible] = useState(false)
  const id = `${descriptor.typeId}:${instanceId ?? 'gallery'}`

  const inputPort = descriptor.inputs[0]
  const outputPort = descriptor.outputs[0]
  const inputConnected = state.connectedPortIds?.has(inputPort.id) ?? false
  const outputConnected = state.connectedPortIds?.has(outputPort.id) ?? false

  const currentTimeWindow = state.parameterValues?.[timeWindowParameterId] ?? defaultTimeWindowSeconds

  // "Seeded from the connected port's declared range" — the live-resolved
  // UPSTREAM port, same lookup CountBody.tsx already established for its
  // own Min/Max.
  const wire = instanceId ? findWireAtInput(instanceId, inputPort.id) : undefined
  const sourceEndpoint = wire ? getEndpoint(wire.fromNodeId, wire.fromPortId, 'output') : undefined
  const declaredMin = sourceEndpoint?.port.minValue
  const declaredMax = sourceEndpoint?.port.maxValue
  const hasDeclaredRange = declaredMin != null && declaredMax != null
  const sourceKey = wire ? `${wire.fromNodeId}:${wire.fromPortId}` : null

  const [observedRange, setObservedRange] = useState<{ min: number; max: number } | null>(null)
  // Mirrors `observedRange`, updated synchronously at the exact same call
  // site — same "ref + state in lockstep" pairing ValueSlider.tsx's own
  // liveValueRef/liveValue already establishes, so render() (below) always
  // sees this immediately, not one animation frame late waiting for React
  // to actually commit the state update.
  const observedRangeRef = useRef<{ min: number; max: number } | null>(null)
  const updateObservedRange = (next: { min: number; max: number } | null): void => {
    observedRangeRef.current = next
    setObservedRange(next)
  }

  // One observation phase per (re)connection — identified by the resolved
  // upstream (nodeId, portId), so wiring a DIFFERENT source in restarts it
  // even if this node's own instanceId/wire-id shape didn't change.
  // Plain refs, not state: read/written from the non-React render loop
  // below, same discipline as CountBody.tsx's own observedMin/MaxRef.
  const observationSourceKeyRef = useRef<string | null | undefined>(undefined)
  const observationStartMsRef = useRef<number | null>(null)
  const rangeFrozenRef = useRef(false)
  const windowCommittedRef = useRef(false)

  // The render loop below is only re-subscribed (registerPreviewRenderer)
  // when visibility/instanceId/outputPort/sourceKey change — re-running it
  // on every prop/state change (a parameter edit, a selection change, ANY
  // GraphSurface.tsx re-render handing down a brand-new `state` object)
  // would be wasteful churn for a callback that fires every animation
  // frame regardless. But `render()` still needs every one of these at
  // their CURRENT value, not whatever they were the one time the effect
  // last ran — so they're read through this ref, kept fresh on every
  // render (a plain assignment, not an effect: there's nothing to clean up
  // about "remembering the latest props"), the standard way to give a
  // long-lived callback fresh values without resubscribing it.
  const latestRef = useRef({
    state,
    declaredMin,
    declaredMax,
    hasDeclaredRange,
    currentTimeWindow,
    traceColor,
    timeWindowParameterId,
    minTimeWindowSeconds,
    maxTimeWindowSeconds,
  })
  // Written in an effect (runs after commit), not directly in the render
  // body — React may call a render function more than once per commit
  // (StrictMode, concurrent features) without this component's props
  // actually changing, and writing a ref mid-render risks the ref
  // reflecting a render that never committed. No dependency array: this
  // one is meant to run after EVERY render, unconditionally.
  useEffect(() => {
    latestRef.current = {
      state,
      declaredMin,
      declaredMax,
      hasDeclaredRange,
      currentTimeWindow,
      traceColor,
      timeWindowParameterId,
      minTimeWindowSeconds,
      maxTimeWindowSeconds,
    }
  })

  useEffect(() => {
    const root = rootRef.current
    if (!root || !instanceId) return
    const observer = new IntersectionObserver(([entry]) => setVisible(entry.isIntersecting), { rootMargin: VISIBILITY_ROOT_MARGIN })
    observer.observe(root)
    return () => observer.disconnect()
  }, [instanceId])

  useEffect(() => {
    if (!visible || !instanceId) return
    void subscribeNodePreview(instanceId, outputPort.id, 'rollingHistory')
    return () => unsubscribeNodePreview(instanceId, outputPort.id, 'rollingHistory')
  }, [visible, instanceId, outputPort.id])

  useEffect(() => {
    const canvas = canvasRef.current
    if (!visible || !canvas || !instanceId) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const tap = tapNameForPreview(instanceId, outputPort.id)

    const render = () => {
      const latest = latestRef.current

      // Restart the observation phase on a genuinely new upstream source
      // (including "freshly connected, was previously nothing").
      if (observationSourceKeyRef.current !== sourceKey) {
        observationSourceKeyRef.current = sourceKey
        observationStartMsRef.current = sourceKey ? performance.now() : null
        rangeFrozenRef.current = latest.hasDeclaredRange // nothing to observe for range if the source already declares one
        windowCommittedRef.current = false
        updateObservedRange(null)
      }

      const interpolated = getInterpolatedTap(tap, TelemetryFrameType.RollingHistory)

      // design/Visualization/Scope1.png's own observation phase: derive a
      // range/window default from what the signal actually does, then
      // stop. Uses the RAW latest frame (not the smoothed interpolated
      // accessor above) — probing wants the real decimated columns, not a
      // blend between two polls of them.
      if (sourceKey && observationStartMsRef.current !== null && (!rangeFrozenRef.current || !windowCommittedRef.current)) {
        const raw = getLatestTapFrame(tap, TelemetryFrameType.RollingHistory)
        if (raw) {
          const numColumns = raw.payload.length / 2
          let min = Infinity
          let max = -Infinity
          for (let c = 0; c < numColumns; c++) {
            const lo = raw.payload[c * 2]
            if (Number.isNaN(lo)) continue
            min = Math.min(min, lo)
            max = Math.max(max, raw.payload[c * 2 + 1])
          }
          // Only live-updates while still actually observing for range —
          // once frozen (or if a declared range made this unnecessary from
          // the start), further scans here exist purely for the window
          // probe below and must not re-render on every frame for nothing.
          if (!rangeFrozenRef.current && min <= max) updateObservedRange({ min, max })

          if (performance.now() - observationStartMsRef.current >= OBSERVATION_MS) {
            if (!rangeFrozenRef.current && min <= max) {
              rangeFrozenRef.current = true
              updateObservedRange(niceRange(min, max)) // "rounds it to a readable number, and then stops adjusting"
            }
            if (!windowCommittedRef.current) {
              windowCommittedRef.current = true
              const columnDuration = latest.currentTimeWindow / numColumns
              const period = estimatePeriodSeconds(raw.payload, columnDuration)
              const defaultWindow =
                period === null
                  ? latest.maxTimeWindowSeconds // no crossings observed: assume slow, per the spec's own stated bias
                  : Math.min(latest.maxTimeWindowSeconds, Math.max(latest.minTimeWindowSeconds, period * CYCLES_TO_SHOW))
              latest.state.onParameterCommit?.(latest.timeWindowParameterId, defaultWindow)
            }
          }
        }
      }

      const effectiveMin = latest.state.viewerRangeMinOverride ?? latest.declaredMin ?? observedRangeRef.current?.min ?? 0
      const effectiveMax = latest.state.viewerRangeMaxOverride ?? latest.declaredMax ?? observedRangeRef.current?.max ?? 1

      // Same dpr*zoom backing-store sizing NodePreview.tsx's own render()
      // already establishes (that file's own comment has the full "why" —
      // a transform runs after layout, so clientWidth/Height never reflect
      // it, and a <canvas> is a genuinely fixed-resolution bitmap that
      // needs the zoom folded into its own backing store rather than
      // stretched after the fact).
      const sizeSource = canvas.parentElement
      if (!sizeSource) return
      const dpr = (window.devicePixelRatio || 1) * Math.max(getCamera().zoom, 1)
      const width = sizeSource.clientWidth
      const height = sizeSource.clientHeight
      if (width === 0 || height === 0) return
      const targetWidth = Math.round(width * dpr)
      const targetHeight = Math.round(height * dpr)
      if (canvas.width !== targetWidth || canvas.height !== targetHeight) {
        canvas.width = targetWidth
        canvas.height = targetHeight
      }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.clearRect(0, 0, width, height)

      if (!interpolated) return
      const payload = interpolated.payload
      const numColumns = payload.length / 2
      const span = effectiveMax - effectiveMin || 1
      const yOf = (value: number) => height - ((value - effectiveMin) / span) * height

      // "The trace is a thin continuous white line... peaks between pixel
      // columns are preserved by min/max decimation" — one continuous
      // path tracing each column's (lo, hi) envelope in sequence (NaN
      // columns, not yet written, are simply skipped — a gap in the path,
      // never drawn as 0).
      ctx.strokeStyle = latest.traceColor
      ctx.lineWidth = 1
      ctx.lineJoin = 'round'
      ctx.beginPath()
      let started = false
      for (let c = 0; c < numColumns; c++) {
        const lo = payload[c * 2]
        if (Number.isNaN(lo)) {
          started = false
          continue
        }
        const hi = payload[c * 2 + 1]
        const x = (c / numColumns) * width
        if (!started) {
          ctx.moveTo(x, yOf(lo))
          started = true
        } else {
          ctx.lineTo(x, yOf(lo))
        }
        ctx.lineTo(x, yOf(hi))
      }
      ctx.stroke()
    }

    registerPreviewRenderer(id, render)
    return () => unregisterPreviewRenderer(id)
    // Deliberately NOT depending on state/declaredMin/traceColor/etc.:
    // render() reads every one of those through latestRef (updated on
    // every render, above) rather than closing over them directly, so this
    // effect only needs to re-subscribe when the TAP ITSELF changes —
    // visibility, which node/port, or which upstream source is feeding it.
  }, [visible, instanceId, outputPort.id, sourceKey, id])

  const commitRangeMin = state.onSetViewerRangeMin
  const commitRangeMax = state.onSetViewerRangeMax
  const effectiveMinForDisplay = state.viewerRangeMinOverride ?? declaredMin ?? observedRange?.min ?? 0
  const effectiveMaxForDisplay = state.viewerRangeMaxOverride ?? declaredMax ?? observedRange?.max ?? 1

  const classNames = [
    'node-card',
    'scope-history-card',
    state.selected && 'node-card-selected',
    state.bypassed && 'node-card-bypassed',
    state.listening && 'node-card-listening',
    state.error && 'node-card-error',
  ]
    .filter(Boolean)
    .join(' ')

  return (
    <div className={classNames} ref={rootRef}>
      <div className="scope-history-port scope-history-port-left">
        <PortGlyph port={inputPort} side="left" instanceId={instanceId} connected={inputConnected} isPoly={false} />
      </div>
      <div className="scope-history-port scope-history-port-right">
        <PortGlyph port={outputPort} side="right" instanceId={instanceId} connected={outputConnected} isPoly={false} />
      </div>
      <div className="scope-history-canvas-wrap">
        <canvas className="scope-history-canvas" ref={canvasRef} />
      </div>
      <div className="scope-history-range">
        <EditableStat label="" value={effectiveMaxForDisplay} decimals={2} onCommit={commitRangeMax} />
        <EditableStat label="" value={effectiveMinForDisplay} decimals={2} onCommit={commitRangeMin} />
      </div>
      <div className="scope-history-window">
        <EditableStat label="" value={currentTimeWindow} decimals={2} unit="s" onCommit={(v) => state.onParameterCommit?.(timeWindowParameterId, v)} />
      </div>
    </div>
  )
}
