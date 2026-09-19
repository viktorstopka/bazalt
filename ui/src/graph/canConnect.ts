// M19 (ADR-0018/ADR-0025): the real engine connection-validity rules,
// hand-mirrored from engine/src/graph/CanConnect.cpp so the UI predicts
// with the same logic the engine enforces, not an independent
// classification-bucket guess (the retired ui/src/graph/wireRules.ts).
// "Generated" per SIGNAL_TYPES.md §4 is aspirational here the same way
// NodeDescriptorJson.cpp/descriptorTypes.ts are "one schema, two
// producers" by hand, not by a codegen tool — there is no such tool, this
// file is kept in sync with CanConnect.cpp by hand, on purpose, the same
// way that pair already is. If UI and engine ever disagree, the engine
// wins (GraphEditController::connectWithAutoAdapt is the real, committing
// authority — see graphCommands.ts) — this file is prediction only, for
// live wire-drag feedback before a drop is ever attempted.
import type { PortDescriptor, Quantity } from './descriptorTypes'

export type ConnectionOutcome = 'ok' | 'needsAdapters' | 'reject'

export interface CanConnectResult {
  outcome: ConnectionOutcome
  reason: string
}

function ok(): CanConnectResult {
  return { outcome: 'ok', reason: '' }
}
function reject(reason: string): CanConnectResult {
  return { outcome: 'reject', reason }
}
function needsAdapters(reason: string): CanConnectResult {
  return { outcome: 'needsAdapters', reason }
}

function isNormalisedQuantity(q: Quantity): boolean {
  return q === 'unipolar' || q === 'bipolar'
}
function isRealQuantity(q: Quantity): boolean {
  return q !== 'dimensionless' && !isNormalisedQuantity(q)
}

function connectAudio(from: PortDescriptor, to: PortDescriptor): CanConnectResult {
  if (from.channels === 'inherited' || to.channels === 'inherited') return ok()
  if (from.channels === 'stereo' && to.channels === 'mono') {
    return needsAdapters('Stereo source into a mono-only port needs mix.downmix (manual insertion for now)')
  }
  return ok() // mono->mono, mono->stereo (free broadcast), stereo->stereo
}

function connectControl(from: PortDescriptor, to: PortDescriptor): CanConnectResult {
  if (from.quantity === to.quantity || from.quantity === 'dimensionless' || to.quantity === 'dimensionless') {
    return ok()
  }
  if (isNormalisedQuantity(from.quantity) && isRealQuantity(to.quantity)) {
    return needsAdapters('Modulation-range value into a real-quantity port needs a Map')
  }
  if (isRealQuantity(from.quantity) && isNormalisedQuantity(to.quantity)) {
    return needsAdapters('Real-quantity value into a modulation-range port needs a Normalise')
  }
  return reject('Incompatible Control quantities with no defined adapter')
}

function dataTagAccepted(produced: string, required: string): boolean {
  return produced === required && produced !== 'unknown'
}

/** Mirrors CanConnect.cpp's canConnect(from, to) exactly. */
export function canConnectPorts(from: PortDescriptor, to: PortDescriptor): CanConnectResult {
  if (from.type === 'data' || to.type === 'data') {
    if (from.type !== 'data' || to.type !== 'data') return reject('Data never converts implicitly (SIGNAL_TYPES.md §5)')
    const producedTag = from.dataTags[0] ?? 'unknown'
    if (to.dataTags.some((tag) => dataTagAccepted(producedTag, tag))) return ok()
    return reject("Data tag mismatch — this port doesn't accept what's produced here")
  }

  if (from.type === to.type) {
    switch (from.type) {
      case 'audio':
        return connectAudio(from, to)
      case 'control':
        return connectControl(from, to)
      case 'event':
        return ok()
      case 'note':
        return ok()
      case 'boolean':
        return ok()
      case 'spectral':
        return reject('Spectral is reserved, not yet implemented')
    }
  }

  if (from.type === 'control' && to.type === 'event') {
    return needsAdapters('A Control signal into an Event-typed port needs a Threshold')
  }

  return reject('Incompatible signal types with no adapter available yet')
}

export interface ConnectionEndpoint {
  nodeId: string
  portId: string
  direction: 'input' | 'output'
  port: PortDescriptor
}

export function findPort(descriptor: { inputs: PortDescriptor[]; outputs: PortDescriptor[] }, portId: string, direction: 'input' | 'output'): PortDescriptor | undefined {
  const list = direction === 'input' ? descriptor.inputs : descriptor.outputs
  return list.find((p) => p.id === portId)
}

/** Live wire-drag feedback only needs a yes/no (ADR-0010's colour scheme
    has no third "will insert an adapter" visual state yet — NeedsAdapters
    renders identically to Ok during a drag; only a true Reject shows the
    red-dashed rejected treatment). The actual commit
    (graphCommands.connectWithAutoAdapt) still gets the real three-outcome
    answer from the engine and inserts the adapter for real.
*/
export function canConnect(output: ConnectionEndpoint, input: ConnectionEndpoint): boolean {
  if (output.direction !== 'output' || input.direction !== 'input') return false
  if (output.nodeId === input.nodeId) return false
  return canConnectPorts(output.port, input.port).outcome !== 'reject'
}
