// view.cycle ("Cycle") — the placeable phase-locked viewer. Shares the
// scrolling-history scopes' panel language (ScopeHistoryBody.css: a titleless
// hairline panel, ports a quarter of the way down its sides, scale labels
// just outside the right edge) but has no time window to set: the horizontal
// axis is the upstream oscillator's phase (ViewCycleNode.h), a fixed number
// of cycles. The scale is fixed too (±1 with headroom), so its labels are
// plain, not editable.
import type { NodeDescriptor } from '../graph/descriptorTypes'
import { type NodeCardState, PortGlyph } from './NodeCard'
import { PhaseLockedPreview } from './PhaseLockedPreview'
import { levelFraction } from './phaseLockedGeometry'
import { nextPlayheadMode, playheadModeFromProperty } from './playheadMode'
import './ScopeHistoryBody.css'
import './CycleBody.css'

export function CycleBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const inputPort = descriptor.inputs[0]
  const outputPort = descriptor.outputs[0]
  const mode = playheadModeFromProperty(state.previewPlayheadMode)
  const setMode = state.onSetPreviewPlayheadMode

  const classNames = [
    'node-card',
    'scope-history-card',
    'cycle-card',
    state.selected && 'node-card-selected',
    state.bypassed && 'node-card-bypassed',
    state.listening && 'node-card-listening',
    state.error && 'node-card-error',
  ]
    .filter(Boolean)
    .join(' ')

  return (
    <div className={classNames}>
      <div className="scope-history-port scope-history-port-left">
        <PortGlyph port={inputPort} side="left" instanceId={instanceId} connected={state.connectedPortIds?.has(inputPort.id) ?? false} />
      </div>
      <div className="scope-history-port scope-history-port-right">
        <PortGlyph port={outputPort} side="right" instanceId={instanceId} connected={state.connectedPortIds?.has(outputPort.id) ?? false} />
      </div>
      {instanceId && (
        <PhaseLockedPreview
          nodeId={instanceId}
          portId={outputPort.id}
          playheadMode={mode}
          onCyclePlayheadMode={setMode ? () => setMode(nextPlayheadMode(mode)) : undefined}
          emptyHint="No oscillator upstream"
        />
      )}
      <div className="scope-history-range scope-history-range-levels">
        <span style={{ top: `${levelFraction(1) * 100}%` }}>1</span>
        <span style={{ top: `${levelFraction(-1) * 100}%` }}>-1</span>
      </div>
    </div>
  )
}
