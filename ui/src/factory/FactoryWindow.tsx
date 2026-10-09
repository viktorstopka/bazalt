// The Factory window (wiki/plans/DataAndWavetable.md D10, the EDIT CURVE
// mockup): editing a factory replaces the canvas with a full window — title,
// back, undo/redo, import, save as preset, a preset menu — and the editor.
// One window type for every editor; the node itself keeps a compact preview
// and an Edit button. Undo/redo are the patch's own (a content edit is one
// ordinary undo step), so closing the window loses nothing.
import { useEffect, useRef, useState } from 'react'
import { closeFactory, useOpenFactory } from './factoryStore'
import { CurveEditor } from './CurveEditor'
import { WavetableEditor } from './WavetableEditor'
import { CURVE_FACTORY_TYPES, CYCLE_PRESETS, TIME_PRESETS, type CurveDoc, curveFromContent } from './curveModel'
import { WAVETABLE_FACTORY_TYPES, WAVETABLE_PRESETS, type WavetableDoc, wavetableFromContent } from './wavetableModel'
import { useGraphSnapshot } from '../graph/useGraphSnapshot'
import { getDescriptor, redo, setNodeContent, setNodeContentLive, undo } from '../graph/graphStore'
import './FactoryWindow.css'

type FactoryDoc = CurveDoc | WavetableDoc

interface Preset {
  name: string
  doc: FactoryDoc
}

const USER_PRESETS_KEY = { curve: 'bazalt.curvePresets', wavetable: 'bazalt.wavetablePresets' } as const
type FactoryKind = keyof typeof USER_PRESETS_KEY

const isDocOf = (kind: FactoryKind, doc: unknown) =>
  kind === 'wavetable' ? Array.isArray((doc as WavetableDoc)?.keyframes) : Array.isArray((doc as CurveDoc)?.points)

/** Presets the user saved — per browser profile, a convenience; the patch
    itself always carries the content it uses. */
function loadUserPresets(kind: FactoryKind): Preset[] {
  try {
    const raw = localStorage.getItem(USER_PRESETS_KEY[kind])
    const parsed = raw ? (JSON.parse(raw) as Preset[]) : []
    return Array.isArray(parsed) ? parsed.filter((p) => typeof p?.name === 'string' && isDocOf(kind, p?.doc)) : []
  } catch {
    return []
  }
}

function saveUserPresets(kind: FactoryKind, presets: Preset[]): void {
  try {
    localStorage.setItem(USER_PRESETS_KEY[kind], JSON.stringify(presets))
  } catch {
    // Storage unavailable: the preset just isn't remembered.
  }
}

export function FactoryWindow() {
  const nodeId = useOpenFactory()
  const { nodes, canUndo, canRedo } = useGraphSnapshot()
  const node = nodeId ? nodes.find((n) => n.id === nodeId) : undefined
  const [snap, setSnap] = useState(true)
  const [presetsOpen, setPresetsOpen] = useState(false)
  const [naming, setNaming] = useState<string | null>(null)
  const kind: FactoryKind | null = node ? (WAVETABLE_FACTORY_TYPES.has(node.typeId) ? 'wavetable' : CURVE_FACTORY_TYPES[node.typeId] ? 'curve' : null) : null
  const [userPresets, setUserPresets] = useState<Record<FactoryKind, Preset[]>>(() => ({
    curve: loadUserPresets('curve'),
    wavetable: loadUserPresets('wavetable'),
  }))
  const fileRef = useRef<HTMLInputElement | null>(null)

  // The node was deleted (or undone away): nothing left to edit.
  useEffect(() => {
    if (nodeId && !node) closeFactory()
  }, [nodeId, node])

  useEffect(() => {
    if (!nodeId) return
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
  }, [nodeId])

  if (!nodeId || !node || !kind) return null

  const curve = kind === 'curve' ? curveFromContent(node.content, CURVE_FACTORY_TYPES[node.typeId]) : null
  const wavetable = kind === 'wavetable' ? wavetableFromContent(node.content) : null
  const doc: FactoryDoc = curve ?? wavetable!
  const title = node.titleOverride || getDescriptor(node.typeId)?.title || node.typeId
  const builtIn: readonly Preset[] = wavetable ? WAVETABLE_PRESETS : curve!.timeBase === 'time' ? TIME_PRESETS : CYCLE_PRESETS
  const matching = userPresets[kind].filter((p) => !curve || (p.doc as CurveDoc).timeBase === curve.timeBase)

  const commit = (next: FactoryDoc) => setNodeContent(node.id, next)
  const live = (next: FactoryDoc) => setNodeContentLive(node.id, next)

  const importFile = (file: File) => {
    void file.text().then((text) => {
      try {
        const json: unknown = JSON.parse(text)
        if (!isDocOf(kind, json)) return
        commit(curve ? curveFromContent(json, curve.timeBase) : wavetableFromContent(json))
      } catch {
        // Not readable: ignore.
      }
    })
  }

  return (
    <div className="factory-window" onContextMenu={(e) => e.preventDefault()}>
      <div className="factory-header">
        <button className="factory-button" onClick={closeFactory} title="Back to the patch (Esc)">
          ← Back
        </button>
        <span className="factory-title">
          {wavetable ? 'EDIT WAVETABLE' : 'EDIT CURVE'} <span className="factory-subtitle">{title}</span>
        </span>
        {curve && <span className="factory-timebase">{curve.timeBase === 'time' ? 'Time' : 'Cycle'}</span>}
        {wavetable && <span className="factory-timebase">{wavetable.keyframes.length} keyframes</span>}
        <div className="factory-spacer" />
        <button className={snap ? 'factory-button active' : 'factory-button'} onClick={() => setSnap(!snap)} title="Snap to the grid">
          Snap
        </button>
        <button className="factory-button" onClick={() => undo()} disabled={!canUndo} title="Undo (Ctrl+Z)">
          Undo
        </button>
        <button className="factory-button" onClick={() => redo()} disabled={!canRedo} title="Redo (Ctrl+Shift+Z)">
          Redo
        </button>
        <button className="factory-button" onClick={() => fileRef.current?.click()} title={wavetable ? 'Import a wavetable (.json)' : 'Import a curve (.json)'}>
          Import
        </button>
        <input
          ref={fileRef}
          type="file"
          accept=".json,application/json"
          style={{ display: 'none' }}
          onChange={(e) => {
            const file = e.target.files?.[0]
            if (file) importFile(file)
            e.target.value = ''
          }}
        />
        {naming === null ? (
          <button className="factory-button" onClick={() => setNaming('')} title="Save as a preset">
            Save as preset
          </button>
        ) : (
          <input
            className="factory-name-input"
            autoFocus
            placeholder="Preset name"
            value={naming}
            onChange={(e) => setNaming(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && naming.trim()) {
                const next = [...userPresets[kind].filter((p) => p.name !== naming.trim()), { name: naming.trim(), doc }]
                setUserPresets({ ...userPresets, [kind]: next })
                saveUserPresets(kind, next)
                setNaming(null)
              } else if (e.key === 'Escape') {
                e.stopPropagation()
                setNaming(null)
              }
            }}
            onBlur={() => setNaming(null)}
          />
        )}
        <div className="factory-presets">
          <button className="factory-button" onClick={() => setPresetsOpen(!presetsOpen)}>
            Presets ▾
          </button>
          {presetsOpen && (
            <div className="factory-presets-menu" onMouseLeave={() => setPresetsOpen(false)}>
              {[...builtIn, ...matching].map((preset, i) => (
                <button
                  key={`${preset.name}-${i}`}
                  className="factory-presets-item"
                  onClick={() => {
                    commit(preset.doc)
                    setPresetsOpen(false)
                  }}
                >
                  {preset.name}
                  {i >= builtIn.length && <span className="factory-presets-user">saved</span>}
                </button>
              ))}
            </div>
          )}
        </div>
      </div>
      {curve && <CurveEditor doc={curve} snap={snap} onLive={live} onCommit={commit} />}
      {wavetable && <WavetableEditor doc={wavetable} snap={snap} onLive={live} onCommit={commit} />}
      <div className="factory-footer">
        {wavetable ? (
          <>
            Click a keyframe to edit it, drag it to move · Curve: drag points, double-click to add · Harmonics: drag across the bars to paint, right-click clears ·
            plug the table into an Oscillator&apos;s Shape and move its Frame
          </>
        ) : (
          <>
            Drag points · drag a round handle to bend a segment · double-click to add · Delete removes · right-click a point for its shape
            {curve!.timeBase === 'time' ? ' and marker (S holds while the gate is high)' : ''} · wheel zooms · Space-drag pans
          </>
        )}
      </div>
    </div>
  )
}
