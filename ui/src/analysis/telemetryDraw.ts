// M5's own scope/spectrum/meter Canvas2D drawing code, factored out of
// TelemetryScope.tsx (M20 C5) so NodeCard.tsx's inline node previews
// (NodePreview.tsx) reuse the exact same drawing code rather than a second
// copy — "port M5's existing... canvases... reusing M5's existing draw
// code verbatim" per the M20 plan.
import { tokens } from '../theme/tokens'

function linearToDb(x: number): number {
  return 20 * Math.log10(Math.max(x, 1e-6))
}

export function drawScope(ctx: CanvasRenderingContext2D, width: number, height: number, payload: Float32Array) {
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

export function drawSpectrum(ctx: CanvasRenderingContext2D, width: number, height: number, payload: Float32Array) {
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

export function drawMeter(ctx: CanvasRenderingContext2D, width: number, height: number, payload: Float32Array) {
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
