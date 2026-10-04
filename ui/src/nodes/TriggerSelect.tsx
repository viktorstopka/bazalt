// The in-node fallback control for a `type: 'event'` port whose
// hasFallbackWhenUnconnected fallback is a discrete preset choice rather
// than a numeric range (NodeCard.tsx's PortRow: ValueSlider covers the
// numeric case, this covers the enum one) — direct feedback's own example:
// the "Random" node's Trigger input picking "On Note Legato"/"On Every
// Note"/etc. from a dropdown when nothing is cabled into it.
//
// Same pill shell/sizing as ValueSlider (shares its visual language rather
// than inventing a second control style) but click-driven, not drag/wheel —
// a discrete list has no "amount" to sweep. The selected option is stored
// as its index into `options`, via the same onCommit(number) contract
// PortRow already threads through for a numeric fallback, so no new state
// shape is needed anywhere upstream (NodeCardState.parameterValues stays a
// plain Record<string, number>).
import { useEffect, useRef, useState } from 'react'
import './TriggerSelect.css'
import { tokens, withAlpha } from '../theme/tokens'

export interface TriggerSelectProps {
  label: string
  options: readonly string[]
  selectedIndex: number
  color: string
  /** Omitted (gallery/demo call sites): still a real, clickable control —
      see ValueSlider's own uncontrolledValue fallback for the same reasoning.
  */
  onCommit?: (index: number) => void
}

export function TriggerSelect({ label, options, selectedIndex, color, onCommit }: TriggerSelectProps) {
  const [uncontrolledIndex, setUncontrolledIndex] = useState(selectedIndex)
  const committedIndex = onCommit ? selectedIndex : uncontrolledIndex
  const commitOut = onCommit ?? setUncontrolledIndex
  const clampedIndex = Math.min(Math.max(Math.round(committedIndex), 0), options.length - 1)

  const [open, setOpen] = useState(false)
  const rootRef = useRef<HTMLDivElement | null>(null)

  useEffect(() => {
    if (!open) return
    const onPointerDown = (e: MouseEvent): void => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) setOpen(false)
    }
    const onKeyDown = (e: KeyboardEvent): void => {
      if (e.key === 'Escape') setOpen(false)
    }
    window.addEventListener('mousedown', onPointerDown)
    window.addEventListener('keydown', onKeyDown)
    return () => {
      window.removeEventListener('mousedown', onPointerDown)
      window.removeEventListener('keydown', onKeyDown)
    }
  }, [open])

  const choose = (index: number): void => {
    commitOut(index)
    setOpen(false)
  }

  return (
    <div
      ref={rootRef}
      className="trigger-select"
      style={{ borderColor: withAlpha(color, tokens.opacity.border), color }}
      onClick={(e) => {
        e.stopPropagation()
        setOpen((v) => !v)
      }}
      onContextMenu={(e) => e.stopPropagation()}
    >
      <span className="trigger-select-label">{label}</span>
      <span className="trigger-select-value">{options[clampedIndex] ?? ''}</span>
      {open && (
        <div className="trigger-select-menu" onClick={(e) => e.stopPropagation()}>
          {options.map((option, index) => (
            <div
              key={option}
              className={`trigger-select-option${index === clampedIndex ? ' trigger-select-option-active' : ''}`}
              onClick={() => choose(index)}
            >
              {option}
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
