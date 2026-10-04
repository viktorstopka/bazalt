// Renders one node instance purely from a NodeDescriptor (NODE_EDITOR.md
// §3: "the UI never hardcodes a node type") plus a display-state bag —
// this is the same component the M10 canvas and the M9 gallery both use
// (gallery imports it directly). Pure DOM/CSS — ADR-0008's "hybrid
// WebGL-background + DOM-overlay" node body approach is deliberately
// deferred; see ADR-0008's Amendment (M10) for why DOM stays the layout
// source of truth.
import { Fragment, useMemo, useState, type ReactNode } from 'react'
import type { NodeDescriptor, ParameterDescriptor, PortDescriptor, Quantity } from '../graph/descriptorTypes'
import { classifyPortUiKind, portUiStyle, portUiStyleForEndpoint, parameterUiColor, resolvePortIsPoly, type PortUiStyle } from '../graph/portUiKind'
import { getEndpoint } from '../graph/graphStore'
import type { NodeMultiplicityBadge, PortMultiplicityInfo } from '../graph/graphCommands'
import { tokens } from '../theme/tokens'
import { ValueSlider } from './ValueSlider'
import { resolveSkew } from './sliderCurve'
import { TriggerSelect } from './TriggerSelect'
import { ToggleSwitch } from '../controls/ToggleSwitch'
import { NodePreview } from './NodePreview'
import { frameTypeForPreviewKind } from '../graph/previewSubscriptions'
import { withRevealedGroupPorts } from '../graph/portGroups'
import { MacroBody, ConstantBody } from './MacroBody'
import { RippleBody } from './RippleBody'
import { CountBody } from './CountBody'
import { ScopeControlBody } from './ScopeControlBody'
import { ScopeModulationBody } from './ScopeModulationBody'
import { GateBody } from './GateBody'
import { MapDiagram } from './MapDiagram'
import { CycleBody } from './CycleBody'
import './NodeCard.css'

/** PortDescriptor.h's own contract: "falls back to id in the UI if empty" —
    several real ports/parameters never got an explicit label/displayName
    filled in (osc.analog's "out", math.add's "a"/"b", ...), which used to
    render as a raw, inconsistently-cased id. This turns any such id/
    camelCase-boundary into a consistent Title Case display string (never
    touches an explicit label/displayName when one is set) — direct
    feedback: "I would like for EVERY port to be consistently named". Not
    applied to portUiKind.ts's own case-insensitive name-matching (that
    already lowercases both sides itself for that unrelated purpose).
*/
function humanizeId(id: string): string {
  const spaced = id.replace(/([a-z0-9])([A-Z])/g, '$1 $2')
  return spaced.charAt(0).toUpperCase() + spaced.slice(1)
}

export interface NodeCardState {
  selected?: boolean
  bypassed?: boolean
  listening?: boolean
  /** Error message; presence alone drives the red-circle-"!" badge next to
      the title (blueprint §4's "filled red circle with '!'", distinct from
      the bare violet trigger glyph).
  */
  error?: string
  /** Port ids currently wired to something, on either side — drives the
      dot-vs-typed-glyph swap (see PortGlyph below). GraphSurface.tsx
      computes this from the live graphStore wires for the real canvas; the
      M9 gallery passes hand-picked demonstration ids instead.
  */
  connectedPortIds?: ReadonlySet<string>
  /** Demo readout value shown for a connected port, since the gallery has
      no live telemetry to read a real one from. The live canvas doesn't
      pass one yet (M11's job — real per-port telemetry doesn't exist as of
      M10, see CLAUDE.md's TelemetryHub note).
  */
  demoConnectedValue?: string
  /** Per-instance overrides of a parameter's or unconnected input port's
      own descriptor-declared defaultValue, keyed by parameter/port id —
      what makes the ValueSlider rows below actually persist a dragged/
      typed value instead of only ever showing the type's static default.
      GraphSurface.tsx reads/writes this through graphStore.ts's
      `setParameterValue`; the M9 gallery never passes it (sliders there
      fall back to their own uncontrolled local state — see ValueSlider.tsx).
  */
  parameterValues?: Readonly<Record<string, number>>
  onParameterCommit?: (id: string, value: number) => void
  /** Per-port Scalar/Poly resolution for this node's LAST successful
      compile (wiki/plans/DomainRedesign.md Batch 4, DomainDot's real
      replacement) — graphStore.ts's `multiplicity` snapshot field,
      GraphSurface.tsx passes this node's own `ports` map straight through.
      Undefined for the M9 gallery (no live graph) and for a node the
      engine hasn't compiled into any plan yet (mid-edit, or a rejected
      command's rolled-back state) — every isPoly-consuming call site below
      (see resolvePortIsPoly()) falls back to the descriptor's own mock-only
      `port.isPolyPlaceholder` field in that case. A port id genuinely
      missing from a present map (a growable-group member beyond the
      throwaway default the engine computed this from) falls back to any
      other listed port of this same node — GraphEditController.h's own
      comment: every ordinary node's ports resolve uniformly.
  */
  portMultiplicity?: ReadonlyMap<string, PortMultiplicityInfo>
  /** Live activeCount/maxCount for an "instance.allocate.voice" node, read
      fresh off the processor on every graph resync — renders as
      InstanceCountBadge's "3/8" corner readout. Undefined for every other
      node type, and for the M9 gallery.
  */
  instanceCountBadge?: NodeMultiplicityBadge
  /** design/Macro.png / wiki/plans/PropsAndMacroRedesign.md Batch E —
      MacroBody.tsx's own Row 2/Edit-T enum-option-label field. Undefined
      for every node except a live util.macro instance; the M9 gallery's
      static util.macro entry has no instanceId at all, so MacroBody.tsx
      guards this behind `instanceId` rather than assuming it's present. */
  macroEnumOptionLabels?: readonly string[]
  onSetMacroEnumOptionLabels?: (labels: readonly string[]) => void
  /** design/Visualization/Count.png's editable Min/Max footer — undefined
      until the user has typed one in (GraphNode.countMinOverride/
      countMaxOverride's own doc comment has the full reasoning); undefined
      for every node except a live view.count instance, same guard shape
      macroEnumOptionLabels above already uses. */
  countMinOverride?: number
  countMaxOverride?: number
  onSetCountMin?: (value: number) => void
  onSetCountMax?: (value: number) => void
  /** design/Visualization/Scope1.png's editable vertical-range footer —
      the generic equivalent of countMinOverride/onSetCountMin above, for
      ANY viewer with an auto-ranging display scale (ScopeHistoryBody.tsx,
      shared across every view.scope.* variant and view.gate).
      A single, type-id-agnostic property key pair rather than one bespoke
      pair per viewer type — see graphStore.ts's setViewerRangeMin/Max.
      Undefined for every other node type, and for a viewer whose range has
      never been edited (ScopeHistoryBody.tsx's own auto-range/auto-freeze
      logic is what supplies a value in that case instead). */
  viewerRangeMinOverride?: number
  viewerRangeMaxOverride?: number
  onSetViewerRangeMin?: (value: number) => void
  onSetViewerRangeMax?: (value: number) => void
  /** design/Visualization/ScopeMod.png's editable centre line — see
      graphStore.ts's GraphNode.viewerCenterOverride. */
  viewerCenterOverride?: number
  onSetViewerCenter?: (value: number) => void
  /** Phase-locked preview playhead mode: 0 Auto (default), 1 On, 2 Off. */
  /** A slider mid-drag (value) or released (null) — streamed to the engine
      live, smoothed, without a recompile (graphStore.ts's setParameterLive). */
  onParameterLive?: (id: string, value: number | null) => void
  previewPlayheadMode?: number
  onSetPreviewPlayheadMode?: (mode: number) => void
  /** MacroEditTypeModal's own portal target (GraphSurface.tsx's
      `overlayTarget`, the screen-space `.infinite-canvas-overlay` div) —
      see MacroBody.tsx's own comment on why this can't just render inline:
      a plain `position: fixed` child of a transformed ancestor (the
      pan/zoom `.infinite-canvas-world` every node lives in) is positioned
      relative to THAT ancestor, not the viewport, same reason
      NodeContextMenu already portals through this exact prop. Undefined in
      the M9 gallery (no live canvas at all).
  */
  overlayTarget?: HTMLElement | null
}

interface NodeCardProps {
  descriptor: NodeDescriptor
  state?: NodeCardState
  /** M10 (GraphSurface.tsx): the live canvas instance's id, threaded down to
      every port/singleton glyph as `data-node-id`/`data-port-id`/
      `data-direction`/`data-port-anchor` so portAnchors.ts can measure each
      port's real screen position for cable drawing and wire-drag hit-
      testing. Omitted (the M9 gallery's every call site) — no data
      attributes are rendered and NodeCard's output is unchanged.
  */
  instanceId?: string
}

interface MergedRow {
  kind: 'merged'
  id: string
  input: PortDescriptor
  output: PortDescriptor
}
interface PortRowData {
  kind: 'port'
  direction: 'input' | 'output'
  port: PortDescriptor
}

/** Heuristic for "the primary input and primary output are the same
    property, just changed" (design-reference feedback: Predelay's single
    "Audio" row vs. Add's separate "a"/"b"/"out" rows). There's no explicit
    engine-side flag for this (PortDescriptor has isPrimaryOutput but no
    isPrimaryInput/samePropertyAs) — best-effort from naming until one
    exists: identical display name on both sides (Predelay's "Audio"/
    "Audio"), or the generic "in"/"out" pass-through naming every real
    single-audio-in/out M1/M2 node (delay.line, filter.svf,
    filter.onepole, instance.mix) happens to already use. Deliberately
    NOT keyed on SignalType matching — the reference says two different
    types are fine here, only the coloured glyphs differ per side.
*/
function isSameProperty(input: PortDescriptor, output: PortDescriptor): boolean {
  const inName = (input.label || input.id).toLowerCase()
  const outName = (output.label || output.id).toLowerCase()
  return inName === outName || (inName === 'in' && outName === 'out')
}

/** The "primary input" is the first-declared input (no explicit flag
    exists for this, unlike isPrimaryOutput — "first" matches how the
    feedback describes it: "in Add it is just the first number"). The
    primary output is the isPrimaryOutput-flagged one, falling back to the
    first. Only these two ever merge, and only when isSameProperty() holds
    — a node with more than one input (Add, Mix, VCA's gain) keeps every
    port on its own row unless its specific primary pair qualifies.
*/
function splitPorts(descriptor: NodeDescriptor): { merged: MergedRow | null; inputs: PortRowData[]; outputs: PortRowData[] } {
  // PortDescriptor.hidden (io.output's own "out" only, today): never a
  // candidate for the primary-output/merge logic below, and never rendered
  // as its own row either - a terminal "Master Out" node shouldn't show a
  // wireable output glyph at all, merged row or not.
  const visibleOutputs = descriptor.outputs.filter((o) => !o.hidden)
  const primaryInput = descriptor.inputs[0] as PortDescriptor | undefined
  const primaryOutput = (visibleOutputs.find((o) => o.isPrimaryOutput) ?? visibleOutputs[0]) as PortDescriptor | undefined

  const merged = primaryInput && primaryOutput && isSameProperty(primaryInput, primaryOutput)
    ? { kind: 'merged' as const, id: primaryInput.id, input: primaryInput, output: primaryOutput }
    : null

  return {
    merged,
    inputs: descriptor.inputs.filter((p) => p !== merged?.input).map((port) => ({ kind: 'port', direction: 'input', port })),
    outputs: visibleOutputs.filter((p) => p !== merged?.output).map((port) => ({ kind: 'port', direction: 'output', port })),
  }
}

/** The UI-facing name for PortDescriptor.hasFallbackWhenUnconnected — "can
    this input's value be set directly in the node, as an alternative to
    connecting a cable" (direct feedback: "we need to differentiate
    properties based on if they can be set manually or not... the dot
    literally means: I have a value, but if you want you can connect me").
    Four states fall out of this one flag crossed with `connected`:
      - not editable, unconnected -> typed glyph (an Audio input, or a
        Control input like mix.gain's "gain" with no setParameter behind it,
        always shows its arrow — "an Audio input will always have an
        arrow").
      - not editable, connected -> typed glyph (nothing changes on connect,
        there was nothing to fall back from).
      - editable, unconnected -> dot ("I have a value, but you can connect
        me").
      - editable, connected -> typed glyph (the cable overrides the
        in-node value).
    A ParameterDescriptor-backed row (ParameterRow, not PortRow) is a fifth,
    separate case ("not controllable" at all — no cable can ever attach) and
    was already handled correctly: it renders no glyph whatsoever, dot or
    arrow, because it isn't a port in the first place.

    This is the SAME flag GraphCompiler already reads to decide whether an
    unconnected input gets a NaN fallback-sentinel or plain silence
    (PortDescriptor.h's own comment) — reused here rather than inventing a
    parallel UI-only concept, per "the ideal implementation somehow
    systematically adds this somewhere in properties."
*/
function isEditableInNode(port: PortDescriptor): boolean {
  return port.hasFallbackWhenUnconnected
}

/** Whether an editable-in-node port's fallback is a plain numeric range
    (renders as a ValueSlider) as opposed to a discrete preset list (renders
    as a TriggerSelect — see PortRow below). Reuses classifyPortUiKind's
    existing numeric/audio/trigger/boolean split rather than inventing a
    second classification.
*/
function hasNumericFallback(port: PortDescriptor): boolean {
  const kind = classifyPortUiKind(port)
  return kind === 'modulation' || kind === 'value' || kind === 'integer'
}

/** wiki/plans/PropsAndMacroRedesign.md Batch C: a polymorphic port's own
    static descriptor only carries its unconnected DEFAULT type/colour
    (Glance's own "in"/"out" default to Audio-pink, even though it accepts
    Audio/Control/Boolean/Event) — the same live resolution
    InfiniteCanvas.tsx's cables already use (graphStore.ts's getEndpoint())
    is needed here too, closing a real asymmetry: a wired Glance's cable
    recoloured live while its own node-body dot stayed frozen at the stale
    default. No-ops (falls back to the plain static style) with no
    `instanceId` (the M9 gallery's static call sites, which have no live
    graph to resolve against).

    Unconditional on `port.polymorphism` now (direct feedback, 2026-10-03:
    "why is the macro value output not adapting to the color") — a
    util.macro/util.constant output's live type is a SEPARATE kind of
    "not really static" from wire-driven polymorphism (it follows the
    node's OWN structural parameters instead, TypedValueNodeBase.h), and
    was falling straight through to the frozen static descriptor here
    because its own `polymorphism` field is (correctly) 'none' — this
    node's shape was never wire-driven, so it never opted into THIS gate,
    even though it needed the exact same live lookup for a different
    reason. graphStore.ts's own endpointFor() is now the single place that
    decides "static / wire-resolved / config-resolved" for ANY port, so
    this function no longer needs to guess which case applies before
    asking it — every ordinary node's own endpointFor() call is an
    unchanged no-op lookup (same `port` object back), so this costs
    nothing for the overwhelming majority of ports it's now also called
    for.
*/
function resolvedPortStyle(port: PortDescriptor, instanceId: string | undefined, direction: 'input' | 'output', isPoly?: boolean) {
  if (instanceId) {
    const endpoint = getEndpoint(instanceId, port.id, direction)
    if (endpoint) return portUiStyleForEndpoint(endpoint, isPoly)
  }
  return portUiStyle(port, isPoly)
}

/** A port's glyph has two states, independent of its type colour: an
    unconnected, editable-in-node INPUT renders as a plain dot ("supposed to
    be a dot, more so than a capsule shape" — direct feedback) meaning
    "connectable, not connected, using its own default"; every other case
    (connected, an output, or an input with no in-node fallback at all)
    renders the normal typed glyph (arrow/!/?). This is entirely
    descriptor-driven — nothing here reads a node's typeId, so every
    current and future node gets this for free.
*/
/** Exported for MacroBody.tsx's own Row 4 (design/Macro.png): the output
    port row needs the exact same border-piercing glyph, with the exact same
    data-node-id/data-port-id/data-direction/data-port-anchor attributes
    portAnchors.ts reads to find a cable's real screen anchor — duplicating
    that positioning/attribute logic in a second file would be a real
    correctness risk (a drifted copy could silently break cable anchoring
    for every util.macro node), not just a style inconsistency. */
export function PortGlyph({
  port,
  side,
  instanceId,
  connected,
  isPoly,
  styleOverride,
}: {
  port: PortDescriptor
  side: 'left' | 'right'
  instanceId?: string
  connected: boolean
  isPoly?: boolean
  /** A viewer body whose whole panel is drawn in one type's colour
      (design/Visualization/ScopeMod.png: "Everything is orange") fixes its
      glyph to that style instead of the live-resolved one — a quantity-
      polymorphic port would otherwise read white while nothing is wired. */
  styleOverride?: PortUiStyle
}) {
  // "side" is a 1:1 proxy for direction at every call site in this file
  // (input always renders left, output always right).
  const direction = side === 'left' ? 'input' : 'output'
  const style = styleOverride ?? resolvedPortStyle(port, instanceId, direction, isPoly)
  const color = style.color
  const showDot = direction === 'input' && !connected && isEditableInNode(port)
  // Every port glyph is the SAME size everywhere, full stop — no per-row
  // variant (direct feedback: "the arrows vary in size... this needs to be
  // rehauled and done in a completely modular way"). A merged pass-through
  // row used to fake "this is one continuous property" by rendering a
  // visibly bigger glyph here; the design reference (Frame 1 Bazalt.png's
  // Predelay) actually does it with a connecting LINE between the label and
  // the arrow, same-size arrowhead — see MergedRowView's `.node-merged-line`
  // below, which is the real fix.
  const className = `node-port-glyph node-port-glyph-${side}${showDot ? ' node-port-glyph-dot' : ''}`
  return (
    <span
      className={className}
      style={{ color }}
      data-node-id={instanceId}
      data-port-id={instanceId ? port.id : undefined}
      data-direction={instanceId ? direction : undefined}
      data-port-anchor={instanceId ? '' : undefined}
    >
      {showDot ? '' : style.glyph}
    </span>
  )
}

function PortLabel({
  port,
  direction,
  instanceId,
  connected,
  demoValue,
  isPoly,
}: {
  port: PortDescriptor
  direction: 'input' | 'output'
  instanceId?: string
  connected: boolean
  demoValue?: string
  isPoly?: boolean
}) {
  const style = resolvedPortStyle(port, instanceId, direction, isPoly)
  return (
    <span className="node-port-label" style={{ color: style.color }}>
      {port.label || humanizeId(port.id)}
      {connected && <span className="node-port-live-value"> {demoValue}</span>}
    </span>
  )
}

/** minValue/maxValue (when a port genuinely declares them) are the real,
    physically-meaningful hard limit — passed to ValueSlider as
    hardMin/hardMax, which it never lets a value go past regardless of how
    it's entered. softMin/softMax (when declared) are just a comfortable
    default drag range — passed as ValueSlider's plain min/max, which only
    drives the fill bar and drag sensitivity, never clamps. A port with no
    declared bounds at all (common for a mock/demo port, or a real one like
    adapt.map's own range ports, which are genuinely unbounded) falls
    back to a plain 0-10 *visual* range and no hard clamp at all — direct
    feedback, citing Blender: a value should still be enterable past
    whatever the slider visually shows as its typical range.
*/
function PortRow({
  direction,
  port,
  connected,
  demoValue,
  instanceId,
  value,
  onCommit,
  onLiveChange,
  isPoly,
}: {
  direction: 'input' | 'output'
  port: PortDescriptor
  connected: boolean
  demoValue?: string
  instanceId?: string
  value?: number
  onCommit?: (value: number) => void
  onLiveChange?: (value: number | null) => void
  isPoly?: boolean
}) {
  // Only an unconnected, editable-in-node INPUT falls back to a shown
  // control — an output has nothing to "fall back" to (it always drives
  // whatever it's wired to, or nothing), and a non-editable input (Audio,
  // or a Control port with no in-node fallback) has nothing to show either,
  // just its plain label. Connecting it (direct feedback): "only the prop
  // label will stay, slider will disappear" — see PortLabel below,
  // unchanged. Which control renders depends on the *shape* of the
  // fallback: a numeric range gets a ValueSlider, a discrete preset list
  // (an event-type port with `options`, e.g. Random's Trigger dropdown)
  // gets a TriggerSelect instead, and a Boolean port gets a ToggleSwitch
  // (wiki/plans/PropsAndMacroRedesign.md Batch B — env.adsr's own "gate"
  // port already declares hasFallbackWhenUnconnected and had no in-node
  // control at all until this existed) — all three read/write the same
  // value/onCommit slot.
  const editable = direction === 'input' && !connected && isEditableInNode(port)
  const showSlider = editable && hasNumericFallback(port)
  const showToggle = editable && !showSlider && classifyPortUiKind(port, isPoly) === 'boolean'
  const showTriggerSelect = editable && !showSlider && !showToggle && port.type === 'event' && !!port.options?.length
  return (
    <div className={`node-row node-row-port node-row-${direction}`}>
      {direction === 'input' && <PortGlyph port={port} side="left" instanceId={instanceId} connected={connected} isPoly={isPoly} />}
      {showSlider ? (
        <ValueSlider
          label={port.label || humanizeId(port.id)}
          value={value ?? port.defaultValue}
          min={port.softMin ?? port.minValue ?? 0}
          max={port.softMax ?? port.maxValue ?? 10}
          hardMin={port.minValue ?? undefined}
          hardMax={port.maxValue ?? undefined}
          defaultValue={port.defaultValue}
          isInteger={port.isInteger}
          unit={port.unit}
          skew={resolveSkew(port.curve, port.quantity)}
          color={portUiStyle(port, isPoly).color}
          onCommit={onCommit}
          onLiveChange={onLiveChange}
        />
      ) : showToggle ? (
        <ToggleSwitch
          label={port.label || humanizeId(port.id)}
          value={value ?? port.defaultValue}
          color={portUiStyle(port, isPoly).color}
          onCommit={onCommit}
        />
      ) : showTriggerSelect ? (
        <TriggerSelect
          label={port.label || humanizeId(port.id)}
          options={port.options!}
          selectedIndex={value ?? port.defaultValue}
          color={portUiStyle(port, isPoly).color}
          onCommit={onCommit}
        />
      ) : (
        <PortLabel port={port} direction={direction} instanceId={instanceId} connected={connected} demoValue={demoValue} isPoly={isPoly} />
      )}
      {direction === 'output' && <PortGlyph port={port} side="right" instanceId={instanceId} connected isPoly={isPoly} />}
    </div>
  )
}

function MergedRowView({
  row,
  instanceId,
  connected,
  multiplicity,
}: {
  row: MergedRow
  instanceId?: string
  connected: boolean
  multiplicity?: ReadonlyMap<string, PortMultiplicityInfo>
}) {
  // The output's own label/id is normally the more meaningful name
  // (Predelay's "Audio"); fall back to the input's if the output never got
  // one. The line is ONE element spanning the row's true edge-to-edge width
  // (left:0/right:0, exactly like PortGlyph's own left:0/right:0 — same
  // border-piercing trick, same span) rather than two flex-grow segments
  // that stopped at the row's padding edge and left a gap before the glyph.
  // The label sits centred on top of it with an opaque background (again,
  // the same trick PortGlyph already uses to "cut" the border it sits on),
  // so the line reads as one continuous stroke broken only where the label
  // text covers it.
  const label = row.output.label || row.input.label || humanizeId(row.output.id)
  const isPolyOutput = resolvePortIsPoly(row.output, multiplicity)
  const lineColor = resolvedPortStyle(row.output, instanceId, 'output', isPolyOutput).color
  return (
    <div className="node-row node-row-port node-row-merged">
      <span className="node-merged-line" aria-hidden="true" style={{ background: lineColor }} />
      <PortGlyph port={row.input} side="left" instanceId={instanceId} connected={connected} isPoly={resolvePortIsPoly(row.input, multiplicity)} />
      <span className="node-port-label node-port-label-merged">{label}</span>
      <PortGlyph port={row.output} side="right" instanceId={instanceId} connected isPoly={isPolyOutput} />
    </div>
  )
}

/** A ParameterDescriptor left as a parameter (rather than being modelled as
    a connectable Control-type input port — see PortRow/hasNumericFallback
    above, and mockDescriptors.ts' own per-parameter reasoning) means it
    genuinely isn't meant to be modulated: an enum/dropdown-style choice, or
    a value where live modulation would be unsafe/meaningless. Direct
    feedback was explicit about this: such a value gets NO socket-like
    decoration at all — no tick mark, nothing pretending it's connectable.
    It's still a real, draggable/typeable ValueSlider though (just never a
    cable target) — "not connectable" and "not adjustable" are different
    questions; this row answers only the first one differently from a port.
*/
function ParameterRow({
  id,
  displayName,
  value,
  minValue,
  maxValue,
  softMin,
  softMax,
  defaultValue,
  isInteger,
  unit,
  quantity,
  skew,
  isBool,
  options,
  onCommit,
  onLiveChange,
}: {
  id: string
  displayName: string
  value: number
  minValue: number
  maxValue: number
  softMin?: number | null
  softMax?: number | null
  defaultValue?: number
  isInteger: boolean
  unit: string
  /** Colour input alongside isInteger/isBool below (classifyParameterUiKind,
      portUiKind.ts) — Modulation (orange) vs. Value (white) for an
      otherwise-plain numeric parameter, same Unipolar/Bipolar check a
      port's own colour already uses. */
  quantity: Quantity
  skew?: number
  /** `kind === 'bool'` (wiki/plans/PropsAndMacroRedesign.md Batch B) —
      renders a ToggleSwitch instead of a numeric ValueSlider. Closes the
      same gap PortRow's own boolean branch closes, for a structural
      ParameterDescriptor rather than a port (e.g. util.macro.isInteger
      already declares `kind: ValueKind::Bool` engine-side and used to
      render as a plain 0/1 slider for lack of this branch). Also this
      row's own colour input now (classifyParameterUiKind) — Boolean-blue
      takes priority over isInteger/quantity, same as a port's own Boolean
      SignalType always wins classifyPortUiKind's branch order.
  */
  isBool?: boolean
  options?: string[]
  onCommit?: (value: number) => void
  onLiveChange?: (value: number | null) => void
}) {
  const color = parameterUiColor({ isBool: isBool ?? false, isInteger, quantity })
  return (
    <div className="node-row node-row-parameter" key={id}>
      {options && options.length > 0 ? (
        <TriggerSelect label={displayName} options={options} selectedIndex={value} color={color} onCommit={onCommit} />
      ) : isBool ? (
        <ToggleSwitch label={displayName} value={value} color={color} onCommit={onCommit} />
      ) : (
        <ValueSlider
          label={displayName}
          value={value}
          min={softMin ?? minValue}
          max={softMax ?? maxValue}
          hardMin={minValue}
          hardMax={maxValue}
          defaultValue={defaultValue}
          isInteger={isInteger}
          unit={unit}
          skew={skew}
          color={color}
          onCommit={onCommit}
          onLiveChange={onLiveChange}
        />
      )}
    </div>
  )
}

/** wiki/plans/DomainRedesign.md Batch 4: DomainDot's real replacement — a
    small structural corner badge, not a title-bar element (see
    NodeCard.css's own comment for why it's positioned off .node-card
    itself instead). Renders only for an "instance.allocate.voice" node —
    every other node's `state.instanceCountBadge` is undefined, same as
    DomainDot used to render nothing for its own `undefined` domain.
*/
function InstanceCountBadge({ badge }: { badge?: NodeMultiplicityBadge }) {
  if (!badge) return null
  return (
    <span className="node-instance-count-badge" title={`${badge.activeCount} of ${badge.maxCount} voices active`}>
      {badge.activeCount}/{badge.maxCount}
    </span>
  )
}

function TitleBar({ descriptor, state }: { descriptor: NodeDescriptor; state: NodeCardState }) {
  return (
    <div className="node-title-bar">
      <span className="node-title-left">
        <span className="node-title">{descriptor.title || descriptor.typeId}</span>
      </span>
      <div className="node-title-icons">
        {state.error && (
          <span className="node-error-badge" title={state.error}>
            !
          </span>
        )}
        {/* wiki/plans/UtilMacro.md: a real util.macro node is an ordinary
            node in every way that matters here — its "Constraints" info-only
            icon (never had an onClick) is gone; bypass+assist render
            unconditionally now, same as every other category. */}
        <span className={`node-bypass-icon${state.bypassed ? ' node-bypass-icon-active' : ''}`} title="Bypass" />
        <span className="node-assist-icon" title="Assist menu">
          +
        </span>
      </div>
    </div>
  )
}

/** Per-instance value + commit callback for one parameter/port id, from the
    NodeCardState bag both StandardBody and HorizontalBody thread through
    to PortRow/ParameterRow — factored out so the "read the override, fall
    back to the descriptor's own default; wrap onParameterCommit for this
    one id" pattern only lives in one place.
*/
/** The labels a discrete parameter offers, or undefined for a continuous one.
    `options` is the mock descriptors' own list; a REAL engine enum parameter
    (osc shape, meter mode, FFT size, ...) declares `enumOptions` instead, and
    the UI used to ignore that and draw an integer slider. An enum parameter's
    value IS the index into its options - every real one starts at 0 - which is
    exactly what TriggerSelect reads and commits.
*/
function parameterOptions(p: ParameterDescriptor): string[] | undefined {
  if (p.options && p.options.length > 0) return p.options
  if (p.kind === 'enum' && p.enumOptions && p.enumOptions.length > 0) return p.enumOptions.map((o) => o.label)
  return undefined
}

function paramValue(state: NodeCardState, id: string, fallback: number): number {
  return state.parameterValues?.[id] ?? fallback
}
function paramCommit(state: NodeCardState, id: string): ((value: number) => void) | undefined {
  const commit = state.onParameterCommit
  return commit ? (value: number) => commit(id, value) : undefined
}

/** wiki/plans/UtilMacro.md's decided slot-reassignment warning: editing an
    ALREADY-PLACED macro's own "util.macro.slot" repoints a live host-
    automation binding (whatever was wired to the old slot's knob/host
    control silently stops reaching this node), so it gets a blocking
    confirm first — matching this project's existing "reject/confirm rather
    than silently do something risky" convention. This never fires during
    creation: createMacroFromPort/addNode's own internal slot claim goes
    straight through graphStore.ts's setParameterValue, never through this
    component's onCommit at all, so every commit ParameterRow actually
    fires here is, by construction, a user editing a macro that's already
    on the canvas.
*/
function macroSlotCommit(state: NodeCardState, id: string): ((value: number) => void) | undefined {
  const commit = paramCommit(state, id)
  if (!commit) return undefined
  return (value: number) => {
    const nextSlot = Math.round(value) + 1
    if (window.confirm(`Reassign this Macro to slot ${nextSlot}? Anything currently bound to its host control will no longer reach this node.`)) {
      commit(value)
    }
  }
}

function parameterRowCommit(descriptor: NodeDescriptor, state: NodeCardState, id: string): ((value: number) => void) | undefined {
  if (descriptor.typeId === 'util.macro' && id === 'util.macro.slot') return macroSlotCommit(state, id)
  return paramCommit(state, id)
}

/** A live diagram drawn between two of a standard node's own rows — the
    one thing a few nodes add to the ordinary row layout (design/Map.png's
    mapping diagram between "In Max" and "Out Min"), keyed by typeId the same
    way the bespoke bodies above are, but keeping every row, title and port
    exactly as StandardBody already renders them. `liveValues` carries a
    range slider's in-progress value mid-drag, before anything is committed,
    so the diagram follows the drag rather than jumping on release. */
interface InlineDiagram {
  afterPortId: string
  render: (props: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string; liveValues: Readonly<Record<string, number>> }) => ReactNode
}

const INLINE_DIAGRAMS: Readonly<Record<string, InlineDiagram>> = {
  'adapt.map': { afterPortId: 'adapt.map.inMax', render: (props) => <MapDiagram {...props} /> },
}

function StandardBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const { merged, inputs, outputs } = useMemo(() => splitPorts(descriptor), [descriptor])
  const connected = state.connectedPortIds ?? new Set<string>()
  const diagram = INLINE_DIAGRAMS[descriptor.typeId]
  const [liveValues, setLiveValues] = useState<Readonly<Record<string, number>>>({})
  // Every slider streams its in-progress value to the engine while dragged
  // (heard live, smoothed); a node with an inline diagram also redraws it.
  const liveChangeFor = (portId: string) => (value: number | null) => {
    state.onParameterLive?.(portId, value)
    if (diagram)
      setLiveValues((previous) => {
        const next = { ...previous }
        if (value === null) delete next[portId]
        else next[portId] = value
        return next
      })
  }

  return (
    <>
      {merged && <MergedRowView row={merged} instanceId={instanceId} connected={connected.has(merged.id)} multiplicity={state.portMultiplicity} />}
      {inputs.map((row) => (
        <Fragment key={row.port.id}>
          <PortRow
            direction="input"
            port={row.port}
            connected={connected.has(row.port.id)}
            demoValue={state.demoConnectedValue}
            instanceId={instanceId}
            value={paramValue(state, row.port.id, row.port.defaultValue)}
            onCommit={paramCommit(state, row.port.id)}
            onLiveChange={liveChangeFor(row.port.id)}
            isPoly={resolvePortIsPoly(row.port, state.portMultiplicity)}
          />
          {diagram?.afterPortId === row.port.id && diagram.render({ descriptor, state, instanceId, liveValues })}
        </Fragment>
      ))}
      {descriptor.parameters.map((p) => (
        <ParameterRow
          key={p.id}
          id={p.id}
          displayName={p.displayName || humanizeId(p.id)}
          value={paramValue(state, p.id, p.defaultValue)}
          minValue={p.minValue}
          maxValue={p.maxValue}
          softMin={p.softMin}
          softMax={p.softMax}
          defaultValue={p.defaultValue}
          isInteger={p.isInteger}
          unit={p.unit}
          quantity={p.quantity}
          skew={resolveSkew(p.curve, p.quantity, p.skew)}
          isBool={p.kind === 'bool'}
          options={parameterOptions(p)}
          onCommit={parameterRowCommit(descriptor, state, p.id)}
          onLiveChange={p.isStructural ? undefined : liveChangeFor(p.id)}
        />
      ))}
      {outputs.map((row) => (
        <PortRow
          key={row.port.id}
          direction="output"
          port={row.port}
          connected={connected.has(row.port.id)}
          demoValue={state.demoConnectedValue}
          instanceId={instanceId}
          isPoly={resolvePortIsPoly(row.port, state.portMultiplicity)}
        />
      ))}
      {instanceId && descriptor.previews?.map((preview) => (
        <NodePreview key={preview.portId} nodeId={instanceId} preview={preview} state={state} />
      ))}
    </>
  )
}

/** Static placeholder preview graphic — the fallback for a Horizontal-
    layout node with no previews[] entry, or one whose declared kind has no
    real telemetry producer yet (RollingHistory/ShapeWithPlayhead/etc., see
    NodePreview.tsx). A node with a real Waveform/Spectrum/Meter preview
    (M20 C5) renders NodePreview here instead.
*/
function PlaceholderPreview() {
  const bars = [0.3, 0.55, 0.4, 0.8, 0.6, 0.35, 0.5]
  return (
    <svg className="node-preview-placeholder" viewBox="0 0 70 32" preserveAspectRatio="none">
      {bars.map((h, i) => (
        <rect key={i} x={i * 10} y={32 - h * 32} width={7} height={h * 32} fill={tokens.color.portModulation} opacity={0.55} />
      ))}
    </svg>
  )
}

/** Horizontal layout variant (blueprint §4: "wide variant with inputs
    left, a large live preview centre, output value indicator next to the
    output"). This is the layout `HorizontalBody` originally got wrong
    (M10_REVIEW.md feedback: Random's input arrow rendered "inside" the
    node instead of on its border) — the cause was `.node-horizontal-body`
    carrying its own horizontal padding, which pushes the whole flex layout
    (including the input column) inward from the card's *true* edge before
    the shared PortGlyph/`.node-row-port` border-piercing trick (`left:0`
    lands on whatever its nearest padded ancestor's edge is, not
    necessarily the card's) ever runs. The fix is structural, not
    Random-specific, and applies to every current/future Horizontal node:
    `.node-horizontal-body` itself must carry ZERO horizontal padding, full
    stop — see the matching contract comment in NodeCard.css right above
    that rule. All the variant's own breathing room instead lives on
    `.node-horizontal-preview`, which never touches the card's edge.
*/
function HorizontalBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const connected = state.connectedPortIds ?? new Set<string>()
  const primaryOutput = descriptor.outputs.find((o) => o.isPrimaryOutput) ?? descriptor.outputs[0]
  return (
    <div className="node-horizontal-body">
      <div className="node-horizontal-column">
        {descriptor.inputs.map((port) => (
          <PortRow
            key={port.id}
            direction="input"
            port={port}
            connected={connected.has(port.id)}
            instanceId={instanceId}
            value={paramValue(state, port.id, port.defaultValue)}
            onCommit={paramCommit(state, port.id)}
            isPoly={resolvePortIsPoly(port, state.portMultiplicity)}
          />
        ))}
        {descriptor.parameters.map((p) => (
          <ParameterRow
            key={p.id}
            id={p.id}
            displayName={p.displayName || humanizeId(p.id)}
            value={paramValue(state, p.id, p.defaultValue)}
            minValue={p.minValue}
            maxValue={p.maxValue}
            softMin={p.softMin}
            softMax={p.softMax}
            defaultValue={p.defaultValue}
            isInteger={p.isInteger}
            unit={p.unit}
            quantity={p.quantity}
            skew={resolveSkew(p.curve, p.quantity, p.skew)}
            isBool={p.kind === 'bool'}
            options={parameterOptions(p)}
            onCommit={parameterRowCommit(descriptor, state, p.id)}
          />
        ))}
      </div>
      <div className={`node-horizontal-preview`}>
        {instanceId && descriptor.previews?.[0] && frameTypeForPreviewKind(descriptor.previews[0].kind) !== undefined ? (
          <NodePreview nodeId={instanceId} preview={descriptor.previews[0]} />
        ) : (
          <PlaceholderPreview />
        )}
      </div>
      {primaryOutput && (
        <div className="node-horizontal-column node-horizontal-column-output">
          <PortRow
            direction="output"
            port={primaryOutput}
            connected={connected.has(primaryOutput.id)}
            instanceId={instanceId}
            isPoly={resolvePortIsPoly(primaryOutput, state.portMultiplicity)}
          />
        </div>
      )}
    </div>
  )
}

function SingletonGlyph({
  port,
  direction,
  instanceId,
  connected,
  isPoly,
}: {
  port: PortDescriptor
  direction: 'input' | 'output'
  instanceId?: string
  connected: boolean
  isPoly?: boolean
}) {
  const style = resolvedPortStyle(port, instanceId, direction, isPoly)
  const color = style.color
  const showDot = direction === 'input' && !connected && isEditableInNode(port)
  // Deliberately NOT the border-piercing PortGlyph used elsewhere: adjacent
  // singletons are meant to visually chain (blueprint §4: "frames touch,
  // arrows join") — a singleton's own opaque background-patch notch would
  // paint over the neighbouring box's arrow tip where they meet, breaking
  // exactly the join effect this layout variant exists for. Still gets the
  // same dot-when-unconnected treatment as every other input, though.
  return (
    <span
      className={`node-singleton-glyph${showDot ? ' node-singleton-glyph-dot' : ''}`}
      style={{ color }}
      data-node-id={instanceId}
      data-port-id={instanceId ? port.id : undefined}
      data-direction={instanceId ? direction : undefined}
      data-port-anchor={instanceId ? '' : undefined}
    >
      {showDot ? '' : style.glyph}
    </span>
  )
}

function SingletonBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const connected = state.connectedPortIds ?? new Set<string>()
  const input = descriptor.inputs[0]
  const output = descriptor.outputs[0]
  return (
    <div className="node-singleton-body">
      {input && <SingletonGlyph port={input} direction="input" instanceId={instanceId} connected={connected.has(input.id)} isPoly={resolvePortIsPoly(input, state.portMultiplicity)} />}
      {/* `node-title` (shared with every other layout variant, not just
          `node-singleton-title`) is what lets GraphSurface.tsx's generic
          `.closest('.node-title')` double-click/rename targeting work here
          too — previously this layout had no element carrying that class
          at all, so rename silently did nothing on it. */}
      <span className="node-singleton-title node-title">{descriptor.title}</span>
      {output && <SingletonGlyph port={output} direction="output" instanceId={instanceId} connected={connected.has(output.id)} isPoly={resolvePortIsPoly(output, state.portMultiplicity)} />}
    </div>
  )
}


/** A minimal ear glyph, no box/border/title — blueprint §4/`Frame 1
    Bazalt.png`'s "Fun Ear Preview Node. Called 'Listen'": view.listen
    doesn't look like an ordinary node at all, just an icon a cable can
    terminate at. Driven by `icon === 'ear'` (ListenNode::getIcon(),
    NODE_EDITOR.md §3) rather than a hardcoded type-id check, so any future
    node marking itself this way gets the same treatment for free.
*/
function EarIcon({ title }: { title: string }) {
  return (
    <svg className="node-ear-icon" width="30" height="30" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
      <title>{title}</title>
      <path d="M8 14.5c-1.6-1.6-2.5-3.5-2.5-5.5a6.5 6.5 0 1 1 11 4.7c-1 1-1.5 2-1.5 3.3v.5a2.5 2.5 0 0 1-5 0" strokeLinecap="round" strokeLinejoin="round" />
      <path d="M9 9a2 2 0 0 1 4 0c0 1-.6 1.4-1.2 1.9-.5.4-.8.7-.8 1.4" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  )
}

/** Milestone 0.6 (wiki/NODES_Gaps.md's `single-type-preview-coverage`
    finding): the minimal inline preview layout — no title, no parameter
    list, just an input glyph, a compact live preview, and an output glyph.
    Reuses `SingletonGlyph` (it was already generic, not singleton-specific
    in what it actually renders) and the same `NodePreview`/`PlaceholderPreview`
    pairing `HorizontalBody` uses, just laid out smaller and without the
    title/parameter column either of those carries.
*/
function GlanceBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const connected = state.connectedPortIds ?? new Set<string>()
  const input = descriptor.inputs[0]
  const output = descriptor.outputs[0]
  const preview = descriptor.previews?.[0]
  return (
    <div className="node-glance-body">
      {input && <SingletonGlyph port={input} direction="input" instanceId={instanceId} connected={connected.has(input.id)} isPoly={resolvePortIsPoly(input, state.portMultiplicity)} />}
      <div className="node-glance-preview">
        {instanceId && preview && frameTypeForPreviewKind(preview.kind) !== undefined ? (
          <NodePreview nodeId={instanceId} preview={preview} />
        ) : (
          <PlaceholderPreview />
        )}
      </div>
      {output && <SingletonGlyph port={output} direction="output" instanceId={instanceId} connected={connected.has(output.id)} isPoly={resolvePortIsPoly(output, state.portMultiplicity)} />}
    </div>
  )
}

function DecorationBody({ descriptor }: { descriptor: NodeDescriptor }) {
  if (descriptor.typeId === 'util.reroute') return <div className="node-knob" title="Reroute" />
  if (descriptor.icon === 'header') return <span className="node-header-label node-title">{descriptor.title}</span>
  if (descriptor.icon === 'image')
    return (
      <div className="node-image-placeholder">
        <span className="node-title">{descriptor.title}</span>
      </div>
    )
  // 'frame' (default): a tinted backdrop with its label above the top-left corner, per the reference's "Bass" frame.
  return (
    <div className="node-frame">
      <span className="node-frame-label node-title">{descriptor.title}</span>
    </div>
  )
}

export function NodeCard({ descriptor: declaredDescriptor, state = {}, instanceId }: NodeCardProps) {
  // A placed node's growable port groups (math.add, mix.sum, ...) show every
  // wired port plus one spare to drop the next cable on — the engine sizes the
  // group from the connections, and this mirrors it (portGroups.ts). Only for a
  // live instance: the gallery has no connections, so it keeps the default
  // minimum-size descriptor exactly as before.
  const descriptor = instanceId ? withRevealedGroupPorts(declaredDescriptor, state.connectedPortIds) : declaredDescriptor

  if (descriptor.icon === 'ear') return <EarIcon title={descriptor.title} />
  // design/Macro.png / wiki/plans/PropsAndMacroRedesign.md Batch E: a
  // bespoke body, keyed by typeId exactly like DecorationBody's own
  // util.reroute special-case below — util.macro's REAL engine descriptor
  // still reports an ordinary layoutVariant ('standard'); this is a
  // client-side-only visual replacement, no engine change needed or made.
  if (descriptor.typeId === 'util.macro') return <MacroBody descriptor={descriptor} state={state} instanceId={instanceId} />
  // util.constant: the same design/Macro.png card without the macro-only
  // parts (MacroBody.tsx's ConstantBody).
  if (descriptor.typeId === 'util.constant') return <ConstantBody descriptor={descriptor} state={state} instanceId={instanceId} />
  // design/Visualization/Ripple.png: same client-side-only typeId dispatch
  // as util.macro above — view.ripple's real engine descriptor reports an
  // ordinary Glance layoutVariant; this is a visual swap only.
  if (descriptor.typeId === 'view.ripple') return <RippleBody descriptor={descriptor} state={state} instanceId={instanceId} />
  // design/Visualization/Count.png: same client-side-only typeId dispatch
  // as view.ripple just above — view.count's real engine descriptor
  // reports an ordinary Glance layoutVariant too; this is a visual swap
  // only.
  if (descriptor.typeId === 'view.count') return <CountBody descriptor={descriptor} state={state} instanceId={instanceId} />
  // design/Visualization/Scope1.png: same client-side-only typeId dispatch
  // as view.ripple/view.count just above.
  if (descriptor.typeId === 'view.scope.control') return <ScopeControlBody descriptor={descriptor} state={state} instanceId={instanceId} />
  // design/Visualization/ScopeMod.png and Gate.png: the other two variants
  // of the same scrolling-history panel (ScopeHistoryBody.tsx).
  if (descriptor.typeId === 'view.scope.modulation') return <ScopeModulationBody descriptor={descriptor} state={state} instanceId={instanceId} />
  if (descriptor.typeId === 'view.gate') return <GateBody descriptor={descriptor} state={state} instanceId={instanceId} />
  // The placeable phase-locked viewer (replaces view.scope/view.glance).
  if (descriptor.typeId === 'view.cycle') return <CycleBody descriptor={descriptor} state={state} instanceId={instanceId} />
  if (descriptor.layoutVariant === 'decoration') return <DecorationBody descriptor={descriptor} />
  if (descriptor.layoutVariant === 'singleton') return <SingletonBody descriptor={descriptor} state={state} instanceId={instanceId} />
  if (descriptor.layoutVariant === 'glance') return <GlanceBody descriptor={descriptor} state={state} instanceId={instanceId} />

  const classNames = [
    'node-card',
    descriptor.layoutVariant === 'horizontal' ? 'node-card-horizontal' : 'node-card-standard',
    state.selected && 'node-card-selected',
    state.bypassed && 'node-card-bypassed',
    state.listening && 'node-card-listening',
    state.error && 'node-card-error',
    descriptor.category === 'Macro' && 'node-card-macro',
  ]
    .filter(Boolean)
    .join(' ')

  return (
    <div className={classNames}>
      <InstanceCountBadge badge={state.instanceCountBadge} />
      <TitleBar descriptor={descriptor} state={state} />
      {descriptor.layoutVariant === 'horizontal' ? (
        <HorizontalBody descriptor={descriptor} state={state} instanceId={instanceId} />
      ) : (
        <StandardBody descriptor={descriptor} state={state} instanceId={instanceId} />
      )}
    </div>
  )
}

