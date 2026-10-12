// A spectrum on the log-frequency axis the EQ editor uses
// (wiki/plans/DataAndWavetable.md 1d) — shared by the EQ editor, the EQ's node
// thumbnail and the Spectrum view, so all three read the same way: 20 Hz to
// 20 kHz, octaves equally wide, the same grid. Drawing only (CLAUDE.md rule 1).
import { useEffect } from 'react'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getInterpolatedTap } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { FREQUENCY_GRID, hzToUnit, spectrumColumnsDb } from '../factory/eqModel'

/** Subscribes a spectrum tap on (nodeId, portId) while mounted. */
export function useSpectrumTap(nodeId: string | null | undefined, portId: string | null | undefined): void {
  useEffect(() => {
    if (!nodeId || !portId) return
    void subscribeNodePreview(nodeId, portId, 'spectrum')
    return () => unsubscribeNodePreview(nodeId, portId, 'spectrum')
  }, [nodeId, portId])
}

export interface SpectrumFrame {
  /** dBFS per column (a full-scale sine reads about 0). */
  columns: Float32Array
  sampleRate: number
}

/** The tap's latest frame resampled onto `points` log columns, or null.
    `smoothed` (same length) is a per-column display smoothing state the
    caller keeps between frames — fast up, slow down, like a meter. */
export function readSpectrum(nodeId: string, portId: string, points: number, smoothed?: Float32Array): SpectrumFrame | null {
  const tap = getInterpolatedTap(tapNameForPreview(nodeId, portId), TelemetryFrameType.Spectrum)
  if (!tap || tap.payload.length === 0) return null
  const columns = spectrumColumnsDb(tap.payload, tap.sampleRate || 48000, points)
  // JUCE's magnitude FFT is unnormalised: a full-scale sine under a Hann
  // window peaks at fftSize / 4 = bins / 2.
  const normDb = 20 * Math.log10(tap.payload.length / 2)
  for (let i = 0; i < points; i++) columns[i] -= normDb
  if (smoothed && smoothed.length === points) {
    for (let i = 0; i < points; i++) {
      const target = columns[i]
      smoothed[i] = Number.isFinite(smoothed[i]) && smoothed[i] > target ? smoothed[i] - (smoothed[i] - target) * 0.18 : target
      columns[i] = smoothed[i]
    }
  }
  return { columns, sampleRate: tap.sampleRate || 48000 }
}

export interface SpectrumStyle {
  minDb: number
  maxDb: number
  stroke: string
  fill?: string
  lineWidth?: number
}

/** Draws `columns` (dB, log-spaced over the shared axis) across the canvas. */
export function drawSpectrumColumns(ctx: CanvasRenderingContext2D, width: number, height: number, columns: Float32Array, style: SpectrumStyle): void {
  const n = columns.length
  if (n === 0) return
  const y = (db: number) => height - Math.max(0, Math.min(1, (db - style.minDb) / (style.maxDb - style.minDb))) * height
  ctx.beginPath()
  for (let i = 0; i < n; i++) {
    const x = ((i + 0.5) / n) * width
    if (i === 0) ctx.moveTo(x, y(columns[i]))
    else ctx.lineTo(x, y(columns[i]))
  }
  if (style.fill) {
    ctx.lineTo(width, height)
    ctx.lineTo(0, height)
    ctx.closePath()
    ctx.fillStyle = style.fill
    ctx.fill()
    // Re-trace the top edge for the stroke (the fill path closed along the bottom).
    ctx.beginPath()
    for (let i = 0; i < n; i++) {
      const x = ((i + 0.5) / n) * width
      if (i === 0) ctx.moveTo(x, y(columns[i]))
      else ctx.lineTo(x, y(columns[i]))
    }
  }
  ctx.strokeStyle = style.stroke
  ctx.lineWidth = style.lineWidth ?? 1
  ctx.stroke()
}

/** The frequency grid (decades labelled) and, optionally, dB lines. */
export function drawFrequencyGrid(
  ctx: CanvasRenderingContext2D,
  width: number,
  height: number,
  color: string,
  labelColor: string,
  dbLines?: { minDb: number; maxDb: number; step: number },
): void {
  ctx.save()
  ctx.lineWidth = 1
  ctx.font = '9px monospace'
  ctx.fillStyle = labelColor
  for (const line of FREQUENCY_GRID) {
    const x = Math.round(hzToUnit(line.hz) * width) + 0.5
    ctx.strokeStyle = color
    ctx.globalAlpha = line.label ? 0.55 : 0.25
    ctx.beginPath()
    ctx.moveTo(x, 0)
    ctx.lineTo(x, height)
    ctx.stroke()
    if (line.label && width > 160) {
      ctx.globalAlpha = 0.8
      ctx.fillText(line.label, Math.min(x + 2, width - 18), height - 3)
    }
  }
  if (dbLines) {
    for (let db = Math.ceil(dbLines.minDb / dbLines.step) * dbLines.step; db <= dbLines.maxDb; db += dbLines.step) {
      const yy = Math.round(height - ((db - dbLines.minDb) / (dbLines.maxDb - dbLines.minDb)) * height) + 0.5
      ctx.strokeStyle = color
      ctx.globalAlpha = db === 0 ? 0.6 : 0.25
      ctx.beginPath()
      ctx.moveTo(0, yy)
      ctx.lineTo(width, yy)
      ctx.stroke()
    }
  }
  ctx.restore()
}
