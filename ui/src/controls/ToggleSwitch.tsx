// A real Boolean in-node control (wiki/plans/PropsAndMacroRedesign.md Batch
// B) — the Boolean counterpart to ValueSlider (numeric) and TriggerSelect
// (discrete list) as an in-node fallback: NodeCard.tsx's PortRow renders
// this for a classifyPortUiKind()==='boolean' editable input, ParameterRow
// renders it for a `kind: 'bool'` parameter. Before this existed, a Boolean
// port declaring `hasFallbackWhenUnconnected = true` (env.adsr's own
// "gate" — confirmed by reading AdsrNode.h) still fell through to a plain,
// non-interactive PortLabel, since `hasNumericFallback()` only recognised
// the modulation/value/integer kinds.
//
// Same pill shell/sizing convention as ValueSlider/TriggerSelect (height:
// 20px, border/radius tokens) so every in-node fallback control reads as
// one visual language regardless of which kind of value it edits. Click
// anywhere on the row to flip — a boolean has no "amount" to drag or type,
// same reasoning TriggerSelect's own header comment gives for being purely
// click-driven.
//
// Stored/committed as a plain 0/1 number via the same onCommit(value)
// contract every other in-node control already uses (ValueSlider,
// TriggerSelect) — a boolean fits the existing Record<string, number>
// value bag with no new shape needed anywhere upstream.
import { useState } from 'react'
import './ToggleSwitch.css'
import { tokens, withAlpha } from '../theme/tokens'

export interface ToggleSwitchProps {
  label: string
  value: number
  color: string
  /** Omitted (gallery/demo call sites): still a real, clickable control —
      see TriggerSelect's own uncontrolledIndex fallback for the same
      reasoning. */
  onCommit?: (value: number) => void
}

export function ToggleSwitch({ label, value, color, onCommit }: ToggleSwitchProps) {
  const [uncontrolledValue, setUncontrolledValue] = useState(value)
  const committedValue = onCommit ? value : uncontrolledValue
  const commitOut = onCommit ?? setUncontrolledValue
  const on = committedValue >= 0.5
  const resolvedBorderColor = withAlpha(color, tokens.opacity.border)

  return (
    <div
      className="toggle-switch"
      style={{ borderColor: resolvedBorderColor, color }}
      onClick={(e) => {
        e.stopPropagation()
        commitOut(on ? 0 : 1)
      }}
      onMouseDown={(e) => e.stopPropagation()}
      onContextMenu={(e) => e.stopPropagation()}
    >
      <span className="toggle-switch-label">{label}</span>
      <span
        className={`toggle-switch-track${on ? ' toggle-switch-track-on' : ''}`}
        style={on ? { borderColor: resolvedBorderColor, background: color } : { borderColor: resolvedBorderColor }}
      >
        <span className="toggle-switch-thumb" />
      </span>
    </div>
  )
}
