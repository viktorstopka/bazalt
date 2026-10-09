// design/Visualization/Ripple.png: view.ripple's own bespoke node body —
// a square, title-less panel whose whole surface IS the visual (a
// constant centre dot, with a ring born per incoming Event and expanding/
// fading outward to the panel's edge). Dispatched from NodeCard.tsx by
// typeId, same pattern MacroBody.tsx already established — a client-side
// visual swap only, no engine change beyond what view.ripple itself
// already is.
//
// The animation is driven entirely by real engine telemetry
// (AnalysisThread::publishEventImpulse's own PreviewKind::EventImpulse
// frames — a list of "how many seconds ago" per recent event, with real
// sample-accurate ages), never a UI-side timer: this component only ever
// SPAWNS a ring in response to a genuinely new telemetry frame (tracked by
// its own sequenceNumber, since re-polling the same still-latest frame
// must never spawn a second ring for the same event) and ages every
// already-spawned ring forward by real elapsed wall-clock time between
// frames. Rendered through the same shared rAF loop every other inline
// preview uses (previewRenderLoop.ts, NODE_EDITOR.md §9's "one shared
// loop", not a second independent one) — but NOT through NodePreview.tsx
// itself, whose "redraw whatever the latest payload says, from scratch,
// every frame" model has no persistent state between frames at all, the
// opposite of what a ring list that ages over real time needs.
//
// Rendered as SVG, not <canvas> — a deliberate do-over, not an
// incremental tweak. Three separate canvas-based attempts at centring the
// dot (a wrap.clientWidth + devicePixelRatio×zoom reconstruction, then
// getBoundingClientRect on the canvas itself, then on its wrapper) each
// looked airtight on paper — the draw math is `cx = PANEL_SIZE/2` fed
// through a transform scaled from that SAME measurement, which is
// algebraically self-cancelling to exactly 50% no matter what the
// measurement's actual value is — and each still drifted in practice, the
// last one with a signature (offset grows zooming in, shrinks zooming
// out, exactly zero at one specific zoom level) that points at the
// backing-store resolution math disagreeing with the browser's own
// transform/paint by a small zoom-proportional amount never fully pinned
// down. A <canvas> bitmap's backing-store resolution is a real, separate
// concept from its CSS display size that THIS component's own code has to
// keep in sync by hand, every frame; an <svg> with a `viewBox` has no such
// concept at all — the browser's existing transform/paint pipeline (the
// same one that already correctly scales every border, cable and grid dot
// in this editor) does 100% of the scaling natively. Removes the whole bug
// class at once instead of chasing its next symptom.
import { useEffect, useRef, useState } from 'react'
import type { NodeDescriptor } from '../graph/descriptorTypes'
import { type NodeCardState, PortGlyph } from './NodeCard'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getLatestTapFrame } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { tokens } from '../theme/tokens'
import './RippleBody.css'

// A generous margin so the subscription starts just before the panel
// scrolls into view — same value, same reasoning, as NodePreview.tsx's own.
const VISIBILITY_ROOT_MARGIN = '200px'

// The panel's own logical coordinate space, and the <svg>'s own viewBox
// size — MUST match .view-ripple-card's declared width/height in
// RippleBody.css exactly (both files read "190" right next to a comment
// pointing at the other one).
const PANEL_SIZE = 190
const MAX_RADIUS = PANEL_SIZE / 2 // "reaching the panel's edge before it disappears"

const RING_LIFETIME_MS = 900
// Of the panel's own half-width — "the dot itself stays constant". Measured
// directly off design/Visualization/Ripple.png (dot diameter vs. frame
// width in the reference crop): ~0.21. 0.08 (the original guess) rendered
// visibly smaller than the reference — direct feedback ("too small
// compared to the reference").
const DOT_RADIUS_FRACTION = 0.2
const DOT_RADIUS = MAX_RADIUS * DOT_RADIUS_FRACTION

// A generous ceiling on concurrent visible rings — pooled <circle>
// elements, created once and never added/removed, so a quiet frame costs
// nothing beyond setting r=0 on each (same "preallocate, never churn DOM
// nodes every frame" discipline the WebGL cable layer already follows for
// its own per-frame-updated geometry). Far more than RING_LIFETIME_MS
// (900ms) could realistically keep alive at once at any real trigger rate;
// genuinely exceeding it just drops the oldest excess rings, a real bound
// rather than an unchecked array.
const RING_POOL_SIZE = 16

interface Ring {
  bornAtMs: number
}

export function RippleBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const svgRef = useRef<SVGSVGElement | null>(null)
  const ringRefs = useRef<(SVGCircleElement | null)[]>([])
  const [visible, setVisible] = useState(false)
  const idRef = useRef(`view.ripple:${instanceId ?? 'gallery'}`)

  const inputPort = descriptor.inputs[0]
  const outputPort = descriptor.outputs[0]
  const color = tokens.color.portTrigger // Event is always violet — fixed ports, nothing to resolve live
  const inputConnected = state.connectedPortIds?.has(inputPort.id) ?? false
  const outputConnected = state.connectedPortIds?.has(outputPort.id) ?? false

  useEffect(() => {
    const svg = svgRef.current
    if (!svg || !instanceId) return
    const observer = new IntersectionObserver(([entry]) => setVisible(entry.isIntersecting), { rootMargin: VISIBILITY_ROOT_MARGIN })
    observer.observe(svg)
    return () => observer.disconnect()
  }, [instanceId])

  useEffect(() => {
    if (!visible || !instanceId) return
    void subscribeNodePreview(instanceId, outputPort.id, 'eventImpulse')
    return () => unsubscribeNodePreview(instanceId, outputPort.id, 'eventImpulse')
  }, [visible, instanceId, outputPort.id])

  useEffect(() => {
    if (!visible || !instanceId) return

    const tap = tapNameForPreview(instanceId, outputPort.id)
    const rings: Ring[] = []
    let lastSequenceSeen: bigint | null = null

    const render = () => {
      // A genuinely NEW frame (never the same sequenceNumber twice) spawns
      // one ring per reported event age — getLatestTapFrame is the RAW
      // latest frame, not telemetryClient's own interpolated accessor
      // (interpolating between two discrete, variable-length event lists
      // makes no sense the way it does for a continuous waveform).
      const frame = getLatestTapFrame(tap, TelemetryFrameType.EventImpulse)
      const now = performance.now()
      if (frame && frame.sequenceNumber !== lastSequenceSeen) {
        lastSequenceSeen = frame.sequenceNumber
        for (let i = 0; i < frame.payload.length; ++i) rings.push({ bornAtMs: now - frame.payload[i] * 1000 })
      }

      // Age out fully-faded rings so this list never grows unbounded.
      for (let i = rings.length - 1; i >= 0; --i) if (now - rings[i].bornAtMs >= RING_LIFETIME_MS) rings.splice(i, 1)
      // A real bound on the pool below, not just a soft expectation — drop
      // the oldest excess rings first if somehow more are alive at once
      // than the pool has slots for.
      if (rings.length > RING_POOL_SIZE) rings.splice(0, rings.length - RING_POOL_SIZE)

      for (let i = 0; i < RING_POOL_SIZE; i++) {
        const circle = ringRefs.current[i]
        if (!circle) continue
        const ring = rings[i]
        if (!ring) {
          circle.setAttribute('r', '0')
          continue
        }
        const t = Math.min(1, (now - ring.bornAtMs) / RING_LIFETIME_MS)
        // Born at the dot's edge, not its centre: a ring growing out from
        // r=0 spends its first fifth hidden under the dot and reads late.
        circle.setAttribute('r', String(DOT_RADIUS + t * (MAX_RADIUS - DOT_RADIUS)))
        circle.setAttribute('stroke-opacity', String(1 - t))
      }
    }

    registerPreviewRenderer(idRef.current, render)
    return () => unregisterPreviewRenderer(idRef.current)
  }, [visible, instanceId, outputPort.id])

  const classNames = ['node-card', 'view-ripple-card', state.selected && 'node-card-selected', state.bypassed && 'node-card-bypassed', state.listening && 'node-card-listening', state.error && 'node-card-error']
    .filter(Boolean)
    .join(' ')

  return (
    <div className={classNames}>
      <div className="view-ripple-port view-ripple-port-left">
        <PortGlyph port={inputPort} side="left" instanceId={instanceId} connected={inputConnected} />
      </div>
      <div className="view-ripple-port view-ripple-port-right">
        <PortGlyph port={outputPort} side="right" instanceId={instanceId} connected={outputConnected} />
      </div>
      <svg ref={svgRef} className="view-ripple-visual" viewBox={`0 0 ${PANEL_SIZE} ${PANEL_SIZE}`}>
        {Array.from({ length: RING_POOL_SIZE }, (_, i) => (
          <circle
            key={i}
            ref={(el) => {
              ringRefs.current[i] = el
            }}
            cx={PANEL_SIZE / 2}
            cy={PANEL_SIZE / 2}
            r={0}
            fill="none"
            stroke={color}
            strokeWidth={1.5}
          />
        ))}
        {/* Drawn last so it paints on top of every ring — "the dot itself
            stays constant" (direct instruction): fixed cx/cy/r, never
            touched by the render loop at all, not even a ref. */}
        <circle cx={PANEL_SIZE / 2} cy={PANEL_SIZE / 2} r={DOT_RADIUS} fill={color} />
      </svg>
    </div>
  )
}
