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

/** A node card's own compact inline waveform preview (NodePreview.tsx) —
    deliberately a SEPARATE function from `drawScope` above rather than a
    shared one with a "compact" flag, so the M5 analysis panel's own scope
    (TelemetryScope.tsx, which still calls `drawScope` directly) is never at
    risk of changing by editing this one. At the small size a node card
    actually gives this (NodeCard.css's `.node-preview-canvas`), hairline
    `stroke()` segments one CSS pixel or less apart anti-alias into a faint,
    gap-toothed scatter — direct feedback described this as "extremely
    pixelated." Filling each bucket's own min/max span as a solid rect
    (at least 1px tall, so a near-silent moment still reads as a flat line
    rather than vanishing) reads as a clean, solid waveform silhouette
    instead — the same technique DAW clip-view waveforms use. Untested at
    the M5 panel's own much larger canvas size on purpose — 128 buckets
    spread across a wide panel would render as visibly separate blocks
    with this fill approach, not the smooth trace `drawScope`'s hairlines
    already give it there; this function is for a small box only.
*/
export function drawScopeFilled(ctx: CanvasRenderingContext2D, width: number, height: number, payload: Float32Array) {
  const numBuckets = payload.length / 2
  const midY = height / 2
  const barWidth = Math.max(width / numBuckets, 1)

  ctx.fillStyle = tokens.color.scopeTrace

  for (let b = 0; b < numBuckets; b++) {
    const lo = payload[b * 2]
    const hi = payload[b * 2 + 1]
    const x = (b / numBuckets) * width
    const yLo = midY - lo * midY
    const yHi = midY - hi * midY
    const top = Math.min(yLo, yHi)
    const barHeight = Math.max(Math.abs(yHi - yLo), 1)
    ctx.fillRect(x, top, barWidth, barHeight)
  }
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
