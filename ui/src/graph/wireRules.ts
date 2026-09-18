// Local (client-side) wiring validity rules for the M10 node editor. Not
// the engine's own validation (there is no live NodeGraph behind this yet —
// see graphStore.ts's own header comment) — this is the UI-side rule set
// that decides in-progress-wire feedback colour (NODE_EDITOR.md §10,
// docs/decisions/0010-wire-feedback-colours.md) and whether a drop commits
// a connection.
import type { NodeDescriptor, PortDescriptor } from './descriptorTypes'
import { classifyPortUiKind } from './portUiKind'

export function findPort(descriptor: NodeDescriptor, portId: string, direction: 'input' | 'output'): PortDescriptor | undefined {
  const list = direction === 'input' ? descriptor.inputs : descriptor.outputs
  return list.find((p) => p.id === portId)
}

/** NODE_EDITOR.md §5: Modulation/Value/Integer are all `Control`-derived UI
    classifications of the same underlying numeric signal, so a connection
    between any two of them is valid (this is the case the real engine would
    auto-insert a Map node for — skipped this pass, see the M10 plan's
    "deliberately deferred" list). Audio/Trigger/Boolean only match their own
    kind — mixing those is a real category error, not just a unit mismatch.
*/
const COMPATIBILITY_GROUP: Record<string, string> = {
  modulation: 'numeric',
  value: 'numeric',
  integer: 'numeric',
  audio: 'audio',
  trigger: 'trigger',
  boolean: 'boolean',
}

export function portsCompatible(a: PortDescriptor, b: PortDescriptor): boolean {
  return COMPATIBILITY_GROUP[classifyPortUiKind(a)] === COMPATIBILITY_GROUP[classifyPortUiKind(b)]
}

export interface ConnectionEndpoint {
  nodeId: string
  portId: string
  direction: 'input' | 'output'
  descriptor: NodeDescriptor
  port: PortDescriptor
}

/** A connection is only ever attempted output->input (see graphStore.ts's
    wire-drag notes: grabbing an input only detaches/redirects an existing
    wire, it never starts a fresh drag) — no self-connections, and the two
    ports' UI kinds must be in the same compatibility group.
*/
export function canConnect(output: ConnectionEndpoint, input: ConnectionEndpoint): boolean {
  if (output.direction !== 'output' || input.direction !== 'input') return false
  if (output.nodeId === input.nodeId) return false
  return portsCompatible(output.port, input.port)
}
