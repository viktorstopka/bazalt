// NODE_EDITOR.md §5 / blueprint §4's 6-colour port palette, derived from
// the engine's SignalType plus PortDescriptor's numeric metadata rather
// than being a separate type system the engine and UI could drift apart on.
import type { PortDescriptor, SignalType } from './descriptorTypes'
import { tokens } from '../theme/tokens'

export type PortUiKind = 'audio' | 'modulation' | 'value' | 'integer' | 'trigger' | 'boolean'

/** SignalType::Control ports with no unit and a 0-1 range render as
    Modulation (orange); anything else numeric renders as Value (white),
    or Integer (yellow) if isInteger is set. See NODE_EDITOR.md §5.
*/
export function classifyPortUiKind(port: Pick<PortDescriptor, 'type' | 'unit' | 'minValue' | 'maxValue' | 'isInteger'>): PortUiKind {
  const type: SignalType = port.type

  if (type === 'audio') return 'audio'
  if (type === 'event') return 'trigger'
  if (type === 'boolean') return 'boolean'
  if (type !== 'control') return 'value' // Note/Spectral: no UI rendering defined yet (§5); fall back rather than crash

  if (port.isInteger) return 'integer'

  const isNormalisedZeroToOne = (port.minValue === null || port.minValue === 0) && (port.maxValue === null || port.maxValue === 1)
  if (!port.unit && isNormalisedZeroToOne) return 'modulation'

  return 'value'
}

export interface PortUiStyle {
  color: string
  /** The type glyph (blueprint §4's table) — Audio/Modulation/Value/Integer
      all use an arrow, Trigger uses "!", Boolean uses "?". Direction
      (input vs. output) is a separate concern: see NodeCard's row layout,
      which places this glyph before the label for inputs, after for
      outputs, matching the reference's "→ By" vs. "Pitch →" convention.
  */
  glyph: string
}

export const PORT_UI_STYLE: Record<PortUiKind, PortUiStyle> = {
  audio: { color: tokens.color.portAudio, glyph: '→' },
  modulation: { color: tokens.color.portModulation, glyph: '→' },
  value: { color: tokens.color.portValue, glyph: '→' },
  integer: { color: tokens.color.portInteger, glyph: '→' },
  trigger: { color: tokens.color.portTrigger, glyph: '!' },
  boolean: { color: tokens.color.portBoolean, glyph: '?' },
}

export function portUiStyle(port: Pick<PortDescriptor, 'type' | 'unit' | 'minValue' | 'maxValue' | 'isInteger'>): PortUiStyle {
  return PORT_UI_STYLE[classifyPortUiKind(port)]
}

/** Parameters (ParameterDescriptor) aren't graph-connectable ports and
    carry no SignalType (CLAUDE.md's interim-simplifications note), but the
    design reference (docs/Slice 1 (1).png) still colours them by the same
    Modulation/Value split §5 uses for ports: a ratio-like quantity (no
    unit, or a "%" unit) renders Modulation-orange; a real-world unit (Hz,
    ms, s, st, ...) renders Value-white. Best-effort — there's no min/max-
    derived signal here the way there is for ports, just the unit string.
*/
export function parameterUiColor(unit: string): string {
  return unit === '' || unit === '%' ? tokens.color.portModulation : tokens.color.portValue
}
