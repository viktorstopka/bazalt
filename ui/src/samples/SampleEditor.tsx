// wiki/plans/Sampling.md 3a.2: the Sample factory — a Sample node's region,
// edited non-destructively over the whole file's waveform: start / end,
// loop, slices, reverse, normalise, gain, fades and the root note.
// Dragging streams the content live (setNodeContentLive); releasing commits
// one undo step, like every other factory.
import { useEffect, useLayoutEffect, useRef, useState } from 'react'
import { redo, setNodeContent, setNodeContentLive, undo } from '../graph/graphStore'
import { closeFactory } from '../factory/factoryStore'
import { tokens } from '../theme/tokens'
import { analyseSample, sampleInfo, sampleOverview, type SampleInfo } from './samples'
import '../factory/FactoryWindow.css'
import './SampleEditor.css'

export interface SampleDoc {
  sample?: string
  name?: string
  start?: number
  end?: number
  loop?: boolean
  loopStart?: number
  loopEnd?: number
  gain?: number
  reverse?: boolean
  normalise?: boolean
  fadeIn?: number
  fadeOut?: number
  slices?: number[]
  root?: number
  [key: string]: unknown
}

const HEIGHT = 260
const BUCKETS = 1600
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

function noteName(midi: number): string {
  const n = Math.round(midi)
  const cents = Math.round((midi - n) * 100)
  return `${NOTE_NAMES[((n % 12) + 12) % 12]}${Math.floor(n / 12) - 1}${cents !== 0 ? ` ${cents > 0 ? '+' : ''}${cents}¢` : ''}`
}

type Handle = 'start' | 'end' | 'loopStart' | 'loopEnd' | { slice: number }

export function SampleEditor({ nodeId, content, title }: { nodeId: string; content: unknown; title: string }) {
  const doc = (content && typeof content === 'object' ? content : {}) as SampleDoc
  const id = typeof doc.sample === 'string' ? doc.sample : ''
  const [info, setInfo] = useState<SampleInfo | null>(null)
  const [peaks, setPeaks] = useState<Float32Array | null>(null)
  const [draft, setDraft] = useState<SampleDoc | null>(null) // while dragging
  const [sensitivity, setSensitivity] = useState(6)
  const [busy, setBusy] = useState(false)
  const [width, setWidth] = useState(800)
  const wrapRef = useRef<HTMLDivElement | null>(null)
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const dragRef = useRef<Handle | null>(null)

  const current = draft ?? doc
  const seconds = info?.found ? (info.seconds ?? 0) : 0
  const start = Math.max(0, current.start ?? 0)
  const end = (current.end ?? 0) > 0 ? current.end! : seconds
  const loopStart = start + Math.max(0, current.loopStart ?? 0)
  const loopEnd = (current.loopEnd ?? 0) > 0 ? start + current.loopEnd! : end
  const slices = (current.slices ?? []).map((s) => start + s)

  useEffect(() => {
    if (!id) return
    let alive = true
    void sampleInfo(id).then((next) => alive && setInfo(next))
    void sampleOverview(id, BUCKETS).then((next) => alive && setPeaks(next))
    return () => {
      alive = false
    }
  }, [id])

  useLayoutEffect(() => {
    const wrap = wrapRef.current
    if (!wrap) return
    const observer = new ResizeObserver(() => setWidth(Math.max(200, wrap.clientWidth)))
    observer.observe(wrap)
    return () => observer.disconnect()
  }, [])

  // Escape closes; Ctrl+Z / Ctrl+Shift+Z undo and redo, as in every factory.
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLElement && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) return
      if (e.key === 'Escape') closeFactory()
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') {
        e.preventDefault()
        if (e.shiftKey) redo()
        else undo()
      }
    }
    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [])

  const toX = (t: number) => (seconds > 0 ? (t / seconds) * width : 0)
  const toT = (x: number) => Math.max(0, Math.min(seconds, (x / width) * seconds))

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || !peaks) return
    const ratio = window.devicePixelRatio || 1
    canvas.width = width * ratio
    canvas.height = HEIGHT * ratio
    const g = canvas.getContext('2d')
    if (!g) return
    g.setTransform(ratio, 0, 0, ratio, 0, 0)
    g.clearRect(0, 0, width, HEIGHT)
    const mid = HEIGHT / 2
    const buckets = peaks.length / 2
    for (let x = 0; x < width; ++x) {
      const b = Math.min(buckets - 1, Math.floor((x / width) * buckets))
      const t = ((x + 0.5) / width) * seconds
      const inside = t >= start && t <= end
      g.fillStyle = tokens.color.portAudio
      g.globalAlpha = inside ? 0.9 : 0.22
      const top = mid - Math.min(1, peaks[b * 2 + 1]) * (mid - 4)
      const bottom = mid - Math.max(-1, peaks[b * 2]) * (mid - 4)
      g.fillRect(x, top, 1, Math.max(1, bottom - top))
    }
    g.globalAlpha = 1
    if (current.loop) {
      g.fillStyle = tokens.color.portData
      g.globalAlpha = 0.12
      g.fillRect(toX(loopStart), 0, toX(loopEnd) - toX(loopStart), HEIGHT)
      g.globalAlpha = 1
    }
  }, [peaks, width, seconds, start, end, loopStart, loopEnd, current.loop])

  const commit = (next: SampleDoc) => setNodeContent(nodeId, next)

  // Region-relative fields (loop, slices) are stored from the region's start.
  const withHandle = (base: SampleDoc, handle: Handle, t: number): SampleDoc => {
    const baseStart = Math.max(0, base.start ?? 0)
    const baseEnd = (base.end ?? 0) > 0 ? base.end! : seconds
    if (handle === 'start') {
      const newStart = Math.min(t, baseEnd - 0.001)
      const shift = newStart - baseStart
      return {
        ...base,
        start: newStart,
        loopStart: Math.max(0, (base.loopStart ?? 0) - shift),
        loopEnd: (base.loopEnd ?? 0) > 0 ? Math.max(0.001, base.loopEnd! - shift) : 0,
        slices: (base.slices ?? []).map((s) => s - shift).filter((s) => s > 0),
      }
    }
    if (handle === 'end') return { ...base, end: Math.max(t, baseStart + 0.001) }
    if (handle === 'loopStart') return { ...base, loopStart: Math.max(0, Math.min(t, baseEnd) - baseStart) }
    if (handle === 'loopEnd') return { ...base, loopEnd: Math.max(0.001, Math.min(t, baseEnd) - baseStart) }
    const sl = [...(base.slices ?? [])]
    sl[handle.slice] = Math.max(0.001, t - baseStart)
    return { ...base, slices: sl.sort((a, b) => a - b) }
  }

  const handleAt = (x: number): Handle | null => {
    const near = (t: number) => Math.abs(toX(t) - x) < 6
    if (near(start)) return 'start'
    if (near(end)) return 'end'
    if (current.loop && near(loopStart)) return 'loopStart'
    if (current.loop && near(loopEnd)) return 'loopEnd'
    const slice = slices.findIndex((s) => near(s))
    return slice >= 0 ? { slice } : null
  }

  const onMouseDown = (e: React.MouseEvent) => {
    const rect = e.currentTarget.getBoundingClientRect()
    const x = e.clientX - rect.left
    if (e.button === 2) {
      const h = handleAt(x)
      if (h && typeof h === 'object') commit({ ...doc, slices: (doc.slices ?? []).filter((_, i) => i !== h.slice) })
      return
    }
    const handle = handleAt(x)
    if (!handle) return
    dragRef.current = handle
    const move = (ev: MouseEvent) => {
      const next = withHandle(doc, handle, toT(ev.clientX - rect.left))
      setDraft(next)
      setNodeContentLive(nodeId, next)
    }
    const up = (ev: MouseEvent) => {
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
      dragRef.current = null
      setDraft(null)
      commit(withHandle(doc, handle, toT(ev.clientX - rect.left)))
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }

  const onDoubleClick = (e: React.MouseEvent) => {
    const rect = e.currentTarget.getBoundingClientRect()
    const t = toT(e.clientX - rect.left)
    if (t <= start || t >= end) return
    commit({ ...doc, slices: [...(doc.slices ?? []), t - start].sort((a, b) => a - b) })
  }

  const detectRoot = async () => {
    setBusy(true)
    const result = await analyseSample(id, sensitivity)
    setBusy(false)
    if (typeof result.root === 'number') commit({ ...doc, root: result.root })
  }

  const sliceAtTransients = async () => {
    setBusy(true)
    const result = await analyseSample(id, sensitivity)
    setBusy(false)
    commit({ ...doc, slices: (result.slices ?? []).filter((t) => t > start && t < end).map((t) => t - start) })
  }

  const marker = (t: number, label: string, color: string, key: string) => (
    <div key={key} className="sample-editor-marker" style={{ left: toX(t), borderColor: color }}>
      <span style={{ background: color }}>{label}</span>
    </div>
  )

  const number = (label: string, value: number, unit: string, onChange: (v: number) => void, step = 1) => (
    <label className="sample-editor-field">
      {label}
      <input
        type="number"
        step={step}
        value={Number.isFinite(value) ? Math.round(value * 1000) / 1000 : 0}
        onChange={(e) => {
          const v = Number(e.target.value)
          if (Number.isFinite(v)) onChange(v)
        }}
      />
      {unit}
    </label>
  )

  const toggle = (label: string, key: 'loop' | 'reverse' | 'normalise') => (
    <button className={doc[key] ? 'factory-button active' : 'factory-button'} onClick={() => commit({ ...doc, [key]: !doc[key] })}>
      {label}
    </button>
  )

  return (
    <div className="factory-window" onContextMenu={(e) => e.preventDefault()}>
      <div className="factory-header">
        <button className="factory-button" onClick={closeFactory} title="Back to the patch (Esc)">
          ← Back
        </button>
        <span className="factory-title">
          EDIT SAMPLE <span className="factory-subtitle">{title}</span>
        </span>
        <span className="factory-timebase">{doc.name || 'Sample'}</span>
        {info?.found && (
          <span className="factory-timebase">
            {info.channels === 1 ? 'mono' : `${info.channels} ch`} · {Math.round((info.sampleRate ?? 0) / 100) / 10} kHz · {seconds.toFixed(2)} s
          </span>
        )}
        <div className="factory-spacer" />
        <button className="factory-button" onClick={() => undo()} title="Undo (Ctrl+Z)">
          Undo
        </button>
        <button className="factory-button" onClick={() => redo()} title="Redo (Ctrl+Shift+Z)">
          Redo
        </button>
      </div>

      <div className="sample-editor-body">
        <div ref={wrapRef} className="sample-editor-wave" onMouseDown={onMouseDown} onDoubleClick={onDoubleClick}>
          <canvas ref={canvasRef} style={{ width, height: HEIGHT }} />
          {info?.found && (
            <>
              {marker(start, 'S', tokens.color.textPrimary, 'start')}
              {marker(end, 'E', tokens.color.textPrimary, 'end')}
              {current.loop && marker(loopStart, 'L', tokens.color.portData, 'ls')}
              {current.loop && marker(loopEnd, 'L', tokens.color.portData, 'le')}
              {slices.map((s, i) => marker(s, String(i + 1), tokens.color.portTrigger, `slice${i}`))}
            </>
          )}
          {!info?.found && <div className="sample-editor-missing">{id ? 'This sample is missing from your library.' : 'No sample loaded.'}</div>}
        </div>

        <div className="sample-editor-controls">
          {toggle('Loop', 'loop')}
          {toggle('Reverse', 'reverse')}
          {toggle('Normalise', 'normalise')}
          {number('Gain', doc.gain ?? 0, 'dB', (v) => commit({ ...doc, gain: Math.max(-60, Math.min(24, v)) }), 0.5)}
          {number('Fade in', (doc.fadeIn ?? 0) * 1000, 'ms', (v) => commit({ ...doc, fadeIn: Math.max(0, v) / 1000 }))}
          {number('Fade out', (doc.fadeOut ?? 0) * 1000, 'ms', (v) => commit({ ...doc, fadeOut: Math.max(0, v) / 1000 }))}
          <span className="sample-editor-group">
            Root {typeof doc.root === 'number' ? noteName(doc.root) : '—'}
            {number('', doc.root ?? 60, '', (v) => commit({ ...doc, root: Math.max(0, Math.min(127, v)) }), 0.01)}
            <button className="factory-button" disabled={busy || !info?.found} onClick={() => void detectRoot()} title="Find the sample's note">
              Detect
            </button>
          </span>
          <span className="sample-editor-group">
            Slices: {(doc.slices ?? []).length}
            <button className="factory-button" disabled={busy || !info?.found} onClick={() => void sliceAtTransients()}>
              Slice at transients
            </button>
            <label className="sample-editor-field" title="How big a jump counts as an attack (lower: more slices)">
              <input type="range" min={2} max={20} step={0.5} value={sensitivity} onChange={(e) => setSensitivity(Number(e.target.value))} />
              {sensitivity} dB
            </label>
            <button className="factory-button" disabled={(doc.slices ?? []).length === 0} onClick={() => commit({ ...doc, slices: [] })}>
              Clear
            </button>
          </span>
        </div>
      </div>

      <div className="factory-footer">
        Drag S and E to set the region · with Loop on, drag the L handles · double-click adds a slice, right-click a slice removes it ·
        the file itself never changes · plug the node into a Sampler&apos;s Sample
      </div>
    </div>
  )
}
