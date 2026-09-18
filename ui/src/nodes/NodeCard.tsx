// Renders one node instance purely from a NodeDescriptor (NODE_EDITOR.md
// §3: "the UI never hardcodes a node type") plus a display-state bag —
// this is the same component the M10 canvas and the M9 gallery both use
// (gallery imports it directly). Pure DOM/CSS — ADR-0008's "hybrid
// WebGL-background + DOM-overlay" node body approach is deliberately
// deferred; see ADR-0008's Amendment (M10) for why DOM stays the layout
// source of truth.
import { useMemo } from 'react'
import type { NodeDescriptor, PortDescriptor } from '../graph/descriptorTypes'
import { classifyPortUiKind, portUiStyle, parameterUiColor } from '../graph/portUiKind'
import { tokens } from '../theme/tokens'
import { ValueSlider } from './ValueSlider'
import { TriggerSelect } from './TriggerSelect'
import './NodeCard.css'

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
  const primaryInput = descriptor.inputs[0] as PortDescriptor | undefined
  const primaryOutput = (descriptor.outputs.find((o) => o.isPrimaryOutput) ?? descriptor.outputs[0]) as PortDescriptor | undefined

  const merged = primaryInput && primaryOutput && isSameProperty(primaryInput, primaryOutput)
    ? { kind: 'merged' as const, id: primaryInput.id, input: primaryInput, output: primaryOutput }
    : null

  return {
    merged,
    inputs: descriptor.inputs.filter((p) => p !== merged?.input).map((port) => ({ kind: 'port', direction: 'input', port })),
    outputs: descriptor.outputs.filter((p) => p !== merged?.output).map((port) => ({ kind: 'port', direction: 'output', port })),
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
    as a TriggerSelect — see PortRow below). Reuses the same numeric/audio/
    trigger/boolean split ui/src/graph/wireRules.ts's canConnect() already
    groups ports by, rather than inventing a second classification.
*/
function hasNumericFallback(port: PortDescriptor): boolean {
  const kind = classifyPortUiKind(port)
  return kind === 'modulation' || kind === 'value' || kind === 'integer'
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
function PortGlyph({
  port,
  side,
  instanceId,
  connected,
}: {
  port: PortDescriptor
  side: 'left' | 'right'
  instanceId?: string
  connected: boolean
}) {
  const style = portUiStyle(port)
  const color = port.isPolyPlaceholder ? tokens.color.portPoly : style.color
  // "side" is a 1:1 proxy for direction at every call site in this file
  // (input always renders left, output always right).
  const direction = side === 'left' ? 'input' : 'output'
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

function PortLabel({ port, connected, demoValue }: { port: PortDescriptor; connected: boolean; demoValue?: string }) {
  const style = portUiStyle(port)
  const color = port.isPolyPlaceholder ? tokens.color.portPoly : style.color
  // PortDescriptor.h's own contract: "falls back to id in the UI if empty"
  // — several real M1/M2/M7 nodes (osc.analog's "out", math.add's "a"/"b",
  // ...) never got a display label filled in, so this is a real, expected
  // case, not a malformed descriptor.
  return (
    <span className="node-port-label" style={{ color }}>
      {port.label || port.id}
      {connected && <span className="node-port-live-value"> {demoValue}</span>}
    </span>
  )
}

/** Precision/range is deliberately generic for now — direct instruction:
    "let's not focus on the types of values" (integer stepping, log scale,
    real per-node bounds) until the real node architecture settles. A port
    with real descriptor bounds (PortDescriptor.minValue/maxValue) still
    uses them; one without (common for a mock/demo port, or any real one
    that never got bounds assigned) falls back to a plain 0-10 range so the
    slider has *something* sensible to drag across, 2 decimals throughout.
*/
function PortRow({
  direction,
  port,
  connected,
  demoValue,
  instanceId,
  value,
  onCommit,
}: {
  direction: 'input' | 'output'
  port: PortDescriptor
  connected: boolean
  demoValue?: string
  instanceId?: string
  value?: number
  onCommit?: (value: number) => void
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
  // gets a TriggerSelect instead — both read/write the same value/onCommit
  // slot, a plain option index in TriggerSelect's case.
  const editable = direction === 'input' && !connected && isEditableInNode(port)
  const showSlider = editable && hasNumericFallback(port)
  const showTriggerSelect = editable && !showSlider && port.type === 'event' && !!port.options?.length
  return (
    <div className={`node-row node-row-port node-row-${direction}`}>
      {direction === 'input' && <PortGlyph port={port} side="left" instanceId={instanceId} connected={connected} />}
      {showSlider ? (
        <ValueSlider
          label={port.label || port.id}
          value={value ?? port.defaultValue}
          min={port.minValue ?? 0}
          max={port.maxValue ?? 10}
          isInteger={port.isInteger}
          unit={port.unit}
          color={port.isPolyPlaceholder ? tokens.color.portPoly : portUiStyle(port).color}
          onCommit={onCommit}
        />
      ) : showTriggerSelect ? (
        <TriggerSelect
          label={port.label || port.id}
          options={port.options!}
          selectedIndex={value ?? port.defaultValue}
          color={portUiStyle(port).color}
          onCommit={onCommit}
        />
      ) : (
        <PortLabel port={port} connected={connected} demoValue={demoValue} />
      )}
      {direction === 'output' && <PortGlyph port={port} side="right" instanceId={instanceId} connected />}
    </div>
  )
}

function MergedRowView({ row, instanceId, connected }: { row: MergedRow; instanceId?: string; connected: boolean }) {
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
  const label = row.output.label || row.input.label || row.output.id
  const lineColor = row.output.isPolyPlaceholder ? tokens.color.portPoly : portUiStyle(row.output).color
  return (
    <div className="node-row node-row-port node-row-merged">
      <span className="node-merged-line" aria-hidden="true" style={{ background: lineColor }} />
      <PortGlyph port={row.input} side="left" instanceId={instanceId} connected={connected} />
      <span className="node-port-label node-port-label-merged">{label}</span>
      <PortGlyph port={row.output} side="right" instanceId={instanceId} connected />
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
  isInteger,
  unit,
  options,
  onCommit,
}: {
  id: string
  displayName: string
  value: number
  minValue: number
  maxValue: number
  isInteger: boolean
  unit: string
  options?: string[]
  onCommit?: (value: number) => void
}) {
  const color = parameterUiColor(unit)
  return (
    <div className="node-row node-row-parameter" key={id}>
      {options && options.length > 0 ? (
        <TriggerSelect label={displayName} options={options} selectedIndex={value} color={color} onCommit={onCommit} />
      ) : (
        <ValueSlider label={displayName} value={value} min={minValue} max={maxValue} isInteger={isInteger} unit={unit} color={color} onCommit={onCommit} />
      )}
    </div>
  )
}

function TitleBar({ descriptor, state }: { descriptor: NodeDescriptor; state: NodeCardState }) {
  const isMacro = descriptor.category === 'Macro'
  return (
    <div className="node-title-bar">
      <span className="node-title">{descriptor.title || descriptor.typeId}</span>
      <div className="node-title-icons">
        {state.error && (
          <span className="node-error-badge" title={state.error}>
            !
          </span>
        )}
        {isMacro ? (
          <span className="node-info-icon" title="Constraints (Type/Shape/Enum)">
            i
          </span>
        ) : (
          <>
            <span className={`node-bypass-icon${state.bypassed ? ' node-bypass-icon-active' : ''}`} title="Bypass" />
            <span className="node-assist-icon" title="Assist menu">
              +
            </span>
          </>
        )}
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
function paramValue(state: NodeCardState, id: string, fallback: number): number {
  return state.parameterValues?.[id] ?? fallback
}
function paramCommit(state: NodeCardState, id: string): ((value: number) => void) | undefined {
  const commit = state.onParameterCommit
  return commit ? (value: number) => commit(id, value) : undefined
}

function StandardBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const { merged, inputs, outputs } = useMemo(() => splitPorts(descriptor), [descriptor])
  const connected = state.connectedPortIds ?? new Set<string>()

  return (
    <>
      {merged && <MergedRowView row={merged} instanceId={instanceId} connected={connected.has(merged.id)} />}
      {inputs.map((row) => (
        <PortRow
          key={row.port.id}
          direction="input"
          port={row.port}
          connected={connected.has(row.port.id)}
          demoValue={state.demoConnectedValue}
          instanceId={instanceId}
          value={paramValue(state, row.port.id, row.port.defaultValue)}
          onCommit={paramCommit(state, row.port.id)}
        />
      ))}
      {descriptor.parameters.map((p) => (
        <ParameterRow
          key={p.id}
          id={p.id}
          displayName={p.displayName || p.id}
          value={paramValue(state, p.id, p.defaultValue)}
          minValue={p.minValue}
          maxValue={p.maxValue}
          isInteger={p.isInteger}
          unit={p.unit}
          options={p.options}
          onCommit={paramCommit(state, p.id)}
        />
      ))}
      {outputs.map((row) => (
        <PortRow key={row.port.id} direction="output" port={row.port} connected={connected.has(row.port.id)} demoValue={state.demoConnectedValue} instanceId={instanceId} />
      ))}
    </>
  )
}

/** Static placeholder preview graphic — real per-node previews (waveform,
    stepped values, envelope+playhead, LFO phase, driven by live telemetry)
    are M11's job (NODE_EDITOR.md §9/§11). This exists only so the
    Horizontal layout variant has something to show in its centre region.
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
          />
        ))}
        {descriptor.parameters.map((p) => (
          <ParameterRow
            key={p.id}
            id={p.id}
            displayName={p.displayName || p.id}
            value={paramValue(state, p.id, p.defaultValue)}
            minValue={p.minValue}
            maxValue={p.maxValue}
            isInteger={p.isInteger}
            unit={p.unit}
            options={p.options}
            onCommit={paramCommit(state, p.id)}
          />
        ))}
      </div>
      <div className="node-horizontal-preview">
        <PlaceholderPreview />
      </div>
      <div className="node-horizontal-column node-horizontal-column-output">
        {primaryOutput && (
          <PortRow direction="output" port={primaryOutput} connected={connected.has(primaryOutput.id)} instanceId={instanceId} />
        )}
      </div>
    </div>
  )
}

function SingletonGlyph({ port, direction, instanceId, connected }: { port: PortDescriptor; direction: 'input' | 'output'; instanceId?: string; connected: boolean }) {
  const style = portUiStyle(port)
  const color = port.isPolyPlaceholder ? tokens.color.portPoly : style.color
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
      {input && <SingletonGlyph port={input} direction="input" instanceId={instanceId} connected={connected.has(input.id)} />}
      {/* `node-title` (shared with every other layout variant, not just
          `node-singleton-title`) is what lets GraphSurface.tsx's generic
          `.closest('.node-title')` double-click/rename targeting work here
          too — previously this layout had no element carrying that class
          at all, so rename silently did nothing on it. */}
      <span className="node-singleton-title node-title">{descriptor.title}</span>
      {output && <SingletonGlyph port={output} direction="output" instanceId={instanceId} connected={connected.has(output.id)} />}
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

export function NodeCard({ descriptor, state = {}, instanceId }: NodeCardProps) {
  if (descriptor.icon === 'ear') return <EarIcon title={descriptor.title} />
  if (descriptor.layoutVariant === 'decoration') return <DecorationBody descriptor={descriptor} />
  if (descriptor.layoutVariant === 'singleton') return <SingletonBody descriptor={descriptor} state={state} instanceId={instanceId} />

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
      <TitleBar descriptor={descriptor} state={state} />
      {descriptor.layoutVariant === 'horizontal' ? (
        <HorizontalBody descriptor={descriptor} state={state} instanceId={instanceId} />
      ) : (
        <StandardBody descriptor={descriptor} state={state} instanceId={instanceId} />
      )}
    </div>
  )
}

