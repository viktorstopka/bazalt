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

function splitPorts(descriptor: NodeDescriptor): { merged: MergedRow[]; inputs: PortRowData[]; outputs: PortRowData[] } {
  const merged: MergedRow[] = []
  const usedInputIds = new Set<string>()
  const usedOutputIds = new Set<string>()

  for (const input of descriptor.inputs) {
    const match = descriptor.outputs.find((o) => o.id === input.id && o.type === input.type)
    if (match) {
      merged.push({ kind: 'merged', id: input.id, input, output: match })
      usedInputIds.add(input.id)
      usedOutputIds.add(match.id)
    }
  }

  return {
    merged,
    inputs: descriptor.inputs.filter((p) => !usedInputIds.has(p.id)).map((port) => ({ kind: 'port', direction: 'input', port })),
    outputs: descriptor.outputs.filter((p) => !usedOutputIds.has(p.id)).map((port) => ({ kind: 'port', direction: 'output', port })),
  }
}

function PortGlyph({ port, side }: { port: PortDescriptor; side: 'left' | 'right' }) {
  const style = portUiStyle(port)
  const color = port.isPolyPlaceholder ? tokens.color.portPoly : style.color
  return (
    <span className={`node-port-glyph node-port-glyph-${side}`} style={{ color }}>
      {style.glyph}
    </span>
  )
}

function PortLabel({ port, connected, demoValue }: { port: PortDescriptor; connected: boolean; demoValue?: string }) {
  const style = portUiStyle(port)
  const color = port.isPolyPlaceholder ? tokens.color.portPoly : style.color
  return (
    <span className="node-port-label" style={{ color }}>
      {port.label}
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
  return (
    <div className="node-row node-row-port node-row-merged">
      <PortGlyph port={row.input} side="left" />
      <PortLabel port={row.output} connected={false} />
      <PortGlyph port={row.output} side="right" />
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
      {merged.map((row) => (
        <MergedRowView key={row.id} row={row} />
      ))}
      {merged.length > 0 && descriptor.parameters.length > 0 && <div className="node-divider" />}
      {inputs.map((row) => (
        <PortRow key={row.port.id} direction="input" port={row.port} connected={connected.has(row.port.id)} demoValue={state.demoConnectedValue} />
      ))}
      {descriptor.parameters.map((p) => (
        <ParameterRow key={p.id} id={p.id} displayName={p.displayName || p.id} defaultValue={p.defaultValue} unit={p.unit} />
      ))}
      {(merged.length > 0 || inputs.length > 0 || descriptor.parameters.length > 0) && outputs.length > 0 && <div className="node-divider" />}
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

function SingletonBody({ descriptor }: { descriptor: NodeDescriptor }) {
  const input = descriptor.inputs[0]
  const output = descriptor.outputs[0]
  return (
    <div className="node-singleton-body">
      {input && <PortGlyph port={input} side="left" />}
      <span className="node-singleton-title">{descriptor.title}</span>
      {output && <PortGlyph port={output} side="right" />}
    </div>
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
