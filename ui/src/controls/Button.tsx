// wiki/plans/PropsAndMacroRedesign.md Batch E / design/Macro.png: a real,
// reusable button primitive — until now nothing in the design system was a
// plain clickable button (every existing control is a value editor:
// ValueSlider/TriggerSelect/ToggleSwitch). First callers are the Macro
// node's own "Edit T" (opens MacroEditTypeModal) and "Trigger" (fires the
// macro's event) rows, but this carries no Macro-specific logic itself —
// same "reusable component, not a one-off" instruction ToggleSwitch's own
// Batch B already followed.
import type { ReactNode } from 'react'
import './Button.css'

export interface ButtonProps {
  label: ReactNode
  onClick?: () => void
  /** Border/text colour — defaults to a neutral grey (`--color-textSecondary`)
      via CSS when omitted, matching "Edit T"'s own neutral look in the
      reference image (unlike the type-coloured rows around it). */
  color?: string
  title?: string
  disabled?: boolean
  /** The whole row is the button (design/Macro.png's "Trigger" row) rather
      than an inline-sized pill next to other content (the type chip's own
      "Edit T"). */
  fullWidth?: boolean
}

export function Button({ label, onClick, color, title, disabled, fullWidth }: ButtonProps) {
  return (
    <button
      type="button"
      className={`ui-button${fullWidth ? ' ui-button-full' : ''}`}
      style={color ? { borderColor: color, color } : undefined}
      onClick={(e) => {
        e.stopPropagation()
        onClick?.()
      }}
      onMouseDown={(e) => e.stopPropagation()}
      onContextMenu={(e) => e.stopPropagation()}
      title={title}
      disabled={disabled}
    >
      {label}
    </button>
  )
}
