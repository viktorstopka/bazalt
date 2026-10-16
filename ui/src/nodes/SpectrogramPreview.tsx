// wiki/plans/Sampling.md 3a.5: view.spectrogram's body — a scrolling
// time-frequency waterfall built from the same spectrum frames the Spectrum
// view draws (no extra analysis): time runs to the left, frequency up on the
// shared log axis (logSpectrum.ts), level as colour. Hover reads frequency,
// note, level and how long ago. Drawing only (CLAUDE.md rule 1).
import { useEffect, useId, useRef, useState } from 'react'
import type { PreviewDescriptor } from '../graph/descriptorTypes'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getInterpolatedTap } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { FREQUENCY_GRID, formatHz, hzToSemitones, hzToUnit, spectrumColumnsDb, unitToHz } from '../factory/eqModel'
import { describePitch } from './pitchReadout'
import { tokens } from '../theme/tokens'
import { getCamera } from '../canvas/interactionStore'
import type { NodeCardState } from './NodeCard'

/** History: columns of `POINTS` log-frequency rows, one pushed every
    `COLUMN_MS` (so it scrolls at the same speed whatever the display rate). */
const COLUMNS = 240
const POINTS = 128
const COLUMN_MS = 25
const MAX_DB = 0

function hexToRgb(hex: string): [number, number, number] {
  const v = parseInt(hex.replace('#', ''), 16)
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255]
}

/** Dark (the node's fill) -> the spectrum colour -> white. */
function buildPalette(): Uint8ClampedArray {
  const lo = hexToRgb(tokens.color.nodeFill)
  const mid = hexToRgb(tokens.color.spectrumTrace)
  const palette = new Uint8ClampedArray(256 * 3)
  for (let i = 0; i < 256; i++) {
    const t = i / 255
    const [a, b, u] = t < 0.7 ? [lo, mid, t / 0.7] : [mid, [255, 255, 255] as [number, number, number], (t - 0.7) / 0.3]
    for (let c = 0; c < 3; c++) palette[i * 3 + c] = a[c] + (b[c] - a[c]) * u
  }
  return palette
}

interface History {
  levels: Float32Array // COLUMNS x POINTS, dB; column `next` is the oldest
  next: number
  lastPush: number
  image: ImageData
  bitmap: HTMLCanvasElement
}

function makeHistory(): History {
  const bitmap = document.createElement('canvas')
  bitmap.width = COLUMNS
  bitmap.height = POINTS
  return { levels: new Float32Array(COLUMNS * POINTS).fill(-200), next: 0, lastPush: 0, image: new ImageData(COLUMNS, POINTS), bitmap }
}

export function SpectrogramPreview({ nodeId, preview, state }: { nodeId: string; preview: PreviewDescriptor; state?: NodeCardState }) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const id = useId()
  const [visible, setVisible] = useState(false)
  const cursorRef = useRef<{ x: number; y: number } | null>(null)
  const settingsRef = useRef({ minDb: -90, freeze: false })
  settingsRef.current = {
    minDb: state?.parameterValues?.['view.spectrogram.range'] ?? -90,
    freeze: (state?.parameterValues?.['view.spectrogram.freeze'] ?? 0) > 0.5,
  }

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const observer = new IntersectionObserver(([entry]) => setVisible(entry.isIntersecting), { rootMargin: '200px' })
    observer.observe(canvas)
    return () => observer.disconnect()
  }, [])

  useEffect(() => {
    if (!visible) return
    void subscribeNodePreview(nodeId, preview.portId, 'spectrogram')
    return () => unsubscribeNodePreview(nodeId, preview.portId, 'spectrogram')
  }, [visible, nodeId, preview.portId])

  useEffect(() => {
    const canvas = canvasRef.current
    if (!visible || !canvas) return
    const ctx = canvas.getContext('2d')
    const sizeSource = canvas.parentElement // see NodePreview.tsx: never size from the canvas itself
    if (!ctx || !sizeSource) return
    const tap = tapNameForPreview(nodeId, preview.portId)
    const history = makeHistory()
    const palette = buildPalette()

    const render = () => {
      const dpr = (window.devicePixelRatio || 1) * Math.max(getCamera().zoom, 1)
      const width = sizeSource.clientWidth
      const height = sizeSource.clientHeight
      if (width === 0 || height === 0) return
      if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
        canvas.width = Math.round(width * dpr)
        canvas.height = Math.round(height * dpr)
      }
      const { minDb, freeze } = settingsRef.current

      // New columns, at a fixed rate (at most a few to catch up after a stall).
      const now = performance.now()
      const data = getInterpolatedTap(tap, TelemetryFrameType.Spectrum)
      if (!freeze && data && data.payload.length > 0) {
        if (history.lastPush === 0) history.lastPush = now - COLUMN_MS
        let due = Math.min(4, Math.floor((now - history.lastPush) / COLUMN_MS))
        if (due > 0) {
          const columns = spectrumColumnsDb(data.payload, data.sampleRate || 48000, POINTS)
          const normDb = 20 * Math.log10(Math.max(1, data.payload.length / 2)) // a full-scale sine reads ~0 dB
          history.lastPush += due * COLUMN_MS
          while (due-- > 0) {
            const base = history.next * POINTS
            for (let p = 0; p < POINTS; p++) history.levels[base + p] = columns[p] - normDb
            history.next = (history.next + 1) % COLUMNS
          }
        }
      } else history.lastPush = now

      // Into the bitmap: x = time (oldest left), y = frequency (low at the bottom).
      const pixels = history.image.data
      for (let c = 0; c < COLUMNS; c++) {
        const base = ((history.next + c) % COLUMNS) * POINTS
        for (let p = 0; p < POINTS; p++) {
          const t = Math.max(0, Math.min(1, (history.levels[base + p] - minDb) / (MAX_DB - minDb)))
          const k = Math.round(t * 255) * 3
          const o = ((POINTS - 1 - p) * COLUMNS + c) * 4
          pixels[o] = palette[k]
          pixels[o + 1] = palette[k + 1]
          pixels[o + 2] = palette[k + 2]
          pixels[o + 3] = 255
        }
      }
      history.bitmap.getContext('2d')?.putImageData(history.image, 0, 0)
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.imageSmoothingEnabled = true
      ctx.drawImage(history.bitmap, 0, 0, width, height)

      // The frequency grid, horizontal here.
      ctx.save()
      ctx.font = '9px monospace'
      for (const line of FREQUENCY_GRID) {
        if (!line.label) continue
        const y = Math.round((1 - hzToUnit(line.hz)) * height) + 0.5
        ctx.strokeStyle = tokens.color.textSecondary
        ctx.globalAlpha = 0.3
        ctx.beginPath()
        ctx.moveTo(0, y)
        ctx.lineTo(width, y)
        ctx.stroke()
        if (height > 60) {
          ctx.globalAlpha = 0.8
          ctx.fillStyle = tokens.color.textSecondary
          ctx.fillText(line.label, 2, Math.max(9, y - 2))
        }
      }
      ctx.restore()

      const cursor = cursorRef.current
      if (cursor) {
        const unit = 1 - cursor.y
        const hz = unitToHz(unit)
        const column = Math.min(COLUMNS - 1, Math.max(0, Math.floor(cursor.x * COLUMNS)))
        const point = Math.min(POINTS - 1, Math.max(0, Math.floor(unit * POINTS)))
        const level = history.levels[((history.next + column) % COLUMNS) * POINTS + point]
        const ago = ((COLUMNS - 1 - column) * COLUMN_MS) / 1000
        const label = `${formatHz(hz)} ${describePitch(hzToSemitones(hz)).note} ${level > -199 ? level.toFixed(0) : '–'} dB  −${ago.toFixed(1)} s`
        ctx.save()
        ctx.font = '10px monospace'
        const textWidth = ctx.measureText(label).width
        const x = Math.round(cursor.x * width) + 0.5
        const y = Math.round(cursor.y * height) + 0.5
        ctx.strokeStyle = tokens.color.textSecondary
        ctx.setLineDash([2, 3])
        ctx.beginPath()
        ctx.moveTo(0, y)
        ctx.lineTo(width, y)
        ctx.stroke()
        const tx = Math.min(width - textWidth - 4, Math.max(4, x + 4))
        const ty = y > 18 ? y - 4 : y + 13
        ctx.fillStyle = tokens.color.nodeFill
        ctx.globalAlpha = 0.85
        ctx.fillRect(tx - 2, ty - 10, textWidth + 4, 13)
        ctx.globalAlpha = 1
        ctx.fillStyle = tokens.color.textPrimary
        ctx.fillText(label, tx, ty)
        ctx.restore()
      }
    }

    registerPreviewRenderer(id, render)
    return () => unregisterPreviewRenderer(id)
  }, [visible, nodeId, preview.portId, id])

  return (
    <div
      className="node-preview-canvas-wrap"
      onMouseMove={(e) => {
        const rect = e.currentTarget.getBoundingClientRect()
        cursorRef.current = rect.width > 0 && rect.height > 0 ? { x: (e.clientX - rect.left) / rect.width, y: (e.clientY - rect.top) / rect.height } : null
      }}
      onMouseLeave={() => (cursorRef.current = null)}
    >
      <canvas className="node-preview-canvas" ref={canvasRef} />
    </div>
  )
}
