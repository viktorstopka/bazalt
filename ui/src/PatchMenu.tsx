// design/TopBar.png: the top bar's centre dropdown — Factory patches and
// the user's own saved patches. Factory patches are "Empty" plus every
// `.bazalt` file in ./patches/factory (direct instruction, 2026-10-09):
// drop a patch saved from Bazalt into that folder and it ships, named by
// its own meta.name. "Empty" mirrors ProofGraphs.h's
// buildMasterOutOnlyGraph() exactly — same node id "masterOut", same
// position — which is also what the app opens on. Each entry is a
// whole-graph JSON string in the shape PatchSerializer.cpp's
// `parsePatchFromJson` expects, loaded via graphStore.ts's `loadPatch`.
import { useEffect, useRef, useState } from 'react'
import { deleteUserPatch, listUserPatches, saveUserPatch, userPatchExists, type UserPatchEntry } from './graph/patchLibrary'
import './PatchMenu.css'

export interface PatchOption {
  name: string
  json: string
}

function emptyPatchJson(): string {
  return JSON.stringify({
    schemaVersion: 16,
    nodes: [{ id: 'masterOut', type: 'io.output', position: { x: 640, y: 360 }, parameters: {}, properties: {} }],
    connections: [],
    outputNodeId: 'masterOut',
    outputPortId: 'out',
  })
}

// Raw text, not JSON.parse'd and re-stringified: loadPatch parses it on its
// own end, and re-serializing could reformat values tuned by ear.
const factoryFiles = import.meta.glob('./patches/factory/*.bazalt', { query: '?raw', import: 'default', eager: true }) as Record<string, string>

function factoryPatchName(path: string, json: string): string {
  try {
    const name = (JSON.parse(json) as { meta?: { name?: unknown } }).meta?.name
    if (typeof name === 'string' && name.trim()) return name.trim()
  } catch {
    // Not readable JSON: fall back to the file name.
  }
  return path.replace(/^.*\//, '').replace(/\.bazalt$/, '')
}

export const PREMADE_PATCHES: readonly PatchOption[] = [
  { name: 'Empty', json: emptyPatchJson() },
  ...Object.entries(factoryFiles)
    .map(([path, json]) => ({ name: factoryPatchName(path, json), json }))
    .sort((a, b) => a.name.localeCompare(b.name)),
]

/** What the live graph was last loaded from or saved as — the menu's label,
    and what Save writes back to. `fileName` is set only for a user patch. */
export interface CurrentPatch {
  name: string
  source: 'factory' | 'user'
  fileName?: string
}

function SaveIcon() {
  return (
    <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M5 3h11l3 3v15H5z" />
      <path d="M8 3v6h8V3" />
      <path d="M8 21v-7h8v7" />
    </svg>
  )
}

/** The patch browser: Factory patches (shipped in this file) and the user's
    own (saved to the app-data folder — patchLibrary.ts), each row labelled
    with where it came from; Save / Save as; deleting a saved patch.

    Save (the disk button, or Ctrl/Cmd+S) writes straight back to the user
    patch that's loaded; anything else — a factory patch, or Save as — asks
    for a name first, and confirms before replacing an existing one. */
export function PatchMenu({
  current,
  onLoadFactory,
  onLoadUser,
  onSaved,
}: {
  current: CurrentPatch
  onLoadFactory: (patch: PatchOption) => void
  onLoadUser: (entry: UserPatchEntry) => void
  onSaved: (patch: CurrentPatch) => void
}) {
  const [open, setOpen] = useState(false)
  const [userPatches, setUserPatches] = useState<UserPatchEntry[]>([])
  const [naming, setNaming] = useState<string | null>(null) // the Save-as field's draft, or null when closed
  const [status, setStatus] = useState<string | null>(null)
  const rootRef = useRef<HTMLDivElement | null>(null)

  const refresh = () => void listUserPatches().then(setUserPatches)

  useEffect(() => {
    if (!open && naming === null) return
    const onPointerDown = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) {
        setOpen(false)
        setNaming(null)
      }
    }
    window.addEventListener('mousedown', onPointerDown, true)
    return () => window.removeEventListener('mousedown', onPointerDown, true)
  }, [open, naming])

  const flash = (message: string) => {
    setStatus(message)
    window.setTimeout(() => setStatus(null), 2500)
  }

  const saveAs = async (rawName: string) => {
    const name = rawName.trim()
    if (!name) return
    const replacing = await userPatchExists(name)
    if (replacing && !(current.source === 'user' && current.name === name) && !window.confirm(`Replace your saved patch "${name}"?`)) return
    const result = await saveUserPatch(name)
    if (!result.success) {
      flash(result.errorMessage || 'Could not save')
      return
    }
    setNaming(null)
    onSaved({ name, source: 'user', fileName: result.fileName })
    flash('Saved')
    refresh()
  }

  const save = () => {
    if (current.source === 'user') void saveAs(current.name)
    else {
      setOpen(false)
      setNaming(current.name === 'Empty' ? '' : current.name)
    }
  }

  // Ctrl/Cmd+S saves, from anywhere in the editor.
  const saveRef = useRef(save)
  useEffect(() => {
    saveRef.current = save
  })
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault()
        saveRef.current()
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])

  const remove = async (entry: UserPatchEntry) => {
    if (!window.confirm(`Delete your saved patch "${entry.name}"? This can't be undone.`)) return
    await deleteUserPatch(entry.fileName)
    refresh()
  }

  return (
    <div ref={rootRef} className="patch-menu">
      <button
        type="button"
        className="patch-menu-button"
        onClick={() => {
          if (!open) refresh()
          setOpen((v) => !v)
          setNaming(null)
        }}
        aria-label="Choose a patch"
      >
        <span className="patch-menu-label">{current.name}</span>
        <span className="patch-menu-caret">▾</span>
      </button>
      <button type="button" className="top-bar-icon-button patch-menu-save" onClick={save} title="Save patch (Ctrl+S)" aria-label="Save patch">
        <SaveIcon />
      </button>
      {status && <span className="patch-menu-status">{status}</span>}

      {open && (
        <div className="patch-menu-list">
          {PREMADE_PATCHES.map((p) => (
            <PatchRow
              key={`factory:${p.name}`}
              name={p.name}
              author="Factory"
              active={current.source === 'factory' && current.name === p.name}
              onChoose={() => {
                onLoadFactory(p)
                setOpen(false)
              }}
            />
          ))}
          {userPatches.length > 0 && <div className="patch-menu-divider" />}
          {userPatches.map((entry) => (
            <PatchRow
              key={`user:${entry.fileName}`}
              name={entry.name}
              author="You"
              active={current.source === 'user' && current.fileName === entry.fileName}
              onChoose={() => {
                onLoadUser(entry)
                setOpen(false)
              }}
              onDelete={() => void remove(entry)}
            />
          ))}
          <div className="patch-menu-divider" />
          <button
            type="button"
            className="patch-menu-item patch-menu-action"
            onClick={() => {
              setOpen(false)
              setNaming(current.source === 'user' ? `${current.name} copy` : current.name === 'Empty' ? '' : current.name)
            }}
          >
            Save as…
          </button>
        </div>
      )}

      {naming !== null && (
        <form
          className="patch-menu-list patch-menu-save-form"
          onSubmit={(e) => {
            e.preventDefault()
            void saveAs(naming)
          }}
        >
          <input
            className="patch-menu-name-input"
            autoFocus
            placeholder="Patch name"
            value={naming}
            onChange={(e) => setNaming(e.target.value)}
            onKeyDown={(e) => {
              e.stopPropagation() // typing a name must not trigger canvas shortcuts
              if (e.key === 'Escape') setNaming(null)
            }}
          />
          <button type="submit" className="patch-menu-save-submit" disabled={!naming.trim()}>
            Save
          </button>
        </form>
      )}
    </div>
  )
}

function PatchRow({ name, author, active, onChoose, onDelete }: { name: string; author: string; active: boolean; onChoose: () => void; onDelete?: () => void }) {
  return (
    <div className={`patch-menu-item${active ? ' patch-menu-item-active' : ''}`} role="button" tabIndex={0} onClick={onChoose}>
      <span className="patch-menu-item-name">{name}</span>
      <span className="patch-menu-item-author">{author}</span>
      {onDelete && (
        <button
          type="button"
          className="patch-menu-item-delete"
          title="Delete this saved patch"
          aria-label={`Delete ${name}`}
          onClick={(e) => {
            e.stopPropagation()
            onDelete()
          }}
        >
          ×
        </button>
      )}
    </div>
  )
}
