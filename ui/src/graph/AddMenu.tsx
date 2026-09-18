// Shift+A / right-click-on-empty-canvas Add menu (blueprint §6.2):
// auto-focused search, category groups derived from the descriptor catalog,
// arrow-key navigation + Enter, auto-flip near viewport edges. Selecting an
// entry hands the typeId back to InfiniteCanvas, which arms ghost placement
// rather than adding the node immediately — the ghost still needs to track
// the cursor and support splice-on-wire-hover before a click commits it.
import { useEffect, useMemo, useRef, useState, type KeyboardEvent } from 'react'
import type { NodeDescriptor } from './descriptorTypes'
import { useAutoFlipPosition } from './useAutoFlipPosition'
import './AddMenu.css'

interface AddMenuProps {
  x: number
  y: number
  descriptors: readonly NodeDescriptor[]
  onChoose: (typeId: string) => void
  onClose: () => void
}

export function AddMenu({ x, y, descriptors, onChoose, onClose }: AddMenuProps) {
  const [query, setQuery] = useState('')
  // Tracked by typeId, not raw index: real descriptors can arrive
  // asynchronously after the menu is already open, which reorders/extends
  // `flat` — remapping by identity means the highlighted row never
  // silently jumps to a different node just because the list underneath it
  // changed (M10_REVIEW.md §5).
  const [focusedTypeId, setFocusedTypeId] = useState<string | null>(null)
  const inputRef = useRef<HTMLInputElement | null>(null)
  const rootRef = useRef<HTMLDivElement | null>(null)
  const pos = useAutoFlipPosition(x, y, rootRef)

  const categories = useMemo(() => {
    const q = query.trim().toLowerCase()
    const matches = q ? descriptors.filter((d) => d.title.toLowerCase().includes(q) || d.typeId.toLowerCase().includes(q)) : descriptors
    const byCategory = new Map<string, NodeDescriptor[]>()
    for (const d of matches) {
      const list = byCategory.get(d.category) ?? []
      list.push(d)
      byCategory.set(d.category, list)
    }
    return [...byCategory.entries()].sort(([a], [b]) => a.localeCompare(b))
  }, [descriptors, query])

  const flat = useMemo(() => categories.flatMap(([, items]) => items), [categories])

  const focusedIndex = useMemo(() => {
    const index = focusedTypeId ? flat.findIndex((d) => d.typeId === focusedTypeId) : -1
    return index >= 0 ? index : 0
  }, [flat, focusedTypeId])

  useEffect(() => inputRef.current?.focus(), [])

  useEffect(() => {
    const onPointerDown = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) onClose()
    }
    window.addEventListener('mousedown', onPointerDown, true)
    return () => window.removeEventListener('mousedown', onPointerDown, true)
  }, [onClose])

  const focusByIndex = (index: number) => {
    const clamped = Math.max(0, Math.min(flat.length - 1, index))
    setFocusedTypeId(flat[clamped]?.typeId ?? null)
  }

  const onKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Escape') {
      e.stopPropagation()
      onClose()
    } else if (e.key === 'ArrowDown') {
      e.preventDefault()
      focusByIndex(focusedIndex + 1)
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      focusByIndex(focusedIndex - 1)
    } else if (e.key === 'Enter') {
      e.preventDefault()
      const chosen = flat[focusedIndex]
      if (chosen) onChoose(chosen.typeId)
    }
  }

  return (
    <div ref={rootRef} className="add-menu" style={{ left: pos.left, top: pos.top }} onKeyDown={onKeyDown} onContextMenu={(e) => e.preventDefault()}>
      <input
        ref={inputRef}
        className="add-menu-search"
        placeholder="Search nodes…"
        value={query}
        onChange={(e) => {
          setQuery(e.target.value)
          setFocusedTypeId(null)
        }}
      />
      <div className="add-menu-results">
        {flat.length === 0 && <div className="add-menu-empty">No matches</div>}
        {categories.map(([category, items]) => (
          <div key={category} className="add-menu-category">
            <div className="add-menu-category-title">{category}</div>
            {items.map((d) => {
              const index = flat.indexOf(d)
              return (
                <button
                  key={d.typeId}
                  className={`add-menu-item${index === focusedIndex ? ' add-menu-item-focused' : ''}`}
                  onMouseEnter={() => setFocusedTypeId(d.typeId)}
                  onClick={() => onChoose(d.typeId)}
                >
                  <span>{d.title}</span>
                  {d.isMock && <span className="add-menu-item-mock">mock</span>}
                </button>
              )
            })}
          </div>
        ))}
      </div>
    </div>
  )
}
