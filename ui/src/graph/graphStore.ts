// Client-side mirror of the REAL engine graph (M19, ADR-0025) — every
// mutating action here fires a real command over the M7 bridge
// (graphCommands.ts) and, once confirmed, re-syncs local state from the
// engine's own JSON snapshot; nothing here invents graph content the engine
// doesn't also have. Earlier (M10) this was a fully local, disconnected
// prototype — see ADR-0025 and CLAUDE.md's "M10's node editor canvas is
// deliberately not wired" note for that history. The canvas is real-graph-
// only as of M19: mock.* node types stay in the read-only component gallery
// (ComponentGallery.tsx, its own separate descriptor fetch+merge) but are no
// longer placeable here, since they have no engine backing to wire to —
// including the M10 "drag a port out to create a Macro" shortcut
// (addMacroFromPort), retired for the same reason (`util.macro`, ADR-0015,
// isn't a real registered node type yet).
//
// A plain external store (subscribe/getSnapshot, read via useGraphSnapshot's
// useSyncExternalStore) rather than a state-management library — matches
// ui/src/telemetry/telemetryClient.ts's existing singleton-module
// convention. Node *positions* are the one exception to "store is the
// source of truth during a gesture": during an active drag, GraphSurface.tsx
// mutates the node's DOM element directly (ARCHITECTURE.md §7's "high-rate
// rendering must run outside React's render cycle") and only calls
// commitNodeMoves() once, on pointerup — see GraphSurface.tsx's header
// comment.
//
// Undo/redo (ADR-0025): a client-side stack of whole-graph JSON snapshots
// (from graphGetSnapshot/graphRestoreSnapshot), not a per-command inverse
// log. Every exported mutating action here is NOT optimistic (deviating
// from NODE_EDITOR.md §6's "apply immediately, reconcile after" proposal,
// a deliberate M19 simplification): it awaits the real command's result and
// only then updates local nodes/wires from the engine's own confirmed
// state, so there is never a local/engine divergence to reconcile. A local
// WebView native-function round-trip is fast enough (no network involved)
// that this costs no perceptible responsiveness — every mutating action
// here already only commits once per user gesture (a full drag, not per
// frame), never on every intermediate tick.
import type { NodeDescriptor } from './descriptorTypes'
import { fetchNodeDescriptors } from './fetchNodeDescriptors'
import { canConnectPorts, findPort, type ConnectionEndpoint } from './canConnect'
import {
  graphAddNode,
  graphConnectWithAutoAdapt,
  graphDeleteNode,
  graphDisconnect,
  graphGetSnapshot,
  graphMoveNode,
  graphRestoreSnapshot,
  graphSetParameterValue,
  graphSetProperty,
  type CommandResult,
} from './graphCommands'

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
      mirrors the engine's own NodeInstance.parameters for this node.
  */
  parameterValues?: Record<string, number>
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
  canUndo: boolean
  canRedo: boolean
  /** The most recent rejected command's reason, or null — NODE_EDITOR.md
      §6's "a rejected command surfaces as an error banner." Cleared at the
      start of the next user gesture, not on a timer.
  */
  lastError: string | null
}

// ---- Wire shape from the engine's own PatchDocument JSON -----------------
// Matches PatchSerializer.cpp's nodeToVar/connectionToVar exactly (a graph
// snapshot is a PatchDocument with empty macro/view/meta fields — ADR-0025).
interface PatchNodeJson {
  id: string
  type: string
  position?: { x: number; y: number }
  parameters?: Record<string, number>
  properties?: Record<string, unknown>
}
interface PatchConnectionJson {
  fromNodeId: string
  fromPortId: string
  toNodeId: string
  toPortId: string
}
interface PatchDocumentJson {
  nodes?: PatchNodeJson[]
  connections?: PatchConnectionJson[]
}

function wireId(toNodeId: string, toPortId: string): string {
  // An input port accepts at most one incoming connection (engine-enforced),
  // so (toNodeId, toPortId) alone is already a stable, unique key — no need
  // to also fold in the source endpoint.
  return `${toNodeId}:${toPortId}`
}

function patchJsonToLocalState(json: string): { nodes: Map<string, GraphNode>; wires: Map<string, GraphWire> } {
  const doc = JSON.parse(json) as PatchDocumentJson
  const nodes = new Map<string, GraphNode>()
  for (const n of doc.nodes ?? []) {
    const properties = n.properties ?? {}
    const title = properties.title
    nodes.set(n.id, {
      id: n.id,
      typeId: n.type,
      x: n.position?.x ?? 0,
      y: n.position?.y ?? 0,
      titleOverride: typeof title === 'string' && title.length > 0 ? title : undefined,
      bypassed: properties.bypassed === true,
      parameterValues: n.parameters && Object.keys(n.parameters).length > 0 ? { ...n.parameters } : undefined,
    })
  }
  const wires = new Map<string, GraphWire>()
  for (const c of doc.connections ?? []) {
    const id = wireId(c.toNodeId, c.toPortId)
    wires.set(id, { id, fromNodeId: c.fromNodeId, fromPortId: c.fromPortId, toNodeId: c.toNodeId, toPortId: c.toPortId })
  }
  return { nodes, wires }
}

let nodes = new Map<string, GraphNode>()
let wires = new Map<string, GraphWire>()
let selection = new Set<string>()
let descriptors: NodeDescriptor[] = []
let descriptorsLoaded = false
let lastError: string | null = null

// The engine's own last-confirmed graph JSON — the "before" a history entry
// captures is always this value, read synchronously (it's just a cached
// string), even though refreshing it after a command is async.
let engineSnapshotCache: string | null = null

let past: string[] = []
let future: string[] = []
const MAX_HISTORY = 100

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
    lastError,
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

// ---- Descriptor catalog (real only — see this file's header comment) ----

export function getDescriptor(typeId: string): NodeDescriptor | undefined {
  return descriptors.find((d) => d.typeId === typeId)
}

/** Was per-instance-shape-rebuilding for a Macro node before M19 retired
    live-canvas macro placement — every node now resolves straight through
    to its shared typeId descriptor, kept as its own function since
    GraphSurface.tsx already calls it by name and a future per-instance
    shape (a real util.macro's bound range, say) would land here again.
*/
export function resolveNodeDescriptor(node: GraphNode): NodeDescriptor | undefined {
  return getDescriptor(node.typeId)
}

let initialized = false
/** Fetches real descriptors and the engine's current graph, then seeds
    local state from it — idempotent, safe to call from a React effect that
    may run more than once (StrictMode double-invoke). Outside the real
    WebView (a plain browser tab), both fetches resolve to nothing and the
    canvas is simply empty — expected, since there is no real graph to show
    (this project's own convention: node-editor iteration happens in the
    Standalone app, not a browser tab).
*/
export function ensureInitialized(): void {
  if (initialized) return
  initialized = true

  void fetchNodeDescriptors()
    .then((real) => {
      descriptors = real
      descriptorsLoaded = true
      notify()
    })
    .catch((error) => {
      console.warn('fetchNodeDescriptors failed', error)
      descriptorsLoaded = true
      notify()
    })

  void graphGetSnapshot()
    .then((json) => {
      if (json === null) return
      engineSnapshotCache = json
      const state = patchJsonToLocalState(json)
      nodes = state.nodes
      wires = state.wires
      notify()
    })
    .catch((error) => {
      console.warn('graphGetSnapshot failed', error)
    })
}

// ---- Endpoint lookup (shared by wire-drag hit-testing and rendering) ----

export function getEndpoint(nodeId: string, portId: string, direction: 'input' | 'output'): ConnectionEndpoint | undefined {
  const node = nodes.get(nodeId)
  if (!node) return undefined
  const descriptor = getDescriptor(node.typeId)
  if (!descriptor) return undefined
  const port = findPort(descriptor, portId, direction)
  if (!port) return undefined
  return { nodeId, portId, direction, port }
}

export function findWireAtInput(nodeId: string, portId: string): GraphWire | undefined {
  return wires.get(wireId(nodeId, portId))
}

// ---- Undo/redo (ADR-0025: whole-graph JSON snapshots) --------------------

export function undo(): void {
  void (async () => {
    const beforeJson = past.pop()
    if (beforeJson === undefined || engineSnapshotCache === null) return
    const afterJson = engineSnapshotCache
    const result = await graphRestoreSnapshot(beforeJson)
    if (!result.success) {
      past.push(beforeJson) // put it back — the restore itself was rejected
      lastError = result.errorMessage
      notify()
      return
    }
    future.push(afterJson)
    engineSnapshotCache = beforeJson
    const state = patchJsonToLocalState(beforeJson)
    nodes = state.nodes
    wires = state.wires
    lastError = null
    notify()
  })()
}

export function redo(): void {
  void (async () => {
    const afterJson = future.pop()
    if (afterJson === undefined || engineSnapshotCache === null) return
    const beforeJson = engineSnapshotCache
    const result = await graphRestoreSnapshot(afterJson)
    if (!result.success) {
      future.push(afterJson)
      lastError = result.errorMessage
      notify()
      return
    }
    past.push(beforeJson)
    engineSnapshotCache = afterJson
    const state = patchJsonToLocalState(afterJson)
    nodes = state.nodes
    wires = state.wires
    lastError = null
    notify()
  })()
}

// ---- Command plumbing -----------------------------------------------------

/** Fires one real command; records the rejection reason (if any) for the
    error banner. Returns whether it succeeded, for composite gestures
    (spliceInsert) to decide whether to keep going.
*/
async function fireCommand(fire: () => Promise<CommandResult>): Promise<boolean> {
  const result = await fire()
  if (!result.success) lastError = result.errorMessage
  return result.success
}

/** Wraps one user gesture (a single command, or a short sequence the UI
    treats as one gesture — spliceInsert's disconnect+addNode+2×connect):
    captures the engine's current snapshot as "before", runs the gesture,
    then re-syncs local nodes/wires from the engine's own confirmed state
    and pushes a history entry if anything actually changed. Clears
    lastError at the start, exactly once per gesture, not per sub-command.
*/
async function withHistory(gesture: () => Promise<void>): Promise<void> {
  lastError = null
  const beforeJson = engineSnapshotCache
  await gesture()

  const afterJson = await graphGetSnapshot()
  if (afterJson !== null) {
    engineSnapshotCache = afterJson
    const state = patchJsonToLocalState(afterJson)
    nodes = state.nodes
    wires = state.wires
    if (beforeJson !== null && beforeJson !== afterJson) {
      past.push(beforeJson)
      if (past.length > MAX_HISTORY) past.shift()
      future = []
    }
  }
  notify()
}

// ---- Actions ---------------------------------------------------------------

export async function addNode(typeId: string, x: number, y: number): Promise<string> {
  const id = makeId('node')
  await withHistory(async () => {
    const ok = await fireCommand(() => graphAddNode(typeId, id, x, y))
    if (ok) selection = new Set([id])
  })
  return id
}

/** Removes the given nodes, one command per node, in one undo step. */
export function deleteNodes(ids: readonly string[]): void {
  if (ids.length === 0) return
  void withHistory(async () => {
    for (const id of ids) {
      selection.delete(id)
      await fireCommand(() => graphDeleteNode(id))
    }
  })
}

export function renameNode(id: string, title: string | undefined): void {
  void withHistory(() => fireCommand(() => graphSetProperty(id, 'title', title && title.trim().length > 0 ? title.trim() : '')).then(() => undefined))
}

export function toggleBypass(id: string): void {
  const node = nodes.get(id)
  if (!node) return
  const next = !node.bypassed
  void withHistory(() => fireCommand(() => graphSetProperty(id, 'bypassed', next)).then(() => undefined))
}

/** Toggles bypass on every given node independently, in one undo step —
    used when right-clicking a node that's already part of the current
    multi-selection (M10_REVIEW.md §14: acting on the whole selection, not
    just the one node under the cursor).
*/
export function toggleBypassMany(ids: readonly string[]): void {
  if (ids.length === 0) return
  void withHistory(async () => {
    for (const id of ids) {
      const node = nodes.get(id)
      if (!node) continue
      await fireCommand(() => graphSetProperty(id, 'bypassed', !node.bypassed))
    }
  })
}

/** One undo step per commit — called once, when a ValueSlider drag ends or
    a typed edit is confirmed (NodeCard.tsx), never on every intermediate
    drag tick (same "commit once per gesture" rule commitNodeMoves already
    follows for node dragging).
*/
export function setParameterValue(nodeId: string, parameterId: string, value: number): void {
  void withHistory(() => fireCommand(() => graphSetParameterValue(nodeId, parameterId, value)).then(() => undefined))
}

export function setSelection(ids: readonly string[]): void {
  const next = new Set(ids)
  if (next.size === selection.size && [...next].every((id) => selection.has(id))) return
  selection = next
  notify() // selection changes aren't undo-tracked, matching most node editors
}

/** Position update for a completed drag gesture (one undo step covering
    every node that moved, one graphMoveNode call each) — the live per-frame
    position updates during the drag itself never touch the store, see this
    file's header comment. Fit-view never calls this; it only moves the
    camera.
*/
export function commitNodeMoves(updates: ReadonlyArray<{ id: string; x: number; y: number }>): void {
  if (updates.length === 0) return
  void withHistory(async () => {
    for (const update of updates) await fireCommand(() => graphMoveNode(update.id, update.x, update.y))
  })
}

export function addWire(fromNodeId: string, fromPortId: string, toNodeId: string, toPortId: string): void {
  void withHistory(() => fireCommand(() => graphConnectWithAutoAdapt(fromNodeId, fromPortId, toNodeId, toPortId)).then(() => undefined))
}

export function removeWire(id: string): void {
  const wire = wires.get(id)
  if (!wire) return
  void withHistory(() => fireCommand(() => graphDisconnect(wire.fromNodeId, wire.fromPortId, wire.toNodeId, wire.toPortId)).then(() => undefined))
}

/** Resolves a completed wire-drag gesture (InfiniteCanvas.tsx) in one undo
    step: `detachedWireId` is set when the drag started by grabbing an
    existing wire's input end; `target` is the input port the drag ended on,
    or null if it ended on empty space / an invalid target. No-ops (no
    history entry) if neither a detach nor a new connection actually
    happened.
*/
export function commitWireDrag(fromNodeId: string, fromPortId: string, target: { nodeId: string; portId: string } | null, detachedWireId?: string): void {
  if (!detachedWireId && !target) return
  void withHistory(async () => {
    if (detachedWireId) {
      const wire = wires.get(detachedWireId)
      if (wire) await fireCommand(() => graphDisconnect(wire.fromNodeId, wire.fromPortId, wire.toNodeId, wire.toPortId))
    }
    if (target) await fireCommand(() => graphConnectWithAutoAdapt(fromNodeId, fromPortId, target.nodeId, target.portId))
  })
}

function splicePrimaryPorts(descriptor: NodeDescriptor): { inputId: string; outputId: string } | undefined {
  const primaryInputId = descriptor.inputs[0]?.id
  const primaryOutputId = (descriptor.outputs.find((o) => o.isPrimaryOutput) ?? descriptor.outputs[0])?.id
  if (!primaryInputId || !primaryOutputId) return undefined
  return { inputId: primaryInputId, outputId: primaryOutputId }
}

/** Whether `typeId` could validly splice into `wireId`: the candidate needs
    at least one input *and* output, and both new connections it would
    create must pass the same canConnect check a normal drag-to-wire
    connection does. Purely local/synchronous — called every animation
    frame for ghost-hover feedback, so it predicts with canConnect.ts
    exactly like a normal wire drag does, never touching the engine (the
    actual commit, spliceInsert below, does).
*/
export function canSplice(wireIdToSplice: string, typeId: string): boolean {
  const wire = wires.get(wireIdToSplice)
  const descriptor = getDescriptor(typeId)
  const primary = descriptor && splicePrimaryPorts(descriptor)
  if (!wire || !descriptor || !primary) return false

  const sourceEndpoint = getEndpoint(wire.fromNodeId, wire.fromPortId, 'output')
  const destEndpoint = getEndpoint(wire.toNodeId, wire.toPortId, 'input')
  if (!sourceEndpoint || !destEndpoint) return false

  const candidateInput = findPort(descriptor, primary.inputId, 'input')
  const candidateOutput = findPort(descriptor, primary.outputId, 'output')
  if (!candidateInput || !candidateOutput) return false

  const intoCandidate = canConnectPorts(sourceEndpoint.port, candidateInput).outcome !== 'reject'
  const outOfCandidate = canConnectPorts(candidateOutput, destEndpoint.port).outcome !== 'reject'
  return intoCandidate && outOfCandidate
}

/** Inserts a new node into an existing wire (disconnect + addNode +
    2×connect, one undo step via withHistory) — a real composite gesture
    (ADR-0025), not a single applyBatch call (see that ADR's own reasoning).
    No-ops if canSplice() would reject it.
*/
export function spliceInsert(wireIdToSplice: string, typeId: string, x: number, y: number): string | undefined {
  if (!canSplice(wireIdToSplice, typeId)) return undefined
  const wire = wires.get(wireIdToSplice)
  const descriptor = getDescriptor(typeId)
  const primary = descriptor && splicePrimaryPorts(descriptor)
  if (!wire || !descriptor || !primary) return undefined

  const newNodeId = makeId('node')

  void withHistory(async () => {
    if (!(await fireCommand(() => graphDisconnect(wire.fromNodeId, wire.fromPortId, wire.toNodeId, wire.toPortId)))) return
    if (!(await fireCommand(() => graphAddNode(typeId, newNodeId, x, y)))) return
    if (!(await fireCommand(() => graphConnectWithAutoAdapt(wire.fromNodeId, wire.fromPortId, newNodeId, primary.inputId)))) return
    if (!(await fireCommand(() => graphConnectWithAutoAdapt(newNodeId, primary.outputId, wire.toNodeId, wire.toPortId)))) return
    selection = new Set([newNodeId])
  })

  return newNodeId
}
