// Client-side graph state for the M10 node editor. Deliberately NOT wired
// to the M7 command bridge (GraphEditController / graphAddNode / etc.) —
// the user scoped this milestone to the UI interface only, since the real
// node architecture (which engine node types exist, their real port/param
// shapes) isn't settled yet. Placed nodes and wires here are local state
// only; nothing in this file ever calls a native function that mutates
// engine state. See CLAUDE.md's "known interim simplifications" note on
// this milestone for the full framing.
//
// A plain external store (subscribe/getSnapshot, read via useGraphSnapshot's
// useSyncExternalStore) rather than a state-management library — matches
// ui/src/telemetry/telemetryClient.ts's existing singleton-module
// convention. Node *positions* are the one exception to "store is the
// source of truth": during an active drag, GraphSurface.tsx mutates the
// node's DOM element directly (ARCHITECTURE.md §7's "high-rate rendering
// must run outside React's render cycle") and only calls commitNodeMoves()
// once, on pointerup — see GraphSurface.tsx's header comment.
import type { NodeDescriptor, PortDescriptor, SignalType } from './descriptorTypes'
import { MOCK_DESCRIPTORS } from './mockDescriptors'
import { fetchNodeDescriptors } from './fetchNodeDescriptors'
import { canConnect, findPort, type ConnectionEndpoint } from './wireRules'

/** A Macro node's own type/metatype (direct feedback: "it should be in the
    Macro node's capabilities to hold a type and metatype") — set once, at
    creation time, by addMacroFromPort() below, from whatever port it was
    dragged out of. Per-instance (unlike every other node, which shares one
    descriptor by typeId) because every dragged-out macro genuinely has its
    own shape: a Predelay "By" macro is 0-500ms, a Random "Rate" macro is a
    log-scaled 0.01-20Hz, an Add "a" macro (no clear bounds on the source
    port at all) falls back to a generic 0-1 Mod range. See
    resolveNodeDescriptor() for how this actually reaches NodeCard, and
    macroConfigForPort() for how it's derived. There's deliberately no UI
    yet to edit this after the fact (no "T" type-settings panel) — direct
    instruction: that's real, later work; this iteration only needs the
    node to be *able* to hold it.
*/
export interface MacroConfig {
  displayName: string
  signalType: SignalType
  unit: string
  minValue: number
  maxValue: number
  defaultValue: number
  isInteger: boolean
  options?: string[]
}

export interface GraphNode {
  id: string
  typeId: string
  x: number
  y: number
  titleOverride?: string
  bypassed: boolean
  error?: string
  /** Per-instance overrides of a parameter's or unconnected input port's
      own descriptor-declared defaultValue, keyed by parameter/port id —
      what a dragged/typed ValueSlider (NodeCard.tsx) actually writes to,
      via setParameterValue() below. Absent for an id means "still at the
      descriptor's own default" — never eagerly populated for every
      parameter a node happens to have.
  */
  parameterValues?: Record<string, number>
  /** Only meaningful for typeId === 'mock.macro' — see MacroConfig's own
      comment. Absent means "still the plain, unconfigured mock.macro
      descriptor shape" (a manually Add-menu-placed macro, not one created
      by dragging a port out).
  */
  macroConfig?: MacroConfig
}

export interface GraphWire {
  id: string
  fromNodeId: string
  fromPortId: string
  toNodeId: string
  toPortId: string
}

export interface GraphSnapshot {
  nodes: GraphNode[]
  wires: GraphWire[]
  selection: ReadonlySet<string>
  descriptors: NodeDescriptor[]
  descriptorsLoaded: boolean
  /** Drives the top bar's Undo/Redo buttons' disabled state
      (M10_REVIEW.md §16/§23: Undo/Redo was the one keyboard-only action
      with no visible UI fallback at all). Reactive via the same snapshot
      subscription everything else here already uses.
  */
  canUndo: boolean
  canRedo: boolean
}

let nodes = new Map<string, GraphNode>()
let wires = new Map<string, GraphWire>()
let selection = new Set<string>()
let descriptors: NodeDescriptor[] = [...MOCK_DESCRIPTORS]
let descriptorsLoaded = false

// Declared here (ahead of buildSnapshot's own immediate call below) rather
// than down by the rest of the undo/redo machinery, purely so buildSnapshot
// can read past.length/future.length without a temporal-dead-zone error at
// module load — see that section's own comment for the undo/redo design.
interface HistorySnapshot {
  nodes: Map<string, GraphNode>
  wires: Map<string, GraphWire>
  selection: Set<string>
}
let past: HistorySnapshot[] = []
let future: HistorySnapshot[] = []

let nextId = 1
function makeId(prefix: string): string {
  return `${prefix}${nextId++}`
}

let cachedSnapshot: GraphSnapshot = buildSnapshot()
function buildSnapshot(): GraphSnapshot {
  return {
    nodes: [...nodes.values()],
    wires: [...wires.values()],
    selection,
    descriptors,
    descriptorsLoaded,
    canUndo: past.length > 0,
    canRedo: future.length > 0,
  }
}

const listeners = new Set<() => void>()
function notify(): void {
  cachedSnapshot = buildSnapshot()
  for (const listener of listeners) listener()
}

export function subscribe(listener: () => void): () => void {
  listeners.add(listener)
  return () => listeners.delete(listener)
}

export function getSnapshot(): GraphSnapshot {
  return cachedSnapshot
}

// ---- Descriptor catalog ----

export function getDescriptor(typeId: string): NodeDescriptor | undefined {
  return descriptors.find((d) => d.typeId === typeId)
}

/** The descriptor GraphSurface.tsx should actually render a given node
    with — identical to getDescriptor(node.typeId) for every node except a
    macro that carries its own MacroConfig (see that field's comment),
    where the single output port and the single parameter are rebuilt from
    it instead of the shared mock.macro shape. Everything else on the base
    descriptor (title, icon, layoutVariant...) passes through unchanged.
*/
export function resolveNodeDescriptor(node: GraphNode): NodeDescriptor | undefined {
  const base = getDescriptor(node.typeId)
  if (!base || node.typeId !== 'mock.macro' || !node.macroConfig) return base
  const cfg = node.macroConfig
  // M14 value-contract fields (kind/quantity/curve/polarity/enumOptions/
  // step/softMin/softMax/group) default to the same "no real tag set"
  // values mockDescriptors.ts's port()/parameter() helpers use — a macro's
  // rebuilt shape doesn't carry a quantity from MacroConfig today (that's
  // ADR-0015/M14's util.macro's own scope, not this local-graph mock path).
  const outPort: PortDescriptor = {
    id: 'out',
    type: cfg.signalType,
    label: 'Out',
    isPrimaryOutput: true,
    unit: cfg.unit,
    minValue: cfg.minValue,
    maxValue: cfg.maxValue,
    defaultValue: cfg.defaultValue,
    isInteger: cfg.isInteger,
    isLogScale: false,
    hasFallbackWhenUnconnected: false,
    options: cfg.options,
    kind: 'float',
    quantity: 'dimensionless',
    curve: 'linear',
    polarity: 'unipolar',
    enumOptions: [],
    step: 0,
    softMin: null,
    softMax: null,
    group: null,
    dataTags: [],
    channels: 'mono',
  }
  return {
    ...base,
    outputs: [outPort],
    parameters: [
      {
        id: 'value',
        minValue: cfg.minValue,
        maxValue: cfg.maxValue,
        defaultValue: cfg.defaultValue,
        skew: 1,
        unit: cfg.unit,
        displayName: cfg.displayName,
        isInteger: cfg.isInteger,
        options: cfg.options,
        kind: 'float',
        quantity: 'dimensionless',
        curve: 'linear',
        polarity: 'unipolar',
        enumOptions: [],
        step: 0,
        softMin: null,
        softMax: null,
        isStructural: false,
      },
    ],
  }
}

let initialized = false
/** Fetches real descriptors (no-op outside the real WebView, see
    fetchNodeDescriptors.ts) and merges them with the mocks already present,
    then seeds a small starting graph so there's something on screen to
    pan/select/drag/rewire immediately. Idempotent — safe to call from a
    React effect that may run more than once (StrictMode double-invoke).
*/
export function ensureInitialized(): void {
  if (initialized) return
  initialized = true

  void fetchNodeDescriptors()
    .then((real) => {
      if (real.length === 0) return
      const mockOnly = descriptors.filter((d) => d.isMock)
      descriptors = [...real, ...mockOnly]
      descriptorsLoaded = true
      notify()
    })
    .catch((error) => {
      // A rejected fetch (host not ready, native function missing) is a
      // harmless "no real descriptors yet" case, same as an empty result —
      // just don't leave an unhandled-rejection warning (M10_REVIEW.md §20).
      console.warn('fetchNodeDescriptors failed, continuing with mocks only', error)
    })
  // Mocks are already available synchronously, so mark loaded even before
  // (or if) the real fetch resolves with anything — the Add menu shouldn't
  // sit empty waiting on a fetch that returns [] in a plain browser tab.
  descriptorsLoaded = true

  seedDemoGraph()
}

function seedDemoGraph(): void {
  const audioIn = addNodeInternal('mock.audioIn', -260, -40)
  const predelay = addNodeInternal('mock.predelay', 0, -40)
  const masterOut = addNodeInternal('mock.masterOut', 300, -40)
  addNodeInternal('mock.midiNote', -260, 160)

  addWireInternal(audioIn, 'out', predelay, 'audio')
  addWireInternal(predelay, 'audio', masterOut, 'in')

  past = []
  future = []
  notify()
}

// ---- Endpoint lookup (shared by wire-drag hit-testing and rendering) ----

export function getEndpoint(nodeId: string, portId: string, direction: 'input' | 'output'): ConnectionEndpoint | undefined {
  const node = nodes.get(nodeId)
  if (!node) return undefined
  const descriptor = getDescriptor(node.typeId)
  if (!descriptor) return undefined
  const port = findPort(descriptor, portId, direction)
  if (!port) return undefined
  return { nodeId, portId, direction, descriptor, port }
}

export function findWireAtInput(nodeId: string, portId: string): GraphWire | undefined {
  for (const wire of wires.values()) {
    if (wire.toNodeId === nodeId && wire.toPortId === portId) return wire
  }
  return undefined
}

// ---- Undo/redo (whole-state snapshots — the graph is small enough that
// this is simpler and far less error-prone than hand-written per-action
// inverses, for the identical user-visible effect). HistorySnapshot/past/
// future are declared up near `nodes`/`wires` themselves — see that
// comment. ----

const MAX_HISTORY = 100

function cloneState(): HistorySnapshot {
  return {
    nodes: new Map([...nodes].map(([id, n]) => [id, { ...n }])),
    wires: new Map([...wires].map(([id, w]) => [id, { ...w }])),
    selection: new Set(selection),
  }
}

function restoreState(snapshot: HistorySnapshot): void {
  nodes = new Map([...snapshot.nodes].map(([id, n]) => [id, { ...n }]))
  wires = new Map([...snapshot.wires].map(([id, w]) => [id, { ...w }]))
  selection = new Set(snapshot.selection)
}

/** Every mutating action calls this with a snapshot taken BEFORE the
    mutation. `moveNode`/live-drag updates never call this directly — only
    `commitNodeMoves` does, once per drag gesture (blueprint §6.7's Ctrl+Z
    expectation: dragging a node is one undo step, not one per frame).
*/
function commit(before: HistorySnapshot): void {
  past.push(before)
  if (past.length > MAX_HISTORY) past.shift()
  future = []
  notify()
}

export function undo(): void {
  const before = past.pop()
  if (!before) return
  future.push(cloneState())
  restoreState(before)
  notify()
}

export function redo(): void {
  const next = future.pop()
  if (!next) return
  past.push(cloneState())
  restoreState(next)
  notify()
}

// ---- Actions ----

function addNodeInternal(typeId: string, x: number, y: number): string {
  const id = makeId('node')
  nodes.set(id, { id, typeId, x, y, bypassed: false })
  return id
}

function addWireInternal(fromNodeId: string, fromPortId: string, toNodeId: string, toPortId: string): string {
  const existing = findWireAtInput(toNodeId, toPortId)
  if (existing) wires.delete(existing.id)
  const id = makeId('wire')
  wires.set(id, { id, fromNodeId, fromPortId, toNodeId, toPortId })
  return id
}

export function addNode(typeId: string, x: number, y: number): string {
  const before = cloneState()
  const id = addNodeInternal(typeId, x, y)
  selection = new Set([id])
  commit(before)
  return id
}

/** Removes the given nodes plus any wire touching them, in one undo step. */
export function deleteNodes(ids: readonly string[]): void {
  if (ids.length === 0) return
  const before = cloneState()
  const idSet = new Set(ids)
  for (const id of idSet) {
    nodes.delete(id)
    selection.delete(id)
  }
  for (const [wireId, wire] of wires) {
    if (idSet.has(wire.fromNodeId) || idSet.has(wire.toNodeId)) wires.delete(wireId)
  }
  commit(before)
}

export function renameNode(id: string, title: string | undefined): void {
  const node = nodes.get(id)
  if (!node) return
  const before = cloneState()
  node.titleOverride = title && title.trim().length > 0 ? title.trim() : undefined
  commit(before)
}

export function toggleBypass(id: string): void {
  const node = nodes.get(id)
  if (!node) return
  const before = cloneState()
  node.bypassed = !node.bypassed
  commit(before)
}

/** Toggles bypass on every given node independently, in one undo step —
    used when right-clicking a node that's already part of the current
    multi-selection (M10_REVIEW.md §14: acting on the whole selection, not
    just the one node under the cursor).
*/
export function toggleBypassMany(ids: readonly string[]): void {
  if (ids.length === 0) return
  const before = cloneState()
  for (const id of ids) {
    const node = nodes.get(id)
    if (node) node.bypassed = !node.bypassed
  }
  commit(before)
}

/** One undo step per commit — called once, when a ValueSlider drag ends or
    a typed edit is confirmed (NodeCard.tsx), never on every intermediate
    drag tick (same "commit once per gesture" rule commitNodeMoves already
    follows for node dragging). Replaces `parameterValues` wholesale rather
    than mutating it in place, so the pre-commit snapshot cloneState() just
    took keeps its own untouched copy of the object — a real requirement,
    not just a style choice, since cloneState()'s per-node `{ ...n }` is
    shallow.
*/
export function setParameterValue(nodeId: string, parameterId: string, value: number): void {
  const node = nodes.get(nodeId)
  if (!node) return
  const before = cloneState()
  node.parameterValues = { ...node.parameterValues, [parameterId]: value }
  commit(before)
}

export function setSelection(ids: readonly string[]): void {
  const next = new Set(ids)
  if (next.size === selection.size && [...next].every((id) => selection.has(id))) return
  selection = next
  notify() // selection changes aren't undo-tracked, matching most node editors
}

/** Direct, immediate position write for a completed drag gesture (one undo
    step covering every node that moved) — the live per-frame position
    updates during the drag itself never touch the store, see this file's
    header comment. Fit-view never calls this; it only moves the camera.
*/
export function commitNodeMoves(updates: ReadonlyArray<{ id: string; x: number; y: number }>): void {
  if (updates.length === 0) return
  const before = cloneState()
  for (const update of updates) {
    const node = nodes.get(update.id)
    if (node) {
      node.x = update.x
      node.y = update.y
    }
  }
  commit(before)
}

export function addWire(fromNodeId: string, fromPortId: string, toNodeId: string, toPortId: string): string {
  const before = cloneState()
  const id = addWireInternal(fromNodeId, fromPortId, toNodeId, toPortId)
  commit(before)
  return id
}

export function removeWire(id: string): void {
  if (!wires.has(id)) return
  const before = cloneState()
  wires.delete(id)
  commit(before)
}

/** Resolves a completed wire-drag gesture (InfiniteCanvas.tsx) in one undo
    step: `detachedWireId` is set when the drag started by grabbing an
    existing wire's input end (that wire is hidden, not yet removed, while
    the drag is in progress — see InfiniteCanvas.tsx's per-frame renderer
    loop); `target` is the input port the drag ended on, or null if it
    ended on empty space / an invalid target. No-ops (no history entry) if
    neither a detach nor a new connection actually happened, e.g. a fresh
    drag from an output that was dropped nowhere.
*/
export function commitWireDrag(fromNodeId: string, fromPortId: string, target: { nodeId: string; portId: string } | null, detachedWireId?: string): void {
  if (!detachedWireId && !target) return
  const before = cloneState()
  if (detachedWireId) wires.delete(detachedWireId)
  if (target) addWireInternal(fromNodeId, fromPortId, target.nodeId, target.portId)
  commit(before)
}

function splicePrimaryPorts(descriptor: NodeDescriptor): { inputId: string; outputId: string } | undefined {
  const primaryInputId = descriptor.inputs[0]?.id
  const primaryOutputId = (descriptor.outputs.find((o) => o.isPrimaryOutput) ?? descriptor.outputs[0])?.id
  if (!primaryInputId || !primaryOutputId) return undefined
  return { inputId: primaryInputId, outputId: primaryOutputId }
}

/** Whether `typeId` could validly splice into `wireId`: the candidate needs
    at least one input *and* output (blueprint §6.2), and both new
    connections it would create must pass the same `canConnect` type check a
    normal drag-to-wire connection does (M10_REVIEW.md §23's retrospective:
    "I'd make splicing go through the same validity check a normal
    drag-connection does"). Used both to gate the click in InfiniteCanvas's
    ghost-hover hint and as spliceInsert's own guard below, so there's no way
    to reach an invalid splice through either path.
*/
export function canSplice(wireId: string, typeId: string): boolean {
  const wire = wires.get(wireId)
  const descriptor = getDescriptor(typeId)
  const primary = descriptor && splicePrimaryPorts(descriptor)
  if (!wire || !descriptor || !primary) return false

  const sourceEndpoint = getEndpoint(wire.fromNodeId, wire.fromPortId, 'output')
  const destEndpoint = getEndpoint(wire.toNodeId, wire.toPortId, 'input')
  if (!sourceEndpoint || !destEndpoint) return false

  const candidateInput = findPort(descriptor, primary.inputId, 'input')
  const candidateOutput = findPort(descriptor, primary.outputId, 'output')
  if (!candidateInput || !candidateOutput) return false

  const intoCandidate = canConnect(sourceEndpoint, { nodeId: '__splice__', portId: primary.inputId, direction: 'input', descriptor, port: candidateInput })
  const outOfCandidate = canConnect({ nodeId: '__splice__', portId: primary.outputId, direction: 'output', descriptor, port: candidateOutput }, destEndpoint)
  return intoCandidate && outOfCandidate
}

/** Inserts a new node into an existing wire (blueprint §6.2's "splice on
    wire hover"): removes the old wire, adds the node, rewires both sides
    through it using the same "primary port" heuristic NodeCard.tsx's
    splitPorts() uses for its merged-row display (first input, the
    isPrimaryOutput-flagged output or the first one) — duplicated here in
    spirit rather than imported since NodeCard deliberately exposes no
    non-rendering API. Returns undefined (no-op) if canSplice() would reject
    it — callers that want a fallback (e.g. placing the node unconnected
    instead) check canSplice() themselves before deciding whether to call
    this at all, see InfiniteCanvas.tsx's onClick.
*/
export function spliceInsert(wireId: string, typeId: string, x: number, y: number): string | undefined {
  if (!canSplice(wireId, typeId)) return undefined
  const wire = wires.get(wireId)
  const descriptor = getDescriptor(typeId)
  const primary = descriptor && splicePrimaryPorts(descriptor)
  if (!wire || !descriptor || !primary) return undefined

  const before = cloneState()
  wires.delete(wireId)
  const newNodeId = addNodeInternal(typeId, x, y)
  addWireInternal(wire.fromNodeId, wire.fromPortId, newNodeId, primary.inputId)
  addWireInternal(newNodeId, primary.outputId, wire.toNodeId, wire.toPortId)
  selection = new Set([newNodeId])
  commit(before)
  return newNodeId
}

// ---- Drag-a-port-out-to-a-Macro shortcut (direct feedback: "dragging a
// cable from an unconnected input socket and releasing it on empty space...
// automatically create[s] a macro with the appropriate type... propagate
// the current value... and the appropriate connection"). ----

/** Whether `port` could sensibly become a Macro's value at all — the one
    thing direct feedback ruled out explicitly ("an audio input obviously
    cannot") is Audio, since a macro is a scalar/discrete automatable value,
    not an audio-rate signal; every other SignalType (Control, Event,
    Boolean...) has some meaningful macro shape (see macroConfigForPort).
    Deliberately NOT narrowed further to "only ports with a numeric
    fallback" (PortDescriptor.hasFallbackWhenUnconnected) — Add's "a"/"b"
    inputs have no in-node fallback at all today and are still meant to be
    macro-able, just with an unclear metatype that defaults to a generic
    Mod range (direct feedback's own example).
*/
export function isMacroablePort(port: Pick<PortDescriptor, 'type'>): boolean {
  return port.type !== 'audio'
}

/** Derives a new Macro's type/metatype from the port it's being dragged out
    of, per direct feedback's own rules:
      - a discrete preset list (an Event port with `options` — Random's
        Trigger dropdown) -> an Enum-shaped macro over those same options.
      - Event without options, or Boolean -> a small integer/boolean range;
        there's no numeric "metatype" to speak of either way.
      - Control with clear bounds (minValue/maxValue both set) -> copy them
        (and unit/isInteger) exactly, so e.g. a Predelay "By" macro is
        still 0-500ms, not a generic 0-1.
      - Control with NO clear bounds (Add's "a"/"b", util.constant-shaped
        ports, anything that was never given real min/max) -> a generic Mod
        range (0-1, no unit) — direct feedback's explicit fallback for "the
        metatype isn't clear." A real metatype-inference system is the
        deferred type-system rehaul, not this pass.
    `currentValue` (the port's live fallback value or selected option
    index, from GraphNode.parameterValues) becomes the macro's seeded
    defaultValue, clamped into whatever range was chosen.
*/
function macroConfigForPort(port: PortDescriptor, currentValue: number): MacroConfig {
  const displayName = port.label || port.id
  const clamp = (value: number, min: number, max: number): number => Math.min(max, Math.max(min, value))

  if (port.type === 'event' && port.options && port.options.length > 0) {
    const maxIndex = port.options.length - 1
    return {
      displayName,
      signalType: 'event',
      unit: '',
      minValue: 0,
      maxValue: maxIndex,
      defaultValue: clamp(Math.round(currentValue), 0, maxIndex),
      isInteger: true,
      options: [...port.options],
    }
  }
  if (port.type === 'event' || port.type === 'boolean') {
    return { displayName, signalType: port.type, unit: '', minValue: 0, maxValue: 1, defaultValue: clamp(Math.round(currentValue), 0, 1), isInteger: true }
  }

  const hasClearBounds = port.minValue !== null && port.maxValue !== null
  if (hasClearBounds) {
    const min = port.minValue as number
    const max = port.maxValue as number
    return { displayName, signalType: 'control', unit: port.unit, minValue: min, maxValue: max, defaultValue: clamp(currentValue, min, max), isInteger: port.isInteger }
  }
  // Unclear metatype (direct feedback's own Add-node example): default to
  // a generic Mod range rather than guess at real DSP semantics.
  return { displayName, signalType: 'control', unit: '', minValue: 0, maxValue: 1, defaultValue: clamp(currentValue, 0, 1), isInteger: false }
}

/** Creates a new Macro node at (x, y) pre-configured from `portId` on
    `nodeId`, wires the macro's output straight into that port, and seeds
    the macro's value from whatever that port currently shows (its
    parameterValues override, or the descriptor's own defaultValue) — one
    undo step, same shape as spliceInsert's. No-ops (returns undefined) if
    the port doesn't exist or isMacroablePort() rejects it; callers
    (InfiniteCanvas.tsx's drag-to-empty-space gesture) are expected to have
    already checked isMacroablePort() before even starting the drag, this
    is just the same guarantee at the point of committing.
*/
export function addMacroFromPort(nodeId: string, portId: string, x: number, y: number): string | undefined {
  const endpoint = getEndpoint(nodeId, portId, 'input')
  if (!endpoint || !isMacroablePort(endpoint.port)) return undefined

  const node = nodes.get(nodeId)
  const currentValue = node?.parameterValues?.[portId] ?? endpoint.port.defaultValue
  const macroConfig = macroConfigForPort(endpoint.port, currentValue)

  const before = cloneState()
  const macroId = addNodeInternal('mock.macro', x, y)
  const macroNode = nodes.get(macroId)
  if (macroNode) {
    macroNode.macroConfig = macroConfig
    macroNode.parameterValues = { value: macroConfig.defaultValue }
  }
  addWireInternal(macroId, 'out', nodeId, portId)
  selection = new Set([macroId])
  commit(before)
  return macroId
}
