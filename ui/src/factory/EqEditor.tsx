// The EQ editor (wiki/plans/DataAndWavetable.md 1d): bands drawn over a live
// spectrum of what the EQ hears (grey) and what it puts out (colour), on the
// log-frequency axis every spectrum in Bazalt shares. Drag a band's dot to
// move it (frequency across, gain up and down), wheel over it for its width,
// double-click empty space to add a bell there. The cursor reads frequency,
// note and the curve's gain. The response is drawn here, never heard here —
// the engine plays the document (CLAUDE.md rule 1).
import { useEffect, useMemo, useRef, useState } from 'react'
import {
  BAND_TYPES,
  type EqBand,
  type EqBandType,
  type EqDoc,
  MAX_BANDS,
  MAX_GAIN_DB,
  MAX_FREQUENCY,
  MIN_FREQUENCY,
  bandMagnitude,
  cleanBand,
  formatHz,
  hasGain,
  hzToSemitones,
  hzToUnit,
  isCut,
  isShelf,
  normaliseBand,
  responseDb,
  snapHzToNote,
  unitToHz,
  FREQUENCY_GRID,
} from './eqModel'
import { drawSpectrumColumns, readSpectrum, useSpectrumTap } from '../analysis/logSpectrum'
import { describePitch } from '../nodes/pitchReadout'
import { tokens } from '../theme/tokens'
import './EqEditor.css'

/** Band colours, in order (a band keeps its colour while it exists). */
const BAND_COLORS = ['#FFB094', '#7cc6f7', '#40FF69', '#e0339e', '#FFFF4D', '#8c7fff', '#3ecfc0', '#e8798a']

const RANGES = [6, 12, 24] as const
const SPECTRUM_MIN_DB = -90
const SPECTRUM_MAX_DB = 6
const COLUMNS = 320

interface EqEditorProps {
  doc: EqDoc
  snap: boolean
  /** Where the live spectrum comes from: the signal into the EQ and out of it. */
  preTap?: { nodeId: string; portId: string }
  postTap?: { nodeId: string; portId: string }
  onLive: (doc: EqDoc) => void
  onCommit: (doc: EqDoc) => void
}

interface Drag {
  index: number
  startX: number
  startY: number
  startUnit: number
  startGain: number
  moved: boolean
}

export function EqEditor({ doc, snap, preTap, postTap, onLive, onCommit }: EqEditorProps) {
  const wrapRef = useRef<HTMLDivElement | null>(null)
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const [size, setSize] = useState({ width: 800, height: 400 })
  const [selected, setSelected] = useState<number | null>(doc.bands.length > 0 ? 0 : null)
  const [range, setRange] = useState<(typeof RANGES)[number]>(12)
  const [cursor, setCursor] = useState<{ x: number; y: number } | null>(null)
  const [working, setWorking] = useState<EqDoc | null>(null) // the doc mid-gesture
  const dragRef = useRef<Drag | null>(null)
  const wheelCommitRef = useRef<number | null>(null)
  const sampleRateRef = useRef(48000)

  const shown = working ?? doc
  const bands = shown.bands

  useSpectrumTap(preTap?.nodeId, preTap?.portId)
  useSpectrumTap(postTap?.nodeId, postTap?.portId)

  useEffect(() => {
    const el = wrapRef.current
    if (!el) return
    const observer = new ResizeObserver(([entry]) => setSize({ width: entry.contentRect.width, height: entry.contentRect.height }))
    observer.observe(el)
    return () => observer.disconnect()
  }, [])

  // Keep the selection valid when bands come and go (undo, presets).
  useEffect(() => {
    if (selected !== null && selected >= doc.bands.length) setSelected(doc.bands.length > 0 ? doc.bands.length - 1 : null)
  }, [doc.bands.length, selected])

  // The live spectra, drawn every frame on the canvas under the SVG.
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return
    const preSmooth = new Float32Array(COLUMNS).fill(Number.NaN)
    const postSmooth = new Float32Array(COLUMNS).fill(Number.NaN)
    let raf = 0
    const frame = () => {
      raf = requestAnimationFrame(frame)
      const dpr = window.devicePixelRatio || 1
      const { width, height } = size
      if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
        canvas.width = Math.round(width * dpr)
        canvas.height = Math.round(height * dpr)
      }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.clearRect(0, 0, width, height)
      const style = { minDb: SPECTRUM_MIN_DB, maxDb: SPECTRUM_MAX_DB }
      if (preTap) {
        const pre = readSpectrum(preTap.nodeId, preTap.portId, COLUMNS, preSmooth)
        if (pre) drawSpectrumColumns(ctx, width, height, pre.columns, { ...style, stroke: 'rgba(232,232,234,0.25)', fill: 'rgba(232,232,234,0.05)' })
      }
      if (postTap) {
        const post = readSpectrum(postTap.nodeId, postTap.portId, COLUMNS, postSmooth)
        if (post) {
          sampleRateRef.current = post.sampleRate
          drawSpectrumColumns(ctx, width, height, post.columns, { ...style, stroke: 'rgba(224,51,158,0.6)', fill: 'rgba(224,51,158,0.10)' })
        }
      }
    }
    raf = requestAnimationFrame(frame)
    return () => cancelAnimationFrame(raf)
  }, [size, preTap, postTap])

  const { width, height } = size
  const toX = (hz: number) => hzToUnit(hz) * width
  const toY = (db: number) => height / 2 - (db / range) * (height / 2 - 14)
  const fromY = (y: number) => ((height / 2 - y) / (height / 2 - 14)) * range

  const sampleRate = sampleRateRef.current
  const responsePath = useMemo(() => {
    const steps = Math.max(64, Math.round(width / 2))
    let d = ''
    for (let i = 0; i <= steps; i++) {
      const u = i / steps
      const db = responseDb(shown, unitToHz(u), sampleRate)
      const y = Math.max(-2, Math.min(height + 2, toY(db)))
      d += `${i === 0 ? 'M' : 'L'}${(u * width).toFixed(1)},${y.toFixed(1)}`
    }
    return d
    // toY depends on range/height, already listed.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shown, width, height, range, sampleRate])

  const selectedPath = useMemo(() => {
    if (selected === null || !bands[selected]) return null
    const steps = Math.max(64, Math.round(width / 3))
    let d = `M0,${toY(0)}`
    for (let i = 0; i <= steps; i++) {
      const u = i / steps
      const db = 20 * Math.log10(Math.max(1e-9, bandMagnitude(bands[selected], unitToHz(u), sampleRate)))
      d += `L${(u * width).toFixed(1)},${Math.max(-2, Math.min(height + 2, toY(db))).toFixed(1)}`
    }
    return `${d}L${width},${toY(0)}Z`
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [bands, selected, width, height, range, sampleRate])

  const handlePosition = (band: EqBand) => {
    const b = normaliseBand(band)
    const y = hasGain(b.type) ? toY(b.gain) : toY(responseDb({ schema: 'eq', version: 1, bands: [b] }, b.frequency, sampleRate))
    return { x: toX(b.frequency), y: Math.max(8, Math.min(height - 8, y)) }
  }

  const withBand = (index: number, change: Partial<EqBand>): EqDoc => ({
    ...shown,
    bands: shown.bands.map((b, i) => (i === index ? cleanBand({ ...b, ...change }) : b)),
  })

  const live = (next: EqDoc) => {
    setWorking(next)
    onLive(next)
  }
  const commit = (next: EqDoc) => {
    setWorking(null)
    onCommit(next)
  }

  const localPoint = (e: { clientX: number; clientY: number }) => {
    const rect = wrapRef.current!.getBoundingClientRect()
    return { x: e.clientX - rect.left, y: e.clientY - rect.top }
  }

  const frequencyAt = (x: number, fine: boolean, startUnit?: number, startX?: number) => {
    const u = startUnit !== undefined && startX !== undefined ? startUnit + ((x - startX) / width) * (fine ? 0.2 : 1) : x / width
    const hz = Math.min(MAX_FREQUENCY, Math.max(MIN_FREQUENCY, unitToHz(u)))
    return snap ? snapHzToNote(hz) : hz
  }
  const gainAt = (y: number) => {
    const db = Math.max(-MAX_GAIN_DB, Math.min(MAX_GAIN_DB, fromY(y)))
    return snap ? Math.round(db * 2) / 2 : db
  }

  const onHandleDown = (e: React.PointerEvent, index: number) => {
    e.stopPropagation()
    if (e.button !== 0) return
    ;(e.currentTarget as Element).setPointerCapture(e.pointerId)
    setSelected(index)
    const b = normaliseBand(bands[index])
    dragRef.current = { index, startX: e.clientX, startY: e.clientY, startUnit: hzToUnit(b.frequency), startGain: b.gain, moved: false }
  }

  const onPointerMove = (e: React.PointerEvent) => {
    const p = localPoint(e)
    setCursor(p)
    const drag = dragRef.current
    if (!drag) return
    drag.moved = true
    const b = normaliseBand(bands[drag.index])
    const fine = e.shiftKey
    const frequency = frequencyAt(p.x, fine, drag.startUnit, localPoint({ clientX: drag.startX, clientY: drag.startY }).x)
    const change: Partial<EqBand> = { frequency }
    if (hasGain(b.type)) {
      const dy = e.clientY - drag.startY
      change.gain = fine ? Math.max(-MAX_GAIN_DB, Math.min(MAX_GAIN_DB, drag.startGain - (dy / (height / 2)) * range * 0.2)) : gainAt(p.y)
    }
    live(withBand(drag.index, change))
  }

  const onPointerUp = () => {
    const drag = dragRef.current
    dragRef.current = null
    if (drag?.moved && working) commit(working)
  }

  const onWheel = (e: React.WheelEvent) => {
    const index = selected
    if (index === null || !bands[index]) return
    e.preventDefault()
    const b = normaliseBand(bands[index])
    const factor = Math.pow(1.1, -Math.sign(e.deltaY))
    const change: Partial<EqBand> = isShelf(b.type)
      ? { slope: Math.max(0, Math.min(1, b.slope + 0.05 * Math.sign(-e.deltaY))) }
      : { q: Math.max(0.1, Math.min(30, b.q * factor)) }
    const next = withBand(index, change)
    live(next)
    if (wheelCommitRef.current !== null) window.clearTimeout(wheelCommitRef.current)
    wheelCommitRef.current = window.setTimeout(() => {
      wheelCommitRef.current = null
      commit(next)
    }, 350)
  }

  const addBandAt = (p: { x: number; y: number }) => {
    if (bands.length >= MAX_BANDS) return
    const band = cleanBand({ type: 'bell', frequency: frequencyAt(p.x, false), gain: gainAt(p.y), q: 1 })
    commit({ ...shown, bands: [...bands, band] })
    setSelected(bands.length)
  }

  const removeBand = (index: number) => {
    commit({ ...shown, bands: bands.filter((_, i) => i !== index) })
    setSelected(bands.length > 1 ? Math.max(0, index - 1) : null)
  }

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLElement && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT')) return
      if ((e.key === 'Delete' || e.key === 'Backspace') && selected !== null && doc.bands[selected]) {
        e.preventDefault()
        onCommit({ ...doc, bands: doc.bands.filter((_, i) => i !== selected) })
        setSelected(doc.bands.length > 1 ? Math.max(0, selected - 1) : null)
      }
    }
    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [doc, selected, onCommit])

  const cursorReadout = cursor && !dragRef.current
    ? (() => {
        const hz = unitToHz(cursor.x / width)
        const pitch = describePitch(hzToSemitones(hz))
        const db = responseDb(shown, hz, sampleRate)
        return { hz, pitch, db }
      })()
    : null

  const sel = selected !== null && bands[selected] ? normaliseBand(bands[selected]) : null

  return (
    <div className="eq-editor">
      <div className="eq-toolbar">
        <span className="eq-toolbar-label">Range</span>
        {RANGES.map((r) => (
          <button key={r} className={r === range ? 'factory-button active' : 'factory-button'} onClick={() => setRange(r)}>
            ±{r} dB
          </button>
        ))}
        <div className="factory-spacer" />
        {cursorReadout && (
          <span className="eq-readout">
            {formatHz(cursorReadout.hz)} · {cursorReadout.pitch.note} {cursorReadout.pitch.centsText}¢ · {cursorReadout.db >= 0 ? '+' : ''}
            {cursorReadout.db.toFixed(1)} dB
          </span>
        )}
      </div>
      <div
        ref={wrapRef}
        className="eq-plot"
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerLeave={() => setCursor(null)}
        onWheel={onWheel}
        onDoubleClick={(e) => addBandAt(localPoint(e))}
        onPointerDown={(e) => {
          if (e.button === 0 && e.target === e.currentTarget.querySelector('svg')) setSelected(null)
        }}
      >
        <canvas ref={canvasRef} className="eq-spectrum" style={{ width, height }} />
        <svg className="eq-svg" width={width} height={height}>
          {FREQUENCY_GRID.map((line) => (
            <g key={line.hz}>
              <line x1={toX(line.hz)} x2={toX(line.hz)} y1={0} y2={height} className={line.label ? 'eq-grid eq-grid-major' : 'eq-grid'} />
              {line.label && (
                <text x={toX(line.hz) + 3} y={height - 4} className="eq-grid-label">
                  {line.label}
                </text>
              )}
            </g>
          ))}
          {Array.from({ length: (range / (range > 12 ? 6 : 3)) * 2 + 1 }, (_, i) => -range + i * (range > 12 ? 6 : 3)).map((db) => (
            <g key={db}>
              <line x1={0} x2={width} y1={toY(db)} y2={toY(db)} className={db === 0 ? 'eq-grid eq-grid-zero' : 'eq-grid'} />
              <text x={4} y={toY(db) - 3} className="eq-grid-label">
                {db > 0 ? '+' : ''}
                {db}
              </text>
            </g>
          ))}
          {selectedPath && selected !== null && (
            <path d={selectedPath} className="eq-band-area" style={{ fill: BAND_COLORS[selected % BAND_COLORS.length] }} />
          )}
          <path d={responsePath} className="eq-response" stroke={tokens.color.textPrimary} />
          {bands.map((band, i) => {
            const b = normaliseBand(band)
            const { x, y } = handlePosition(b)
            const color = BAND_COLORS[i % BAND_COLORS.length]
            return (
              <g key={i} className={b.enabled ? 'eq-handle' : 'eq-handle eq-handle-off'} onPointerDown={(e) => onHandleDown(e, i)} onDoubleClick={(e) => e.stopPropagation()}>
                <circle cx={x} cy={y} r={i === selected ? 10 : 8} fill={i === selected ? color : tokens.color.nodeFill} stroke={color} strokeWidth={2} />
                <text x={x} y={y + 3.5} textAnchor="middle" className="eq-handle-label" fill={i === selected ? tokens.color.nodeFill : color}>
                  {i + 1}
                </text>
              </g>
            )
          })}
          {cursor && <line x1={cursor.x} x2={cursor.x} y1={0} y2={height} className="eq-cursor" />}
        </svg>
        {bands.length === 0 && <div className="eq-empty">Double-click to add a band</div>}
      </div>
      <div className="eq-inspector">
        <div className="eq-chips">
          {bands.map((band, i) => (
            <button
              key={i}
              className={i === selected ? 'eq-chip eq-chip-selected' : 'eq-chip'}
              style={{ borderColor: BAND_COLORS[i % BAND_COLORS.length] }}
              onClick={() => setSelected(i)}
            >
              <span style={{ color: BAND_COLORS[i % BAND_COLORS.length] }}>{i + 1}</span> {BAND_TYPES.find((t) => t.type === band.type)?.label}
            </button>
          ))}
          <button
            className="factory-button"
            disabled={bands.length >= MAX_BANDS}
            onClick={() => addBandAt({ x: toX(1000), y: toY(0) })}
            title="Add a bell at 1 kHz (or double-click the plot)"
          >
            + Band
          </button>
        </div>
        {sel && selected !== null && (
          <div className="eq-fields">
            <label>
              Type
              <select value={sel.type} onChange={(e) => commit(withBand(selected, { type: e.target.value as EqBandType }))}>
                {BAND_TYPES.map((t) => (
                  <option key={t.type} value={t.type}>
                    {t.label}
                  </option>
                ))}
              </select>
            </label>
            <NumberField
              label="Freq"
              value={sel.frequency}
              format={(v) => formatHz(v)}
              parse={(s) => {
                const n = parseFloat(s)
                return /k/i.test(s) ? n * 1000 : n
              }}
              onCommit={(v) => commit(withBand(selected, { frequency: v }))}
            />
            <span className="eq-note">{describePitch(hzToSemitones(sel.frequency)).note}</span>
            {hasGain(sel.type) && (
              <NumberField label="Gain" value={sel.gain} format={(v) => `${v >= 0 ? '+' : ''}${v.toFixed(1)} dB`} onCommit={(v) => commit(withBand(selected, { gain: v }))} />
            )}
            {isShelf(sel.type) ? (
              <NumberField label="Slope" value={sel.slope} format={(v) => v.toFixed(2)} onCommit={(v) => commit(withBand(selected, { slope: v }))} />
            ) : (
              <NumberField
                label={isCut(sel.type) ? 'Resonance' : 'Q'}
                value={sel.q}
                format={(v) => v.toFixed(2)}
                onCommit={(v) => commit(withBand(selected, { q: v }))}
              />
            )}
            {isCut(sel.type) && (
              <label>
                Slope
                <select value={sel.order} onChange={(e) => commit(withBand(selected, { order: Number(e.target.value) }))}>
                  {[1, 2, 3, 4].map((o) => (
                    <option key={o} value={o}>
                      {o * 12} dB/oct
                    </option>
                  ))}
                </select>
              </label>
            )}
            <label className="eq-check">
              <input type="checkbox" checked={sel.enabled} onChange={(e) => commit(withBand(selected, { enabled: e.target.checked }))} />
              On
            </label>
            <button className="factory-button" onClick={() => removeBand(selected)} title="Remove this band (Delete)">
              Remove
            </button>
          </div>
        )}
      </div>
    </div>
  )
}

function NumberField({
  label,
  value,
  format,
  parse = parseFloat,
  onCommit,
}: {
  label: string
  value: number
  format: (v: number) => string
  parse?: (s: string) => number
  onCommit: (v: number) => void
}) {
  const [text, setText] = useState<string | null>(null)
  const cancelled = useRef(false)
  return (
    <label>
      {label}
      <input
        className="eq-number"
        value={text ?? format(value)}
        onFocus={(e) => {
          setText(String(Number(value.toFixed(3))))
          e.currentTarget.select()
        }}
        onChange={(e) => setText(e.target.value)}
        onBlur={() => {
          if (text !== null && !cancelled.current) {
            const v = parse(text)
            if (Number.isFinite(v)) onCommit(v)
          }
          cancelled.current = false
          setText(null)
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter') (e.currentTarget as HTMLInputElement).blur()
          if (e.key === 'Escape') {
            cancelled.current = true
            ;(e.currentTarget as HTMLInputElement).blur()
          }
        }}
      />
    </label>
  )
}
