import { useEffect, useId, useRef } from 'react'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { getInterpolatedTap, type TapName } from '../telemetry/telemetryClient'
import { tokens } from '../theme/tokens'
import { registerPreviewRenderer, unregisterPreviewRenderer } from './previewRenderLoop'

export type ScopeKind = 'scope' | 'spectrum' | 'meter'

interface TelemetryScopeProps {
  tap: TapName
  kind: ScopeKind
  label: string
}

const FRAME_TYPE_BY_KIND: Record<ScopeKind, TelemetryFrameType> = {
  scope: TelemetryFrameType.Oscilloscope,
  spectrum: TelemetryFrameType.Spectrum,
  meter: TelemetryFrameType.Meter,
}

function linearToDb(x: number): number {
  return 20 * Math.log10(Math.max(x, 1e-6))
}

function drawScope(ctx: CanvasRenderingContext2D, width: number, height: number, payload: Float32Array) {
  const numBuckets = payload.length / 2
  const midY = height / 2

  ctx.strokeStyle = tokens.color.scopeTrace
  ctx.lineWidth = 1
  ctx.beginPath()

  for (let b = 0; b < numBuckets; b++) {
    const lo = payload[b * 2]
    const hi = payload[b * 2 + 1]
    const x = (b / numBuckets) * width
    const yLo = midY - lo * midY
    const yHi = midY - hi * midY
    ctx.moveTo(x, yLo)
    ctx.lineTo(x, yHi)
  }

  ctx.stroke()
}

function drawSpectrum(ctx: CanvasRenderingContext2D, width: number, height: number, payload: Float32Array) {
  const minDb = -80
  const maxDb = 20

  ctx.strokeStyle = tokens.color.spectrumTrace
  ctx.lineWidth = 1
  ctx.beginPath()

  for (let i = 0; i < payload.length; i++) {
    const db = linearToDb(payload[i])
    const normalized = Math.max(0, Math.min(1, (db - minDb) / (maxDb - minDb)))
    const x = (i / payload.length) * width
    const y = height - normalized * height
    if (i === 0) ctx.moveTo(x, y)
    else ctx.lineTo(x, y)
  }

  ctx.stroke()
}

function drawMeter(ctx: CanvasRenderingContext2D, width: number, height: number, payload: Float32Array) {
  const [peak, rms] = payload
  const minDb = -60

  const barFraction = (value: number) => Math.max(0, Math.min(1, (linearToDb(value) - minDb) / (0 - minDb)))

  const rmsHeight = barFraction(rms) * height
  ctx.fillStyle = tokens.color.meterRms
  ctx.fillRect(0, height - rmsHeight, width, rmsHeight)

  const peakY = height - barFraction(peak) * height
  ctx.strokeStyle = tokens.color.meterPeak
  ctx.lineWidth = 2
  ctx.beginPath()
  ctx.moveTo(0, peakY)
  ctx.lineTo(width, peakY)
  ctx.stroke()
}

/** One oscilloscope/spectrum/meter canvas, driven entirely outside React's
    render cycle (ARCHITECTURE.md §7): reads the latest interpolated
    telemetry payload once per shared-loop tick (previewRenderLoop.ts, M20
    C3) and draws directly, with no React state involved in the render path
    itself. Through M5 each instance ran its own independent rAF loop —
    fine at M5's fixed count of 15 small canvases, but NODE_EDITOR.md §9
    calls for one shared loop once the node editor's dynamic preview count
    replaces this panel; this component and the node editor's own inline
    previews (NodeCard.tsx, M20 C5) now both register with that one loop
    instead of each owning one.
*/
export function TelemetryScope({ tap, kind, label }: TelemetryScopeProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const id = useId()

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const frameType = FRAME_TYPE_BY_KIND[kind]

    const render = () => {
      const dpr = window.devicePixelRatio || 1
      const width = canvas.clientWidth
      const height = canvas.clientHeight
      if (canvas.width !== width * dpr || canvas.height !== height * dpr) {
        canvas.width = width * dpr
        canvas.height = height * dpr
      }

      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.fillStyle = tokens.color.panel
      ctx.fillRect(0, 0, width, height)

      const tapData = getInterpolatedTap(tap, frameType)
      if (tapData) {
        if (kind === 'scope') drawScope(ctx, width, height, tapData.payload)
        else if (kind === 'spectrum') drawSpectrum(ctx, width, height, tapData.payload)
        else drawMeter(ctx, width, height, tapData.payload)
      }
    }

    registerPreviewRenderer(id, render)
    return () => unregisterPreviewRenderer(id)
  }, [tap, kind, id])

  return (
    <div className="telemetry-scope">
      <span className="telemetry-scope-label">{label}</span>
      <canvas ref={canvasRef} />
    </div>
  )
}
