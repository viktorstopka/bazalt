// design/Visualization/Tune.png: view.tune's own node body — the default
// viewer for Pitch. A title-less panel, ports at the top corners; top to
// bottom: a centre-zero deviation meter, the nearest note + octave large with
// the deviation in cents beside it, and a muted footer with the raw value in
// semitones and its frequency. Within TUNE_TOLERANCE_CENTS of the note the
// readout and meter are neutral white; outside it both turn red, so being out
// of tune reads at a glance.
//
// Driven by real telemetry, the same way CountBody.tsx is: view.tune declares
// an ordinary Waveform preview on "out" (ViewTuneNode.h) and this reads the
// newest Oscilloscope bucket as "the current pitch". Note, cents and Hz are
// all derived from that ONE value here (never from each other after
// rounding), and written straight into the DOM on every new frame — outside
// React's render cycle (ARCHITECTURE.md §7) — so a bend or glide moves the
// meter and the cents smoothly instead of the readout snapping.
import { useEffect, useRef, useState } from 'react'
import type { NodeDescriptor } from '../graph/descriptorTypes'
import { type NodeCardState, PortGlyph } from './NodeCard'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getLatestTapFrame } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { describePitch, TUNE_TOLERANCE_CENTS } from './pitchReadout'
import './TuneBody.css'

const VISIBILITY_ROOT_MARGIN = '200px'

/** The newest Oscilloscope bucket's midpoint — continuous, never rounded. */
function readLatestValue(payload: Float32Array): number | null {
  const bucketCount = payload.length / 2
  if (bucketCount < 1) return null
  return (payload[(bucketCount - 1) * 2] + payload[(bucketCount - 1) * 2 + 1]) / 2
}

export function TuneBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const rootRef = useRef<HTMLDivElement | null>(null)
  const noteRef = useRef<HTMLSpanElement | null>(null)
  const centsRef = useRef<HTMLSpanElement | null>(null)
  const bandRef = useRef<HTMLDivElement | null>(null)
  const semitonesRef = useRef<HTMLSpanElement | null>(null)
  const hzRef = useRef<HTMLSpanElement | null>(null)
  const [visible, setVisible] = useState(false)
  const idRef = useRef(`view.tune:${instanceId ?? 'gallery'}`)

  const inputPort = descriptor.inputs[0]
  const outputPort = descriptor.outputs[0]
  const inputConnected = state.connectedPortIds?.has(inputPort.id) ?? false
  const outputConnected = state.connectedPortIds?.has(outputPort.id) ?? false

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

    const render = () => {
      const frame = getLatestTapFrame(tap, TelemetryFrameType.Oscilloscope)
      if (!frame || frame.sequenceNumber === lastSequenceSeen) return
      lastSequenceSeen = frame.sequenceNumber
      const value = readLatestValue(frame.payload)
      if (value === null || !Number.isFinite(value)) return

      const pitch = describePitch(value)
      if (noteRef.current) noteRef.current.textContent = pitch.note
      if (centsRef.current) centsRef.current.textContent = pitch.centsText
      if (semitonesRef.current) semitonesRef.current.textContent = pitch.semitonesText
      if (hzRef.current) hzRef.current.textContent = pitch.hzText
      if (bandRef.current) {
        // The band grows from the centre tick toward the deviation: ±50 cents
        // reaches the strip's end.
        const fraction = Math.min(1, Math.abs(pitch.cents) / 50) * 50
        bandRef.current.style.left = pitch.cents >= 0 ? '50%' : `${50 - fraction}%`
        bandRef.current.style.width = `${fraction}%`
      }
      rootRef.current?.classList.toggle('view-tune-out', !pitch.inTune)
    }

    const rendererId = idRef.current
    registerPreviewRenderer(rendererId, render)
    return () => unregisterPreviewRenderer(rendererId)
  }, [visible, instanceId, outputPort.id])

  const classNames = ['node-card', 'view-tune-card', state.selected && 'node-card-selected', state.bypassed && 'node-card-bypassed', state.listening && 'node-card-listening', state.error && 'node-card-error']
    .filter(Boolean)
    .join(' ')

  return (
    <div className={classNames} ref={rootRef} title={`Within ±${TUNE_TOLERANCE_CENTS} cents reads white; outside, red`}>
      <div className="view-tune-port view-tune-port-left">
        <PortGlyph port={inputPort} side="left" instanceId={instanceId} connected={inputConnected} />
      </div>
      <div className="view-tune-port view-tune-port-right">
        <PortGlyph port={outputPort} side="right" instanceId={instanceId} connected={outputConnected} />
      </div>
      <div className="view-tune-meter">
        <div ref={bandRef} className="view-tune-band" />
        <div className="view-tune-tick" />
      </div>
      <div className="view-tune-readout">
        <span ref={noteRef} className="view-tune-note">
          –
        </span>
        <span ref={centsRef} className="view-tune-cents" />
      </div>
      <div className="view-tune-footer">
        <span ref={semitonesRef}>–</span>
        <span ref={hzRef} />
      </div>
    </div>
  )
}
