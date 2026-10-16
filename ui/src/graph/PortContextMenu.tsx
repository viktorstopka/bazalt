// Right-click an output port: what can be done with what it carries.
// wiki/plans/Sampling.md 3a.3: "Capture" records it into a Record node.
import { useEffect, useRef } from 'react'
import { useAutoFlipPosition } from './useAutoFlipPosition'
import './NodeContextMenu.css'

interface PortContextMenuProps {
  x: number
  y: number
  onCapture: () => void
  onClose: () => void
}

export function PortContextMenu({ x, y, onCapture, onClose }: PortContextMenuProps) {
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
      <button onClick={onCapture} title="Record what this output carries — keep the take as a Sample node">
        Capture
      </button>
    </div>
  )
}
