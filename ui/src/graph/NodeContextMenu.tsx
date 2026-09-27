// Right-click node menu (blueprint §6.4: "Right-click context menu: Rename,
// Toggle Bypass, Delete"). Rendered via a portal into InfiniteCanvas's
// screen-space overlay (see GraphSurface.tsx) so it's never subject to the
// world layer's pan/zoom CSS transform. Closes on outside click or Escape.
// Auto-flips near viewport edges via the same hook AddMenu.tsx uses
// (M10_REVIEW.md §14 — this menu never got that treatment originally).
import { useEffect, useRef } from 'react'
import { useAutoFlipPosition } from './useAutoFlipPosition'
import './NodeContextMenu.css'

interface NodeContextMenuProps {
  x: number
  y: number
  bypassed: boolean
  onRename: () => void
  onToggleBypass: () => void
  onSetAsOutput: () => void
  onDelete: () => void
  onClose: () => void
}

export function NodeContextMenu({ x, y, bypassed, onRename, onToggleBypass, onSetAsOutput, onDelete, onClose }: NodeContextMenuProps) {
  const rootRef = useRef<HTMLDivElement | null>(null)
  const pos = useAutoFlipPosition(x, y, rootRef)

  useEffect(() => {
    const onPointerDown = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) onClose()
    }
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose()
    }
    window.addEventListener('mousedown', onPointerDown, true)
    window.addEventListener('keydown', onKeyDown)
    return () => {
      window.removeEventListener('mousedown', onPointerDown, true)
      window.removeEventListener('keydown', onKeyDown)
    }
  }, [onClose])

  return (
    <div ref={rootRef} className="node-context-menu" style={{ left: pos.left, top: pos.top }} onContextMenu={(e) => e.preventDefault()}>
      <button onClick={onRename}>Rename</button>
      <button onClick={onToggleBypass}>{bypassed ? 'Un-bypass' : 'Toggle Bypass'}</button>
      <button onClick={onSetAsOutput}>Set as Output</button>
      <button className="node-context-menu-delete" onClick={onDelete}>
        Delete
      </button>
    </div>
  )
}
