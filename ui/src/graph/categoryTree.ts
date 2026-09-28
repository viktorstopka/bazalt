// Turns each descriptor's flat `category` string into a nested tree the Add
// menu can walk (09-29-AddMenu.1). A category is just a "/"-separated path
// ("Domain/Allocate") — nothing engine- or UI-side enforces a fixed depth, so
// this stays correct for however deep a future category ever goes. Most
// nodes today still have a single-segment category ("Filters", "Utility",
// ...) and render exactly as before: this only changes anything for a
// category that genuinely has more than one segment somewhere in the catalog
// (today: only `instance.allocate.voice`'s "Domain/Allocate").
import type { NodeDescriptor } from './descriptorTypes'

export interface CategoryTreeNode {
  /** Full path from the root, e.g. "Domain/Allocate" — a stable identity key. */
  path: string
  /** This node's own segment, e.g. "Allocate" — what's actually displayed. */
  label: string
  /** Subcategories nested under this one, alphabetical by label. */
  children: CategoryTreeNode[]
  /** Descriptors whose category is exactly this node's path (not a child's) — alphabetical by title. */
  items: NodeDescriptor[]
}

const FALLBACK_CATEGORY = 'Uncategorized'

function splitCategoryPath(category: string): string[] {
  const segments = category
    .split('/')
    .map((s) => s.trim())
    .filter(Boolean)
  return segments.length > 0 ? segments : [FALLBACK_CATEGORY]
}

function getOrCreate(byPath: Map<string, CategoryTreeNode>, roots: CategoryTreeNode[], segments: string[]): CategoryTreeNode {
  const path = segments.join('/')
  const existing = byPath.get(path)
  if (existing) return existing

  const node: CategoryTreeNode = { path, label: segments[segments.length - 1], children: [], items: [] }
  byPath.set(path, node)

  if (segments.length === 1) {
    roots.push(node)
  } else {
    const parent = getOrCreate(byPath, roots, segments.slice(0, -1))
    parent.children.push(node)
  }
  return node
}

/** Builds the full tree from a (possibly pre-filtered, e.g. by search) descriptor list. */
export function buildCategoryTree(descriptors: readonly NodeDescriptor[]): CategoryTreeNode[] {
  const byPath = new Map<string, CategoryTreeNode>()
  const roots: CategoryTreeNode[] = []

  for (const d of descriptors) {
    const node = getOrCreate(byPath, roots, splitCategoryPath(d.category))
    node.items.push(d)
  }

  const sortNode = (n: CategoryTreeNode) => {
    n.children.sort((a, b) => a.label.localeCompare(b.label))
    n.items.sort((a, b) => a.title.localeCompare(b.title))
    n.children.forEach(sortNode)
  }
  roots.sort((a, b) => a.label.localeCompare(b.label))
  roots.forEach(sortNode)

  return roots
}

/** Just the top-level (first path segment) — what search-mode results group by, since drilling into a multi-level flyout while a query is active would defeat the point of typing one. */
export function topLevelCategory(category: string): string {
  return splitCategoryPath(category)[0]
}

export type CategoryRow = { kind: 'item'; key: string; label: string; descriptor: NodeDescriptor } | { kind: 'category'; key: string; label: string; node: CategoryTreeNode }

/** One level's worth of rows, items and subcategories interleaved alphabetically by label — a category row carries its own chevron/flyout in the UI, an item row is choosable directly. */
export function rowsOf(node: CategoryTreeNode): CategoryRow[] {
  const rows: CategoryRow[] = [
    ...node.items.map((d): CategoryRow => ({ kind: 'item', key: `item:${d.typeId}`, label: d.title, descriptor: d })),
    ...node.children.map((c): CategoryRow => ({ kind: 'category', key: `cat:${c.path}`, label: c.label, node: c })),
  ]
  rows.sort((a, b) => a.label.localeCompare(b.label))
  return rows
}
