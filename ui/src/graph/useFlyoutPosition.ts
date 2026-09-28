// Positions a flyout submenu (AddMenu.tsx's nested-category panels,
// 09-29-AddMenu.1) beside the row that opened it, flipping to the row's left
// or clamping vertically near a viewport edge instead of clipping — the same
// "flip near a viewport edge" spirit as useAutoFlipPosition.ts, just anchored
// to a row's own measured rect instead of a fixed (x, y) point, since a
// flyout can open from anywhere in an already-positioned menu. Kept as a
// separate hook rather than generalizing useAutoFlipPosition itself, so
// NodeContextMenu.tsx's existing (x, y)-based call site can't regress.
import { useEffect, useState, type RefObject } from 'react'
import type { FlippablePosition } from './useAutoFlipPosition'

const GAP = 2

export function useFlyoutPosition(anchorRect: DOMRect | null, panelRef: RefObject<HTMLElement | null>): FlippablePosition {
  const [pos, setPos] = useState<FlippablePosition>({ left: 0, top: 0 })

  useEffect(() => {
    if (!anchorRect) return
    const measure = () => {
      const el = panelRef.current
      if (!el) return
      const rect = el.getBoundingClientRect()

      const rightEdge = anchorRect.right + GAP + rect.width
      const left = rightEdge > window.innerWidth ? Math.max(8, anchorRect.left - GAP - rect.width) : anchorRect.right + GAP

      const bottomEdge = anchorRect.top + rect.height
      const top = bottomEdge > window.innerHeight ? Math.max(8, window.innerHeight - rect.height - 8) : anchorRect.top

      setPos({ left, top })
    }
    measure()
    window.addEventListener('resize', measure)
    return () => window.removeEventListener('resize', measure)
  }, [anchorRect, panelRef])

  return pos
}
