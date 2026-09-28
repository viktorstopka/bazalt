// Shift+A / right-click-on-empty-canvas Add menu (blueprint §6.2):
// auto-focused search, nested category browsing, arrow-key navigation +
// Enter, auto-flip near viewport edges. Selecting an entry hands the typeId
// back to InfiniteCanvas, which arms ghost placement rather than adding the
// node immediately — the ghost still needs to track the cursor and support
// splice-on-wire-hover before a click commits it.
//
// 09-29-AddMenu.1: categories can now nest ("Domain/Allocate", a "/"-
// separated `category` string — see categoryTree.ts) and browsing them is a
// real Blender-style flyout: the menu opens showing category names ONLY
// (Adapters, Domain, Effects, Filters, ...), every one of them a closed,
// hoverable row — hovering (or clicking) any one opens a panel beside it
// with that category's own contents (items, and further subcategory rows
// if it has any, recursively). Nothing is expanded inline up front, at any
// depth — that's the whole point versus the old flat "everything visible at
// once" list. Typing a query drops all of this and shows a flat, top-level-
// grouped match list instead — drilling into nested flyouts while search-
// filtering would defeat the point of typing a query.
import { useEffect, useMemo, useRef, useState, type KeyboardEvent, type MouseEvent as ReactMouseEvent, type ReactElement } from 'react'
import type { NodeDescriptor } from './descriptorTypes'
import { useAutoFlipPosition } from './useAutoFlipPosition'
import { useFlyoutPosition } from './useFlyoutPosition'
import { buildCategoryTree, rowsOf, topLevelCategory, type CategoryRow, type CategoryTreeNode } from './categoryTree'
import './AddMenu.css'

interface AddMenuProps {
  x: number
  y: number
  descriptors: readonly NodeDescriptor[]
  onChoose: (typeId: string) => void
  onClose: () => void
}

/** One open flyout in the chain — index 0 is opened from the root list,
    index N+1 from a row inside index N's own panel. */
interface OpenFlyout {
  path: string
  node: CategoryTreeNode
  anchorRect: DOMRect
}

const HOVER_INTENT_MS = 150

export function AddMenu({ x, y, descriptors, onChoose, onClose }: AddMenuProps) {
  const [query, setQuery] = useState('')
  // Keyed by a row's stable key ("item:<typeId>" / "cat:<path>"), not raw
  // index: real descriptors can arrive asynchronously after the menu is
  // already open, which reorders/extends the tree underneath it — remapping
  // by identity means the highlighted row never silently jumps just because
  // the list changed (M10_REVIEW.md §5).
  const [focusedKey, setFocusedKey] = useState<string | null>(null)
  const [openChain, setOpenChain] = useState<OpenFlyout[]>([])
  const inputRef = useRef<HTMLInputElement | null>(null)
  const rootRef = useRef<HTMLDivElement | null>(null)
  const pos = useAutoFlipPosition(x, y, rootRef)

  // DOM elements for whichever rows are currently rendered, keyed the same
  // way as focusedKey — lets a keyboard-driven open (ArrowRight/Enter, no
  // mouse event to read a rect from) measure the same rect a hover would.
  const rowElsRef = useRef(new Map<string, HTMLElement>())
  const registerRowEl = (key: string, el: HTMLElement | null) => {
    if (el) rowElsRef.current.set(key, el)
    else rowElsRef.current.delete(key)
  }
  const hoverTimerRef = useRef<number | null>(null)
  const clearHoverTimer = () => {
    if (hoverTimerRef.current != null) {
      window.clearTimeout(hoverTimerRef.current)
      hoverTimerRef.current = null
    }
  }
  useEffect(() => clearHoverTimer, [])

  const searching = query.trim().length > 0

  const roots = useMemo(() => buildCategoryTree(descriptors), [descriptors])

  // The root list itself is just category rows — every top-level category is
  // a closed, hoverable entry (even one with no subcategories of its own;
  // its "children" from the root's point of view are just its items), never
  // expanded inline. Reuses the exact same CategoryRow/renderRow shape a
  // nested flyout's rows use, so root behaves like any other level.
  const rootRows = useMemo<CategoryRow[]>(
    () => roots.map((node): CategoryRow => ({ kind: 'category', key: `cat:${node.path}`, label: node.label, node })),
    [roots],
  )

  // Search mode: a flat, top-level-grouped match list — no nesting, same
  // shape as this menu had before 09-29-AddMenu.1.
  const searchSections = useMemo(() => {
    if (!searching) return []
    const q = query.trim().toLowerCase()
    const matches = descriptors.filter((d) => d.title.toLowerCase().includes(q) || d.typeId.toLowerCase().includes(q))
    const byCategory = new Map<string, NodeDescriptor[]>()
    for (const d of matches) {
      const top = topLevelCategory(d.category)
      const list = byCategory.get(top) ?? []
      list.push(d)
      byCategory.set(top, list)
    }
    for (const list of byCategory.values()) list.sort((a, b) => a.title.localeCompare(b.title))
    return [...byCategory.entries()].sort(([a], [b]) => a.localeCompare(b))
  }, [descriptors, query, searching])

  const searchRows = useMemo<CategoryRow[]>(
    () => searchSections.flatMap(([, items]) => items.map((d): CategoryRow => ({ kind: 'item', key: `item:${d.typeId}`, label: d.title, descriptor: d }))),
    [searchSections],
  )

  // Browse mode's currently keyboard-navigable rows: the deepest open
  // flyout's own rows, or the closed root category list if nothing's open
  // yet — matches what's actually visible on screen.
  const browseRows = useMemo<CategoryRow[]>(() => {
    if (openChain.length > 0) return rowsOf(openChain[openChain.length - 1].node)
    return rootRows
  }, [rootRows, openChain])

  const activeRows = searching ? searchRows : browseRows

  const focusedIndex = useMemo(() => {
    const index = focusedKey ? activeRows.findIndex((r) => r.key === focusedKey) : -1
    return index >= 0 ? index : 0
  }, [activeRows, focusedKey])

  useEffect(() => inputRef.current?.focus(), [])

  useEffect(() => {
    const onPointerDown = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) onClose()
    }
    window.addEventListener('mousedown', onPointerDown, true)
    return () => window.removeEventListener('mousedown', onPointerDown, true)
  }, [onClose])

  // Reset flyout/focus state right on the searching/browsing transition —
  // stale openChain entries point at rows that may not even render in
  // search mode. Adjusted during render (React's own documented pattern for
  // "reset state when a prop changes") rather than in an effect, since an
  // effect here would just add an extra, unnecessary re-render pass.
  const [wasSearching, setWasSearching] = useState(searching)
  if (searching !== wasSearching) {
    setWasSearching(searching)
    setOpenChain([])
    setFocusedKey(null)
  }

  const focusByIndex = (index: number) => {
    const clamped = Math.max(0, Math.min(activeRows.length - 1, index))
    setFocusedKey(activeRows[clamped]?.key ?? null)
  }

  /** containerDepth: -1 for a root row, or the openChain index of the panel
      a row lives in — see categoryTree.ts's rowsOf for why every row
      (root or nested) is built the same shape. */
  const openCategoryAt = (containerDepth: number, node: CategoryTreeNode, rect: DOMRect) => {
    setOpenChain((prev) => [...prev.slice(0, containerDepth + 1), { path: node.path, node, anchorRect: rect }])
    setFocusedKey(`cat:${node.path}`)
  }

  const closeFromDepth = (containerDepth: number) => {
    setOpenChain((prev) => (prev.length > containerDepth + 1 ? prev.slice(0, containerDepth + 1) : prev))
  }

  const openFocusedCategory = () => {
    const row = activeRows[focusedIndex]
    if (!row || row.kind !== 'category') return
    const el = rowElsRef.current.get(row.key)
    if (!el) return
    const containerDepth = openChain.length - 1
    openCategoryAt(containerDepth, row.node, el.getBoundingClientRect())
    const firstChildRow = rowsOf(row.node)[0]
    if (firstChildRow) setFocusedKey(firstChildRow.key)
  }

  const closeDeepestFlyout = () => {
    if (openChain.length === 0) return
    const closing = openChain[openChain.length - 1]
    setOpenChain((prev) => prev.slice(0, prev.length - 1))
    setFocusedKey(`cat:${closing.path}`)
  }

  const chooseFocused = () => {
    const row = activeRows[focusedIndex]
    if (!row) return
    if (row.kind === 'item') onChoose(row.descriptor.typeId)
    else openFocusedCategory()
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
    } else if (!searching && e.key === 'ArrowRight') {
      e.preventDefault()
      openFocusedCategory()
    } else if (!searching && e.key === 'ArrowLeft') {
      e.preventDefault()
      closeDeepestFlyout()
    } else if (e.key === 'Enter') {
      e.preventDefault()
      chooseFocused()
    }
  }

  const renderRow = (row: CategoryRow, containerDepth: number): ReactElement => {
    const isFocused = row.key === focusedKey
    if (row.kind === 'item') {
      return (
        <button
          key={row.key}
          ref={(el) => registerRowEl(row.key, el)}
          className={`add-menu-item${isFocused ? ' add-menu-item-focused' : ''}`}
          onMouseEnter={() => {
            clearHoverTimer()
            setFocusedKey(row.key)
            closeFromDepth(containerDepth)
          }}
          onClick={() => onChoose(row.descriptor.typeId)}
        >
          <span>{row.label}</span>
          {row.descriptor.isMock && <span className="add-menu-item-mock">mock</span>}
        </button>
      )
    }

    const isOpen = openChain[containerDepth + 1]?.path === row.node.path
    return (
      <button
        key={row.key}
        ref={(el) => registerRowEl(row.key, el)}
        className={`add-menu-item add-menu-item-category${isFocused ? ' add-menu-item-focused' : ''}${isOpen ? ' add-menu-item-open' : ''}`}
        onMouseEnter={(e: ReactMouseEvent<HTMLButtonElement>) => {
          setFocusedKey(row.key)
          const rect = e.currentTarget.getBoundingClientRect()
          clearHoverTimer()
          hoverTimerRef.current = window.setTimeout(() => openCategoryAt(containerDepth, row.node, rect), HOVER_INTENT_MS)
        }}
        onClick={(e: ReactMouseEvent<HTMLButtonElement>) => {
          clearHoverTimer()
          openCategoryAt(containerDepth, row.node, e.currentTarget.getBoundingClientRect())
        }}
      >
        <span>{row.label}</span>
        <span className="add-menu-chevron">▸</span>
      </button>
    )
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
          setFocusedKey(null)
        }}
      />
      <div className="add-menu-results">
        {searching ? (
          <>
            {searchRows.length === 0 && <div className="add-menu-empty">No matches</div>}
            {searchSections.map(([category, items]) => (
              <div key={category} className="add-menu-category">
                <div className="add-menu-category-title">{category}</div>
                {items.map((d) => renderRow({ kind: 'item', key: `item:${d.typeId}`, label: d.title, descriptor: d }, -1))}
              </div>
            ))}
          </>
        ) : (
          <>
            {rootRows.length === 0 && <div className="add-menu-empty">No matches</div>}
            {rootRows.map((row) => renderRow(row, -1))}
          </>
        )}
      </div>
      {!searching && openChain.map((entry, depth) => (
        <FlyoutPanel key={entry.path} entry={entry} depth={depth} renderRow={renderRow} />
      ))}
    </div>
  )
}

function FlyoutPanel({
  entry,
  depth,
  renderRow,
}: {
  entry: OpenFlyout
  depth: number
  renderRow: (row: CategoryRow, containerDepth: number) => ReactElement
}) {
  const panelRef = useRef<HTMLDivElement | null>(null)
  const pos = useFlyoutPosition(entry.anchorRect, panelRef)
  const rows = rowsOf(entry.node)

  return (
    <div ref={panelRef} className="add-menu-flyout" style={{ left: pos.left, top: pos.top }}>
      {rows.length === 0 && <div className="add-menu-empty">Empty</div>}
      {rows.map((row) => renderRow(row, depth))}
    </div>
  )
}
