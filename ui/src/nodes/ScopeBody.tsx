// view.scope — the one scrolling-history viewer (wiki/plans/DataAndWavetable.md
// §2: the old Control / Modulation / Gate scopes merged). The engine node is a
// type-following pass-through; how it draws follows what is wired, read off
// the live-resolved upstream port:
// - a Boolean: blue, the binary TRUE/FALSE scale (design/Visualization/Gate.png)
// - a modulation (Unipolar/Bipolar) or an audio signal: orange, filled to an
//   editable centre line, range seeded from the polarity (ScopeMod.png)
// - anything else: a white line over its declared or observed range (Scope1.png)
// Panel, pass-through, scrolling history and the editable window are
// ScopeHistoryBody's.
import type { NodeDescriptor, PortDescriptor } from '../graph/descriptorTypes'
import type { NodeCardState } from './NodeCard'
import { ScopeHistoryBody, type ScopeHistoryVariant } from './ScopeHistoryBody'
import { classifyPortUiKind, PORT_UI_STYLE } from '../graph/portUiKind'
import { findWireAtInput, getEndpoint } from '../graph/graphStore'
import { tokens } from '../theme/tokens'

// Mirrors ViewHistoryWindow.h's own static constexpr bounds.
const MIN_TIME_WINDOW_SECONDS = 0.01
const MAX_TIME_WINDOW_SECONDS = 30.0
const DEFAULT_TIME_WINDOW_SECONDS = 2.0

/** "The range autofills from the source port's polarity: bipolar gives -1…1,
    unipolar 0…1." An audio signal reads as bipolar. */
function rangeFromPolarity(port: PortDescriptor): { min: number; max: number } | undefined {
  if (port.type === 'audio' || port.quantity === 'bipolar') return { min: -1, max: 1 }
  if (port.quantity === 'unipolar') return { min: 0, max: 1 }
  return undefined
}

export function scopeVariantForSource(port: PortDescriptor | undefined): ScopeHistoryVariant {
  if (!port) return 'line'
  if (port.type === 'audio') return 'centred'
  const kind = classifyPortUiKind(port)
  if (kind === 'boolean') return 'binary'
  if (kind === 'modulation') return 'centred'
  return 'line'
}

export function ScopeBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const wire = instanceId ? findWireAtInput(instanceId, 'in') : undefined
  const source = wire ? getEndpoint(wire.fromNodeId, wire.fromPortId, 'output')?.port : undefined
  const variant = scopeVariantForSource(source)
  const style =
    variant === 'binary'
      ? { traceColor: tokens.color.portBoolean }
      : variant === 'centred'
        ? { traceColor: tokens.color.portModulation, portStyle: PORT_UI_STYLE.modulation, labelColor: tokens.color.portModulation }
        : { traceColor: tokens.color.portValue }

  return (
    <ScopeHistoryBody
      // A different variant is a different panel: remount so its observed
      // range and centre start fresh.
      key={variant}
      descriptor={descriptor}
      state={state}
      instanceId={instanceId}
      variant={variant}
      {...style}
      seedRangeFromSource={variant === 'centred' ? rangeFromPolarity : undefined}
      timeWindowParameterId="view.scope.timeWindow"
      minTimeWindowSeconds={MIN_TIME_WINDOW_SECONDS}
      maxTimeWindowSeconds={MAX_TIME_WINDOW_SECONDS}
      defaultTimeWindowSeconds={DEFAULT_TIME_WINDOW_SECONDS}
    />
  )
}
