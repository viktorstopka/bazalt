// view.spectrogram (wiki/plans/Sampling.md 3a.5) in Scope's visual language
// (ScopeHistoryBody.tsx): the same near-black panel with no title, the ports
// on its sides, the time span, range and Freeze just below it. Frequencies are
// marked faintly inside (100 / 1k / 10k), clear of the ports. Inside, a scrolling waterfall built from
// the Spectrum's own analysis frames: time runs to the left, frequency up on
// the shared log axis (logSpectrum.ts), level as colour.
//
// Zoom: the waterfall is a <canvas> whose backing store is the history itself
// (COLUMNS x POINTS pixels), never resized to the screen, shown with CSS at
// the panel's size — so the canvas zoom scales it like any other DOM content
// and it can never outgrow or be cropped by its box (what the old preview's
// zoom-sized canvas did). The grid, cursor and read-out are SVG with a
// viewBox, crisp at every zoom, the same fix ScopeHistoryBody.tsx made.
// Drawing only (CLAUDE.md rule 1).
import { useEffect, useRef, useState } from 'react'
import type { NodeDescriptor } from '../graph/descriptorTypes'
import { type NodeCardState, PortGlyph } from './NodeCard'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview } from '../graph/previewSubscriptions'
import { getInterpolatedTap } from '../telemetry/telemetryClient'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { formatHz, hzToSemitones, hzToUnit, spectrumColumnsDb, unitToHz } from '../factory/eqModel'
import { describePitch } from './pitchReadout'
import { EditableStat } from './EditableStat'
import { tokens } from '../theme/tokens'
import './ScopeHistoryBody.css'
import './SpectrogramBody.css'

// Must match .scope-history-card's size (ScopeHistoryBody.css), like Scope's own panel.
const PANEL_WIDTH = 190
const PANEL_HEIGHT = 130
/** The history: one column every COLUMN_MS whatever the display rate. */
const COLUMNS = 300
const POINTS = 256
const COLUMN_MS = 20
const WINDOW_SECONDS = (COLUMNS * COLUMN_MS) / 1000
const MAX_DB = 0
const DEFAULT_RANGE_DB = -90
const GRID: { hz: number; label: string }[] = [
  { hz: 100, label: '100' },
  { hz: 1000, label: '1k' },
  { hz: 10000, label: '10k' },
]

function hexToRgb(hex: string): [number, number, number] {
  const v = parseInt(hex.replace('#', ''), 16)
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255]
}

/** tokens.color.spectrogramMap spread evenly over 256 levels. */
function buildPalette(): Uint8ClampedArray {
  const stops = tokens.color.spectrogramMap.map(hexToRgb)
  const palette = new Uint8ClampedArray(256 * 3)
  for (let i = 0; i < 256; i++) {
    const at = (i / 255) * (stops.length - 1)
    const k = Math.min(stops.length - 2, Math.floor(at))
    const u = at - k
    for (let c = 0; c < 3; c++) palette[i * 3 + c] = stops[k][c] + (stops[k + 1][c] - stops[k][c]) * u
  }
  return palette
}

interface History {
  levels: Float32Array // COLUMNS x POINTS, dB; column `next` is the oldest
  next: number
  lastPush: number
  dirty: boolean
}

export function SpectrogramBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const rootRef = useRef<HTMLDivElement | null>(null)
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const cursorLineRef = useRef<SVGLineElement | null>(null)
  const readoutRef = useRef<SVGTextElement | null>(null)
  const readoutBackRef = useRef<SVGRectElement | null>(null)
  const cursorRef = useRef<{ x: number; y: number } | null>(null)
  const historyRef = useRef<History>({ levels: new Float32Array(COLUMNS * POINTS).fill(-200), next: 0, lastPush: 0, dirty: true })
  const [visible, setVisible] = useState(false)
  const id = `${descriptor.typeId}:${instanceId ?? 'gallery'}`

  const inputPort = descriptor.inputs[0]
  const outputPort = descriptor.outputs[0]
  const tapPortId = descriptor.previews?.[0]?.portId ?? outputPort?.id ?? 'out'
  const rangeDb = state.parameterValues?.['view.spectrogram.range'] ?? DEFAULT_RANGE_DB
  const frozen = (state.parameterValues?.['view.spectrogram.freeze'] ?? 0) > 0.5

  const settingsRef = useRef({ rangeDb, frozen })
  useEffect(() => {
    settingsRef.current = { rangeDb, frozen }
    historyRef.current.dirty = true // a new range recolours the whole history
  })

  useEffect(() => {
    const root = rootRef.current
    if (!root || !instanceId) return
    const observer = new IntersectionObserver(([entry]) => setVisible(entry.isIntersecting), { rootMargin: '200px' })
    observer.observe(root)
    return () => observer.disconnect()
  }, [instanceId])

  useEffect(() => {
    if (!visible || !instanceId) return
    void subscribeNodePreview(instanceId, tapPortId, 'spectrogram')
    return () => unsubscribeNodePreview(instanceId, tapPortId, 'spectrogram')
  }, [visible, instanceId, tapPortId])

  useEffect(() => {
    const canvas = canvasRef.current
    if (!visible || !instanceId || !canvas) return
    canvas.width = COLUMNS
    canvas.height = POINTS
    const ctx = canvas.getContext('2d')
    if (!ctx) return
    const image = ctx.createImageData(COLUMNS, POINTS)
    const palette = buildPalette()
    const tap = tapNameForPreview(instanceId, tapPortId)
    const history = historyRef.current

    const render = () => {
      const { rangeDb: minDb, frozen: isFrozen } = settingsRef.current
      const now = performance.now()
      const data = getInterpolatedTap(tap, TelemetryFrameType.Spectrum)
      if (!isFrozen && data && data.payload.length > 0) {
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
          history.dirty = true
        }
      } else history.lastPush = now

      if (history.dirty) {
        history.dirty = false
        const pixels = image.data
        const span = MAX_DB - minDb
        for (let c = 0; c < COLUMNS; c++) {
          const base = ((history.next + c) % COLUMNS) * POINTS
          for (let p = 0; p < POINTS; p++) {
            const t = Math.max(0, Math.min(1, (history.levels[base + p] - minDb) / span))
            const k = Math.round(t * 255) * 3
            const o = ((POINTS - 1 - p) * COLUMNS + c) * 4
            pixels[o] = palette[k]
            pixels[o + 1] = palette[k + 1]
            pixels[o + 2] = palette[k + 2]
            pixels[o + 3] = 255
          }
        }
        ctx.putImageData(image, 0, 0)
      }

      // The cursor: a line at the frequency, and frequency / note / level / how long ago.
      const cursor = cursorRef.current
      const line = cursorLineRef.current
      const text = readoutRef.current
      const back = readoutBackRef.current
      if (!line || !text || !back) return
      if (!cursor) {
        line.style.display = text.style.display = back.style.display = 'none'
        return
      }
      const unit = 1 - cursor.y
      const hz = unitToHz(unit)
      const column = Math.min(COLUMNS - 1, Math.max(0, Math.floor(cursor.x * COLUMNS)))
      const point = Math.min(POINTS - 1, Math.max(0, Math.floor(unit * POINTS)))
      const level = history.levels[((history.next + column) % COLUMNS) * POINTS + point]
      const ago = ((COLUMNS - 1 - column) * COLUMN_MS) / 1000
      const y = cursor.y * PANEL_HEIGHT
      line.setAttribute('y1', String(y))
      line.setAttribute('y2', String(y))
      text.textContent = `${formatHz(hz)} ${describePitch(hzToSemitones(hz)).note} ${level > -199 ? `${level.toFixed(0)} dB` : ''} −${ago.toFixed(1)}s`
      const textY = y > 16 ? y - 4 : y + 11
      text.setAttribute('y', String(textY))
      // Beside the pointer, on whichever side has room.
      const width = text.getComputedTextLength()
      const pointerX = cursor.x * PANEL_WIDTH
      const textX = pointerX < PANEL_WIDTH / 2 ? Math.min(PANEL_WIDTH - width - 3, pointerX + 6) : Math.max(3, pointerX - width - 6)
      text.setAttribute('x', String(textX))
      back.setAttribute('x', String(textX - 2))
      back.setAttribute('y', String(textY - 8))
      back.setAttribute('width', String(width + 4))
      line.style.display = text.style.display = back.style.display = ''
    }

    registerPreviewRenderer(id, render)
    return () => unregisterPreviewRenderer(id)
  }, [visible, instanceId, tapPortId, id])

  const classNames = [
    'node-card',
    'scope-history-card',
    'spectrogram-card',
    state.selected && 'node-card-selected',
    state.bypassed && 'node-card-bypassed',
    state.listening && 'node-card-listening',
    state.error && 'node-card-error',
  ]
    .filter(Boolean)
    .join(' ')

  return (
    <div
      className={classNames}
      ref={rootRef}
      onMouseMove={(e) => {
        const rect = e.currentTarget.getBoundingClientRect()
        cursorRef.current = rect.width > 0 && rect.height > 0 ? { x: (e.clientX - rect.left) / rect.width, y: (e.clientY - rect.top) / rect.height } : null
      }}
      onMouseLeave={() => (cursorRef.current = null)}
    >
      <canvas className="spectrogram-bitmap" ref={canvasRef} />
      <svg className="scope-history-visual" viewBox={`0 0 ${PANEL_WIDTH} ${PANEL_HEIGHT}`}>
        {GRID.map((g) => {
          const y = (1 - hzToUnit(g.hz)) * PANEL_HEIGHT
          return (
            <g key={g.hz}>
              <line x1={0} x2={PANEL_WIDTH} y1={y} y2={y} stroke={tokens.color.textPrimary} strokeOpacity={0.18} strokeWidth={0.5} />
              <text className="spectrogram-grid-label" x={PANEL_WIDTH - 3} y={y - 2} textAnchor="end" fill={tokens.color.textPrimary} fillOpacity={0.55}>
                {g.label}
              </text>
            </g>
          )
        })}
        <line ref={cursorLineRef} x1={0} x2={PANEL_WIDTH} stroke={tokens.color.textPrimary} strokeOpacity={0.6} strokeWidth={0.5} strokeDasharray="2 3" style={{ display: 'none' }} />
        <rect ref={readoutBackRef} height={11} rx={2} fill={tokens.color.nodeFill} fillOpacity={0.8} style={{ display: 'none' }} />
        <text ref={readoutRef} className="spectrogram-readout" fill={tokens.color.textPrimary} style={{ display: 'none' }} />
      </svg>
      {inputPort && (
        <div className="scope-history-port scope-history-port-left">
          <PortGlyph port={inputPort} side="left" instanceId={instanceId} connected={state.connectedPortIds?.has(inputPort.id) ?? false} />
        </div>
      )}
      {outputPort && (
        <div className="scope-history-port scope-history-port-right">
          <PortGlyph port={outputPort} side="right" instanceId={instanceId} connected={state.connectedPortIds?.has(outputPort.id) ?? false} />
        </div>
      )}
      <div className="scope-history-window spectrogram-footer">
        <span>{WINDOW_SECONDS.toFixed(0)} s</span>
        <EditableStat label="" value={rangeDb} decimals={0} unit=" dB" onCommit={(v) => state.onParameterCommit?.('view.spectrogram.range', Math.min(-30, Math.max(-140, v)))} />
        <button
          className={frozen ? 'spectrogram-freeze active' : 'spectrogram-freeze'}
          title={frozen ? 'Scrolling stopped — click to go on' : 'Stop scrolling to read a moment'}
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => {
            e.stopPropagation()
            state.onParameterCommit?.('view.spectrogram.freeze', frozen ? 0 : 1)
          }}
        >
          {frozen ? 'Frozen' : 'Freeze'}
        </button>
      </div>
    </div>
  )
}
