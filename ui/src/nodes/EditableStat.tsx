// Extracted from CountBody.tsx (design/Visualization/Count.png) once
// design/Visualization/Scope1.png needed the exact same interaction for its
// own Min/Max/time-window fields — a click-to-type numeric span, Enter-
// commits/Escape-cancels/blur-commits, same convention ValueSlider's own
// edit mode already established. Deliberately NOT a ValueSlider itself:
// none of these fields are a modulatable port value to drag or scroll,
// just an editable display number — a plain click-to-type span is the
// whole interaction, no fill bar.
import { useRef, useState } from 'react'
import './EditableStat.css'

export interface EditableStatProps {
  label: string
  value: number
  /** How many decimals to show/round to when not editing — 0 (the
      default) for an integer-flavoured stat like Count's own Min/Max,
      pass more for a continuous one (Scope's own range/time-window).
  */
  decimals?: number
  /** Appended after the number when not editing (e.g. "s" for a time
      window) — never shown while the field is in edit mode, matching
      ValueSlider's own `unit` convention.
  */
  unit?: string
  /** Undefined (the M9 gallery's static entries, which have no live node
      instance to persist a value on) makes this read-only — clicking it
      does nothing, same "no live graph, no persistence" shape every other
      viewer body's gallery path already accepts.
  */
  onCommit?: (next: number) => void
}

export function EditableStat({ label, value, decimals = 0, unit = '', onCommit }: EditableStatProps) {
  const [editing, setEditing] = useState(false)
  const resolvedRef = useRef(false)

  const commit = (raw: string): void => {
    if (resolvedRef.current) return
    resolvedRef.current = true
    const parsed = parseFloat(raw)
    if (!Number.isNaN(parsed)) onCommit?.(parsed)
    setEditing(false)
  }
  const cancel = (): void => {
    resolvedRef.current = true
    setEditing(false)
  }

  if (editing) {
    return (
      <input
        className="editable-stat-input"
        autoFocus
        defaultValue={value.toFixed(decimals)}
        onFocus={(e) => e.currentTarget.select()}
        onMouseDown={(e) => e.stopPropagation()}
        onClick={(e) => e.stopPropagation()}
        onBlur={(e) => commit(e.currentTarget.value)}
        onKeyDown={(e) => {
          if (e.key === 'Enter') commit(e.currentTarget.value)
          else if (e.key === 'Escape') cancel()
        }}
      />
    )
  }

  return (
    <span
      className={`editable-stat${onCommit ? ' editable-stat-editable' : ''}`}
      onMouseDown={(e) => e.stopPropagation()}
      onClick={(e) => {
        e.stopPropagation()
        if (!onCommit) return
        resolvedRef.current = false
        setEditing(true)
      }}
    >
      {label}
      {label ? ': ' : ''}
      {value.toFixed(decimals)}
      {unit}
    </span>
  )
}
