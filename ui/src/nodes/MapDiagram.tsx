// design/Map.png: the mapping diagram drawn inside adapt.map's card, between
// its "In Max" and "Out Min" rows (NodeCard.tsx's INLINE_DIAGRAMS slot).
//
// A fixed-size box — "its size never changes, only the shapes inside it
// scale and shift, so the node keeps a stable height whatever the values
// are" — holding one closed outline: the input range as a vertical segment
// at the left edge, the output range at the right edge, and the two lines
// joining corresponding ends. Both segments sit on ONE common scale spanning
// everything the four values cover, so their relative size is honest (0..1
// into 0..3000 collapses the left segment to a point: a triangle). An
// inverted mapping makes the joining lines cross: a bowtie, which is exactly
// the "this range flips" signal.
//
// A live readout: an unwired range uses its committed value, or the slider's
// in-progress value mid-drag (liveValues); a WIRED range is read from engine
// telemetry on that input port (findTappableBufferIndex taps the buffer that
// feeds it), so a modulated range moves the diagram too. Redrawn from the
// shared preview render loop, outside React, like every other live visual.
import { useEffect, useRef, useState } from 'react'
import type { NodeDescriptor } from '../graph/descriptorTypes'
import type { NodeCardState } from './NodeCard'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getLatestTapFrame } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import './MapDiagram.css'

const RANGE_PORTS = ['math.map.inMin', 'math.map.inMax', 'math.map.outMin', 'math.map.outMax'] as const
type RangePort = (typeof RANGE_PORTS)[number]

// The diagram's own coordinate space — MUST match .map-diagram-svg's CSS
// width/height. SVG, not canvas, for the same zoom-exactness reason
// ScopeHistoryBody.tsx's header gives.
const WIDTH = 60
const HEIGHT = 120
const INSET = 1 // keeps a stroke on the box's edge from being clipped in half

/** The closed outline for four range values: left segment (inMin -> inMax),
    joining line to outMax, right segment down to outMin, joining line back. */
function mapDiagramPath(inMin: number, inMax: number, outMin: number, outMax: number): string {
  const lo = Math.min(inMin, inMax, outMin, outMax)
  const hi = Math.max(inMin, inMax, outMin, outMax)
  const span = hi - lo
  const y = (v: number) => (span > 0 ? INSET + ((hi - v) / span) * (HEIGHT - 2 * INSET) : HEIGHT / 2)
  const left = INSET
  const right = WIDTH - INSET
  return `M ${left} ${y(inMin)} L ${left} ${y(inMax)} L ${right} ${y(outMax)} L ${right} ${y(outMin)} Z`
}

/** The latest value on a tapped port: the newest Oscilloscope column's
    midpoint (the same raw-frame read CountBody.tsx uses for its number). */
function latestTapValue(nodeId: string, portId: string): number | undefined {
  const frame = getLatestTapFrame(tapNameForPreview(nodeId, portId), TelemetryFrameType.Oscilloscope)
  if (!frame || frame.payload.length < 2) return undefined
  const lo = frame.payload[frame.payload.length - 2]
  const hi = frame.payload[frame.payload.length - 1]
  return Number.isNaN(lo) || Number.isNaN(hi) ? undefined : (lo + hi) / 2
}

export function MapDiagram({
  descriptor,
  state,
  instanceId,
  liveValues,
}: {
  descriptor: NodeDescriptor
  state: NodeCardState
  instanceId?: string
  liveValues: Readonly<Record<string, number>>
}) {
  const pathRef = useRef<SVGPathElement | null>(null)
  const rootRef = useRef<HTMLDivElement | null>(null)
  const [visible, setVisible] = useState(!instanceId) // the gallery has no canvas to scroll out of

  const valueOf = (portId: RangePort): number =>
    liveValues[portId] ?? state.parameterValues?.[portId] ?? descriptor.inputs.find((p) => p.id === portId)?.defaultValue ?? 0
  const wired = RANGE_PORTS.filter((portId) => state.connectedPortIds?.has(portId))
  const wiredKey = wired.join(',')

  // Fresh values for the render loop without resubscribing it every render
  // (ScopeHistoryBody.tsx's latestRef pattern).
  const staticValues = Object.fromEntries(RANGE_PORTS.map((p) => [p, valueOf(p)])) as Record<RangePort, number>
  const latestRef = useRef(staticValues)
  useEffect(() => {
    latestRef.current = staticValues
  })

  useEffect(() => {
    const root = rootRef.current
    if (!root || !instanceId) return
    const observer = new IntersectionObserver(([entry]) => setVisible(entry.isIntersecting), { rootMargin: '200px' })
    observer.observe(root)
    return () => observer.disconnect()
  }, [instanceId])

  useEffect(() => {
    if (!visible || !instanceId || wired.length === 0) return
    for (const portId of wired) void subscribeNodePreview(instanceId, portId, 'waveform')
    return () => {
      for (const portId of wired) unsubscribeNodePreview(instanceId, portId, 'waveform')
    }
    // wiredKey stands in for `wired` (a fresh array every render).
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [visible, instanceId, wiredKey])

  useEffect(() => {
    if (!visible || !instanceId) return
    const wiredPorts = wiredKey ? (wiredKey.split(',') as RangePort[]) : []
    const id = `adapt.map:${instanceId}`
    const render = () => {
      const values = { ...latestRef.current }
      for (const portId of wiredPorts) values[portId] = latestTapValue(instanceId, portId) ?? values[portId]
      pathRef.current?.setAttribute('d', mapDiagramPath(values['math.map.inMin'], values['math.map.inMax'], values['math.map.outMin'], values['math.map.outMax']))
    }
    registerPreviewRenderer(id, render)
    return () => unregisterPreviewRenderer(id)
  }, [visible, instanceId, wiredKey])

  // The initial/gallery shape comes straight from React; the render loop
  // above takes over (same element) once the node is live and visible.
  const d = mapDiagramPath(staticValues['math.map.inMin'], staticValues['math.map.inMax'], staticValues['math.map.outMin'], staticValues['math.map.outMax'])

  return (
    <div className="map-diagram" ref={rootRef}>
      <svg className="map-diagram-svg" viewBox={`0 0 ${WIDTH} ${HEIGHT}`}>
        <path ref={pathRef} d={d} />
      </svg>
    </div>
  )
}
