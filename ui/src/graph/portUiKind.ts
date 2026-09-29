// NODE_EDITOR.md §5 / blueprint §4's 6-colour port palette, derived from
// the engine's SignalType plus PortDescriptor's numeric metadata rather
// than being a separate type system the engine and UI could drift apart on.
import type { PortDescriptor, SignalType } from './descriptorTypes'
import type { PortMultiplicityInfo } from './graphCommands'
import { tokens } from '../theme/tokens'

// wiki/plans/DomainRedesign.md Batch 4: 'audio' splits into 'audio-scalar'/
// 'audio-poly' — both real PortUiKind entries now, replacing the ad-hoc
// `port.isPolyPlaceholder ? tokens.color.portPoly : ...` override that used
// to sit outside this classifier at every call site (NodeCard.tsx had 5 of
// them). Multiplicity (Scalar vs. Poly) is a live per-port RESOLVED fact
// from MultiplicityResolver, not a static PortDescriptor field the way
// SignalType is — so it can't be read off `port` alone the way every other
// branch below is; callers pass it in as `isPoly`.
export type PortUiKind = 'audio-scalar' | 'audio-poly' | 'modulation' | 'value' | 'integer' | 'trigger' | 'boolean' | 'note' | 'data'

/** SignalType::Control ports with no unit and a 0-1 range render as
    Modulation (orange); anything else numeric renders as Value (white),
    or Integer (yellow) if isInteger is set. See NODE_EDITOR.md §5.

    `isPoly` (default false) picks Audio's Scalar-vs-Poly colour split
    (wiki/plans/DomainRedesign.md Batch 4) — irrelevant to every other
    branch, since Multiplicity is Audio-only for now (§8's still-open
    question on Control/other types isn't resolved here). Callers with no
    live multiplicity data (the gallery/mock case) pass the mock-only
    `port.isPolyPlaceholder` field through as this same argument.
*/
export function classifyPortUiKind(
  port: Pick<PortDescriptor, 'type' | 'unit' | 'minValue' | 'maxValue' | 'isInteger'>,
  isPoly = false
): PortUiKind {
  const type: SignalType = port.type

  if (type === 'audio') return isPoly ? 'audio-poly' : 'audio-scalar'
  if (type === 'event') return 'trigger'
  if (type === 'boolean') return 'boolean'
  // wiki/NODES_Gaps.md's Note-port-connectivity finding: Note used to fall
  // through the generic "unknown type" branch below into 'value' — the
  // exact same white a real-quantity Control port renders as, so wiring
  // Note In's output into an incompatible-but-same-coloured Control input
  // looked like "same colour won't connect" when the real story was a
  // genuine, correctly-rejected type mismatch wearing a borrowed colour.
  if (type === 'note') return 'note'
  // Direct feedback, the same collision Note's own fix above already
  // named: Data used to fall through to 'value' below, the exact white a
  // real-quantity Control port renders as - a Data(scale)/Data(curve) port
  // looked like an ordinary numeric control, no visible sign it needed a
  // Data-tagged source specifically. Spectral has no real port anywhere in
  // the catalog yet (still reserved/unimplemented, wiki/NODES.md's
  // "Deliberately deferred" section) - nothing to fix live for it, so it
  // stays on the 'value' fallback below rather than inventing a colour for
  // a type nothing produces.
  if (type === 'data') return 'data'
  if (type !== 'control') return 'value' // Spectral: reserved, no real port exists yet (§5); fall back rather than crash

  if (port.isInteger) return 'integer'

  // Direct feedback: an UNDECLARED range must not be treated the same as a
  // genuinely-declared 0-1 one — a port with no minValue/maxValue at all
  // (adapt.remap's "out", math.add/math.multiply's "out", any node whose
  // output range is inherently context-dependent) was rendering as
  // Modulation-orange purely because null happened to satisfy this check,
  // not because it's actually a normalised 0-1 signal. "They're just
  // numbers" (white/Value) is the correct default for an unknown range;
  // Modulation is reserved for a port that actually declares 0-1.
  const isNormalisedZeroToOne = port.minValue === 0 && port.maxValue === 1
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
  'audio-scalar': { color: tokens.color.portAudio, glyph: '→' },
  'audio-poly': { color: tokens.color.portAudioPoly, glyph: '→' },
  modulation: { color: tokens.color.portModulation, glyph: '→' },
  value: { color: tokens.color.portValue, glyph: '→' },
  integer: { color: tokens.color.portInteger, glyph: '→' },
  trigger: { color: tokens.color.portTrigger, glyph: '!' },
  boolean: { color: tokens.color.portBoolean, glyph: '?' },
  note: { color: tokens.color.portNote, glyph: '♪' }, // eighth note (♪) — distinct from every arrow/!/? glyph above
  data: { color: tokens.color.portData, glyph: '≡' }, // stacked lines — "many values", distinct from every glyph above
}

export function portUiStyle(
  port: Pick<PortDescriptor, 'type' | 'unit' | 'minValue' | 'maxValue' | 'isInteger'>,
  isPoly = false
): PortUiStyle {
  return PORT_UI_STYLE[classifyPortUiKind(port, isPoly)]
}

/** Scalar-vs-Poly for one port, live per-port multiplicity data first
    (wiki/plans/DomainRedesign.md Batch 4's graphGetNodeMultiplicity, keyed
    by port id), the mock-only `port.isPolyPlaceholder` placeholder as the
    fallback when there's no live map at all (the gallery, or outside the
    real WebView) — shared by every consumer that colours a port/cable by
    multiplicity (NodeCard.tsx's port glyphs/labels/sliders, InfiniteCanvas.tsx's
    cable colouring). A port id genuinely missing from a present map (a
    growable-group member beyond the throwaway default the engine computed
    this from) falls back to any other listed port of the same node —
    GraphEditController.h's own comment: every ordinary node's ports
    resolve uniformly.
*/
export function resolvePortIsPoly(port: Pick<PortDescriptor, 'id' | 'isPolyPlaceholder'>, multiplicity?: ReadonlyMap<string, PortMultiplicityInfo>): boolean {
  if (!multiplicity) return port.isPolyPlaceholder ?? false
  const info = multiplicity.get(port.id) ?? multiplicity.values().next().value
  return info?.kind === 'poly'
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
