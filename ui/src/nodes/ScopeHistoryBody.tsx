// design/Visualization/Scope1.png: the shared body behind every long-window
// scrolling-history viewer — "Scope for Control values, Scope for
// Modulation, and Gate... variants of one panel with different vertical
// scales and different trace styles, so build the shared pieces here and
// reuse them for the other two." Each typeId has a thin wrapper supplying
// only what differs: ScopeControlBody.tsx (white line), ScopeModulationBody.tsx
// (orange, filled to an editable centre line, range seeded from the source's
// polarity) and GateBody.tsx (blue, binary TRUE/FALSE scale, square-edged
// trace that never drops a brief true state). The `variant` prop selects the
// vertical scale + trace style; everything else here is shared by all three.
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
import type { NodeDescriptor, PortDescriptor } from '../graph/descriptorTypes'
import { type NodeCardState, PortGlyph } from './NodeCard'
import type { PortUiStyle } from '../graph/portUiKind'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getInterpolatedTap, getLatestTapFrame } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { findWireAtInput, getEndpoint } from '../graph/graphStore'
import { niceRange } from '../format/niceRange'
import { EditableStat } from './EditableStat'
import './ScopeHistoryBody.css'

const VISIBILITY_ROOT_MARGIN = '200px'

// The panel's own logical coordinate space, and the <svg>'s own viewBox
// size — MUST match .scope-history-card's declared width/height in
// ScopeHistoryBody.css exactly (both files read these same numbers right
// next to a comment pointing at the other one). Rendered as SVG, not
// <canvas>, for the same reason RippleBody.tsx's own header comment gives
// in full: a direct, reproducible bug ("doesn't fit the frame", and the
// trace visibly drifting relative to the panel while zooming — the exact
// signature RippleBody.tsx's own comment describes, "offset grows zooming
// in, shrinks zooming out") traced back to a <canvas>'s backing-store
// resolution disagreeing with the browser's own zoom transform by a small
// zoom-proportional amount. An <svg> with a viewBox has no such concept at
// all — the same transform/paint pipeline that already correctly scales
// every border, cable and grid dot in this editor does 100% of the
// scaling natively, removing the whole bug class rather than chasing its
// next symptom, same fix RippleBody.tsx already made for the identical
// reason.
const PANEL_WIDTH = 190
const PANEL_HEIGHT = 130

// Gate.png: the TRUE and FALSE levels sit inset from the panel's own edges
// (the reference draws FALSE as a line well above the bottom border, TRUE
// just under the top one), so a held-false baseline is never hidden under
// the hairline border. Measured off the reference: ~11% and ~16% of height.
const BINARY_TRUE_INSET = 14
const BINARY_FALSE_INSET = 20
// ScopeMod.png/Gate.png: "dim" fill under a brighter trace — one shared
// opacity so the two filled variants read as the same visual language.
const FILL_OPACITY = 0.28
const CENTRE_LINE_OPACITY = 0.55

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

/** Which vertical scale + trace style the shared panel draws — the only
    thing that genuinely differs between the three history viewers.
    - 'line' (view.scope.control, Scope1.png): an editable min/max range and
      a thin min/max-decimated line.
    - 'centred' (view.scope.modulation, ScopeMod.png): the same editable
      range plus an editable centre line; the area between the trace and the
      centre is filled dim, with the line drawn bright on top — above and
      below read with the same weight, a signed value rather than a level.
    - 'binary' (view.gate, Gate.png): fixed TRUE/FALSE levels, nothing to
      zoom into; a square-edged filled region wherever the value was true.
*/
export type ScopeHistoryVariant = 'line' | 'centred' | 'binary'

export interface ScopeHistoryBodyProps {
  descriptor: NodeDescriptor
  state: NodeCardState
  instanceId?: string
  variant: ScopeHistoryVariant
  /** The trace stroke colour — and the fill's colour too, at reduced
      opacity, for the variants that fill. */
  traceColor: string
  /** Fixes the port glyphs to one type's style (ScopeMod.png: "Everything is
      orange") instead of the live-resolved one. Omitted, the glyphs follow
      the ports' own resolved type exactly like every other node. */
  portStyle?: PortUiStyle
  /** The range labels' colour; omitted, the muted secondary text colour
      every other viewer uses. */
  labelColor?: string
  /** "The range autofills from the source port's polarity" (ScopeMod.png):
      a variant-specific seed read off the live-resolved upstream port. When
      it returns a range, that range is used exactly the way a declared one
      is (no observation phase for range); undefined falls back to the
      source's declared min/max, then to observing the signal. */
  seedRangeFromSource?: (sourcePort: PortDescriptor) => { min: number; max: number } | undefined
  /** The node's own ParameterDescriptor id for its time window — committed
      through the ordinary state.onParameterCommit path (real engine
      parameter, not a cosmetic property: it changes what AnalysisThread
      computes, ViewHistoryWindow.h's own doc comment has the full
      reasoning), both for manual edits and for this component's own
      auto-window default.
  */
  timeWindowParameterId: string
  minTimeWindowSeconds: number
  maxTimeWindowSeconds: number
  defaultTimeWindowSeconds: number
}

/** A whole number reads as one ("1", "0", "-1", "127" — every reference
    image labels its range that way); anything else keeps two decimals. */
function labelDecimals(value: number): number {
  return Number.isInteger(value) ? 0 : 2
}

/** ScopeMod.png: the centre line defaults to the middle of the range —
    exactly 0 for a bipolar -1…1 signal ("for a bipolar signal it sits at
    zero"), 0.5 for a unipolar 0…1 one. It is a signed view ("above and below
    the centre read with the same weight"), so the default splits the panel
    evenly; "a unipolar signal can be given a centre of 0.5, or of 0, as the
    user prefers" — 0 is one edit away (viewer.center). */
function defaultCentre(min: number, max: number): number {
  return (min + max) / 2
}

// The centre label is nudged inward this far from either edge so it never
// sits on top of the max/min labels when the centre is edited to a range end.
const CENTRE_LABEL_EDGE_CLEARANCE = 0.12

const clamp = (v: number, lo: number, hi: number) => Math.min(Math.max(v, lo), hi)

export function ScopeHistoryBody({
  descriptor,
  state,
  instanceId,
  variant,
  traceColor,
  portStyle,
  labelColor,
  seedRangeFromSource,
  timeWindowParameterId,
  minTimeWindowSeconds,
  maxTimeWindowSeconds,
  defaultTimeWindowSeconds,
}: ScopeHistoryBodyProps) {
  const pathRef = useRef<SVGPathElement | null>(null)
  const fillRef = useRef<SVGPathElement | null>(null)
  const svgRef = useRef<SVGSVGElement | null>(null)
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
  // own Min/Max. A variant-specific seed (polarity, for Modulation) wins
  // over the declared bounds; the binary scale has a fixed range and never
  // observes at all.
  const wire = instanceId ? findWireAtInput(instanceId, inputPort.id) : undefined
  const sourceEndpoint = wire ? getEndpoint(wire.fromNodeId, wire.fromPortId, 'output') : undefined
  const variantSeed =
    variant === 'binary' ? { min: 0, max: 1 } : sourceEndpoint ? seedRangeFromSource?.(sourceEndpoint.port) : undefined
  const declaredMin = variantSeed?.min ?? sourceEndpoint?.port.minValue ?? undefined
  const declaredMax = variantSeed?.max ?? sourceEndpoint?.port.maxValue ?? undefined
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
    variant,
    declaredMin,
    declaredMax,
    hasDeclaredRange,
    currentTimeWindow,
    timeWindowParameterId,
    minTimeWindowSeconds,
    maxTimeWindowSeconds,
  })
  // Written in an effect (runs after commit), not directly in the render
  // body — React may call a render function more than once per commit
  // (StrictMode, concurrent features) without this component's props
  // actually changing, and writing a ref mid-render risks the ref
  // reflecting a render that never committed. No dependency array: this
  // one is meant to run after EVERY render, unconditionally. traceColor is
  // NOT in this bundle — it's a fixed prop for this node variant (set once
  // directly in JSX's own `stroke`), never read from the render loop.
  useEffect(() => {
    latestRef.current = {
      state,
      variant,
      declaredMin,
      declaredMax,
      hasDeclaredRange,
      currentTimeWindow,
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
    if (!visible || !instanceId) return

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

      // design/Visualization/Scope1.png's own observation phase: derive a
      // range/window default from what the signal actually does, then
      // stop. Uses the RAW latest frame (not the smoothed interpolated
      // accessor below) — probing wants the real decimated columns, not a
      // blend between two polls of them.
      const raw = getLatestTapFrame(tap, TelemetryFrameType.RollingHistory)
      if (sourceKey && observationStartMsRef.current !== null && (!rangeFrozenRef.current || !windowCommittedRef.current)) {
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

      const path = pathRef.current
      if (!path) return

      if (latest.variant === 'binary') {
        // Gate.png: "A brief true state must never be dropped." Read the RAW
        // latest frame, never the interpolated one: blending two scrolled
        // frames column-by-column smears a one-column pulse into two
        // half-strength ghosts, exactly the averaging this node exists to
        // refuse (the same reason CountBody/RippleBody read raw frames for
        // their discrete values). The engine already folds every sample into
        // its column's (lo, hi), so hi = 1 means "true at some point in this
        // column" even for a single-sample pulse.
        if (!raw) return
        const svg = svgRef.current
        const screenWidth = svg?.getBoundingClientRect().width ?? PANEL_WIDTH
        drawBinary(raw.payload, path, fillRef.current, screenWidth > 0 ? PANEL_WIDTH / screenWidth : 1)
        return
      }

      const interpolated = getInterpolatedTap(tap, TelemetryFrameType.RollingHistory)
      if (!interpolated) return

      const effectiveMin = latest.state.viewerRangeMinOverride ?? latest.declaredMin ?? observedRangeRef.current?.min ?? 0
      const effectiveMax = latest.state.viewerRangeMaxOverride ?? latest.declaredMax ?? observedRangeRef.current?.max ?? 1
      const payload = interpolated.payload
      const numColumns = payload.length / 2
      const span = effectiveMax - effectiveMin || 1
      const yOf = (value: number) => PANEL_HEIGHT - ((value - effectiveMin) / span) * PANEL_HEIGHT

      // "The trace is a thin continuous white line... peaks between pixel
      // columns are preserved by min/max decimation" — one continuous SVG
      // path tracing each column's (lo, hi) envelope in sequence, in the
      // panel's own fixed PANEL_WIDTH/PANEL_HEIGHT coordinate space (NOT
      // measured pixels — see this file's own top comment on why). NaN
      // columns (never yet written) start a fresh subpath (a real "M",
      // not drawn as a line through 0) rather than being skipped outright,
      // so a gap reads as a gap.
      let d = ''
      let started = false
      for (let c = 0; c < numColumns; c++) {
        const lo = payload[c * 2]
        if (Number.isNaN(lo)) {
          started = false
          continue
        }
        const hi = payload[c * 2 + 1]
        const x = (c / numColumns) * PANEL_WIDTH
        d += started ? `L ${x} ${yOf(lo)} ` : `M ${x} ${yOf(lo)} `
        started = true
        d += `L ${x} ${yOf(hi)} `
      }
      // stroke colour is set once in JSX below (a fixed prop for this
      // node variant, never changes live) — only `d` needs touching here.
      path.setAttribute('d', d)

      const fill = fillRef.current
      if (latest.variant === 'centred' && fill) {
        // ScopeMod.png: "the area between the line and the centre line is
        // painted". Per column that's the union of [centre, hi] and
        // [lo, centre] — i.e. from min(lo, centre) up to max(hi, centre) —
        // so one closed polygon per contiguous run of real columns: forward
        // along the upper edge, back along the lower one.
        const centre = latest.state.viewerCenterOverride ?? defaultCentre(effectiveMin, effectiveMax)
        const yCentre = clamp(yOf(centre), 0, PANEL_HEIGHT)
        let f = ''
        let runStart = -1
        const closeRun = (end: number) => {
          if (runStart < 0) return
          let upper = ''
          let lower = ''
          for (let c = runStart; c < end; c++) {
            const x = (c / numColumns) * PANEL_WIDTH
            upper += `${c === runStart ? 'M' : 'L'} ${x} ${Math.min(yOf(payload[c * 2 + 1]), yCentre)} `
            lower = `L ${x} ${Math.max(yOf(payload[c * 2]), yCentre)} ` + lower
          }
          f += upper + lower + 'Z '
          runStart = -1
        }
        for (let c = 0; c < numColumns; c++) {
          if (Number.isNaN(payload[c * 2])) closeRun(c)
          else if (runStart < 0) runStart = c
        }
        closeRun(numColumns)
        fill.setAttribute('d', f)
      }
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
  const centreForDisplay = state.viewerCenterOverride ?? defaultCentre(effectiveMinForDisplay, effectiveMaxForDisplay)
  const displaySpan = effectiveMaxForDisplay - effectiveMinForDisplay || 1
  // The centre line's height as a 0..1 fraction from the panel's top —
  // shared by the SVG line itself and its label outside the panel, so the
  // two can never disagree.
  const centreFraction = clamp((effectiveMaxForDisplay - centreForDisplay) / displaySpan, 0, 1)

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

  const labelStyle = labelColor ? { color: labelColor } : undefined

  return (
    <div className={classNames} ref={rootRef}>
      <div className="scope-history-port scope-history-port-left">
        <PortGlyph port={inputPort} side="left" instanceId={instanceId} connected={inputConnected} isPoly={false} styleOverride={portStyle} />
      </div>
      <div className="scope-history-port scope-history-port-right">
        <PortGlyph port={outputPort} side="right" instanceId={instanceId} connected={outputConnected} isPoly={false} styleOverride={portStyle} />
      </div>
      <svg className="scope-history-visual" ref={svgRef} viewBox={`0 0 ${PANEL_WIDTH} ${PANEL_HEIGHT}`}>
        {variant === 'centred' && (
          <line
            x1={0}
            x2={PANEL_WIDTH}
            y1={centreFraction * PANEL_HEIGHT}
            y2={centreFraction * PANEL_HEIGHT}
            stroke={traceColor}
            strokeOpacity={CENTRE_LINE_OPACITY}
            strokeWidth={1}
          />
        )}
        {variant !== 'line' && <path ref={fillRef} fill={traceColor} fillOpacity={FILL_OPACITY} stroke="none" />}
        <path ref={pathRef} fill="none" stroke={traceColor} strokeWidth={1} strokeLinejoin={variant === 'binary' ? 'miter' : 'round'} />
      </svg>
      {variant === 'binary' ? (
        // Gate.png: TRUE/FALSE "in the same small muted face the other
        // viewers use for their range. Neither is editable — there is
        // nothing to zoom into." Positioned at the exact levels the trace
        // uses (BINARY_TRUE_INSET/BINARY_FALSE_INSET), not the panel's own edges.
        <div className="scope-history-range scope-history-range-levels" style={labelStyle}>
          <span style={{ top: `${(BINARY_TRUE_INSET / PANEL_HEIGHT) * 100}%` }}>TRUE</span>
          <span style={{ top: `${(1 - BINARY_FALSE_INSET / PANEL_HEIGHT) * 100}%` }}>FALSE</span>
        </div>
      ) : (
        <div className="scope-history-range" style={labelStyle}>
          <EditableStat label="" value={effectiveMaxForDisplay} decimals={labelDecimals(effectiveMaxForDisplay)} onCommit={commitRangeMax} />
          <EditableStat label="" value={effectiveMinForDisplay} decimals={labelDecimals(effectiveMinForDisplay)} onCommit={commitRangeMin} />
        </div>
      )}
      {variant === 'centred' && (
        <div className="scope-history-range scope-history-range-levels" style={labelStyle}>
          <span style={{ top: `${clamp(centreFraction, CENTRE_LABEL_EDGE_CLEARANCE, 1 - CENTRE_LABEL_EDGE_CLEARANCE) * 100}%` }}>
            <EditableStat label="" value={centreForDisplay} decimals={labelDecimals(centreForDisplay)} onCommit={state.onSetViewerCenter} />
          </span>
        </div>
      )}
      <div className="scope-history-window">
        <EditableStat label="" value={currentTimeWindow} decimals={2} unit="s" onCommit={(v) => state.onParameterCommit?.(timeWindowParameterId, v)} />
      </div>
    </div>
  )
}

/** Gate.png's trace: "a square-edged region: filled from the FALSE line up
    to the TRUE line wherever the value is true, empty wherever it is false.
    Dim blue fill, brighter blue along the edges."

    Drawn on the real SCREEN-pixel grid, not the 256 data columns: "if
    several transitions fall inside one pixel column, that column is drawn
    as filled rather than being point-sampled and missed". Each pixel column
    is filled iff any data column belonging to it was true at any instant
    (hi = 1 — the engine folds every sample into its column, so a single-
    sample pulse counts). That is exactly the guarantee and no more: a pulse
    always lights at least one whole pixel at every zoom level, while a
    false stretch spanning a whole pixel still reads as empty, so a dense
    pulse train never smears into one solid block. `unitsPerScreenPixel` is
    measured live, so the grid follows the canvas zoom. NaN (never-written)
    columns draw nothing at all, not a FALSE baseline.
*/
function drawBinary(payload: Float32Array, outline: SVGPathElement, fill: SVGPathElement | null, unitsPerScreenPixel: number): void {
  const numColumns = payload.length / 2
  const columnWidth = PANEL_WIDTH / numColumns
  const pixelCount = Math.max(1, Math.round(PANEL_WIDTH / Math.max(unitsPerScreenPixel, 1e-3)))
  const pixelWidth = PANEL_WIDTH / pixelCount
  const yTrue = BINARY_TRUE_INSET
  const yFalse = PANEL_HEIGHT - BINARY_FALSE_INSET

  // Per pixel column: 0 = no data yet, 1 = data, all false, 2 = true somewhere.
  const state = new Uint8Array(pixelCount)
  const EPSILON = 1e-6 // a column ending exactly on a pixel boundary must not spill into the next pixel
  for (let c = 0; c < numColumns; c++) {
    if (Number.isNaN(payload[c * 2])) continue
    // A column narrower than a pixel belongs to the one pixel holding its
    // centre (so a pulse lights exactly one pixel, never two neighbours it
    // merely grazes); a wider one (zoomed in) fills every pixel it spans.
    const narrow = columnWidth < pixelWidth
    const first = narrow ? Math.floor(((c + 0.5) * columnWidth) / pixelWidth) : Math.floor((c * columnWidth) / pixelWidth + EPSILON)
    const last = narrow ? first : Math.min(pixelCount - 1, Math.ceil(((c + 1) * columnWidth) / pixelWidth - EPSILON) - 1)
    const value = payload[c * 2 + 1] >= 0.5 ? 2 : 1
    for (let px = first; px <= last; px++) state[px] = Math.max(state[px], value)
  }

  let edges = ''
  let rects = ''
  let px = 0
  while (px < pixelCount) {
    if (state[px] === 0) {
      px++
      continue
    }
    // One contiguous stretch of real data: its own FALSE baseline with the
    // true regions raised out of it.
    const segmentX0 = px * pixelWidth
    edges += `M ${segmentX0} ${yFalse} `
    while (px < pixelCount && state[px] !== 0) {
      if (state[px] === 2) {
        const x0 = px * pixelWidth
        while (px < pixelCount && state[px] === 2) px++
        const x1 = px * pixelWidth
        edges += `L ${x0} ${yFalse} L ${x0} ${yTrue} L ${x1} ${yTrue} L ${x1} ${yFalse} `
        rects += `M ${x0} ${yFalse} L ${x0} ${yTrue} L ${x1} ${yTrue} L ${x1} ${yFalse} Z `
      } else px++
    }
    edges += `L ${px * pixelWidth} ${yFalse} `
  }
  outline.setAttribute('d', edges)
  fill?.setAttribute('d', rects)
}
