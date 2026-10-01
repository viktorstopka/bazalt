// Single external source of truth for the M10 canvas's interaction state
// (M10_REVIEW.md §23's retrospective, option (a)) — replaces the
// ghostRef-plus-React-state duplication that caused a stale-closure bug,
// and gives InfiniteCanvas.tsx's mount effect one place to clear "stuck"
// drag state from, instead of each gesture inventing its own recovery path.
//
// Camera is a module-level singleton with NO pub/sub: nothing in React ever
// needs to re-render from a camera change (it's applied to the DOM
// imperatively, once per animation frame, exactly like graphStore.ts's own
// node-drag comment explains for live positions — notifying on every
// pan/zoom tick would reintroduce that same perf bug). Being module-level
// rather than a ref created per InfiniteCanvas mount is what lets the
// camera survive switching to the Component Gallery / Stress Test and back
// (M10_REVIEW.md §20) instead of resetting by accident.
//
// The active gesture (pan/box-select/node-drag/wire-drag) is likewise a
// plain mutable field: InfiniteCanvas's gesture dispatcher guarantees at
// most one is set at a time (see startGesture()'s callers), and it's read
// at animation-frame rate, not through React.
//
// Ghost placement is the one piece of state a React render genuinely needs
// (GraphSurface.tsx mounts a portal for it) — it alone gets a real
// subscribe/getSnapshot pair, following graphStore.ts's own external-store
// convention.
import type { Camera } from './webgl/webglUtils'

export interface GhostPlacement {
  typeId: string
}

let camera: Camera = { panX: 0, panY: 0, zoom: 1 }
export function getCamera(): Camera {
  return camera
}
export function setCamera(next: Camera): void {
  camera = next
}

let ghost: GhostPlacement | null = null
const ghostListeners = new Set<() => void>()
function notifyGhost(): void {
  for (const listener of ghostListeners) listener()
}
export function getGhost(): GhostPlacement | null {
  return ghost
}
export function armGhost(typeId: string): void {
  ghost = { typeId }
  notifyGhost()
}
export function clearGhost(): void {
  if (!ghost) return
  ghost = null
  notifyGhost()
}
export function subscribeGhost(listener: () => void): () => void {
  ghostListeners.add(listener)
  return () => ghostListeners.delete(listener)
}

export interface PanGesture {
  kind: 'pan'
}
export interface BoxSelectGesture {
  kind: 'boxSelect'
  startX: number
  startY: number
  additive: boolean
  /** Selection at the moment the drag started — an additive (Shift)
      box-select is always `baseSelection ∪ currently-inside-the-box`,
      recomputed fresh every move, never an accumulation of past hits
      (M10_REVIEW.md §3).
  */
  baseSelection: ReadonlySet<string>
}
export interface NodeDragGesture {
  kind: 'nodeDrag'
  origins: Map<string, { x: number; y: number; el: HTMLElement }>
  startClientX: number
  startClientY: number
  /** The node actually clicked to start the drag — the one whose position
      gets snapped; every other selected node then moves by that same
      (unsnapped) delta, so a multi-drag stays rigid instead of shearing
      apart (M10_REVIEW.md §9).
  */
  primaryId: string
  /** The current frame's live (already-snapped) delta from each node's
      origin, updated every mousemove — read by the render loop to resolve
      cable anchors for the dragged node(s) before the move commits to
      graphStore on mouseup.
  */
  liveDx: number
  liveDy: number
}
export interface WireDragGesture {
  kind: 'wireDrag'
  fromNodeId: string
  fromPortId: string
  detachedWireId?: string
  hoverNodeId?: string
  hoverPortId?: string
  hoverValid?: boolean
  /** Hovering a compatible input that already has a wire plugged into it —
      committing here silently replaces that connection. Rendered as a
      distinct state (the about-to-be-replaced wire fades) rather than
      looking identical to dropping on an empty port (M10_REVIEW.md §7).
  */
  hoverReplacing?: boolean
}
/** wiki/plans/UtilMacro.md: mousedown on an unconnected, macro-able input
    port starts this instead of a plain WireDragGesture (which only ever
    starts from an input that already HAS a wire to detach — see
    InfiniteCanvas.tsx's mousedown handler). Committed on mouseup over
    genuine empty canvas/world space (graphStore.ts's createMacroFromPort);
    any other mouseup target (a node, a port, an overlay) cancels with no
    effect, same "only a true empty-space drop commits" rule ghost
    placement already follows for an unsplice-able wire hover.
*/
export interface DragToMacroGesture {
  kind: 'dragToMacro'
  nodeId: string
  portId: string
}
export type Gesture = PanGesture | BoxSelectGesture | NodeDragGesture | WireDragGesture | DragToMacroGesture | null

let gesture: Gesture = null
export function getGesture(): Gesture {
  return gesture
}
export function setGesture(next: Gesture): void {
  gesture = next
}

let spaceHeld = false
export function getSpaceHeld(): boolean {
  return spaceHeld
}
export function setSpaceHeld(next: boolean): void {
  spaceHeld = next
}

/** Focus-loss / tab-switch safety net (M10_REVIEW.md 0.2/0.3): no `mouseup`
    or `keyup` is guaranteed to arrive if the button/key is released outside
    the window, or the window loses focus mid-gesture (Alt-Tab, a host DAW
    stealing focus). Call from `window`'s own `blur` listener — clears every
    kind of "stuck" interaction state in one place.
*/
export function resetInteractionOnFocusLoss(): void {
  gesture = null
  spaceHeld = false
}
