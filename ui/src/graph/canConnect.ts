// M19 (ADR-0018/ADR-0025): the real engine connection-validity rules,
// hand-mirrored from engine/src/graph/CanConnect.cpp so the UI predicts
// with the same logic the engine enforces, not an independent
// classification-bucket guess (the retired ui/src/graph/wireRules.ts).
// "Generated" per wiki/NODES.System.md §4 (supersedes archive_docs/
// SIGNAL_TYPES.md §4, which this comment used to cite — archive_docs/ is
// historical reference only, not current spec, per CLAUDE.md) is
// aspirational here the same way NodeDescriptorJson.cpp/descriptorTypes.ts
// are "one schema, two producers" by hand, not by a codegen tool — there is
// no such tool, this file is kept in sync with CanConnect.cpp by hand, on
// purpose, the same way that pair already is. If UI and engine ever
// disagree, the engine wins (GraphEditController::connectWithAutoAdapt is
// the real, committing authority — see graphCommands.ts) — this file is
// prediction only, for live wire-drag feedback before a drop is ever
// attempted.
import type { PortDescriptor, Quantity } from './descriptorTypes'
import { synthesizeGroupPort } from './portGroups'
import type { PortMultiplicityInfo } from './graphCommands'

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

/** wiki/plans/DataAndWavetable.md D1 (mirrors CanConnect.cpp): Audio, Control
    and Boolean are one numeric signal; connecting them is decided by what a
    value means — an Audio port reads as a waveform (±1, like Bipolar), a
    Boolean as a plain 0/1 that fits anywhere. */
function isValueType(type: PortDescriptor['type']): boolean {
  return type === 'audio' || type === 'control' || type === 'boolean'
}
function meaningOf(port: PortDescriptor): Quantity {
  if (port.type === 'audio') return 'bipolar'
  if (port.type === 'boolean') return 'dimensionless'
  return port.quantity
}
/** What a destination expects: a port inheriting both type and quantity (Multiply,
    Add, Clip's `in`, Reroute) expects nothing of its own — what it resolved to was
    borrowed from its inputs, so nothing is ever rescaled into it. */
function expectedMeaningOf(port: PortDescriptor): Quantity {
  if (port.polymorphism === 'signalAndQuantity') return 'dimensionless'
  return meaningOf(port)
}

/** The rescaling reason between two meanings, or null when they meet directly. */
function rescaleFor(from: Quantity, to: Quantity): string | null {
  if (from === to || from === 'dimensionless' || to === 'dimensionless') return null
  if (isNormalisedQuantity(from) && isNormalisedQuantity(to)) return null
  if (isNormalisedQuantity(from) && isRealQuantity(to)) return 'A modulation or audio value into a real-quantity port needs a Map'
  if (isRealQuantity(from) && isNormalisedQuantity(to)) return 'A real-quantity value into a modulation or audio port needs a Map'
  if (from === 'pitch' && to === 'frequency') return 'Pitch into a Frequency-typed port needs an exact conversion, not a linear remap'
  if (from === 'frequency' && to === 'pitch') return 'Frequency into a Pitch-typed port needs an exact conversion, not a linear remap'
  return 'Different real quantities — rescaled via Map'
}

function connectValues(from: PortDescriptor, to: PortDescriptor): CanConnectResult {
  const rescale = rescaleFor(meaningOf(from), expectedMeaningOf(to))
  if (from.channels === 'stereo' && to.channels === 'mono') {
    if (rescale && (rescale.startsWith('Pitch') || rescale.startsWith('Frequency'))) return reject('Stereo into this port needs a Downmix first')
    return needsAdapters('Stereo into a mono-only port: choose Mid, Left, Right or Side')
  }
  return rescale ? needsAdapters(rescale) : ok()
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

  if (isValueType(from.type) && isValueType(to.type)) return connectValues(from, to)

  if (from.type === to.type) {
    switch (from.type) {
      case 'event':
        return ok()
      case 'note':
        return ok()
      case 'spectral':
        return reject('Spectral is reserved, not yet implemented')
    }
  }

  if (isValueType(from.type) && to.type === 'event') {
    return needsAdapters('A value into an Event-typed port needs a Threshold')
  }

  return reject('Incompatible signal types with no adapter available yet')
}

export interface ConnectionEndpoint {
  nodeId: string
  portId: string
  direction: 'input' | 'output'
  port: PortDescriptor
  /** A polymorphic node (deco.reroute) with nothing feeding it yet: its declared
      port type is only a default, so there is nothing real to predict with. See
      graphStore.endpointFor().
  */
  unresolved?: boolean
}

/** Also resolves a growable-group member the default descriptor doesn't list
    (the spare `in.3` revealed on a node whose `in.0..in.2` are wired) — see
    portGroups.ts. Without that, dropping a cable on the spare port would find
    no port and be treated as a miss.
*/
export function findPort(descriptor: { inputs: PortDescriptor[]; outputs: PortDescriptor[] }, portId: string, direction: 'input' | 'output'): PortDescriptor | undefined {
  const list = direction === 'input' ? descriptor.inputs : descriptor.outputs
  const listed = list.find((p) => p.id === portId)
  if (listed || direction !== 'input') return listed
  return synthesizeGroupPort(descriptor, portId)
}

/** wiki/plans/DomainRedesign.md Batch 4's own UI-side mirror of
    MultiplicityResolver's real origin-mismatch rejection
    (MultiplicityResolver.cpp: "Node 'X' is fed by two different voice
    allocators ('Y', 'Z')...") — a rule CanConnect.cpp itself has no notion
    of (multiplicity/origin resolution is a whole-graph fixed-point pass,
    not a per-port type/quantity check), so it lives here as an extra guard
    in canConnect() rather than inside canConnectPorts() above, which stays
    a hand-mirror of CanConnect.cpp only. Only ever fires for two ALREADY-
    Poly endpoints with two DIFFERENT known origins; an unresolved or
    Scalar endpoint, or one with no live multiplicity data at all (the
    gallery, or a node the engine hasn't compiled into any plan yet), lets
    the drop through exactly as before — the engine still gets the final
    say (connectWithAutoAdapt), this is prediction only.
*/
function hasOriginMismatch(
  output: ConnectionEndpoint,
  input: ConnectionEndpoint,
  multiplicity?: ReadonlyMap<string, { ports: ReadonlyMap<string, PortMultiplicityInfo> }>,
): boolean {
  if (!multiplicity) return false
  const outputInfo = multiplicity.get(output.nodeId)?.ports.get(output.portId)
  const inputInfo = multiplicity.get(input.nodeId)?.ports.get(input.portId)
  if (outputInfo?.kind !== 'poly' || inputInfo?.kind !== 'poly') return false
  if (!outputInfo.originId || !inputInfo.originId) return false
  return outputInfo.originId !== inputInfo.originId
}

/** Live wire-drag feedback only needs a yes/no (ADR-0010's colour scheme
    has no third "will insert an adapter" visual state yet — NeedsAdapters
    renders identically to Ok during a drag; only a true Reject shows the
    red-dashed rejected treatment). The actual commit
    (graphCommands.connectWithAutoAdapt) still gets the real three-outcome
    answer from the engine and inserts the adapter for real.

    `multiplicity` (optional — see hasOriginMismatch above) is the current
    graphStore snapshot's own per-node multiplicity map; omit it to predict
    with type/quantity rules alone, exactly as before this parameter existed.
*/
export function canConnect(
  output: ConnectionEndpoint,
  input: ConnectionEndpoint,
  multiplicity?: ReadonlyMap<string, { ports: ReadonlyMap<string, PortMultiplicityInfo> }>,
): boolean {
  if (output.direction !== 'output' || input.direction !== 'input') return false
  if (output.nodeId === input.nodeId) return false
  // Nothing real to predict with: let the drop through and let the engine decide.
  if (output.unresolved || input.unresolved) return true
  if (hasOriginMismatch(output, input, multiplicity)) return false
  return canConnectPorts(output.port, input.port).outcome !== 'reject'
}
