// design/Visualization/ScopeMod.png: view.scope.modulation's thin wrapper
// around ScopeHistoryBody.tsx — "the Control scope with a different vertical
// scale and a filled trace, so reuse everything built for that one." Only
// what differs lives here: everything orange, the 'centred' variant (filled
// to an editable centre line), and the range seeded from the source's
// polarity. Panel, ports, pass-through, scrolling history, editable window
// and low-rate behaviour are ScopeHistoryBody's, unchanged.
import type { NodeDescriptor, PortDescriptor } from '../graph/descriptorTypes'
import type { NodeCardState } from './NodeCard'
import { ScopeHistoryBody } from './ScopeHistoryBody'
import { PORT_UI_STYLE } from '../graph/portUiKind'
import { tokens } from '../theme/tokens'

// Mirrors ViewHistoryWindow.h's own static constexpr bounds exactly — the
// same duplication ScopeControlBody.tsx already makes, for the same reason.
const MIN_TIME_WINDOW_SECONDS = 0.01
const MAX_TIME_WINDOW_SECONDS = 30.0
const DEFAULT_TIME_WINDOW_SECONDS = 2.0

/** "The range autofills from the source port's polarity: bipolar gives
    -1…1, unipolar 0…1. Still editable afterwards." A source that is not a
    Modulation quantity at all falls back to the shared declared/observed
    range logic. */
function rangeFromPolarity(port: PortDescriptor): { min: number; max: number } | undefined {
  if (port.quantity === 'bipolar') return { min: -1, max: 1 }
  if (port.quantity === 'unipolar') return { min: 0, max: 1 }
  return undefined
}

export function ScopeModulationBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  return (
    <ScopeHistoryBody
      descriptor={descriptor}
      state={state}
      instanceId={instanceId}
      variant="centred"
      traceColor={tokens.color.portModulation}
      portStyle={PORT_UI_STYLE.modulation}
      labelColor={tokens.color.portModulation}
      seedRangeFromSource={rangeFromPolarity}
      timeWindowParameterId="view.scope.modulation.timeWindow"
      minTimeWindowSeconds={MIN_TIME_WINDOW_SECONDS}
      maxTimeWindowSeconds={MAX_TIME_WINDOW_SECONDS}
      defaultTimeWindowSeconds={DEFAULT_TIME_WINDOW_SECONDS}
    />
  )
}
