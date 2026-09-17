// Renders one node instance purely from a NodeDescriptor (NODE_EDITOR.md
// §3: "the UI never hardcodes a node type") plus a display-state bag —
// this is the same component the M10 canvas and the M9 gallery both use
// (gallery imports it directly; the canvas doesn't exist yet). Pure DOM/CSS
// for now — ADR-0008's "hybrid WebGL-background + DOM-overlay" node body
// approach is deliberately deferred to M10, where nodes actually pan/zoom
// on a live canvas; a static gallery has neither the node count nor the
// camera transform that hybrid rendering earns its cost from (see ADR-0008
// amendment).
import { useMemo } from 'react'
import type { NodeDescriptor, PortDescriptor } from '../graph/descriptorTypes'
import { portUiStyle, parameterUiColor } from '../graph/portUiKind'
import { tokens } from '../theme/tokens'
import './NodeCard.css'

export interface NodeCardState {
  selected?: boolean
  hovered?: boolean
  bypassed?: boolean
  listening?: boolean
  /** Error message; presence alone drives the red-circle-"!" badge next to
      the title (blueprint §4's "filled red circle with '!'", distinct from
      the bare violet trigger glyph).
  */
  error?: string
  /** Port ids currently shown as "connected" (glyph + live-value readout
      instead of no control — see PortRow). Gallery-only demonstration data;
      a real canvas derives this from the live NodeGraph's connections.
  */
  connectedPortIds?: ReadonlySet<string>
  /** Demo readout value shown for a connected port, since the gallery has
      no live telemetry to read a real one from.
  */
  demoConnectedValue?: string
}

interface NodeCardProps {
  descriptor: NodeDescriptor
  state?: NodeCardState
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
    single-audio-in/out M1/M2 node (delay.basic, filter.svf,
    filter.onepole, util.voiceSum) happens to already use. Deliberately
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

function PortGlyph({ port, side, long }: { port: PortDescriptor; side: 'left' | 'right'; long?: boolean }) {
  const style = portUiStyle(port)
  const color = port.isPolyPlaceholder ? tokens.color.portPoly : style.color
  // "long" (the merged pass-through row): a visibly bigger/longer arrow
  // than an ordinary port row's, per the design reference — the row
  // stands for two ports at once, so its glyphs read as more significant.
  const className = `node-port-glyph node-port-glyph-${side}${long ? ' node-port-glyph-long' : ''}`
  return (
    <span className={className} style={{ color }}>
      {style.glyph}
    </span>
  )
}

function PortLabel({ port, connected, demoValue }: { port: PortDescriptor; connected: boolean; demoValue?: string }) {
  const style = portUiStyle(port)
  const color = port.isPolyPlaceholder ? tokens.color.portPoly : style.color
  // PortDescriptor.h's own contract: "falls back to id in the UI if empty"
  // — several real M1/M2/M7 nodes (osc.basic's "out", util.add's "a"/"b",
  // ...) never got a display label filled in, so this is a real, expected
  // case, not a malformed descriptor.
  return (
    <span className="node-port-label" style={{ color }}>
      {port.label || port.id}
      {connected && <span className="node-port-live-value"> {demoValue ?? port.defaultValue}</span>}
    </span>
  )
}

function PortRow({ direction, port, connected, demoValue }: { direction: 'input' | 'output'; port: PortDescriptor; connected: boolean; demoValue?: string }) {
  return (
    <div className={`node-row node-row-port node-row-${direction}`}>
      {direction === 'input' && <PortGlyph port={port} side="left" />}
      <PortLabel port={port} connected={connected} demoValue={demoValue} />
      {direction === 'output' && <PortGlyph port={port} side="right" />}
    </div>
  )
}

function MergedRowView({ row }: { row: MergedRow }) {
  // One shared label for the row (blueprint: "the property label appears
  // in the centre") — the output's own label/id is normally the more
  // meaningful name (Predelay's "Audio"); fall back to the input's if the
  // output never got one.
  const label = row.output.label || row.input.label || row.output.id
  return (
    <div className="node-row node-row-port node-row-merged">
      <PortGlyph port={row.input} side="left" long />
      <span className="node-port-label node-port-label-merged">{label}</span>
      <PortGlyph port={row.output} side="right" long />
    </div>
  )
}

/** Real engine parameters (e.g. filter.svf's Resonance) carry full float
    precision that's meaningless to show verbatim in a value pill — round
    to 3 decimals and drop trailing zeros, matching the reference's own mix
    of precisions ("50%", "34 ms", "3.000 Hz", "42.53%").
*/
function formatParameterValue(value: number): string {
  return parseFloat(value.toFixed(3)).toString()
}

/** Parameters (ParameterDescriptor) are DSP-smoothed floats set via
    setParameterValue, not graph-connectable ports as of M9 — see
    CLAUDE.md's interim-simplifications note. They render as a plain inline
    value pill, never a type glyph, matching the reference's "By 34ms" /
    "Rate 3.000 Hz" rows. Still gets a small tick mark straddling the
    node's left border (docs/Slice 1 (1).png's "Treshold" row) — smaller
    and plainer than a full port glyph, since it isn't a cable-connectable
    port, but the row is still part of the node's edge-notch visual
    language.
*/
function ParameterRow({ id, displayName, defaultValue, unit }: { id: string; displayName: string; defaultValue: number; unit: string }) {
  const color = parameterUiColor(unit)
  return (
    <div className="node-row node-row-parameter" key={id}>
      <span className="node-parameter-tick" style={{ background: tokens.color.nodeFill }}>
        <span style={{ background: color }} />
      </span>
      <span className="node-parameter-label" style={{ color }}>
        {displayName}
      </span>
      <span className="node-value-pill" style={{ borderColor: color, color }}>
        {formatParameterValue(defaultValue)}
        {unit}
      </span>
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

function StandardBody({ descriptor, state }: { descriptor: NodeDescriptor; state: NodeCardState }) {
  const { merged, inputs, outputs } = useMemo(() => splitPorts(descriptor), [descriptor])
  const connected = state.connectedPortIds ?? new Set<string>()

  return (
    <>
      {merged && <MergedRowView row={merged} />}
      {inputs.map((row) => (
        <PortRow key={row.port.id} direction="input" port={row.port} connected={connected.has(row.port.id)} demoValue={state.demoConnectedValue} />
      ))}
      {descriptor.parameters.map((p) => (
        <ParameterRow key={p.id} id={p.id} displayName={p.displayName || p.id} defaultValue={p.defaultValue} unit={p.unit} />
      ))}
      {outputs.map((row) => (
        <PortRow key={row.port.id} direction="output" port={row.port} connected={connected.has(row.port.id)} demoValue={state.demoConnectedValue} />
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

function HorizontalBody({ descriptor, state }: { descriptor: NodeDescriptor; state: NodeCardState }) {
  const primaryOutput = descriptor.outputs.find((o) => o.isPrimaryOutput) ?? descriptor.outputs[0]
  return (
    <div className="node-horizontal-body">
      <div className="node-horizontal-column">
        {descriptor.inputs.map((port) => (
          <PortRow key={port.id} direction="input" port={port} connected={state.connectedPortIds?.has(port.id) ?? false} />
        ))}
        {descriptor.parameters.map((p) => (
          <ParameterRow key={p.id} id={p.id} displayName={p.displayName || p.id} defaultValue={p.defaultValue} unit={p.unit} />
        ))}
      </div>
      <PlaceholderPreview />
      <div className="node-horizontal-column node-horizontal-column-output">
        {primaryOutput && <PortRow direction="output" port={primaryOutput} connected={state.connectedPortIds?.has(primaryOutput.id) ?? false} />}
      </div>
    </div>
  )
}

function SingletonGlyph({ port }: { port: PortDescriptor }) {
  const style = portUiStyle(port)
  const color = port.isPolyPlaceholder ? tokens.color.portPoly : style.color
  // Deliberately NOT the border-piercing PortGlyph used elsewhere: adjacent
  // singletons are meant to visually chain (blueprint §4: "frames touch,
  // arrows join") — a singleton's own opaque background-patch notch would
  // paint over the neighbouring box's arrow tip where they meet, breaking
  // exactly the join effect this layout variant exists for.
  return (
    <span className="node-singleton-glyph" style={{ color }}>
      {style.glyph}
    </span>
  )
}

function SingletonBody({ descriptor }: { descriptor: NodeDescriptor }) {
  const input = descriptor.inputs[0]
  const output = descriptor.outputs[0]
  return (
    <div className="node-singleton-body">
      {input && <SingletonGlyph port={input} />}
      <span className="node-singleton-title">{descriptor.title}</span>
      {output && <SingletonGlyph port={output} />}
    </div>
  )
}


/** A minimal ear glyph, no box/border/title — blueprint §4/`Frame 1
    Bazalt.png`'s "Fun Ear Preview Node. Called 'Listen'": util.listen
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
  if (descriptor.icon === 'header') return <span className="node-header-label">{descriptor.title}</span>
  if (descriptor.icon === 'image')
    return (
      <div className="node-image-placeholder">
        <span>{descriptor.title}</span>
      </div>
    )
  // 'frame' (default): a tinted backdrop with its label above the top-left corner, per the reference's "Bass" frame.
  return (
    <div className="node-frame">
      <span className="node-frame-label">{descriptor.title}</span>
    </div>
  )
}

export function NodeCard({ descriptor, state = {} }: NodeCardProps) {
  if (descriptor.icon === 'ear') return <EarIcon title={descriptor.title} />
  if (descriptor.layoutVariant === 'decoration') return <DecorationBody descriptor={descriptor} />
  if (descriptor.layoutVariant === 'singleton') return <SingletonBody descriptor={descriptor} />

  const classNames = [
    'node-card',
    descriptor.layoutVariant === 'horizontal' ? 'node-card-horizontal' : 'node-card-standard',
    state.selected && 'node-card-selected',
    state.hovered && 'node-card-hovered',
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
        <HorizontalBody descriptor={descriptor} state={state} />
      ) : (
        <StandardBody descriptor={descriptor} state={state} />
      )}
    </div>
  )
}
