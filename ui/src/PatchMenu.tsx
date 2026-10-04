// design/TopBar.png: the top bar's centre dropdown. Direct instruction:
// "The purpose is not saving or authoring patches from inside the software
// yet. I need a menu of premade patches so the system can be shown to a
// few people without instructing them how to connect everything." — so
// this is a fixed, hand-written list (not a file picker, not user-savable
// patches), each entry a whole-graph JSON string in the exact shape
// PatchSerializer.cpp's `parsePatchFromJson` expects (schemaVersion/nodes/
// connections/outputNodeId/outputPortId — the same shape graphGetSnapshot
// returns and graphRestoreSnapshot/undo-redo already round-trip), loaded
// via graphStore.ts's `loadPatch`. "Do not create those [real demo]
// patches yet" — only two placeholders exist for now: "Empty" (mirrors
// ProofGraphs.h's own buildMasterOutOnlyGraph() exactly — same node id
// "masterOut", same position — so picking it matches a fresh plugin
// instance's own default graph) and "Sine" (adds one osc.sine straight
// into the same Master Out).
import { useEffect, useRef, useState } from 'react'
import './PatchMenu.css'

export interface PatchOption {
  name: string
  json: string
}

function emptyPatchJson(): string {
  return JSON.stringify({
    schemaVersion: 7,
    nodes: [{ id: 'masterOut', type: 'io.output', position: { x: 640, y: 360 }, parameters: {}, properties: {} }],
    connections: [],
    outputNodeId: 'masterOut',
    outputPortId: 'out',
  })
}

function sinePatchJson(): string {
  return JSON.stringify({
    schemaVersion: 7,
    nodes: [
      { id: 'sine1', type: 'osc.sine', position: { x: 300, y: 360 }, parameters: {}, properties: {} },
      { id: 'masterOut', type: 'io.output', position: { x: 640, y: 360 }, parameters: {}, properties: {} },
    ],
    connections: [{ fromNodeId: 'sine1', fromPortId: 'out', toNodeId: 'masterOut', toPortId: 'in' }],
    outputNodeId: 'masterOut',
    outputPortId: 'out',
  })
}

/** Exactly the two placeholders direct instruction asked for — "MIDI Saw,
    Karplus-Strong, Arp, Swarm and many others" are explicitly future work,
    not stubbed here even as empty entries. Add a real demo patch by
    appending another `{ name, json }` — nothing else needs to change. */
export const PREMADE_PATCHES: readonly PatchOption[] = [
  { name: 'Empty', json: emptyPatchJson() },
  { name: 'Sine', json: sinePatchJson() },
]

export function PatchMenu({ patches, value, onChoose }: { patches: readonly PatchOption[]; value: string; onChoose: (patch: PatchOption) => void }) {
  const [open, setOpen] = useState(false)
  const rootRef = useRef<HTMLDivElement | null>(null)

  useEffect(() => {
    if (!open) return
    const onPointerDown = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) setOpen(false)
    }
    window.addEventListener('mousedown', onPointerDown, true)
    return () => window.removeEventListener('mousedown', onPointerDown, true)
  }, [open])

  return (
    <div ref={rootRef} className="patch-menu">
      <button type="button" className="patch-menu-button" onClick={() => setOpen((v) => !v)} aria-label="Choose a patch">
        <span className="patch-menu-label">{value}</span>
        <span className="patch-menu-caret">▾</span>
      </button>
      {open && (
        <div className="patch-menu-list">
          {patches.map((p) => (
            <button
              type="button"
              key={p.name}
              className={`patch-menu-item${p.name === value ? ' patch-menu-item-active' : ''}`}
              onClick={() => {
                onChoose(p)
                setOpen(false)
              }}
            >
              {p.name}
            </button>
          ))}
        </div>
      )}
    </div>
  )
}
