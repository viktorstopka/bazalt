// The Factory window (wiki/plans/DataAndWavetable.md D10, the EDIT CURVE
// mockup): editing a factory replaces the canvas with a full window — title,
// back, undo/redo, import, save as preset, a preset menu — and the editor.
// One window type for every editor; the node itself keeps a compact preview
// and an Edit button. Undo/redo are the patch's own (a content edit is one
// ordinary undo step), so closing the window loses nothing.
import { useEffect, useRef, useState } from 'react'
import { closeFactory, useOpenFactory } from './factoryStore'
import { CurveEditor } from './CurveEditor'
import { CURVE_FACTORY_TYPES, CYCLE_PRESETS, TIME_PRESETS, type CurveDoc, type CurvePreset, curveFromContent } from './curveModel'
import { useGraphSnapshot } from '../graph/useGraphSnapshot'
import { getDescriptor, redo, setNodeContent, setNodeContentLive, undo } from '../graph/graphStore'
import './FactoryWindow.css'

const USER_PRESETS_KEY = 'bazalt.curvePresets'

/** Presets the user saved — per browser profile, a convenience; the patch
    itself always carries the curve it uses. */
function loadUserPresets(): CurvePreset[] {
  try {
    const raw = localStorage.getItem(USER_PRESETS_KEY)
    const parsed = raw ? (JSON.parse(raw) as CurvePreset[]) : []
    return Array.isArray(parsed) ? parsed.filter((p) => typeof p?.name === 'string' && Array.isArray(p?.doc?.points)) : []
  } catch {
    return []
  }
}

function saveUserPresets(presets: CurvePreset[]): void {
  try {
    localStorage.setItem(USER_PRESETS_KEY, JSON.stringify(presets))
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
  const [userPresets, setUserPresets] = useState<CurvePreset[]>(loadUserPresets)
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

  if (!nodeId || !node) return null
  const timeBase = CURVE_FACTORY_TYPES[node.typeId]
  if (!timeBase) return null

  const doc = curveFromContent(node.content, timeBase)
  const title = node.titleOverride || getDescriptor(node.typeId)?.title || node.typeId
  const builtIn = doc.timeBase === 'time' ? TIME_PRESETS : CYCLE_PRESETS
  const matching = userPresets.filter((p) => p.doc.timeBase === doc.timeBase)

  const commit = (next: CurveDoc) => setNodeContent(node.id, next)
  const live = (next: CurveDoc) => setNodeContentLive(node.id, next)

  const importFile = (file: File) => {
    void file.text().then((text) => {
      try {
        const parsed = curveFromContent(JSON.parse(text), doc.timeBase)
        commit(parsed)
      } catch {
        // Not a curve: ignore.
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
          EDIT CURVE <span className="factory-subtitle">{title}</span>
        </span>
        <span className="factory-timebase">{doc.timeBase === 'time' ? 'Time' : 'Cycle'}</span>
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
        <button className="factory-button" onClick={() => fileRef.current?.click()} title="Import a curve (.json)">
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
          <button className="factory-button" onClick={() => setNaming('')} title="Save this curve as a preset">
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
                const next = [...userPresets.filter((p) => p.name !== naming.trim()), { name: naming.trim(), doc }]
                setUserPresets(next)
                saveUserPresets(next)
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
      <CurveEditor doc={doc} snap={snap} onLive={live} onCommit={commit} />
      <div className="factory-footer">
        Drag points · drag a round handle to bend a segment · double-click to add · Delete removes · right-click a point for its shape
        {doc.timeBase === 'time' ? ' and marker (S holds while the gate is high)' : ''} · wheel zooms · Space-drag pans
      </div>
    </div>
  )
}
