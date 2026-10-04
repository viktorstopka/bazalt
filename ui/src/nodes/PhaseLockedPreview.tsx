// The phase-locked waveform display (PreviewKind 'phaseLocked') — one
// component shared by every phase source's inline preview (the oscillators,
// via NodePreview.tsx) and the placeable view.cycle node (CycleBody.tsx).
//
// The horizontal axis is PHASE: the engine sends PHASE_LOCKED_CYCLES cycles
// already aligned to phase zero (AnalysisThread::publishPhaseLocked), so the
// shape is stationary and identical at 0.3 Hz and 440 Hz — nothing here
// searches for triggers or rescales by rate. The UI only draws; every value
// is computed by the engine (CLAUDE.md rule 1), including the band-limited
// shape itself.
//
// Rendering, deliberately:
// - SVG, not canvas: the path lives in a fixed viewBox and the browser's own
//   transform does the scaling, so canvas zoom can never desynchronize it
//   (the zoom-drift bug RippleBody.tsx/ScopeHistoryBody.tsx already fixed
//   the same way). `vector-effect: non-scaling-stroke` keeps the stroke one
//   consistent screen weight at every zoom, anti-aliased by the browser.
// - Fixed vertical scale: nominal ±1 with headroom, never normalised, so
//   amplitude and its modulation read as the wave growing and shrinking in a
//   stable frame. Anything past the headroom is clipped and marked.
// - Interpolated between telemetry frames point by point (same phase grid
//   every frame), so a slowly modulated shape morphs instead of stepping.
// - No background and no frame: it sits directly on the node.
//
// The playhead is where the source is in its cycles right now — what makes
// the same node read as an LFO at 2 Hz. Extrapolated between frames from the
// latest frame's playhead and frequency, so it glides; a Sync reset arrives in
// the next frame and visibly jumps, as it should. Auto shows it only below
// PLAYHEAD_AUTO_MAX_HZ, where it can actually be followed.
import { useEffect, useRef, useState } from 'react'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getInterpolatedTap, getLatestTapFrame, getLatestTapFrameReceivedAtMs } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { tokens } from '../theme/tokens'
import { HEADER_FLOATS, HEADROOM, HEIGHT, PHASE_LOCKED_CYCLES, PLAYHEAD_AUTO_MAX_HZ, WIDTH, yOf } from './phaseLockedGeometry'
import './PhaseLockedPreview.css'

export type PlayheadMode = 'auto' | 'on' | 'off'




export function PhaseLockedPreview({
  nodeId,
  portId,
  playheadMode,
  onCyclePlayheadMode,
  emptyHint,
}: {
  nodeId: string
  portId: string
  playheadMode: PlayheadMode
  /** Shown as a small Auto/On/Off control on hover; omitted, no control. */
  onCyclePlayheadMode?: () => void
  /** Shown while no frame has arrived (e.g. view.cycle with no phase source upstream). */
  emptyHint?: string
}) {
  const rootRef = useRef<HTMLDivElement | null>(null)
  const traceRef = useRef<SVGPathElement | null>(null)
  const clipRef = useRef<SVGPathElement | null>(null)
  const playheadRef = useRef<SVGLineElement | null>(null)
  const dotRef = useRef<HTMLDivElement | null>(null)
  const [visible, setVisible] = useState(false)
  const [hasFrame, setHasFrame] = useState(false)
  const modeRef = useRef(playheadMode)
  useEffect(() => {
    modeRef.current = playheadMode
  })

  useEffect(() => {
    const root = rootRef.current
    if (!root) return
    const observer = new IntersectionObserver(([entry]) => setVisible(entry.isIntersecting), { rootMargin: '200px' })
    observer.observe(root)
    return () => observer.disconnect()
  }, [])

  useEffect(() => {
    if (!visible) return
    void subscribeNodePreview(nodeId, portId, 'phaseLocked')
    return () => unsubscribeNodePreview(nodeId, portId, 'phaseLocked')
  }, [visible, nodeId, portId])

  useEffect(() => {
    if (!visible) return
    const tap = tapNameForPreview(nodeId, portId)
    const id = `phaseLocked:${nodeId}:${portId}`
    let sawFrame = false

    const render = () => {
      const shape = getInterpolatedTap(tap, TelemetryFrameType.PhaseLocked)
      const latest = getLatestTapFrame(tap, TelemetryFrameType.PhaseLocked)
      if (!shape || !latest || latest.payload.length <= HEADER_FLOATS) return
      if (!sawFrame) {
        sawFrame = true
        setHasFrame(true)
      }

      const values = shape.payload
      const numPoints = values.length - HEADER_FLOATS
      const xOf = (i: number) => (i / numPoints) * WIDTH

      // The trace, with NaN (a fold bin nothing has landed in yet) as a gap
      // and anything past the headroom clamped to the edge and collected as
      // a clip mark.
      let d = ''
      let clips = ''
      let started = false
      for (let i = 0; i < numPoints; i++) {
        const v = values[i + HEADER_FLOATS]
        if (Number.isNaN(v)) {
          started = false
          continue
        }
        const clamped = Math.max(-HEADROOM, Math.min(HEADROOM, v))
        const x = xOf(i)
        const y = yOf(clamped)
        d += `${started ? 'L' : 'M'}${x.toFixed(2)} ${y.toFixed(2)} `
        started = true
        if (clamped !== v) clips += `M${x.toFixed(2)} ${(clamped > 0 ? 1 : HEIGHT - 1).toFixed(2)} h${(WIDTH / numPoints).toFixed(2)} `
      }
      traceRef.current?.setAttribute('d', d)
      clipRef.current?.setAttribute('d', clips)

      // Playhead: latest position, advanced by real elapsed time.
      const frequency = latest.payload[1]
      const show = modeRef.current === 'on' || (modeRef.current === 'auto' && Math.abs(frequency) <= PLAYHEAD_AUTO_MAX_HZ)
      const playhead = playheadRef.current
      const dot = dotRef.current
      if (!playhead || !dot) return
      if (!show) {
        playhead.setAttribute('visibility', 'hidden')
        dot.style.visibility = 'hidden'
        return
      }
      const elapsedSeconds = (performance.now() - getLatestTapFrameReceivedAtMs(tap, TelemetryFrameType.PhaseLocked)) / 1000
      const position = ((((latest.payload[0] + frequency * elapsedSeconds) % PHASE_LOCKED_CYCLES) + PHASE_LOCKED_CYCLES) % PHASE_LOCKED_CYCLES) / PHASE_LOCKED_CYCLES
      const x = position * WIDTH
      playhead.setAttribute('x1', x.toFixed(2))
      playhead.setAttribute('x2', x.toFixed(2))
      playhead.setAttribute('visibility', 'visible')

      const index = Math.min(numPoints - 1, Math.floor(position * numPoints))
      const v = values[index + HEADER_FLOATS]
      // The dot is a DOM element positioned in percent of the box, not an
      // SVG circle: the viewBox is stretched non-uniformly, which would
      // squash a circle into an ellipse.
      if (Number.isNaN(v)) dot.style.visibility = 'hidden'
      else {
        dot.style.left = `${position * 100}%`
        dot.style.top = `${(yOf(Math.max(-HEADROOM, Math.min(HEADROOM, v))) / HEIGHT) * 100}%`
        dot.style.visibility = 'visible'
      }
    }

    registerPreviewRenderer(id, render)
    return () => unregisterPreviewRenderer(id)
  }, [visible, nodeId, portId])

  return (
    <div className="phase-locked-preview" ref={rootRef}>
      <svg className="phase-locked-preview-svg" viewBox={`0 0 ${WIDTH} ${HEIGHT}`} preserveAspectRatio="none">
        <line className="phase-locked-preview-playhead" ref={playheadRef} y1={0} y2={HEIGHT} visibility="hidden" />
        <path className="phase-locked-preview-trace" ref={traceRef} />
        <path className="phase-locked-preview-clip" ref={clipRef} stroke={tokens.color.error} />
      </svg>
      <div className="phase-locked-preview-dot" ref={dotRef} style={{ visibility: 'hidden' }} />
      {!hasFrame && emptyHint && <span className="phase-locked-preview-hint">{emptyHint}</span>}
      {onCyclePlayheadMode && (
        <button
          type="button"
          className="phase-locked-preview-mode"
          title="Playhead: Auto shows it below 30 Hz"
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => {
            e.stopPropagation()
            onCyclePlayheadMode()
          }}
        >
          {playheadMode === 'auto' ? 'Auto' : playheadMode === 'on' ? 'On' : 'Off'}
        </button>
      )}
    </div>
  )
}
