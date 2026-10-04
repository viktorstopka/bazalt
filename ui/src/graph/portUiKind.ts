// NODE_EDITOR.md §5 / blueprint §4's 6-colour port palette, derived from
// the engine's SignalType plus PortDescriptor's numeric metadata rather
// than being a separate type system the engine and UI could drift apart on.
import type { PortDescriptor, Quantity, SignalType } from './descriptorTypes'
import type { PortMultiplicityInfo } from './graphCommands'
import type { ConnectionEndpoint } from './canConnect'
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

/** SignalType::Control ports tagged Quantity::Unipolar/Bipolar render as
    Modulation (orange); anything else numeric renders as Value (white), or
    Integer (yellow) if isInteger is set. See NODE_EDITOR.md §5.

    wiki/plans/PropsAndMacroRedesign.md Batch A5: this used to be a numeric
    coincidence (`!unit && minValue===0 && maxValue===1`) that never read
    `quantity` at all — wiki/NODES.System.md §2 is explicit that "this
    [Unipolar/Bipolar] distinction, not the signal type, is what the UI
    colours," so a genuinely Bipolar -1..1 port (which that old check
    missed entirely) now gets the Modulation colour it was always supposed
    to, and the colour no longer depends on a port coincidentally declaring
    the literal bounds 0/1.

    `isPoly` (default false) picks Audio's Scalar-vs-Poly colour split
    (wiki/plans/DomainRedesign.md Batch 4) — irrelevant to every other
    branch, since Multiplicity is Audio-only for now (§8's still-open
    question on Control/other types isn't resolved here). Callers with no
    live multiplicity data (the gallery/mock case) pass the mock-only
    `port.isPolyPlaceholder` field through as this same argument.
*/
export function classifyPortUiKind(
  port: Pick<PortDescriptor, 'type' | 'isInteger' | 'quantity'>,
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

  if (port.quantity === 'unipolar' || port.quantity === 'bipolar') return 'modulation'

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
  port: Pick<PortDescriptor, 'type' | 'isInteger' | 'quantity'>,
  isPoly = false
): PortUiStyle {
  return PORT_UI_STYLE[classifyPortUiKind(port, isPoly)]
}

/** wiki/plans/PropsAndMacroRedesign.md Batch C: a polymorphic port
    (`polymorphism !== 'none'` — view.glance, deco.reroute, logic.select,
    adapt.sampleHold, view.scope/meter) with nothing resolvable feeding it
    yet has no real type to colour by — its declared default (e.g. Glance's
    own Audio) is just a fallback for the compiler, not a claim "this only
    takes Audio." graphStore.ts's endpointFor() already computes exactly
    this as `unresolved: true`; this is the one place that turns it into
    the right colour (Value/white) instead of classifying the stale
    default — used by both the canvas's live cable colouring and
    NodeCard.tsx's own port dot, so the two always agree (closing a real
    asymmetry: the cable used to recolour live while the node's own dot
    stayed frozen at the static per-typeId descriptor).
*/
export function portUiStyleForEndpoint(endpoint: Pick<ConnectionEndpoint, 'port' | 'unresolved'>, isPoly = false): PortUiStyle {
  if (endpoint.unresolved) return PORT_UI_STYLE.value
  return portUiStyle(endpoint.port, isPoly)
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
    carry no SignalType (CLAUDE.md's interim-simplifications note) — but
    they DO carry the same `kind`/`isInteger`/`quantity` value-contract
    fields every port does (M14), via `isBool`/`isInteger`/`quantity`
    (ParameterRow's own already-derived locals; NodeCard.tsx threads them
    through rather than re-deriving `isBool` from a raw `kind` string
    here). This used to ignore all three and guess from `unit` alone ("no
    unit, or '%' -> orange, else white") — direct feedback, 2026-10-03:
    "Distribution in Random should be enum/int, mode in Clip as well...
    there are SO MANY mistypes regarding color". Checked directly:
    `random.stepped.distribution` and `shape.clip.mode` both ALREADY
    declare `.isInteger = true, .kind = ValueKind::Enum` engine-side — the
    data was always right, this just never looked at it, so EVERY
    enum/int/bool structural parameter in the whole catalog rendered
    Modulation-orange (none of them carry a unit) regardless of how
    correctly it was tagged, and a genuinely unitless plain float rendered
    the same wrong orange too. Mirrors `classifyPortUiKind`'s own
    Control-branch priority exactly (isInteger checked before quantity) —
    an Enum parameter colours the same Integer-yellow a real Enum PORT
    does (no separate "enum" hue exists anywhere in this palette); every
    enum parameter in this codebase that sets `kind: Enum` also sets
    `isInteger: true` alongside it, so checking `isInteger` alone already
    covers it, same as the port side.
*/
export function classifyParameterUiKind(p: { isBool: boolean; isInteger: boolean; quantity: Quantity }): PortUiKind {
  if (p.isBool) return 'boolean'
  if (p.isInteger) return 'integer'
  if (p.quantity === 'unipolar' || p.quantity === 'bipolar') return 'modulation'
  return 'value'
}

export function parameterUiColor(p: { isBool: boolean; isInteger: boolean; quantity: Quantity }): string {
  return PORT_UI_STYLE[classifyParameterUiKind(p)].color
}
