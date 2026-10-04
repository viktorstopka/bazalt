// design/Visualization/Scope1.png: the thin, type-specific wrapper around
// ScopeHistoryBody.tsx — the 'line' variant; its siblings
// ScopeModulationBody.tsx and GateBody.tsx are the other two. Everything this node needs
// that isn't plain-Control-specific lives there; this file only supplies
// what's different: the trace colour and ViewScopeControlNode.h's own
// parameter id/bounds.
import type { NodeDescriptor } from '../graph/descriptorTypes'
import type { NodeCardState } from './NodeCard'
import { ScopeHistoryBody } from './ScopeHistoryBody'
import { tokens } from '../theme/tokens'

// Mirrors ViewScopeControlNode.h's own static constexpr bounds exactly —
// duplicated rather than fetched from the node descriptor's own parameter
// metadata (which DOES carry minValue/maxValue/defaultValue already) so
// this component doesn't need to find-by-id into descriptor.parameters
// just to read three numbers that are fixed for this node type; the
// descriptor's own values are still what actually governs ValueSlider-style
// editing anywhere else this parameter is shown.
const MIN_TIME_WINDOW_SECONDS = 0.01
const MAX_TIME_WINDOW_SECONDS = 30.0
const DEFAULT_TIME_WINDOW_SECONDS = 2.0

export function ScopeControlBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  return (
    <ScopeHistoryBody
      descriptor={descriptor}
      state={state}
      instanceId={instanceId}
      variant="line"
      traceColor={tokens.color.portValue} // plain Control — white, same colour classifyPortUiKind gives this port
      timeWindowParameterId="view.scope.control.timeWindow"
      minTimeWindowSeconds={MIN_TIME_WINDOW_SECONDS}
      maxTimeWindowSeconds={MAX_TIME_WINDOW_SECONDS}
      defaultTimeWindowSeconds={DEFAULT_TIME_WINDOW_SECONDS}
    />
  )
}
