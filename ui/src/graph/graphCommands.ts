// M19 (ADR-0006/ADR-0025): thin async wrappers over every graph-editing
// native function PluginEditor.cpp's withGraphCommands() registers — the
// actual sending end of the M7 command bridge, which existed but had no UI
// caller before this milestone (getNodeDescriptors, fetchNodeDescriptors.ts,
// was the only one called from JS until now). Same `typeof window.__JUCE__
// === 'undefined'` guard fetchNodeDescriptors.ts already established, for
// plain-browser iteration outside the real WebView.
import { getNativeFunction } from '@juce-framework/webview'

export interface CommandResult {
  success: boolean
  errorMessage: string
  /** graphConnectWithAutoAdapt only: the connection loses information
      (stereo into a mono-only port) and needs one of these choices —
      call again passing it. */
  choices?: string[]
}

const NOT_IN_WEBVIEW: CommandResult = { success: false, errorMessage: 'Not running inside the plugin WebView' }

async function callCommand(name: string, ...args: unknown[]): Promise<CommandResult> {
  if (typeof window.__JUCE__ === 'undefined') return NOT_IN_WEBVIEW
  const result = await getNativeFunction(name)(...args)
  return result as CommandResult
}

export function graphAddNode(typeId: string, nodeId: string, x: number, y: number): Promise<CommandResult> {
  return callCommand('graphAddNode', typeId, nodeId, x, y)
}

/** wiki/plans/UtilMacro.md P2.1 (post-ship sweep): adds one fully-configured
    util.macro node (every structural parameter plus its cosmetic unit
    property, all atomically with the node's own creation) in a single
    recompile — see GraphEditController::createMacro's own doc comment.
    Replaces graphAddNode + 5x graphSetParameterValue + graphSetProperty for
    this one gesture; still followed by a separate graphConnectWithAutoAdapt
    call to wire it in, not folded into this same call.
*/
export function graphCreateMacro(
  nodeId: string,
  x: number,
  y: number,
  slot: number,
  min: number,
  max: number,
  isInteger: boolean,
  quantity: number,
  unit: string,
  value: number,
  type = 0,
): Promise<CommandResult> {
  return callCommand('graphCreateMacro', nodeId, x, y, slot, min, max, isInteger ? 1 : 0, quantity, unit, value, type)
}

/** wiki/plans/Decorations.md §4: a deco.image node showing `base64` (already
    compressed), stored once per patch; refused past the patch's image limit. */
export function graphAddImage(nodeId: string, x: number, y: number, mimeType: string, base64: string, width: number, height: number): Promise<CommandResult> {
  return callCommand('graphAddImage', nodeId, x, y, mimeType, base64, width, height)
}

export function graphDeleteNode(nodeId: string): Promise<CommandResult> {
  return callCommand('graphDeleteNode', nodeId)
}

export function graphDisconnect(fromNodeId: string, fromPortId: string, toNodeId: string, toPortId: string): Promise<CommandResult> {
  return callCommand('graphDisconnect', fromNodeId, fromPortId, toNodeId, toPortId)
}

/** Always the real command a UI-initiated connect should call — gives all
    three of canConnect's outcomes real behaviour (Ok connects directly,
    NeedsAdapters inserts the real adapter chain, Reject fails with the
    engine's own reason), unlike a flat graphConnect call which only ever
    succeeds or fails outright. See canConnect.ts for the client-side
    prediction used for live drag feedback before a drop is attempted.
*/
export function graphConnectWithAutoAdapt(fromNodeId: string, fromPortId: string, toNodeId: string, toPortId: string, choice = ''): Promise<CommandResult> {
  return callCommand('graphConnectWithAutoAdapt', fromNodeId, fromPortId, toNodeId, toPortId, choice)
}

export function graphSetParameterValue(nodeId: string, parameterId: string, value: number): Promise<CommandResult> {
  return callCommand('graphSetParameterValue', nodeId, parameterId, value)
}

/** While a slider is dragged: the value goes to the running nodes, smoothed,
    with no recompile and no undo step (plugin/source/LiveParameterEdits.h).
    Resolves false for a value that can't change live (a structural
    parameter) — that one simply takes effect on commit. */
export async function graphSetParameterLive(nodeId: string, parameterId: string, value: number): Promise<boolean> {
  return (await getNativeFunction('graphSetParameterLive')(nodeId, parameterId, value)) as boolean
}

export async function graphReleaseParameterLive(nodeId: string, parameterId: string): Promise<void> {
  await getNativeFunction('graphReleaseParameterLive')(nodeId, parameterId)
}

export function graphMoveNode(nodeId: string, x: number, y: number): Promise<CommandResult> {
  return callCommand('graphMoveNode', nodeId, x, y)
}

/** Designates which node's output port is the compiled graph's actual
    audible output — real and tested on the native side
    (`GraphEditController::setOutput`) since M7/M8, but never called from
    `ui/src` until wiki/NODES_Gaps.md's `missing-ui-command` finding: wiring
    a cable into `io.output`'s input alone does nothing to this designation,
    since `io.output` ("Master Out") is an ordinary passthrough node, not a
    compiler special case. See graphStore.ts's `designateOutputIfMasterOut`/
    `setAsOutput` for the two real call sites this now has.
*/
export function graphSetOutput(nodeId: string, portId: string): Promise<CommandResult> {
  return callCommand('graphSetOutput', nodeId, portId)
}

/** Backs both rename ("title") and bypass ("bypassed") — see
    GraphEditController::setProperty's own doc comment. Neither has a real
    DSP-level effect yet (no bypass audio behaviour exists in the engine) —
    this only makes the value real and persisted, not audible.
*/
export function graphSetProperty(nodeId: string, propertyKey: string, value: string | number | boolean): Promise<CommandResult> {
  return callCommand('graphSetProperty', nodeId, propertyKey, value)
}

/** A factory node's content (a curve, a wavetable), sent as JSON text — one
    undoable edit (GraphEditController::setContent). */
export function graphSetContent(nodeId: string, content: unknown): Promise<CommandResult> {
  return callCommand('graphSetContent', nodeId, JSON.stringify(content))
}

/** While an editor drags: the content goes straight to the running nodes, no
    recompile, no undo step. Commit with graphSetContent on release. */
export async function graphSetContentLive(nodeId: string, content: unknown): Promise<boolean> {
  if (typeof window.__JUCE__ === 'undefined') return false
  return (await getNativeFunction('graphSetContentLive')(nodeId, JSON.stringify(content))) as boolean
}

/** Returns the current graph as a JSON string (a PatchDocument with empty
    macro/view/meta fields — see ADR-0025), or null outside the real WebView.
*/
export async function graphGetSnapshot(): Promise<string | null> {
  if (typeof window.__JUCE__ === 'undefined') return null
  const result = await getNativeFunction('graphGetSnapshot')()
  return typeof result === 'string' ? result : null
}

export function graphRestoreSnapshot(json: string): Promise<CommandResult> {
  return callCommand('graphRestoreSnapshot', json)
}

/** Dev-convenience export (direct instruction — "does Claude have quick
    access to the patch I'm building?" / "yes, build that"): dumps the
    live graph to a fixed file on disk (repo root, next to the project
    itself — `exported-patch.json`, gitignored) as pretty-printed JSON, so
    it can be read directly without describing the patch in words every
    time. NOT a real save/load feature — same scope as graphGetSnapshot
    (no macro/view/meta content), not undo-tracked, overwrites the same
    file every call. `path` is the absolute path actually written, for
    a confirmation message; empty outside the real WebView.
*/
export interface ExportSnapshotResult {
  success: boolean
  errorMessage: string
  path: string
}

export async function graphExportSnapshot(): Promise<ExportSnapshotResult> {
  if (typeof window.__JUCE__ === 'undefined') {
    return { success: false, errorMessage: 'Not running inside the plugin WebView', path: '' }
  }
  const result = await getNativeFunction('graphExportSnapshot')()
  return result as ExportSnapshotResult
}

/** Which region ("voice" | "global" | "mono") each node in the current
    graph's LAST SUCCESSFUL compile landed in — a plain `{ [nodeId]: domain }`
    object, computed once per recompile by GraphEditController. Its UI
    consumer (NodeCard.tsx's DomainDot) was removed outright by
    wiki/plans/DomainRedesign.md Batch 4 — a per-node voice/global/mono label
    stopped being the right question once MultiplicityResolver made
    Scalar-vs-Poly a per-PORT fact instead (see graphGetNodeMultiplicity
    below, DomainDot's real replacement) — but the native function itself
    stays real and tested, independent complementary info, so this wrapper
    stays too even with no current caller in ui/src. Null outside the real
    WebView, same convention as graphGetSnapshot.
*/
export async function graphGetNodeDomains(): Promise<Record<string, 'voice' | 'global' | 'mono'> | null> {
  if (typeof window.__JUCE__ === 'undefined') return null
  const result = await getNativeFunction('graphGetNodeDomains')()
  return typeof result === 'string' ? (JSON.parse(result) as Record<string, 'voice' | 'global' | 'mono'>) : null
}

/** Per-port Scalar/Poly multiplicity plus the live instance-count badge data
    for every "life.voice" node — DomainDot's real replacement
    (wiki/plans/DomainRedesign.md Batch 4). `kind` is `'scalar'` or `'poly'`;
    `originId` is only ever set when `kind === 'poly'`. `badges` has an entry
    only for allocator nodes, and its two numbers are read fresh off the
    live processor on every call (they change on every voice on/off, far
    more often than a recompile) — never cache these across calls the way a
    graph snapshot could be. Null outside the real WebView.
*/
export interface PortMultiplicityInfo {
  kind: 'scalar' | 'poly'
  originId?: string
}

export interface NodeMultiplicityBadge {
  activeCount: number
  maxCount: number
}

export interface GraphMultiplicity {
  ports: Record<string, Record<string, PortMultiplicityInfo>>
  badges: Record<string, NodeMultiplicityBadge>
  /** Node id -> its output ports carrying stereo in the compiled graph
      (wiki/plans/StereoChannels.md). */
  stereo?: Record<string, string[]>
}

export async function graphGetNodeMultiplicity(): Promise<GraphMultiplicity | null> {
  if (typeof window.__JUCE__ === 'undefined') return null
  const result = await getNativeFunction('graphGetNodeMultiplicity')()
  return typeof result === 'string' ? (JSON.parse(result) as GraphMultiplicity) : null
}
