// design/Visualization/Count.png: view.count's own bespoke node body — a
// small rectangular, title-less panel whose whole surface IS the visual (a
// large yellow number, centred, over a Min/Max footer). Dispatched from
// NodeCard.tsx by typeId, same pattern RippleBody.tsx already established
// (NodeCard.tsx's own comment on why this is a client-side-only visual
// swap, no engine change beyond what view.count itself already is).
//
// The number is driven entirely by real engine telemetry — view.count
// declares an ordinary Waveform preview on its own "out" port
// (ViewCountNode.h), the exact same Oscilloscope tap machinery
// view.scope/view.glance already drive for a Control signal — not a
// UI-side timer and not a new engine producer the way view.ripple's own
// EventImpulse was. "Without animating between values — an integer should
// read as a clean change, not a roll" (direct instruction) is why this
// reads the RAW latest frame (getLatestTapFrame, never
// getInterpolatedTap's own blend-between-frames) and writes it straight
// into the DOM via a ref on every genuinely new frame, exactly the
// "redraw from scratch, no animation" discipline RippleBody.tsx's own
// header comment already established for the same reason — the one
// available Oscilloscope bucket already holds the full, unsmoothed min/max
// of whatever the tap actually saw (AnalysisThread::publishOscilloscope
// applies no ballistics at all, unlike view.meter's own ballistic peak);
// this only reads its LAST bucket (lo+hi averaged then rounded, so a value
// that happens to change mid-bucket reads as its midpoint rather than
// silently favouring whichever direction "peak" would) instead of drawing
// the whole window as a trace.
import { useEffect, useRef, useState } from 'react'
import type { NodeDescriptor } from '../graph/descriptorTypes'
import { type NodeCardState, PortGlyph } from './NodeCard'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getLatestTapFrame } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { findWireAtInput, getEndpoint } from '../graph/graphStore'
import { tokens } from '../theme/tokens'
import { EditableStat } from './EditableStat'
import './CountBody.css'

const VISIBILITY_ROOT_MARGIN = '200px'

/** The Oscilloscope payload's own (lo, hi) pair layout (TelemetryFrame.h),
    read only for its very last bucket — "now", the most recent sample(s)
    the tap saw. Averaging lo/hi (rather than always taking `hi`, say)
    reads correctly whether the value just stepped UP or DOWN inside that
    tiny bucket span; the overwhelmingly common case is lo === hi exactly
    (a Control value is almost always constant across one ~128th of a
    0.05s window), where this reduces to the real value with no rounding
    error at all. */
function readLatestValue(payload: Float32Array): number | null {
  const bucketCount = payload.length / 2
  if (bucketCount < 1) return null
  const lo = payload[(bucketCount - 1) * 2]
  const hi = payload[(bucketCount - 1) * 2 + 1]
  return Math.round((lo + hi) / 2)
}

// The footer's own "Min: <n>" / "Max: <n>" field — EditableStat.tsx, shared
// with ScopeControlBody.tsx's own range/window fields (that file's header
// comment has the full reasoning this used to carry here).

export function CountBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const valueRef = useRef<HTMLSpanElement | null>(null)
  const rootRef = useRef<HTMLDivElement | null>(null)
  const [visible, setVisible] = useState(false)
  const idRef = useRef(`view.count:${instanceId ?? 'gallery'}`)

  const inputPort = descriptor.inputs[0]
  const outputPort = descriptor.outputs[0]
  const color = tokens.color.portInteger // Integer is always yellow — fixed ports, nothing to resolve live
  const inputConnected = state.connectedPortIds?.has(inputPort.id) ?? false
  const outputConnected = state.connectedPortIds?.has(outputPort.id) ?? false

  // Observed running min/max — the fallback half of "seeded from the
  // connected port's declared range... when the port declares no range,
  // they follow the lowest and highest values actually observed" (design/
  // Visualization/Count.png). Unlike the big number itself, an occasional
  // new-extreme update is cheap enough to go through ordinary React state
  // rather than a direct DOM write — this isn't the continuous, every-
  // frame case ARCHITECTURE.md §7 reserves for the non-React render loop.
  const [observedMin, setObservedMin] = useState<number | null>(null)
  const [observedMax, setObservedMax] = useState<number | null>(null)

  useEffect(() => {
    const root = rootRef.current
    if (!root || !instanceId) return
    const observer = new IntersectionObserver(([entry]) => setVisible(entry.isIntersecting), { rootMargin: VISIBILITY_ROOT_MARGIN })
    observer.observe(root)
    return () => observer.disconnect()
  }, [instanceId])

  useEffect(() => {
    if (!visible || !instanceId) return
    void subscribeNodePreview(instanceId, outputPort.id, 'waveform')
    return () => unsubscribeNodePreview(instanceId, outputPort.id, 'waveform')
  }, [visible, instanceId, outputPort.id])

  useEffect(() => {
    if (!visible || !instanceId) return

    const tap = tapNameForPreview(instanceId, outputPort.id)
    let lastSequenceSeen: bigint | null = null
    let observedMinSoFar: number | null = null
    let observedMaxSoFar: number | null = null

    const render = () => {
      const frame = getLatestTapFrame(tap, TelemetryFrameType.Oscilloscope)
      if (!frame || frame.sequenceNumber === lastSequenceSeen) return
      lastSequenceSeen = frame.sequenceNumber

      const value = readLatestValue(frame.payload)
      if (value === null) return

      if (valueRef.current) valueRef.current.textContent = String(value)

      if (observedMinSoFar === null || value < observedMinSoFar) {
        observedMinSoFar = value
        setObservedMin(value)
      }
      if (observedMaxSoFar === null || value > observedMaxSoFar) {
        observedMaxSoFar = value
        setObservedMax(value)
      }
    }

    registerPreviewRenderer(idRef.current, render)
    return () => unregisterPreviewRenderer(idRef.current)
  }, [visible, instanceId, outputPort.id])

  // "Seeded from the connected port's declared range" — the live-resolved
  // UPSTREAM port (getEndpoint, not view.count's own static "in" port,
  // which never declares a range of its own at all: it's a pass-through
  // viewer, not an independent value). Recomputed every render, same cost
  // NodeCard.tsx's own resolvedPortStyle already accepts for the identical
  // lookup.
  const wire = instanceId ? findWireAtInput(instanceId, inputPort.id) : undefined
  const sourceEndpoint = wire ? getEndpoint(wire.fromNodeId, wire.fromPortId, 'output') : undefined
  const declaredMin = sourceEndpoint?.port.minValue
  const declaredMax = sourceEndpoint?.port.maxValue

  const effectiveMin = state.countMinOverride ?? declaredMin ?? observedMin ?? 0
  const effectiveMax = state.countMaxOverride ?? declaredMax ?? observedMax ?? 0

  const classNames = ['node-card', 'view-count-card', state.selected && 'node-card-selected', state.bypassed && 'node-card-bypassed', state.listening && 'node-card-listening', state.error && 'node-card-error']
    .filter(Boolean)
    .join(' ')

  return (
    <div className={classNames} ref={rootRef}>
      <div className="view-count-port view-count-port-left">
        <PortGlyph port={inputPort} side="left" instanceId={instanceId} connected={inputConnected} isPoly={false} />
      </div>
      <div className="view-count-port view-count-port-right">
        <PortGlyph port={outputPort} side="right" instanceId={instanceId} connected={outputConnected} isPoly={false} />
      </div>
      <div className="view-count-value-area">
        <span ref={valueRef} className="view-count-value" style={{ color }}>
          0
        </span>
      </div>
      <div className="view-count-divider" />
      <div className="view-count-footer">
        <EditableStat label="Min" value={effectiveMin} onCommit={state.onSetCountMin} />
        <EditableStat label="Max" value={effectiveMax} onCommit={state.onSetCountMax} />
      </div>
    </div>
  )
}
