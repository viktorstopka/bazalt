// The wavetable editor (wiki/plans/DataAndWavetable.md 1c, the EDIT WAVETABLE
// mockup): an overview of the whole table with a preview frame, a strip of
// keyframes (click to select, drag to move, add / duplicate / delete), and
// below them the selected keyframe — drawn as a curve, or painted as
// harmonics. Morph crossfades between keyframes; Step switches at each one.
// Every drag streams live and commits once on release.
import { useRef, useState } from 'react'
import { CurveEditor } from './CurveEditor'
import { HarmonicsEditor } from './HarmonicsEditor'
import {
  type Keyframe,
  type WavetableDoc,
  curveKeyframe,
  frameCycle,
  keyframeCycle,
  locateFrame,
  toCurve,
  toHarmonics,
} from './wavetableModel'
import { CYCLE_PRESETS } from './curveModel'
import { tokens } from '../theme/tokens'
import './WavetableEditor.css'

interface Props {
  doc: WavetableDoc
  snap: boolean
  onLive: (doc: WavetableDoc) => void
  onCommit: (doc: WavetableDoc) => void
}

const OVERVIEW_FRAMES = 24
const SAMPLES = 128

function cyclePath(cycle: number[], x0: number, width: number, yMid: number, height: number): string {
  return cycle
    .map((v, i) => `${i === 0 ? 'M' : 'L'}${(x0 + (i / (cycle.length - 1)) * width).toFixed(1)},${(yMid - v * height * 0.5).toFixed(1)}`)
    .join('')
}

/** The whole table at a glance: frames stacked back to front, the preview frame bright. */
function Overview({ doc, frame }: { doc: WavetableDoc; frame: number }) {
  const width = 600
  const height = 150
  const cache = new Map<Keyframe, number[]>()
  const frames = Array.from({ length: OVERVIEW_FRAMES }, (_, i) => i / (OVERVIEW_FRAMES - 1))
  const depthX = 120
  const depthY = 60
  const waveW = width - depthX - 10
  const waveH = height - depthY - 20
  const place = (f: number) => ({ x: 5 + f * depthX, y: height - 10 - waveH / 2 - f * depthY })
  return (
    <svg className="wavetable-overview" viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="xMidYMid meet">
      {[...frames].reverse().map((f) => {
        const { x, y } = place(f)
        return <path key={f} d={cyclePath(frameCycle(doc, f, SAMPLES, cache), x, waveW, y, waveH)} className="wavetable-overview-frame" />
      })}
      {(() => {
        const { x, y } = place(frame)
        return <path d={cyclePath(frameCycle(doc, frame, SAMPLES, cache), x, waveW, y, waveH)} stroke={tokens.color.portModulation} className="wavetable-overview-current" />
      })()}
    </svg>
  )
}

function KeyframeThumb({ keyframe }: { keyframe: Keyframe }) {
  const cycle = keyframeCycle(keyframe, 48)
  return (
    <svg className="wavetable-keyframe-thumb" viewBox="0 0 48 24" preserveAspectRatio="none">
      <path d={cyclePath(cycle, 0, 48, 12, 22)} stroke={tokens.color.portModulation} />
    </svg>
  )
}

export function WavetableEditor({ doc, snap, onLive, onCommit }: Props) {
  const [selected, setSelected] = useState(0)
  const [frame, setFrame] = useState(0)
  const [dragDoc, setDragDoc] = useState<WavetableDoc | null>(null)
  const stripRef = useRef<HTMLDivElement | null>(null)

  const shown = dragDoc ?? doc
  const index = Math.min(selected, shown.keyframes.length - 1)
  const keyframe = shown.keyframes[index]

  const withKeyframe = (base: WavetableDoc, i: number, next: Keyframe): WavetableDoc => ({
    ...base,
    keyframes: base.keyframes.map((k, j) => (j === i ? next : k)),
  })
  const sorted = (base: WavetableDoc, keep: Keyframe): { doc: WavetableDoc; index: number } => {
    const keyframes = [...base.keyframes].sort((a, b) => a.position - b.position)
    return { doc: { ...base, keyframes }, index: keyframes.indexOf(keep) }
  }

  const addKeyframe = (duplicate: boolean) => {
    const next = shown.keyframes[index + 1]
    const position = next ? (keyframe.position + next.position) / 2 : Math.min(1, keyframe.position + (1 - keyframe.position) / 2)
    const made: Keyframe = duplicate
      ? { ...structuredClone(keyframe), position }
      : (() => {
          // A new keyframe is the table as it sounds at that position, drawn as a curve.
          const cycle = frameCycle(shown, position, 64)
          return curveKeyframe(position, {
            schema: 'curve',
            version: 1,
            timeBase: 'cycle',
            points: cycle.map((y, i) => ({ x: i / 64, y: Math.round(y * 1000) / 1000 })),
          })
        })()
    const result = sorted({ ...shown, keyframes: [...shown.keyframes, made] }, made)
    setSelected(result.index)
    onCommit(result.doc)
  }

  const deleteKeyframe = () => {
    if (shown.keyframes.length <= 1) return
    const keyframes = shown.keyframes.filter((_, i) => i !== index)
    setSelected(Math.max(0, index - 1))
    onCommit({ ...shown, keyframes })
  }

  const startMove = (e: React.PointerEvent, i: number) => {
    e.preventDefault()
    e.stopPropagation()
    setSelected(i)
    const rect = stripRef.current!.getBoundingClientRect()
    const moving = shown.keyframes[i]
    const startX = e.clientX
    let current: { doc: WavetableDoc; index: number } = { doc: shown, index: i }
    let moved = false
    const move = (ev: PointerEvent) => {
      if (!moved && Math.abs(ev.clientX - startX) < 3) return
      moved = true
      let position = Math.min(1, Math.max(0, (ev.clientX - rect.left) / rect.width))
      if (snap) position = Math.round(position * 32) / 32
      const replaced: Keyframe = { ...moving, position }
      current = sorted(withKeyframe(shown, i, replaced), replaced)
      setDragDoc(current.doc)
      setSelected(current.index)
      setFrame(position)
      onLive(current.doc)
    }
    const up = () => {
      window.removeEventListener('pointermove', move)
      window.removeEventListener('pointerup', up)
      setDragDoc(null)
      if (moved) onCommit(current.doc)
      else setFrame(moving.position)
    }
    window.addEventListener('pointermove', move)
    window.addEventListener('pointerup', up)
  }

  const setKind = (kind: Keyframe['kind']) => {
    if (kind === keyframe.kind) return
    onCommit(withKeyframe(shown, index, kind === 'harmonics' ? toHarmonics(keyframe) : toCurve(keyframe)))
  }

  const { lower, upper, mix } = locateFrame(shown, frame)

  return (
    <div className="wavetable-editor">
      <div className="wavetable-top">
        <Overview doc={shown} frame={frame} />
        <div className="wavetable-controls">
          <label className="wavetable-frame">
            <span>Preview frame</span>
            <input type="range" min={0} max={1} step={0.001} value={frame} onChange={(e) => setFrame(Number(e.target.value))} />
            <span className="wavetable-frame-value">{frame.toFixed(3)}</span>
          </label>
          <div className="wavetable-frame-hint">
            {mix > 0 ? `between keyframes ${lower + 1} and ${upper + 1}` : `keyframe ${lower + 1}`} — the Frame port plays this position
          </div>
          <div className="wavetable-segmented">
            <button className={shown.interpolation === 'morph' ? 'active' : ''} onClick={() => onCommit({ ...shown, interpolation: 'morph' })} title="Crossfade between keyframes">
              Morph
            </button>
            <button className={shown.interpolation === 'step' ? 'active' : ''} onClick={() => onCommit({ ...shown, interpolation: 'step' })} title="Switch at each keyframe">
              Step
            </button>
          </div>
        </div>
      </div>

      <div className="wavetable-strip-row">
        <div ref={stripRef} className="wavetable-strip" onPointerDown={(e) => {
          const rect = stripRef.current!.getBoundingClientRect()
          setFrame(Math.min(1, Math.max(0, (e.clientX - rect.left) / rect.width)))
        }}>
          <div className="wavetable-strip-playhead" style={{ left: `${frame * 100}%` }} />
          {shown.keyframes.map((k, i) => (
            <div
              key={i}
              className={i === index ? 'wavetable-keyframe selected' : 'wavetable-keyframe'}
              style={{ left: `${k.position * 100}%` }}
              onPointerDown={(e) => startMove(e, i)}
              title={`Keyframe ${i + 1} at ${k.position.toFixed(3)} — drag to move`}
            >
              <KeyframeThumb keyframe={k} />
              <span className="wavetable-keyframe-label">{i + 1}</span>
            </div>
          ))}
        </div>
        <div className="wavetable-strip-buttons">
          <button className="factory-button" onClick={() => addKeyframe(false)} title="Add a keyframe after the selected one, as the table sounds there">
            Add
          </button>
          <button className="factory-button" onClick={() => addKeyframe(true)} title="Duplicate the selected keyframe">
            Duplicate
          </button>
          <button className="factory-button" onClick={deleteKeyframe} disabled={shown.keyframes.length <= 1} title="Delete the selected keyframe">
            Delete
          </button>
        </div>
      </div>

      <div className="wavetable-keyframe-bar">
        <span className="wavetable-keyframe-title">
          Keyframe {index + 1} <span className="wavetable-keyframe-position">at {keyframe.position.toFixed(3)}</span>
        </span>
        <div className="wavetable-segmented">
          <button className={keyframe.kind === 'curve' ? 'active' : ''} onClick={() => setKind('curve')} title="Draw this keyframe's cycle">
            Curve
          </button>
          <button className={keyframe.kind === 'harmonics' ? 'active' : ''} onClick={() => setKind('harmonics')} title="Paint this keyframe's harmonics">
            Harmonics
          </button>
        </div>
        {keyframe.kind === 'curve' && (
          <div className="wavetable-shapes">
            {CYCLE_PRESETS.map((preset) => (
              <button key={preset.name} className="factory-button" onClick={() => onCommit(withKeyframe(shown, index, { ...keyframe, curve: preset.doc }))}>
                {preset.name}
              </button>
            ))}
          </div>
        )}
      </div>

      <div className="wavetable-main">
        {keyframe.kind === 'curve' ? (
          <CurveEditor
            key={`curve-${index}`}
            doc={keyframe.curve}
            snap={snap}
            onLive={(curve) => onLive(withKeyframe(shown, index, { ...keyframe, curve }))}
            onCommit={(curve) => onCommit(withKeyframe(shown, index, { ...keyframe, curve }))}
          />
        ) : (
          <HarmonicsArea keyframe={keyframe} onLive={(k) => onLive(withKeyframe(shown, index, k))} onCommit={(k) => onCommit(withKeyframe(shown, index, k))} />
        )}
      </div>
    </div>
  )
}

function HarmonicsArea({
  keyframe,
  onLive,
  onCommit,
}: {
  keyframe: Extract<Keyframe, { kind: 'harmonics' }>
  onLive: (k: Keyframe) => void
  onCommit: (k: Keyframe) => void
}) {
  return (
    <HarmonicsEditor
      amplitudes={keyframe.amplitudes}
      onLive={(amplitudes) => onLive({ ...keyframe, amplitudes })}
      onCommit={(amplitudes) => onCommit({ ...keyframe, amplitudes })}
    />
  )
}
