// Client-side mirror of the REAL engine graph (M19, ADR-0025) — every
// mutating action here fires a real command over the M7 bridge
// (graphCommands.ts) and, once confirmed, re-syncs local state from the
// engine's own JSON snapshot; nothing here invents graph content the engine
// doesn't also have. Earlier (M10) this was a fully local, disconnected
// prototype — see ADR-0025 and CLAUDE.md's "M10's node editor canvas is
// deliberately not wired" note for that history. The canvas is real-graph-
// only as of M19: mock.* node types stay in the read-only component gallery
// (ComponentGallery.tsx, its own separate descriptor fetch+merge) but are no
// longer placeable here, since they have no engine backing to wire to. The
// M10 "drag a port out to create a Macro" shortcut (addMacroFromPort) was
// retired for the same reason and is now BACK, revived as
// createMacroFromPort (see this file's own "Drag-a-port-out-to-a-Macro
// shortcut" section below) — `util.macro` is a real registered node type as
// of wiki/plans/UtilMacro.md (ADR-0015 amended, not reversed).
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
// log.
//
// Every exported mutating action IS locally optimistic (ADR-0025 Amendment,
// M20 — corrects that ADR's original "not optimistic" call): each one
// applies its own effect to local nodes/wires/selection *synchronously*,
// before the real command is even sent, then reconciles from the engine's
// own confirmed snapshot once the round trip resolves. The original
// non-optimistic design awaited the command's result first and only then
// updated local state — reasoned (wrongly, per real hands-on testing) to
// "cost no perceptible responsiveness" since the round trip is local, no
// network involved. It does not: every gesture-completion handler
// (ValueSlider's onUp, InfiniteCanvas's node-drag/wire-drag mouseup) clears
// its own LIVE/preview rendering state synchronously and immediately, and
// with nothing optimistic to fall back on, rendering reverted to the STALE
// pre-gesture store value for the round trip's duration before the resync
// corrected it — a real, visible glitch on every single node move,
// parameter drag, connect, and disconnect (release a dragged node/slider
// and it visibly snaps back before snapping to the right place; connect a
// wire and it briefly vanishes; disconnect one and it briefly reappears).
// Since the gesture-clearing and the optimistic store update now both
// happen synchronously within the same event-handler tick, there is never
// a frame where the live preview is gone but the store hasn't caught up.
// The post-command resync (graphGetSnapshot) is still unconditional and
// authoritative — it corrects anything the optimistic guess couldn't
// predict (e.g. connectWithAutoAdapt silently inserting an adapter node)
// and undoes the optimistic guess entirely if the command is rejected
// (rare enough — most rejections are caught by canConnect.ts's own
// prediction before a command is even sent — that a brief flash-then-
// revert there is an acceptable, honest "that didn't work" signal rather
// than the previous glitch-on-every-gesture cost).
import type { NodeDescriptor, PortDescriptor, Quantity, ValueKind } from './descriptorTypes'
import { fetchNodeDescriptors } from './fetchNodeDescriptors'
import { canConnectPorts, findPort, type ConnectionEndpoint } from './canConnect'
import { quantityUnit } from '../format/valueFormat'
import { classifyPortUiKind, type PortUiKind } from './portUiKind'
import {
  graphAddNode,
  graphConnectWithAutoAdapt,
  graphCreateMacro,
  graphDeleteNode,
  graphDisconnect,
  graphGetNodeMultiplicity,
  graphGetSnapshot,
  graphMoveNode,
  graphRestoreSnapshot,
  graphSetOutput,
  graphSetParameterValue,
  graphSetProperty,
  type CommandResult,
  type NodeMultiplicityBadge,
  type PortMultiplicityInfo,
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
  /** wiki/plans/PropsAndMacroRedesign.md Batch E / design/Macro.png: an
      Enum-typed macro's own option labels — a cosmetic property, not a
      real engine parameter (properties["util.macro.enumOptions"], a JSON
      string array), since
      TypedValueNodeBase.h's `buildTypedOutputPort`/`buildTypedValueParameter`
      don't populate PortDescriptor/ParameterDescriptor's own `enumOptions`
      for a Macro/Constant's Enum type (no per-instance storage for it
      engine-side yet — a confirmed, real gap, left alone per this task's own
      "engine behaviour stays as it is"). Undefined for every other node
      type, and for a macro that's never had its enum options set — see
      MacroBody.tsx's own `enumOptionLabelsFor()` for the synthesized
      "Option N" fallback used in that case.
  */
  macroEnumOptionLabels?: string[]
  /** design/Visualization/Count.png: "Either way they are editable by the
      user" — a view.count instance's own Min/Max footer, once the user has
      typed one in (properties["view.count.min"]/["view.count.max"], plain
      numbers via graphSetProperty, same storage shape `title`'s own string
      and `bypassed`'s own boolean already use for cosmetic per-instance
      state). Undefined for every other node type, and for a view.count
      instance whose footer has never been edited — CountBody.tsx's own
      effectiveRange() is what falls back to the connected source's
      declared range, or observed min/max, in that case.
  */
  countMinOverride?: number
  countMaxOverride?: number
  /** design/Visualization/Scope1.png's editable vertical-range footer — the
      generic, type-id-agnostic equivalent of countMinOverride/
      countMaxOverride above (properties["viewer.rangeMin"]/["viewer.rangeMax"],
      same plain-number-via-graphSetProperty storage), shared by every
      auto-ranging viewer (ScopeHistoryBody.tsx, every view.scope.* variant
      and view.gate) rather than one bespoke property-key pair per
      node type. Undefined until the user has edited it — ScopeHistoryBody.tsx's
      own auto-range/auto-freeze logic is what supplies a value before that.
  */
  viewerRangeMinOverride?: number
  viewerRangeMaxOverride?: number
  /** design/Visualization/ScopeMod.png's editable centre line
      (properties["viewer.center"]) — the value the filled trace is painted
      from. Same storage and same "undefined until edited" rule as the range
      pair above; ScopeHistoryBody.tsx supplies the default (the middle
      of the range: 0 for bipolar, 0.5 for unipolar) before that. */
  viewerCenterOverride?: number
}

/** TypedValueNodeBase.h's `TypedValueType` enum, mirrored — rewritten per
    direct correction, 2026-10-03: "controls (with subtypes), int (can be
    toggled to be enum), mod (uni/bi), trigger, bool. That is the system."
    THREE top-level kinds, not five — `Control` covers Value/Modulation/Int
    as subtypes of the SAME existing `quantity`/`isInteger` axes every
    other Control port in this codebase already uses (see
    `classifyMacroControlSubtype` below), rather than being separate
    parallel type values. `isEnum` (a new, separately-stored flag — see
    `ParameterValues`/MacroBody.tsx) is literally "Int toggled to be Enum".
    See `macroTypeOrdinal`/`macroTypeFromOrdinal` below for the ordinal
    round-trip — same hand-kept-in-sync convention `QUANTITY_ORDER` already
    established for `util.macro.quantity`. */
export type MacroValueType = 'control' | 'bool' | 'trigger'

/** Order MUST match TypedValueNodeBase.h's `commonParameters()` own
    `typeOptions` vector exactly (control, bool, then trigger when
    `includeTrigger` — util.macro always includes it, util.constant never
    does). Trigger stays LAST deliberately (same reasoning
    TypedValueNodeBase.h's own header comment gives). Same silent-mismatch
    risk `QUANTITY_ORDER`'s own comment already flags for quantity. */
export const MACRO_TYPE_ORDER: readonly MacroValueType[] = ['control', 'bool', 'trigger']
export function macroTypeOrdinal(type: MacroValueType): number {
  const index = MACRO_TYPE_ORDER.indexOf(type)
  return index >= 0 ? index : 0
}
export function macroTypeFromOrdinal(value: number): MacroValueType {
  const index = Math.round(value)
  return MACRO_TYPE_ORDER[index] ?? 'control'
}

/** A Control macro/constant's own "subtype" (direct instruction's own
    word) — a pure UI-level grouping over `isInteger`/`quantity`, not a
    separately stored field (TypedValueNodeBase.h stores no such thing
    either): Value is a real-quantity float, Modulation is quantity
    Unipolar/Bipolar, Int is `isInteger`. Mirrors exactly what
    `classifyPortUiKind` (portUiKind.ts) already derives for every ordinary
    Control port's colour — this just names the same three-way split for
    the macro type system specifically. */
export type MacroControlSubtype = 'value' | 'modulation' | 'int'
export function classifyMacroControlSubtype(isInteger: boolean, quantity: Quantity): MacroControlSubtype {
  if (isInteger) return 'int'
  if (quantity === 'unipolar' || quantity === 'bipolar') return 'modulation'
  return 'value'
}

function isTypedValueNodeType(typeId: string): boolean {
  return typeId === 'util.macro' || typeId === 'util.constant'
}

/** `endpointFor`'s own live-resolution for a util.macro/util.constant
    output — a TypeScript mirror of TypedValueNodeBase.h's
    `buildTypedOutputPort` (outputSignalTypeFor/outputValueKindFor/
    portIsIntegerFor/effectiveMinMax/unitForQuantity), hand-kept in sync
    with that C++ the same way QUANTITY_ORDER/MACRO_TYPE_ORDER above
    already are. Reads `node.parameterValues` directly (both node types
    share the same `<prefix>.type`/`.isInteger`/`.isEnum`/`.min`/`.max`/
    `.quantity` ids, `node.typeId` itself IS that prefix) rather than
    trusting `staticPort` (the one-shot default-instance descriptor
    `findPort` above just returned), which is what was frozen/wrong.
    Everything else about the port (id, label, isPrimaryOutput, hidden, ...)
    is copied from `staticPort` unchanged — only the value-contract fields
    actually depend on live parameters.
*/
function resolveTypedValueOutputPort(node: GraphNode, staticPort: PortDescriptor): PortDescriptor {
  const prefix = node.typeId
  const pv = node.parameterValues ?? {}
  const type = macroTypeFromOrdinal(pv[`${prefix}.type`] ?? 0)
  const isInteger = (pv[`${prefix}.isInteger`] ?? 0) >= 0.5
  const isEnum = (pv[`${prefix}.isEnum`] ?? 0) >= 0.5
  const quantity = quantityFromOrdinal(pv[`${prefix}.quantity`] ?? 0)
  const rawMin = pv[`${prefix}.min`] ?? 0
  const rawMax = pv[`${prefix}.max`] ?? 1

  if (type === 'bool') return { ...staticPort, type: 'boolean', kind: 'bool', isInteger: false, quantity, minValue: null, maxValue: null, unit: '' }
  if (type === 'trigger') return { ...staticPort, type: 'event', kind: 'int', isInteger: true, quantity, minValue: null, maxValue: null, unit: '' }

  // Control.
  const kind: ValueKind = isEnum ? 'enum' : isInteger ? 'int' : 'float'
  const [min, max] = quantity === 'unipolar' ? [0, 1] : quantity === 'bipolar' ? [-1, 1] : [rawMin, rawMax]
  return { ...staticPort, type: 'control', kind, isInteger, quantity, minValue: min, maxValue: max, unit: quantityUnit(quantity) }
}

export interface GraphWire {
  id: string
  fromNodeId: string
  fromPortId: string
  toNodeId: string
  toPortId: string
}

/** One node's worth of graphGetNodeMultiplicity data — see GraphSnapshot's
    own `multiplicity` field comment below. Named/exported so every UI layer
    that threads this through (GraphSurface.tsx, NodeCard.tsx) shares one
    type instead of repeating the same inline shape.
*/
export interface NodeMultiplicity {
  ports: ReadonlyMap<string, PortMultiplicityInfo>
  badge?: NodeMultiplicityBadge
}

export interface GraphSnapshot {
  nodes: GraphNode[]
  wires: GraphWire[]
  selection: ReadonlySet<string>
  descriptors: NodeDescriptor[]
  descriptorsLoaded: boolean
  /** Per-node Scalar/Poly port multiplicity plus the live instance-count
      badge, as of the last confirmed compile — see graphCommands.ts's
      graphGetNodeMultiplicity (wiki/plans/DomainRedesign.md Batch 4,
      DomainDot's real replacement: a per-node voice/global/mono label
      stopped being the right question once Scalar-vs-Poly became a
      per-PORT resolved fact rather than a whole-graph region). Refreshed
      alongside every nodes/wires resync (ensureInitialized, withHistory,
      undo, redo); absent for a node the engine hasn't compiled yet, or
      entirely outside the real WebView. `badge` is present only for
      "instance.allocate.voice" nodes.
  */
  multiplicity: ReadonlyMap<string, NodeMultiplicity>
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

/** Parses `properties["util.macro.enumOptions"]` (a JSON string array,
    written by `setMacroEnumOptionLabels` below) back into `string[]` —
    tolerant of anything else (absent, malformed JSON, wrong shape), since
    this is UI-only cosmetic data a corrupt/hand-edited patch file could
    still contain; MacroBody.tsx's own synthesized-fallback path covers the
    `undefined` result the same way it covers "never set". */
function parseMacroEnumOptionLabels(raw: unknown): string[] | undefined {
  if (typeof raw !== 'string' || raw.length === 0) return undefined
  try {
    const parsed: unknown = JSON.parse(raw)
    if (Array.isArray(parsed) && parsed.every((v) => typeof v === 'string')) return parsed as string[]
  } catch {
    // Malformed JSON — fall through to undefined, same as "never set".
  }
  return undefined
}

function patchJsonToLocalState(json: string): { nodes: Map<string, GraphNode>; wires: Map<string, GraphWire> } {
  const doc = JSON.parse(json) as PatchDocumentJson
  const nodes = new Map<string, GraphNode>()
  for (const n of doc.nodes ?? []) {
    const properties = n.properties ?? {}
    const title = properties.title
    const macroEnumOptionLabels = parseMacroEnumOptionLabels(properties['util.macro.enumOptions'])
    const countMin = properties['view.count.min']
    const countMax = properties['view.count.max']
    const viewerRangeMin = properties['viewer.rangeMin']
    const viewerRangeMax = properties['viewer.rangeMax']
    const viewerCenter = properties['viewer.center']
    nodes.set(n.id, {
      id: n.id,
      typeId: n.type,
      x: n.position?.x ?? 0,
      y: n.position?.y ?? 0,
      titleOverride: typeof title === 'string' && title.length > 0 ? title : undefined,
      bypassed: properties.bypassed === true,
      parameterValues: n.parameters && Object.keys(n.parameters).length > 0 ? { ...n.parameters } : undefined,
      macroEnumOptionLabels,
      countMinOverride: typeof countMin === 'number' ? countMin : undefined,
      countMaxOverride: typeof countMax === 'number' ? countMax : undefined,
      viewerRangeMinOverride: typeof viewerRangeMin === 'number' ? viewerRangeMin : undefined,
      viewerRangeMaxOverride: typeof viewerRangeMax === 'number' ? viewerRangeMax : undefined,
      viewerCenterOverride: typeof viewerCenter === 'number' ? viewerCenter : undefined,
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
let multiplicity = new Map<string, NodeMultiplicity>()
let lastError: string | null = null

/** Fetched in parallel with graphGetSnapshot everywhere that's refreshed
    (see the header comment for why: both are "resync local state from the
    engine's own confirmed truth" after the same events) — a separate round
    trip rather than folding into the snapshot JSON itself because
    multiplicity is DERIVED, recomputed every compile (and the badge's own
    counts change far more often than that, on every voice on/off), never
    part of the persisted PatchDocument (unlike bypassed/title, which live
    in NodeInstance.properties and round-trip through save/load).
*/
/** Direct feedback, a second real regression from the SAME polling fix
    (the first was the unbounded-backlog one `.finally()`-chaining already
    fixed): even self-paced, a poll that always calls `notify()` was STILL
    laggy with almost nothing in the graph — because the cost was never
    about the native round trip's own size at all. `notify()` reassigns
    `cachedSnapshot` to a brand-new object every time (buildSnapshot()), and
    `useSyncExternalStore` (useGraphSnapshot.ts) treats any new object
    REFERENCE as "changed", full stop — it has no way to know multiplicity
    was the only field that even ran, forcing a full re-render of the
    entire node canvas (GraphSurface -> every NodeWrapper -> every
    NodeCard) several times a second, forever, whether or not a single
    voice actually turned on or off. Gating notify() on an actual content
    change turns the overwhelming majority of poll ticks (nothing changed)
    into a no-op past this point - only a real voice on/off still costs a
    render, exactly as it should.
*/
function multiplicityEqual(a: ReadonlyMap<string, NodeMultiplicity>, b: ReadonlyMap<string, NodeMultiplicity>): boolean {
  if (a.size !== b.size) return false
  for (const [nodeId, aEntry] of a) {
    const bEntry = b.get(nodeId)
    if (!bEntry) return false
    if (aEntry.badge?.activeCount !== bEntry.badge?.activeCount) return false
    if (aEntry.badge?.maxCount !== bEntry.badge?.maxCount) return false
    if (aEntry.ports.size !== bEntry.ports.size) return false
    for (const [portId, aInfo] of aEntry.ports) {
      const bInfo = bEntry.ports.get(portId)
      if (!bInfo || aInfo.kind !== bInfo.kind || aInfo.originId !== bInfo.originId) return false
    }
  }
  return true
}

async function fetchMultiplicity(): Promise<Map<string, NodeMultiplicity>> {
  const result = await graphGetNodeMultiplicity()
  const map = new Map<string, NodeMultiplicity>()
  if (!result) return map
  for (const [nodeId, ports] of Object.entries(result.ports)) {
    map.set(nodeId, { ports: new Map(Object.entries(ports)), badge: result.badges[nodeId] })
  }
  // A badge-bearing node with no port entries (shouldn't happen in practice —
  // an allocator always has ports — but guards against a badge silently
  // never surfacing if it ever did) still gets its own entry.
  for (const [nodeId, badge] of Object.entries(result.badges)) {
    if (!map.has(nodeId)) map.set(nodeId, { ports: new Map(), badge })
  }
  return map
}

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

/** Re-seeds `nextId` past every "node<N>" id already present in a graph
    just loaded from the engine — a real bug found in testing: `nextId`
    started fresh at 1 on every page load regardless of what a restored
    saved patch (or the engine's own current graph) already contained, so
    adding a node to a patch that already had "node1"/"node2"/... (e.g. from
    an earlier session's own auto-named nodes) collided and failed with
    "Node id already exists" — repeatedly, once per already-used number,
    until `nextId` happened to count past all of them. Called once from
    `ensureInitialized()`'s initial snapshot load; new nodes placed during
    the session already keep `nextId` correctly ahead of themselves.
*/
function reseedNextIdPast(loadedNodes: readonly GraphNode[]): void {
  let highest = 0
  for (const node of loadedNodes) {
    const match = /^node(\d+)$/.exec(node.id)
    if (match) highest = Math.max(highest, Number(match[1]))
  }
  nextId = Math.max(nextId, highest + 1)
}

let cachedSnapshot: GraphSnapshot = buildSnapshot()
function buildSnapshot(): GraphSnapshot {
  return {
    nodes: [...nodes.values()],
    wires: [...wires.values()],
    selection,
    descriptors,
    descriptorsLoaded,
    multiplicity,
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
      reseedNextIdPast([...state.nodes.values()])
      notify()
    })
    .catch((error) => {
      console.warn('graphGetSnapshot failed', error)
    })

  void fetchMultiplicity()
    .then((result) => {
      multiplicity = result
      notify()
    })
    .catch((error) => {
      console.warn('graphGetNodeMultiplicity failed', error)
    })

  // Direct feedback: the instance-count badge always read 0 - not a wiring
  // bug on the native side (graphGetNodeMultiplicity's activeCount really
  // is read fresh off the processor's live atomics on every call, per its
  // own doc comment), the bug was that nothing here ever called it again
  // after the initial fetch above except the 4 graph-EDIT call sites
  // (undo/redo/withHistory/this function) - playing a note doesn't edit
  // the graph, so the badge just showed whatever the count happened to be
  // at the last recompile (almost always 0).
  //
  // A REAL, SERIOUS REGRESSION this same fix introduced, found live
  // (direct feedback: the app got progressively laggier over a session
  // until text-field focus took seconds, then stopped visibly reacting to
  // edits at all, then a fresh launch showed nothing but a blank WebView):
  // this used to be a plain `window.setInterval(..., 200)`, which fires
  // again every 200ms NO MATTER WHAT, even if the previous
  // fetchMultiplicity() call hadn't resolved yet. That round trip shares
  // the same native message-thread bridge as every other native call
  // (edits, and - on Windows/WebView2 - the pump that lets the WebView
  // paint a frame at all). Once a real patch made one call take longer
  // than 200ms, calls piled up faster than they could drain: an unbounded,
  // ever-growing backlog on that one thread - worse the longer the app
  // stayed open and the bigger the patch got (more multiplicity data to
  // serialize per call), eventually starving everything else that shares
  // it, including a fresh launch's very first paint.
  //
  // Fixed by self-pacing instead of fixed-rate: the NEXT poll is scheduled
  // only after THIS one's promise actually settles (success or failure,
  // `finally`), with a flat 200ms gap measured from completion, not from
  // start. Two calls can now never be in flight at once - if a call ever
  // takes 500ms, the cycle becomes ~700ms, gracefully backing off exactly
  // in proportion to real load instead of compounding.
  //
  // STILL laggy after that fix alone, found live immediately after (direct
  // feedback: laggy again on a near-empty graph, right after adding one
  // Master Out node) - see multiplicityEqual()'s own doc comment just
  // above fetchMultiplicity(): the real cost was never the round trip's
  // size, it was calling notify() (a forced full node-canvas re-render)
  // on every tick regardless of whether anything changed. Gated on that
  // now - a tick where nothing changed touches no state and renders
  // nothing.
  const pollMultiplicity = (): void => {
    void fetchMultiplicity()
      .then((result) => {
        if (!multiplicityEqual(multiplicity, result)) {
          multiplicity = result
          notify()
        }
      })
      .catch((error) => {
        console.warn('graphGetNodeMultiplicity poll failed', error)
      })
      .finally(() => {
        window.setTimeout(pollMultiplicity, 500)
      })
  }
  window.setTimeout(pollMultiplicity, 500)
}

// ---- Endpoint lookup (shared by wire-drag hit-testing and rendering) ----

export function getEndpoint(nodeId: string, portId: string, direction: 'input' | 'output'): ConnectionEndpoint | undefined {
  return endpointFor(nodeId, portId, direction, new Set())
}

/** A polymorphic node (util.reroute, logic.select/compare, adapt.sampleHold)
    declares default port types, but a placed one's real types follow what is
    wired to it (Node.h, hasPolymorphicPorts(); the engine resolves it in
    GraphCompiler). Predicting a wire against the declared defaults would
    reject a Control cable into a Reroute the engine accepts, and draw a
    rerouted cable pink, so resolve it here the same way.

    Which ports: only those with a `polymorphism` other than 'none' (select's
    Boolean `condition` stays as declared). Which source wins: the first
    DECLARED polymorphic input that has a resolvable wire — the rule the
    engine's InheritingPortsNode applies. What is adopted: always the quantity
    (and unit/range, for colouring); the SignalType only for
    'signalAndQuantity', since compare/sampleHold keep their values Control.

    Recurses through chains of them; `seen` breaks a cycle, which never
    resolves. A 'signalAndQuantity' port with nothing resolvable feeding it has
    no type to predict with, so it is marked `unresolved` and canConnect() lets
    the drop through — the engine decides, and its rejection reason reaches the
    error banner. A 'quantity' port already has its right type, so it just
    stays as declared (Dimensionless, accepting any quantity), exactly like the
    engine's unwired default.
*/
function endpointFor(nodeId: string, portId: string, direction: 'input' | 'output', seen: Set<string>): ConnectionEndpoint | undefined {
  const node = nodes.get(nodeId)
  if (!node) return undefined
  const descriptor = getDescriptor(node.typeId)
  if (!descriptor) return undefined
  const port = findPort(descriptor, portId, direction)
  if (!port) return undefined

  // util.macro/util.constant's own output: a SEPARATE kind of "not really
  // static" from the polymorphism case just below — that one follows what's
  // WIRED to a node; this one follows the node's OWN structural parameters
  // (TypedValueNodeBase.h's type/isInteger/isEnum/min/max/quantity), set via
  // the Edit T modal (MacroBody.tsx), never by wiring. `descriptor.outputs[0]`
  // (what `port` already is, from `findPort` above) is NodeFactory::
  // describeAll()'s one-shot default-constructed-instance snapshot,
  // fetched once at editor load (fetchNodeDescriptors.ts's own header
  // comment) — frozen at a fresh node's own defaults (Control/Dimensionless/
  // white) forever, regardless of what any PLACED instance is actually
  // configured as. Confirmed, real bug (direct feedback, 2026-10-03: "why
  // is the macro value output not adapting to the color") — this is the
  // fix, at the one place both cable colouring (InfiniteCanvas.tsx's
  // endpointColorRgb) and node-body port-glyph colouring (NodeCard.tsx's
  // resolvedPortStyle) already funnel every port lookup through.
  if (direction === 'output' && isTypedValueNodeType(node.typeId)) {
    return { nodeId, portId, direction, port: resolveTypedValueOutputPort(node, port) }
  }

  const polymorphism = port.polymorphism ?? 'none'
  if (!descriptor.hasPolymorphicPorts || polymorphism === 'none') return { nodeId, portId, direction, port }

  const unresolvedEndpoint: ConnectionEndpoint =
    polymorphism === 'signalAndQuantity' ? { nodeId, portId, direction, port, unresolved: true } : { nodeId, portId, direction, port }
  if (seen.has(nodeId)) return unresolvedEndpoint
  seen.add(nodeId)

  for (const source of descriptor.inputs) {
    if (!source.polymorphism || source.polymorphism === 'none') continue
    const wire = [...wires.values()].find((w) => w.toNodeId === nodeId && w.toPortId === source.id)
    if (!wire) continue // nothing on this source; a lower-priority one may still resolve the node

    // The first declared source that HAS a wire decides, resolved or not: if its
    // upstream can't say yet, the engine holds that default too rather than
    // falling through to a lower-priority source.
    const upstream = endpointFor(wire.fromNodeId, wire.fromPortId, 'output', seen)
    if (!upstream || upstream.unresolved) return unresolvedEndpoint

    const { type, quantity, unit, minValue, maxValue, isInteger, polarity } = upstream.port
    const adopted = { ...port, quantity, unit, minValue, maxValue, isInteger, polarity }
    if (polymorphism === 'signalAndQuantity') adopted.type = type
    return { nodeId, portId, direction, port: adopted }
  }
  return unresolvedEndpoint
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

    // Optimistic: apply the target state immediately (it's already fully
    // known, no guessing needed) rather than waiting on the round trip —
    // same reasoning as every other action in this file (see header
    // comment).
    const optimistic = patchJsonToLocalState(beforeJson)
    nodes = optimistic.nodes
    wires = optimistic.wires
    lastError = null
    notify()

    const result = await graphRestoreSnapshot(beforeJson)
    if (!result.success) {
      past.push(beforeJson) // put it back — the restore itself was rejected
      const reverted = patchJsonToLocalState(afterJson)
      nodes = reverted.nodes
      wires = reverted.wires
      lastError = result.errorMessage
      notify()
      return
    }
    future.push(afterJson)
    engineSnapshotCache = beforeJson
    multiplicity = await fetchMultiplicity()
    notify()
  })()
}

export function redo(): void {
  void (async () => {
    const afterJson = future.pop()
    if (afterJson === undefined || engineSnapshotCache === null) return
    const beforeJson = engineSnapshotCache

    const optimistic = patchJsonToLocalState(afterJson)
    nodes = optimistic.nodes
    wires = optimistic.wires
    lastError = null
    notify()

    const result = await graphRestoreSnapshot(afterJson)
    if (!result.success) {
      future.push(afterJson)
      const reverted = patchJsonToLocalState(beforeJson)
      nodes = reverted.nodes
      wires = reverted.wires
      lastError = result.errorMessage
      notify()
      return
    }
    past.push(beforeJson)
    engineSnapshotCache = afterJson
    multiplicity = await fetchMultiplicity()
    notify()
  })()
}

/** Top bar's premade-patch menu (ui/src/PatchMenu.tsx) — loads a whole
    replacement graph. An ordinary graphRestoreSnapshot call wrapped in the
    same withHistory gesture every other mutating action here uses, so
    loading a patch is itself one undoable step, same as any other graph
    edit (Ctrl+Z after loading "Sine" goes back to whatever was open
    before). No optimistic local guess (unlike a hot-path drag gesture, a
    brief round trip before the canvas updates is fine for a deliberate,
    infrequent click) — withHistory's own post-gesture resync from the
    engine's confirmed snapshot is what actually updates `nodes`/`wires`.
*/
export function loadPatch(json: string): Promise<void> {
  // Returns the gesture's own promise (unlike undo/redo/toggleBypass/etc.
  // above, which are fire-and-forget) specifically so App.tsx's PatchMenu
  // handler can fit-view once the new graph is actually in local state —
  // loading an unrelated patch is a wholesale graph REPLACEMENT, not an
  // incremental edit, so (unlike every other action here) it genuinely
  // needs to pull the camera to wherever the new nodes actually are.
  return withHistory(async () => {
    await fireCommand(() => graphRestoreSnapshot(json))
  })
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
    applies `optimistic` (if given) to local nodes/wires/selection
    synchronously and notifies immediately — see this file's header comment
    on why this has to happen before the command is even sent, not after —
    then runs the gesture, then re-syncs local nodes/wires from the engine's
    own confirmed state (correcting the optimistic guess, or undoing it
    entirely if the gesture was rejected) and pushes a history entry if
    anything actually changed. Clears lastError at the start, exactly once
    per gesture, not per sub-command.
*/
async function withHistory(gesture: () => Promise<void>, optimistic?: () => void): Promise<void> {
  lastError = null
  const beforeJson = engineSnapshotCache
  if (optimistic) {
    optimistic()
    notify()
  }
  await gesture()

  const [afterJson, multiplicityResult] = await Promise.all([graphGetSnapshot(), fetchMultiplicity()])
  multiplicity = multiplicityResult
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
  // wiki/plans/UtilMacro.md: a bare Macro placed via the Add menu or ghost
  // click (not the drag-from-a-port shortcut, createMacroFromPort below)
  // still auto-claims a free slot inside this same gesture, rather than
  // landing unclaimed (-1) and doing nothing until the user finds the slot
  // parameter themselves. Left unclaimed (-1) if every slot is already
  // taken - a 33rd macro is still a valid, if inert, placement, not an
  // error. Routed through the same single-recompile graphCreateMacro as
  // createMacroFromPort below (post-ship sweep P2.1) rather than
  // addNode+setParameterValue's own old 2-recompile sequence, with the
  // engine's own plain defaults (0..1 Dimensionless, not integer) for
  // everything a dragged-from-a-port macro would otherwise derive from the
  // source port.
  const macroSlot = typeId === 'util.macro' ? pickFreeMacroSlot() ?? -1 : undefined
  await withHistory(
    async () => {
      if (macroSlot !== undefined) {
        await fireCommand(() => graphCreateMacro(id, x, y, macroSlot, 0, 1, false, 0, '', 0))
        return
      }
      await fireCommand(() => graphAddNode(typeId, id, x, y))
    },
    () => {
      nodes.set(id, {
        id,
        typeId,
        x,
        y,
        bypassed: false,
        parameterValues: macroSlot !== undefined ? { 'util.macro.slot': macroSlot } : undefined,
      })
      selection = new Set([id])
    },
  )
  return id
}

/** Removes the given nodes, one command per node, in one undo step. */
export function deleteNodes(ids: readonly string[]): void {
  if (ids.length === 0) return
  void withHistory(
    async () => {
      for (const id of ids) await fireCommand(() => graphDeleteNode(id))
    },
    () => {
      for (const id of ids) {
        selection.delete(id)
        nodes.delete(id)
      }
      for (const [wId, wire] of [...wires]) {
        if (ids.includes(wire.fromNodeId) || ids.includes(wire.toNodeId)) wires.delete(wId)
      }
    },
  )
}

/** The node's own declared primary output port, or its first output if none
    is marked primary — same fallback `splicePrimaryPorts` already uses.
    `undefined` for a pure sink (view.listen/scope/spectrum/meter have no
    outputs at all) or an unloaded descriptor.
*/
function primaryOutputPortId(typeId: string): string | undefined {
  const descriptor = getDescriptor(typeId)
  if (!descriptor) return undefined
  return (descriptor.outputs.find((o) => o.isPrimaryOutput) ?? descriptor.outputs[0])?.id
}

/** wiki/NODES_Gaps.md's `missing-ui-command` finding: `io.output` ("Master
    Out") is an ordinary passthrough node, not a compiler special case —
    wiring a cable into its input does nothing to what the compiled graph
    actually outputs until something calls `graphSetOutput`. Every gesture
    that can land a new connection on a node's input calls this afterward
    (with that node's id) so the obvious gesture — "plug into Master Out" —
    is what actually becomes audible, without the user needing to know a
    separate designation step exists. A no-op for any node that isn't
    `io.output`, so it's safe to call unconditionally after every connect.
*/
async function designateOutputIfMasterOut(toNodeId: string): Promise<void> {
  const node = nodes.get(toNodeId)
  if (!node || node.typeId !== 'io.output') return
  const outputPortId = primaryOutputPortId(node.typeId)
  if (!outputPortId) return
  await fireCommand(() => graphSetOutput(toNodeId, outputPortId))
}

/** The general case `designateOutputIfMasterOut` doesn't cover: explicitly
    designating any node's own output as the graph's audible output, from
    the right-click "Set as Output" menu action — not gated on the node
    being `io.output`, since a patch may want its output cable to visibly
    terminate somewhere other than a literal Master Out node (or none at
    all placed yet). No-ops for a node with no output port.
*/
export function setAsOutput(nodeId: string): void {
  const node = nodes.get(nodeId)
  if (!node) return
  const outputPortId = primaryOutputPortId(node.typeId)
  if (!outputPortId) return
  void withHistory(() => fireCommand(() => graphSetOutput(nodeId, outputPortId)).then(() => undefined))
}

export function renameNode(id: string, title: string | undefined): void {
  const trimmed = title && title.trim().length > 0 ? title.trim() : ''
  void withHistory(
    () => fireCommand(() => graphSetProperty(id, 'title', trimmed)).then(() => undefined),
    () => {
      const node = nodes.get(id)
      if (node) nodes.set(id, { ...node, titleOverride: trimmed.length > 0 ? trimmed : undefined })
    },
  )
}

/** design/Macro.png's "Edit T" modal, Enum type only — see GraphNode's own
    `macroEnumOptionLabels` doc comment for why this is a cosmetic property
    rather than a real engine-modeled `enumOptions` list. Empty labels are
    dropped (a blank line in the modal's textarea shouldn't become a real,
    selectable "" option). */
export function setMacroEnumOptionLabels(id: string, labels: readonly string[]): void {
  const cleaned = labels.map((l) => l.trim()).filter((l) => l.length > 0)
  void withHistory(
    () => fireCommand(() => graphSetProperty(id, 'util.macro.enumOptions', JSON.stringify(cleaned))).then(() => undefined),
    () => {
      const node = nodes.get(id)
      if (node) nodes.set(id, { ...node, macroEnumOptionLabels: cleaned.length > 0 ? cleaned : undefined })
    },
  )
}

/** design/Visualization/Count.png's editable Min/Max footer — one undo step
    per field, same granularity renameNode's own single-property commit
    already uses, so editing Min doesn't also disturb Max in the undo
    stack. Rounded before it's sent: the footer is an integer viewer's own
    range, never a fractional one, same "whole numbers only" contract its
    ports themselves declare (isInteger/kind=Int). */
export function setCountMin(id: string, value: number): void {
  const rounded = Math.round(value)
  void withHistory(
    () => fireCommand(() => graphSetProperty(id, 'view.count.min', rounded)).then(() => undefined),
    () => {
      const node = nodes.get(id)
      if (node) nodes.set(id, { ...node, countMinOverride: rounded })
    },
  )
}

export function setCountMax(id: string, value: number): void {
  const rounded = Math.round(value)
  void withHistory(
    () => fireCommand(() => graphSetProperty(id, 'view.count.max', rounded)).then(() => undefined),
    () => {
      const node = nodes.get(id)
      if (node) nodes.set(id, { ...node, countMaxOverride: rounded })
    },
  )
}

/** design/Visualization/Scope1.png's editable vertical-range footer — the
    generic sibling of setCountMin/setCountMax above, usable by any
    auto-ranging viewer (ScopeHistoryBody.tsx) regardless of its own typeId.
    Unlike Count's Min/Max, these are never rounded to a whole number: a
    Scope's own range is a continuous display scale, not an integer's. */
export function setViewerRangeMin(id: string, value: number): void {
  void withHistory(
    () => fireCommand(() => graphSetProperty(id, 'viewer.rangeMin', value)).then(() => undefined),
    () => {
      const node = nodes.get(id)
      if (node) nodes.set(id, { ...node, viewerRangeMinOverride: value })
    },
  )
}

export function setViewerRangeMax(id: string, value: number): void {
  void withHistory(
    () => fireCommand(() => graphSetProperty(id, 'viewer.rangeMax', value)).then(() => undefined),
    () => {
      const node = nodes.get(id)
      if (node) nodes.set(id, { ...node, viewerRangeMaxOverride: value })
    },
  )
}

/** design/Visualization/ScopeMod.png: "The centre line's position is
    editable, so a unipolar signal can be given a centre of 0.5, or of 0" —
    the same generic, type-id-agnostic property storage as the range pair
    above. */
export function setViewerCenter(id: string, value: number): void {
  void withHistory(
    () => fireCommand(() => graphSetProperty(id, 'viewer.center', value)).then(() => undefined),
    () => {
      const node = nodes.get(id)
      if (node) nodes.set(id, { ...node, viewerCenterOverride: value })
    },
  )
}

export function toggleBypass(id: string): void {
  const node = nodes.get(id)
  if (!node) return
  const next = !node.bypassed
  void withHistory(
    () => fireCommand(() => graphSetProperty(id, 'bypassed', next)).then(() => undefined),
    () => nodes.set(id, { ...node, bypassed: next }),
  )
}

/** Toggles bypass on every given node independently, in one undo step —
    used when right-clicking a node that's already part of the current
    multi-selection (M10_REVIEW.md §14: acting on the whole selection, not
    just the one node under the cursor).
*/
export function toggleBypassMany(ids: readonly string[]): void {
  if (ids.length === 0) return
  const nextBysById = new Map<string, boolean>()
  for (const id of ids) {
    const node = nodes.get(id)
    if (node) nextBysById.set(id, !node.bypassed)
  }
  void withHistory(
    async () => {
      for (const [id, next] of nextBysById) await fireCommand(() => graphSetProperty(id, 'bypassed', next))
    },
    () => {
      for (const [id, next] of nextBysById) {
        const node = nodes.get(id)
        if (node) nodes.set(id, { ...node, bypassed: next })
      }
    },
  )
}

/** One undo step per commit — called once, when a ValueSlider drag ends or
    a typed edit is confirmed (NodeCard.tsx), never on every intermediate
    drag tick (same "commit once per gesture" rule commitNodeMoves already
    follows for node dragging).
*/
export function setParameterValue(nodeId: string, parameterId: string, value: number): void {
  void withHistory(
    () => fireCommand(() => graphSetParameterValue(nodeId, parameterId, value)).then(() => undefined),
    () => {
      const node = nodes.get(nodeId)
      if (node) nodes.set(nodeId, { ...node, parameterValues: { ...node.parameterValues, [parameterId]: value } })
    },
  )
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
  void withHistory(
    async () => {
      for (const update of updates) await fireCommand(() => graphMoveNode(update.id, update.x, update.y))
    },
    () => {
      for (const update of updates) {
        const node = nodes.get(update.id)
        if (node) nodes.set(update.id, { ...node, x: update.x, y: update.y })
      }
    },
  )
}

export function addWire(fromNodeId: string, fromPortId: string, toNodeId: string, toPortId: string): void {
  void withHistory(
    async () => {
      if (await fireCommand(() => graphConnectWithAutoAdapt(fromNodeId, fromPortId, toNodeId, toPortId))) {
        await designateOutputIfMasterOut(toNodeId)
      }
    },
    () => {
      const id = wireId(toNodeId, toPortId)
      wires.set(id, { id, fromNodeId, fromPortId, toNodeId, toPortId })
    },
  )
}

export function removeWire(id: string): void {
  const wire = wires.get(id)
  if (!wire) return
  void withHistory(
    () => fireCommand(() => graphDisconnect(wire.fromNodeId, wire.fromPortId, wire.toNodeId, wire.toPortId)).then(() => undefined),
    () => wires.delete(id),
  )
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
  // Captured up front, before the optimistic step below can delete it from
  // `wires` — the gesture callback runs asynchronously, after withHistory's
  // synchronous optimistic-apply has already removed `detachedWireId` from
  // the live map, so a lazy `wires.get(detachedWireId)` inside the gesture
  // itself would always resolve to undefined and silently skip sending the
  // real graphDisconnect command (the exact bug this fixes: the wire looked
  // disconnected locally for a moment, then the following resync — which
  // never actually happened server-side — snapped it back to connected).
  const detachedWire = detachedWireId ? wires.get(detachedWireId) : undefined
  void withHistory(
    async () => {
      if (detachedWire) await fireCommand(() => graphDisconnect(detachedWire.fromNodeId, detachedWire.fromPortId, detachedWire.toNodeId, detachedWire.toPortId))
      if (target && (await fireCommand(() => graphConnectWithAutoAdapt(fromNodeId, fromPortId, target.nodeId, target.portId)))) {
        await designateOutputIfMasterOut(target.nodeId)
      }
    },
    () => {
      if (detachedWireId) wires.delete(detachedWireId)
      if (target) {
        const id = wireId(target.nodeId, target.portId)
        wires.set(id, { id, fromNodeId, fromPortId, toNodeId: target.nodeId, toPortId: target.portId })
      }
    },
  )
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

  void withHistory(
    async () => {
      if (!(await fireCommand(() => graphDisconnect(wire.fromNodeId, wire.fromPortId, wire.toNodeId, wire.toPortId)))) return
      if (!(await fireCommand(() => graphAddNode(typeId, newNodeId, x, y)))) return
      if (!(await fireCommand(() => graphConnectWithAutoAdapt(wire.fromNodeId, wire.fromPortId, newNodeId, primary.inputId)))) return
      if (!(await fireCommand(() => graphConnectWithAutoAdapt(newNodeId, primary.outputId, wire.toNodeId, wire.toPortId)))) return
      await designateOutputIfMasterOut(wire.toNodeId)
    },
    () => {
      wires.delete(wireIdToSplice)
      nodes.set(newNodeId, { id: newNodeId, typeId, x, y, bypassed: false })
      const inWireId = wireId(newNodeId, primary.inputId)
      const outWireId = wireId(wire.toNodeId, wire.toPortId)
      wires.set(inWireId, { id: inWireId, fromNodeId: wire.fromNodeId, fromPortId: wire.fromPortId, toNodeId: newNodeId, toPortId: primary.inputId })
      wires.set(outWireId, { id: outWireId, fromNodeId: newNodeId, fromPortId: primary.outputId, toNodeId: wire.toNodeId, toPortId: wire.toPortId })
      selection = new Set([newNodeId])
    },
  )

  return newNodeId
}

// ---- Drag-a-port-out-to-a-Macro shortcut ----------------------------------
// Direct instruction: "taking an unconnected input node and dragging and
// releasing to automatically create a macro with the correct predefined
// types." Revives the retired M10 addMacroFromPort/macroConfigForPort shape
// (this file's own header comment, and see InfiniteCanvas.tsx's mousedown
// handler for the other retirement note) now that util.macro is a real
// registered node type (wiki/plans/UtilMacro.md, ADR-0015 amended not
// reversed) — upgraded to the real M14 value-contract fields
// (kind/quantity/enumOptions) that didn't exist at M10 time.

/** util.macro.quantity's own ParameterDescriptor (MacroNode.h) is a plain
    integer ordinal, not a string — this table's index MUST match Quantity's
    declaration order here AND engine/include/bazalt/engine/graph/
    PortDescriptor.h's `Quantity` enum EXACTLY (both are hand-kept in sync
    by design, the same "one schema, two producers, kept in sync by hand"
    convention canConnect.ts's own header comment already established for
    this project). A silent mismatch would misencode every non-Dimensionless
    macro's own structural "Quantity" parameter — Dimensionless (index 0)
    is `util.macro.quantity`'s own descriptor default, so a mismatch would
    stay invisible for the single most common case and only misencode the
    other nine.
*/
export const QUANTITY_ORDER: readonly Quantity[] = [
  'dimensionless',
  'frequency',
  'pitch',
  'time',
  'gain',
  'ratio',
  'unipolar',
  'bipolar',
  'count',
  'phase',
]
export function quantityOrdinal(quantity: Quantity): number {
  const index = QUANTITY_ORDER.indexOf(quantity)
  return index >= 0 ? index : 0
}
/** The decode direction of the same table — reading a macro's own stored
    `util.macro.quantity` ordinal back out (App.tsx's macro top panel,
    NodeCard.tsx's in-node rows) rather than hand-keeping a second copy of
    this order-sensitive array. Out-of-range (shouldn't happen, but a
    corrupt/future patch file is still just data) falls back to
    Dimensionless, same as the engine's own `juce::jlimit` clamp in
    MacroNode::setParameter does for an out-of-range value.
*/
export function quantityFromOrdinal(value: number): Quantity {
  const index = Math.round(value)
  return QUANTITY_ORDER[index] ?? 'dimensionless'
}

interface MacroSeed {
  min: number
  max: number
  isInteger: boolean
  quantity: Quantity
  /** The macro's own raw 0..1 storage value (util.macro.value) that
      reproduces `currentValue` under this seed's min/max — purely
      cosmetic, see createMacroFromPort's own comment on why it's seeded
      at all.
  */
  defaultRaw: number
}

/** Derives a new macro's own min/max/isInteger/quantity contract from the
    port it's being dragged out of, matching the retired M10
    macroConfigForPort()'s rules, upgraded to real fields:
      - a real `kind: 'enum'` port (with enumOptions) -> an integer
        0..(N-1) range over those same options (no real engine port
        exercises this today — a deliberate, explicitly-flagged v1
        simplification, forward-looking/test-fixture-only for now; also
        doesn't seed `isEnum` — `graphCreateMacro`'s own native command has
        no such argument yet, a separate, smaller gap than this comment's
        own already-flagged one).
      - Boolean, or Event with no options -> a small integer 0..1 range.
      - Event WITH options (mock-only field, see descriptorTypes.ts) -> an
        integer range over that list, same shape as the enum case above.
      - Control with clear bounds (minValue/maxValue both set) -> copied
        verbatim (isInteger/quantity too), so e.g. a filter cutoff macro is
        still Frequency-quantitied, not a generic 0..1. Unit is NOT copied
        — it's derived from quantity now, never a separately seeded field
        (direct instruction, 2026-10-03: "Unit should not be a field").
      - Control with NO clear bounds (math.add's growable "a"/"b" inputs,
        never given a real min/max) -> a generic Dimensionless 0..1 range.
    `currentValue` (the port's live fallback value, from GraphNode.
    parameterValues, or the descriptor's own defaultValue) becomes the
    macro's seeded raw value, inverted into whatever 0..1 fraction of the
    chosen range it lands at.
*/
function macroConfigForPort(port: PortDescriptor, currentValue: number): MacroSeed {
  const clamp = (value: number, min: number, max: number): number => Math.min(max, Math.max(min, value))
  const rawOf = (value: number, min: number, max: number): number => (max > min ? (clamp(value, min, max) - min) / (max - min) : 0)

  if (port.kind === 'enum' && port.enumOptions.length > 0) {
    const maxIndex = port.enumOptions.length - 1
    return { min: 0, max: maxIndex, isInteger: true, quantity: 'dimensionless', defaultRaw: rawOf(Math.round(currentValue), 0, maxIndex) }
  }
  if (port.type === 'event' && port.options && port.options.length > 0) {
    const maxIndex = port.options.length - 1
    return { min: 0, max: maxIndex, isInteger: true, quantity: 'dimensionless', defaultRaw: rawOf(Math.round(currentValue), 0, maxIndex) }
  }
  if (port.type === 'event' || port.type === 'boolean') {
    return { min: 0, max: 1, isInteger: true, quantity: 'dimensionless', defaultRaw: rawOf(Math.round(currentValue), 0, 1) }
  }

  const hasClearBounds = port.minValue !== null && port.maxValue !== null
  if (hasClearBounds) {
    const min = port.minValue as number
    const max = port.maxValue as number
    return { min, max, isInteger: port.isInteger, quantity: port.quantity, defaultRaw: rawOf(currentValue, min, max) }
  }

  // Unclear metatype (e.g. math.add's "a"/"b" — no real bound ever set):
  // a generic Dimensionless 0..1 range, the same fallback the retired M10
  // gesture used for exactly this case.
  return { min: 0, max: 1, isInteger: false, quantity: 'dimensionless', defaultRaw: clamp(currentValue, 0, 1) }
}

/** Whether `port` could sensibly become a Macro's value at all. Control and
    Event both have a meaningful macro shape (see macroConfigForPort above).
    Audio and Note don't (a macro is a scalar/discrete automatable value,
    not an audio-rate or event-stream signal), and Data never converts
    implicitly to anything (SIGNAL_TYPES.md §5, canConnect.ts's own rule) —
    a macro's plain float output is no exception. Spectral is reserved, not
    real anywhere yet.

    Deliberately NOT `Boolean`, even though a macro has a meaningful 0/1
    shape for one (direct feedback bug: a real util.macro's own output port
    is ALWAYS Control — MacroNode.h never declares a Boolean output — and
    the real engine's CanConnect.cpp has an adapter for Boolean->Control but
    none for the reverse Control->Boolean, so dragging out of e.g.
    env.adsr's "gate" used to add the macro node, claim a slot, and seed its
    config, only to have the final connect command hard-reject, leaving an
    orphaned, disconnected macro on the canvas every single time. Revisit
    once a real Control->Boolean adapter exists (a genuinely separate,
    bigger feature, not a one-line fix) — until then this is exactly what
    isMacroConnectable() below also guards, belt-and-suspenders.
*/
export function isMacroablePort(port: Pick<PortDescriptor, 'type'>): boolean {
  return port.type === 'control' || port.type === 'event'
}

/** The authoritative check `createMacroFromPort` uses right before
    committing — mirrors canSplice()'s own "check canConnectPorts against
    the real candidate descriptor before sending any command" discipline,
    which the original drag-to-macro gesture skipped (it only ever checked
    isMacroablePort()'s coarse type-based gate, never actually asked whether
    util.macro's own real declared output port could connect into this
    specific target). Catches any future port-type mismatch the same way,
    not just the Boolean case isMacroablePort() above already excludes by
    type.
*/
function isMacroConnectable(port: PortDescriptor): boolean {
  const macroDescriptor = getDescriptor('util.macro')
  const macroOutput = macroDescriptor && findPort(macroDescriptor, 'out', 'output')
  if (!macroOutput) return false
  return canConnectPorts(macroOutput, port).outcome !== 'reject'
}

/** Scans the local node mirror for every placed util.macro's own claimed
    slot (`parameterValues['util.macro.slot']` — already present on every
    node mirror, patchJsonToLocalState copies NodeInstance.parameters
    wholesale) and returns the lowest one in [0,31] nothing has claimed
    yet, or undefined if all 32 are already taken. Pure and synchronous —
    reads only the already-resynced local mirror, never asks the engine.
*/
export function pickFreeMacroSlot(): number | undefined {
  const claimed = new Set<number>()
  for (const node of nodes.values()) {
    if (node.typeId !== 'util.macro') continue
    const slot = node.parameterValues?.['util.macro.slot']
    if (slot === undefined) continue
    const rounded = Math.round(slot)
    if (rounded >= 0 && rounded < 32) claimed.add(rounded)
  }
  for (let slot = 0; slot < 32; slot++) {
    if (!claimed.has(slot)) return slot
  }
  return undefined
}

/** The drag-a-port-out-to-a-Macro shortcut: creates a new util.macro node
    at (x, y), pre-configured from `portId` on `nodeId` (macroConfigForPort
    above), claims the lowest free slot, and wires its output straight into
    that port — one undo step (addNode -> claim slot -> seed min/max/
    isInteger/quantity/unit/value -> connect), modeled on spliceInsert's own
    composite-command shape above. The slot claim is deliberately its own
    SEPARATE command after addNode, never part of the initial NodeInstance
    (wiki/plans/UtilMacro.md's "Finding B": util.macro.slot's descriptor
    default is -1/unclaimed specifically so the transient state between
    these two commands can never collide with an already-placed macro).

    No-ops (returns undefined, no command sent) if the port doesn't exist,
    isMacroablePort() rejects its type, or isMacroConnectable() says the
    real engine would reject the resulting wire anyway — callers
    (InfiniteCanvas.tsx's drag-to-empty-space gesture) are expected to have
    already checked isMacroablePort() before even starting the drag, but
    this function re-checks both itself rather than trusting the caller, so
    it can never commit the addNode step and then fail the connect step,
    leaving an orphaned macro node behind (a real bug this exact two-check
    guard fixes — see isMacroablePort()'s own comment). Matches
    spliceInsert/canSplice's own "check first, commit only if it would
    work" discipline. Also no-ops, but sets lastError to a clear reason, if
    every slot is already claimed — rather than firing a doomed addNode+
    setParameterValue sequence GraphEditController's own collision check
    would reject anyway.
*/
export function createMacroFromPort(nodeId: string, portId: string, x: number, y: number): string | undefined {
  const endpoint = getEndpoint(nodeId, portId, 'input')
  if (!endpoint || !isMacroablePort(endpoint.port) || !isMacroConnectable(endpoint.port)) return undefined

  const slot = pickFreeMacroSlot()
  if (slot === undefined) {
    lastError = 'All 32 macro slots are in use'
    notify()
    return undefined
  }

  const node = nodes.get(nodeId)
  const currentValue = node?.parameterValues?.[portId] ?? endpoint.port.defaultValue
  const seed = macroConfigForPort(endpoint.port, currentValue)
  const isIntegerValue = seed.isInteger ? 1 : 0
  const quantityValue = quantityOrdinal(seed.quantity)

  const macroId = makeId('node')

  void withHistory(
    async () => {
      // One atomic call creates the node AND sets every structural
      // parameter (slot/min/max/isInteger/quantity/value) in a single
      // recompile — see graphCommands.ts's own comment on graphCreateMacro.
      // Previously this was addNode + 5x setParameterValue + setProperty, 6
      // separate recompiles with a real transient-slot-collision race
      // between them (wiki/plans/UtilMacro.md Finding B's own "-1
      // unclaimed sentinel" workaround existed only to paper over that
      // race); now a slot collision is rejected as one atomic no-op,
      // nothing added at all. The native command's own `unit` positional
      // argument is passed empty — "Unit should not be a field" (direct
      // instruction, 2026-10-03): a macro's unit is derived from quantity
      // now (TypedValueNodeBase.h's unitForQuantity), never seeded.
      if (
        !(await fireCommand(() => graphCreateMacro(macroId, x, y, slot, seed.min, seed.max, seed.isInteger, quantityValue, '', seed.defaultRaw)))
      )
        return
      if (await fireCommand(() => graphConnectWithAutoAdapt(macroId, 'out', nodeId, portId))) {
        await designateOutputIfMasterOut(nodeId)
      }
    },
    () => {
      nodes.set(macroId, {
        id: macroId,
        typeId: 'util.macro',
        x,
        y,
        bypassed: false,
        parameterValues: {
          'util.macro.slot': slot,
          'util.macro.min': seed.min,
          'util.macro.max': seed.max,
          'util.macro.isInteger': isIntegerValue,
          'util.macro.quantity': quantityValue,
          'util.macro.value': seed.defaultRaw,
        },
      })
      const newWireId = wireId(nodeId, portId)
      wires.set(newWireId, { id: newWireId, fromNodeId: macroId, fromPortId: 'out', toNodeId: nodeId, toPortId: portId })
      selection = new Set([macroId])
    },
  )

  return macroId
}

/** "Ctrl/Cmd-clicking an output port spawns the viewer matching that
    port's type, already connected" (design/Visualization/Scope1.png; the
    same rule Ripple.png, Count.png, ScopeMod.png and Gate.png each state for
    their own type). One table, keyed by the same port classification that
    colours the port (classifyPortUiKind), so "which viewer" can never drift
    from "which colour": a port that looks orange opens the orange viewer.
    Undefined for a type with no viewer of its own (Audio, Note, Data). */
const DEFAULT_VIEWER_BY_PORT_KIND: Partial<Record<PortUiKind, string>> = {
  trigger: 'view.ripple', // Ripple.png — the Event viewer
  integer: 'view.count', // Count.png
  value: 'view.scope.control', // Scope1.png — plain real-quantity Control
  modulation: 'view.scope.modulation', // ScopeMod.png — Unipolar/Bipolar Control
  boolean: 'view.gate', // Gate.png
}

export function defaultViewerTypeForPort(port: PortDescriptor): string | undefined {
  // classifyPortUiKind falls back to 'value' for a type it has no colour for
  // (Spectral) — only a genuine Control port gets the Control scope.
  const kind = classifyPortUiKind(port)
  if (kind === 'value' && port.type !== 'control') return undefined
  return DEFAULT_VIEWER_BY_PORT_KIND[kind]
}

/** Spawns `defaultViewerTypeForPort`'s viewer at (x, y) and wires the
    clicked output port straight into its own 'in' port — one undo step
    (addNode -> connect), the same composite-command shape createMacroFromPort
    above uses, just simpler: no viewer needs a seed/slot-claim step in
    between.

    No-op (returns undefined, no command sent) if the port doesn't exist,
    has no viewer, or canConnectPorts() against the real viewer descriptor
    says the engine would reject the wire anyway — the same "check first,
    commit only if it would work" discipline createMacroFromPort's own doc
    comment explains in full (never commit the addNode step and then fail
    the connect step, leaving an orphaned viewer behind).
*/
export function createViewerFromPort(nodeId: string, portId: string, x: number, y: number): string | undefined {
  const endpoint = getEndpoint(nodeId, portId, 'output')
  const typeId = endpoint && defaultViewerTypeForPort(endpoint.port)
  if (!endpoint || !typeId) return undefined

  const viewerDescriptor = getDescriptor(typeId)
  const viewerInput = viewerDescriptor && findPort(viewerDescriptor, 'in', 'input')
  if (!viewerInput || canConnectPorts(endpoint.port, viewerInput).outcome === 'reject') return undefined

  const viewerId = makeId('node')

  void withHistory(
    async () => {
      if (!(await fireCommand(() => graphAddNode(typeId, viewerId, x, y)))) return
      await fireCommand(() => graphConnectWithAutoAdapt(nodeId, portId, viewerId, 'in'))
    },
    () => {
      nodes.set(viewerId, { id: viewerId, typeId, x, y, bypassed: false })
      const newWireId = wireId(viewerId, 'in')
      wires.set(newWireId, { id: newWireId, fromNodeId: nodeId, fromPortId: portId, toNodeId: viewerId, toPortId: 'in' })
      selection = new Set([viewerId])
    },
  )

  return viewerId
}
