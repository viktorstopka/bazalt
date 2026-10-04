// design/Visualization/Gate.png: view.gate's thin wrapper around
// ScopeHistoryBody.tsx — "same panel and the same scrolling history as the
// Scope, with a binary vertical scale." Only what differs lives here:
// everything blue, the "?" boolean glyph (the ports' own Boolean type gives
// both), and the 'binary' variant — fixed TRUE/FALSE labels and a square-
// edged trace that never drops a brief true state (see drawBinary()).
import type { NodeDescriptor } from '../graph/descriptorTypes'
import type { NodeCardState } from './NodeCard'
import { ScopeHistoryBody } from './ScopeHistoryBody'
import { tokens } from '../theme/tokens'

// Mirrors ViewHistoryWindow.h's own static constexpr bounds exactly — the
// same duplication ScopeControlBody.tsx already makes, for the same reason.
const MIN_TIME_WINDOW_SECONDS = 0.01
const MAX_TIME_WINDOW_SECONDS = 30.0
const DEFAULT_TIME_WINDOW_SECONDS = 2.0

export function GateBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  return (
    <ScopeHistoryBody
      descriptor={descriptor}
      state={state}
      instanceId={instanceId}
      variant="binary"
      traceColor={tokens.color.portBoolean}
      timeWindowParameterId="view.gate.timeWindow"
      minTimeWindowSeconds={MIN_TIME_WINDOW_SECONDS}
      maxTimeWindowSeconds={MAX_TIME_WINDOW_SECONDS}
      defaultTimeWindowSeconds={DEFAULT_TIME_WINDOW_SECONDS}
    />
  )
}
