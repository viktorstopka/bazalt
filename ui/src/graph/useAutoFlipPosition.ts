// Shared "flip near a viewport edge instead of clipping" positioning for
// screen-space popovers (AddMenu.tsx, NodeContextMenu.tsx) — extracted so
// both reuse one measurement instead of drifting out of sync
// (M10_REVIEW.md §14: the node context menu never got the edge-aware
// repositioning the Add menu already had). Re-measures on x/y changes AND
// on window resize, so a menu that's already open and happens to be
// off-screen after a resize corrects itself too (§5's matching gap for the
// Add menu specifically).
import { useEffect, useState, type RefObject } from 'react'

export interface FlippablePosition {
  left: number
  top: number
}

export function useAutoFlipPosition(x: number, y: number, ref: RefObject<HTMLElement | null>): FlippablePosition {
  const [pos, setPos] = useState<FlippablePosition>({ left: x, top: y })

  useEffect(() => {
    const measure = () => {
      const el = ref.current
      if (!el) return
      const rect = el.getBoundingClientRect()
      const overflowX = rect.right - window.innerWidth
      const overflowY = rect.bottom - window.innerHeight
      setPos({
        left: overflowX > 0 ? Math.max(8, x - rect.width) : x,
        top: overflowY > 0 ? Math.max(8, y - rect.height) : y,
      })
    }
    measure()
    window.addEventListener('resize', measure)
    return () => window.removeEventListener('resize', measure)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [x, y])

  return pos
}
