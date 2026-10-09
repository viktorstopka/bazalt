import { forwardRef, useCallback, useEffect, useImperativeHandle, useRef, useState, useSyncExternalStore } from 'react'
import type { ReactNode } from 'react'
import { tokens } from '../theme/tokens'
import { createNodeEditorRenderer, tessellateCable, type CableSpec, type NodeEditorRenderer, type Point } from './webgl/nodeEditorRenderer'
import { hexToRgb, type Camera } from './webgl/webglUtils'
import { GraphSurface } from '../graph/GraphSurface'
import { AddMenu } from '../graph/AddMenu'
import { buildAnchorMap, measureNodeLocalPortOffsets, portKey, type PortAnchor } from '../graph/portAnchors'
import { portUiStyleForEndpoint } from '../graph/portUiKind'
import { canConnect, type ConnectionEndpoint } from '../graph/canConnect'
import { importImage } from '../graph/imageImport'
import {
  addImageAt,
  addMapFromMainOutput,
  addNode,
  addSumOfNodes,
  canAddFromNode,
  canSplice,
  copyFragment,
  copyNodes,
  cutNodes,
  getClipboard,
  pasteFragment,
  toggleListenAtPort,
  reportError,
  commitNodeMoves,
  commitWireDrag,
  createMacroFromPort,
  createViewerFromPort,
  defaultViewerTypeForPort,
  deleteNodes,
  ensureInitialized,
  findWireAtInput,
  getEndpoint,
  getSnapshot as getGraphSnapshot,
  isMacroablePort,
  redo,
  setSelection,
  spliceInsert,
  subscribe as subscribeGraph,
  undo,
} from '../graph/graphStore'
import { useGraphSnapshot } from '../graph/useGraphSnapshot'
import {
  armFragmentGhost,
  armGhost,
  clearGhost,
  getCamera,
  getGesture,
  getGhost,
  getSpaceHeld,
  resetInteractionOnFocusLoss,
  setCamera,
  setGesture,
  setSpaceHeld,
  subscribeGhost,
} from './interactionStore'
import './InfiniteCanvas.css'

const MIN_ZOOM = 0.1
const MAX_ZOOM = 8
const RIGHT_CLICK_MOVE_THRESHOLD = 4 // px — beyond this, a right-button gesture is a pan, not an Add-menu click
const BOX_SELECT_CLICK_THRESHOLD_PX = 4 // px — beyond this, a background mousedown-then-up is a real (if tiny) box-select drag, not a deselect-click
const PORT_HOVER_TOLERANCE = 16 // px
// Direct feedback: a flat radius from the mouse point (formerly 10px, then
// bumped to 22px) still felt too tight, and doesn't match how this actually
// reads on screen — "if the ghost box is over the cable it should insert."
// The splice hit-test below uses the GHOST'S OWN rendered bounding box
// instead of a fixed radius; this is just the extra margin padded onto that
// box on every side, so a small ghost (a Singleton/Decoration layout, much
// narrower than a Standard node card) still gets a reasonable minimum catch
// zone rather than only its own tiny footprint.
const SPLICE_HOVER_PADDING = 18 // px
const FIT_VIEW_PADDING = 80 // px
// Direct feedback: the initial view should just start out a bit more
// zoomed out than 1:1 — deliberately NOT computed by fitView's own
// "shrink/grow zoom until the bounds fit the viewport" algorithm (that's a
// different, bigger algorithm this is explicitly not meant to reuse); this
// is a plain fixed default, only the node bounds' *centre* is measured (see
// centerViewAtDefaultZoom below), not their extent.
const DEFAULT_INITIAL_ZOOM = 0.65

/** Shared by fitView and centerViewAtDefaultZoom: every currently-painted
    node's screen rect, converted to world-space and unioned into one box,
    via the current camera (so it's correct regardless of what pan/zoom
    happens to be active when this is called).
*/
function measureNodeWorldBounds(
  container: HTMLElement,
  canvasRect: DOMRect,
  camera: Camera,
): { minX: number; minY: number; maxX: number; maxY: number } | null {
  const nodeEls = container.querySelectorAll<HTMLElement>('[data-node-instance]')
  if (nodeEls.length === 0) return null

  let minX = Infinity
  let minY = Infinity
  let maxX = -Infinity
  let maxY = -Infinity
  nodeEls.forEach((el) => {
    const r = el.getBoundingClientRect()
    const worldX = (r.left - canvasRect.left - camera.panX) / camera.zoom
    const worldY = (r.top - canvasRect.top - camera.panY) / camera.zoom
    const worldW = r.width / camera.zoom
    const worldH = r.height / camera.zoom
    minX = Math.min(minX, worldX)
    minY = Math.min(minY, worldY)
    maxX = Math.max(maxX, worldX + worldW)
    maxY = Math.max(maxY, worldY + worldH)
  })
  return { minX, minY, maxX, maxY }
}

export interface SnapSettings {
  enabled: boolean
  sizeWorldUnits: number
}

export interface InfiniteCanvasHandle {
  fitView(): void
}

interface InfiniteCanvasProps {
  snapSettings: SnapSettings
  children?: ReactNode
}

function snapValue(value: number, settings: SnapSettings): number {
  if (!settings.enabled) return value
  return Math.round(value / settings.sizeWorldUnits) * settings.sizeWorldUnits
}

function closestNodeId(target: EventTarget | null): string | null {
  if (!(target instanceof Element)) return null
  return target.closest('[data-node-instance]')?.getAttribute('data-node-instance') ?? null
}

interface PortHit {
  nodeId: string
  portId: string
  direction: 'input' | 'output'
}

function closestPortAnchor(target: EventTarget | null): PortHit | null {
  if (!(target instanceof Element)) return null
  const el = target.closest('[data-port-anchor]') as HTMLElement | null
  if (!el) return null
  const { nodeId, portId, direction } = el.dataset
  if (!nodeId || !portId || (direction !== 'input' && direction !== 'output')) return null
  return { nodeId, portId, direction }
}

/** True for anything that owns its own drag/wheel gesture and must never
    also be treated as a node/canvas one (ValueSlider.tsx's drag-to-adjust,
    scroll-to-adjust). A React `stopPropagation()` call inside one of these
    controls can't prevent this file's own mousedown/wheel listeners from
    firing — they're plain `addEventListener` calls on `container`, which
    sits between the control and React's own event-delegation root, so the
    native event reaches `container` during the real bubble phase before
    React's synthetic dispatch (and therefore the control's own
    stopPropagation call) ever runs. Excluding the target here, at the
    source, is the only place this can actually be stopped.
*/
function isOwnGestureTarget(target: EventTarget | null): boolean {
  return target instanceof Element && !!target.closest('.value-slider, .trigger-select, .deco-own-gesture')
}

// Module-level (not a component ref) for the same reason interactionStore's
// camera is module-level: App.tsx unmounts/remounts InfiniteCanvas when
// switching to the Component Gallery or Stress Test and back
// (M10_REVIEW.md §20). If this were a per-mount ref, the very next mount
// would see "node count went 0→N" again (its own local counter starts at 0
// every time) and immediately re-run fitView(), silently overwriting the
// camera position interactionStore just finished preserving across that
// remount. Tracking it here instead makes "camera survives switching views"
// an actual, deliberate behavior rather than undone one line later.
let prevNodeCountForAutoFit = 0

/** The real node-editor canvas: owns the camera (via interactionStore.ts,
    not a local ref — see that module's own header comment), the WebGL2
    cable layer, and every pointer/keyboard interaction — pan/zoom,
    box-select, node drag/select, wire drag, ghost placement, Add menu,
    undo/redo, fit-view.

    Interaction architecture (M10_REVIEW.md §23's retrospective): gestures
    are OPT-IN, not opt-out — pan/box-select may only *start* when the
    mousedown target is the bare canvas or the world layer's own background
    (isCanvasOrWorldTarget, below), never "whatever's left after checking
    known exceptions." Every overlay control (top bar, Add menu, node
    context menu, and any future one) therefore needs zero awareness of the
    canvas to be safe — this is what bug 0.1 (top-bar clicks silently
    clearing selection) needed, structurally, not another stopPropagation
    call. At most one gesture is ever active at a time (interactionStore's
    `gesture` field), which also resolves the two-button-chord edge case
    for free. A `blur`/`visibilitychange` safety net force-clears gesture
    state if the window loses focus mid-drag, since no `mouseup` is
    guaranteed to arrive in that case.

    The render loop is dirty-flag-driven (requestFrame()/looping/dirty,
    below): idle time schedules zero animation frames, rather than the
    unconditional forever-loop this replaced. Port-anchor positions are
    resolved from a per-node offset cache (portAnchors.ts) plus plain
    arithmetic through the camera each frame, not a DOM re-measure sweep.
*/
export const InfiniteCanvas = forwardRef<InfiniteCanvasHandle, InfiniteCanvasProps>(function InfiniteCanvas({ snapSettings, children }, ref) {
  const containerRef = useRef<HTMLDivElement | null>(null)
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const worldRef = useRef<HTMLDivElement | null>(null)
  const selectionBoxRef = useRef<HTMLDivElement | null>(null)
  const hintLabelRef = useRef<HTMLDivElement | null>(null)
  const linkPlusRef = useRef<HTMLDivElement | null>(null)
  const ghostElRef = useRef<HTMLDivElement | null>(null)
  const snapSettingsRef = useRef(snapSettings)
  const lastMouseRef = useRef({ clientX: 0, clientY: 0 })
  // Assigned by the mount effect once it exists; lets component-level code
  // outside that effect's own closure (fitView, the Add menu's onChoose)
  // wake the render loop after mutating camera/ghost from the outside.
  const requestFrameRef = useRef<() => void>(() => {})

  const [overlayEl, setOverlayEl] = useState<HTMLDivElement | null>(null)
  const [addMenu, setAddMenu] = useState<{ x: number; y: number } | null>(null)
  const ghost = useSyncExternalStore(subscribeGhost, getGhost)

  const snapshot = useGraphSnapshot()

  useEffect(() => {
    snapSettingsRef.current = snapSettings
  }, [snapSettings])

  useEffect(() => {
    ensureInitialized()
  }, [])

  // Not currently reachable from any UI (the Fit View button was removed —
  // direct feedback: too much top-bar clutter while focusing on the editor
  // itself) but deliberately left wired rather than deleted: fitView is a
  // standard, likely-to-return navigation feature, not backend scaffolding
  // — cheap to keep dormant, a real rebuild if actually removed.
  const fitView = useCallback(() => {
    const container = containerRef.current
    const canvas = canvasRef.current
    if (!container || !canvas) return
    const canvasRect = canvas.getBoundingClientRect()
    const camera = getCamera()
    const bounds = measureNodeWorldBounds(container, canvasRect, camera)
    if (!bounds) return

    const viewportW = canvas.clientWidth
    const viewportH = canvas.clientHeight
    const boxW = Math.max(1, bounds.maxX - bounds.minX)
    const boxH = Math.max(1, bounds.maxY - bounds.minY)
    const zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, Math.min((viewportW - FIT_VIEW_PADDING * 2) / boxW, (viewportH - FIT_VIEW_PADDING * 2) / boxH)))
    const centerX = (bounds.minX + bounds.maxX) / 2
    const centerY = (bounds.minY + bounds.maxY) / 2
    setCamera({
      zoom,
      panX: viewportW / 2 - centerX * zoom,
      panY: viewportH / 2 - centerY * zoom,
    })
    requestFrameRef.current()
  }, [])

  useImperativeHandle(ref, () => ({ fitView }), [fitView])

  /** The actual initial-load default (direct feedback: "zoomed out a bit
      more by default, not fit view") — centres on the seeded nodes' own
      midpoint, same measurement fitView uses, but at a fixed zoom instead
      of one computed to snugly fit the viewport. Not exposed on
      InfiniteCanvasHandle — this is purely the one-time load behaviour.
  */
  const centerViewAtDefaultZoom = useCallback(() => {
    const container = containerRef.current
    const canvas = canvasRef.current
    if (!container || !canvas) return
    const canvasRect = canvas.getBoundingClientRect()
    const bounds = measureNodeWorldBounds(container, canvasRect, getCamera())
    if (!bounds) return

    const zoom = DEFAULT_INITIAL_ZOOM
    const centerX = (bounds.minX + bounds.maxX) / 2
    const centerY = (bounds.minY + bounds.maxY) / 2
    setCamera({
      zoom,
      panX: canvas.clientWidth / 2 - centerX * zoom,
      panY: canvas.clientHeight / 2 - centerY * zoom,
    })
    requestFrameRef.current()
  }, [])

  // Re-centre (at the fixed default zoom, not a fitView-computed one — see
  // that function's own comment) whenever the node count goes from 0 to
  // non-zero — not just the very first time ever (M10_REVIEW.md §4):
  // deleting every node and adding one back later re-triggers this too.
  useEffect(() => {
    // Real descriptors and the real graph snapshot arrive from two
    // independent async fetches (ensureInitialized(), M19) — nodes.length
    // can go 0->N before descriptorsLoaded is true, at which point
    // GraphSurface.tsx renders nothing yet (resolveNodeDescriptor returns
    // undefined), so measureNodeWorldBounds finds no DOM elements and
    // centerViewAtDefaultZoom() silently no-ops. Since
    // prevNodeCountForAutoFit still updated to a nonzero value on that
    // failed attempt, the 0->nonzero edge this effect watches for would
    // never fire again — permanently skipping the auto-centre-on-load this
    // was supposed to do. Wait for descriptors too before treating the
    // transition as "consumed".
    if (!snapshot.descriptorsLoaded) return
    const count = snapshot.nodes.length
    if (prevNodeCountForAutoFit === 0 && count > 0) {
      requestAnimationFrame(() => centerViewAtDefaultZoom())
    }
    prevNodeCountForAutoFit = count
  }, [snapshot.nodes.length, snapshot.descriptorsLoaded, centerViewAtDefaultZoom])

  useEffect(() => {
    const container = containerRef.current
    const canvas = canvasRef.current
    const world = worldRef.current
    if (!container || !canvas || !world) return
    const gl = canvas.getContext('webgl2')
    if (!gl) {
      console.error('WebGL2 not available — cables will not render')
    }
    let renderer: NodeEditorRenderer | null = gl ? createNodeEditorRenderer(gl) : null

    let rafHandle = 0
    let size = { cssWidth: 0, cssHeight: 0, dpr: window.devicePixelRatio || 1 }
    let lastHintText = ''
    let hasMouseMoved = false

    // ---- Dirty-flag render loop: idle time schedules zero animation
    // frames. requestFrame() is called from any handler that changes
    // something the WebGL layer needs to redraw (camera, an active
    // gesture's live state) or that needs at least one more measurement
    // pass (graph changed). frame() itself decides whether to keep looping
    // based on whether a gesture/ghost is actively animating. ----
    let looping = false
    let dirty = true
    const requestFrame = (): void => {
      dirty = true
      if (!looping) {
        looping = true
        rafHandle = requestAnimationFrame(frame)
      }
    }
    requestFrameRef.current = requestFrame

    // ---- Per-node port-anchor offset cache (M10_REVIEW.md §23's
    // retrospective, rendering item 2): measured once per node (here, and
    // again only if that node's own ResizeObserver fires), not swept via
    // getBoundingClientRect every frame. ----
    const nodeOffsetCache = new Map<string, Map<string, PortAnchor>>()
    const nodeElByIdCache = new Map<string, HTMLElement>()
    const remeasureNode = (id: string, el: HTMLElement): void => {
      nodeOffsetCache.set(id, measureNodeLocalPortOffsets(el, getCamera().zoom))
    }
    // A node just seen for the first time keeps getting remeasured for a few
    // more frames rather than trusting that very first getBoundingClientRect()
    // forever — found via real hands-on testing (restoring a persisted
    // session, which mounts every node card in one React commit instead of
    // one at a time): several nodes' cards weren't at their final CSS layout
    // yet on the frame they were first queried (parameter-value pills/text
    // still settling their width), and ResizeObserver's own first callback
    // can land on that same already-shifted size, so it never fires again to
    // correct it — the wrong offset then sticks forever, producing cables
    // that render pinned to nothing. This is a real layout-settling race,
    // separate from (and found after fixing) the camera-zoom/transform
    // ordering race elsewhere in this file.
    const NODE_SETTLE_FRAMES = 10
    const nodeSettleFramesRemaining = new Map<string, number>()
    const nodeResizeObserver = new ResizeObserver((entries) => {
      for (const entry of entries) {
        const el = entry.target as HTMLElement
        const id = el.getAttribute('data-node-instance')
        if (id) remeasureNode(id, el)
      }
      requestFrame()
    })
    // Returns whether any node still has no cached measurement — real
    // engine data arrives from two independent async fetches (the graph
    // snapshot and the descriptor catalog, ensureInitialized()), so a node
    // can legitimately exist in the graph for one or more frames before
    // React has actually painted its card (it renders nothing at all until
    // BOTH have arrived — GraphSurface.tsx's resolveNodeDescriptor()
    // returns undefined until descriptors load). The querySelector below
    // finding nothing that frame is a real, expected transient state, not
    // a bug on its own — but frame()'s caller MUST keep the RAF loop alive
    // until it resolves, or nothing ever measures these nodes at all once
    // `dirty` next goes false with no gesture/ghost active (a real bug this
    // return value fixes: cables silently rendered from stale/absent
    // offsets — found via real hands-on testing, M19).
    const syncNodeOffsetCache = (currentNodes: ReadonlyArray<{ id: string }>): boolean => {
      const currentIds = new Set(currentNodes.map((n) => n.id))
      for (const id of [...nodeElByIdCache.keys()]) {
        if (currentIds.has(id)) continue
        const el = nodeElByIdCache.get(id)
        if (el) nodeResizeObserver.unobserve(el)
        nodeElByIdCache.delete(id)
        nodeOffsetCache.delete(id)
        nodeSettleFramesRemaining.delete(id)
      }
      let pending = false
      for (const id of currentIds) {
        if (nodeElByIdCache.has(id)) {
          const remaining = nodeSettleFramesRemaining.get(id) ?? 0
          if (remaining > 0) {
            const el = nodeElByIdCache.get(id)
            if (el) remeasureNode(id, el)
            nodeSettleFramesRemaining.set(id, remaining - 1)
            pending = true
          }
          continue
        }
        const el = container.querySelector<HTMLElement>(`[data-node-instance="${id}"]`)
        if (!el) {
          pending = true
          continue // not painted yet this frame — the caller keeps looping until it is
        }
        nodeResizeObserver.observe(el)
        nodeElByIdCache.set(id, el)
        remeasureNode(id, el)
        nodeSettleFramesRemaining.set(id, NODE_SETTLE_FRAMES)
        pending = true
      }
      return pending
    }

    // A port's connected-state can change its glyph's rendered size (dot
    // vs. typed glyph, NodeCard.tsx's PortGlyph) without changing its NODE
    // WRAPPER's own flow size — position:absolute children don't affect
    // their ancestor's sizing, so nodeResizeObserver above never fires for
    // this specific change. Compare a cheap connectivity signature on every
    // graph notification and force a full remeasure when it actually
    // changed, so a just-connected port's cable snaps to its new (bigger)
    // glyph instead of a stale cached edge from when it was still a dot.
    let lastWireSignature = ''
    const wireSignature = (wires: ReadonlyArray<{ fromNodeId: string; fromPortId: string; toNodeId: string; toPortId: string }>): string =>
      wires
        .map((w) => `${w.fromNodeId}.${w.fromPortId}>${w.toNodeId}.${w.toPortId}`)
        .sort()
        .join('|')

    const unsubscribeGraph = subscribeGraph(() => {
      const signature = wireSignature(getGraphSnapshot().wires)
      if (signature !== lastWireSignature) {
        lastWireSignature = signature
        for (const [, el] of nodeElByIdCache) nodeResizeObserver.unobserve(el)
        nodeElByIdCache.clear()
        nodeOffsetCache.clear()
      }
      requestFrame()
    })

    // ---- WebGL context loss/restore (M10_REVIEW.md 0.6) ----
    const onContextLost = (e: Event): void => {
      e.preventDefault()
      renderer = null
      requestFrame()
    }
    const onContextRestored = (): void => {
      const gl2 = canvas.getContext('webgl2')
      renderer = gl2 ? createNodeEditorRenderer(gl2) : null
      requestFrame()
    }
    canvas.addEventListener('webglcontextlost', onContextLost, false)
    canvas.addEventListener('webglcontextrestored', onContextRestored, false)

    // ---- Interaction refs (imperative; gesture "presence" lives in
    // interactionStore, this closure just needs a few local working
    // variables the store doesn't need to know about). ----
    let panLastX = 0
    let panLastY = 0
    let ghostSpliceHoverWireId: string | null = null
    // The anchor map of the last frame, for hit-testing wires outside frame().
    let latestAnchors = new Map<string, PortAnchor>()
    let rightClickStart: { x: number; y: number; wasGhostActive: boolean } | null = null
    let lastMouseCanvasX = 0
    let lastMouseCanvasY = 0

    const resize = (): void => {
      const dpr = window.devicePixelRatio || 1
      const width = container.clientWidth
      const height = container.clientHeight
      canvas.width = Math.max(1, Math.round(width * dpr))
      canvas.height = Math.max(1, Math.round(height * dpr))
      canvas.style.width = `${width}px`
      canvas.style.height = `${height}px`
      size = { cssWidth: width, cssHeight: height, dpr }
      requestFrame()
    }

    const canvasToWorld = (canvasX: number, canvasY: number): Point => {
      const camera = getCamera()
      return { x: (canvasX - camera.panX) / camera.zoom, y: (canvasY - camera.panY) / camera.zoom }
    }

    /** Opt-in gate for pan/box-select/right-click-Add-menu: true only for
        the bare canvas element or anywhere within the world layer's own
        background (which includes `.graph-surface`'s unoccupied area
        between nodes — a descendant of `world`, not `world` itself, so a
        strict `=== world` check would wrongly reject it). Nodes/ports are
        already claimed earlier by closestNodeId/closestPortAnchor; overlay
        UI (top bar, Add menu, context menu) lives outside `world` entirely,
        so it's excluded structurally, with zero awareness of this file.
    */
    const isCanvasOrWorldTarget = (target: EventTarget | null): boolean => {
      return target === canvas || (target instanceof Node && world.contains(target))
    }

    const endpointColorRgb = (endpoint: ConnectionEndpoint | undefined): readonly [number, number, number] => {
      if (!endpoint) return hexToRgb(tokens.color.portValue)
      // Colour is what the signal means; poly shows on the node, never on the cable (DataAndWavetable.md D3/D4).
      return hexToRgb(portUiStyleForEndpoint(endpoint).color)
    }

    const distanceToSegment = (p: Point, a: Point, b: Point): number => {
      const dx = b.x - a.x
      const dy = b.y - a.y
      const lengthSq = dx * dx + dy * dy
      const t = lengthSq === 0 ? 0 : Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq))
      const projX = a.x + t * dx
      const projY = a.y + t * dy
      return Math.hypot(p.x - projX, p.y - projY)
    }

    // ---- Main per-frame update ----
    const frame = (): void => {
      dirty = false

      const currentDpr = window.devicePixelRatio || 1
      if (currentDpr !== size.dpr) resize() // M10_REVIEW.md 0.5: catches a monitor-DPI change without needing an unrelated resize

      // Camera must be read AND applied to the world layer's CSS transform
      // before any port measurement happens below (syncNodeOffsetCache ->
      // remeasureNode -> getBoundingClientRect()). Port measurement divides
      // by getCamera().zoom independently — if the DOM still visually
      // reflected a stale zoom (this used to be applied at the END of
      // frame(), one call site down), a frame landing right when zoom is
      // changing (e.g. centerViewAtDefaultZoom's fit-view RAF) would measure
      // against last frame's transform while dividing by this frame's zoom,
      // producing a wrong, internally-inconsistent world-space offset that
      // only self-corrected if a later frame happened to re-measure under a
      // now-consistent DOM state. Root cause of the intermittent "cables
      // connect to the wrong place" bug — reproduced across several
      // relaunches with identical code before this fix.
      const camera = getCamera()
      world.style.transform = `translate(${camera.panX}px, ${camera.panY}px) scale(${camera.zoom})`
      // Direct feedback, repeatedly: "borders are completely randomly
      // disappearing and appearing [during] zoom... when releasing...
      // stops on some of them being gone." Root cause: every node's own
      // `border: 1px solid` (NodeCard.css etc.) lives INSIDE this scaled
      // layer, so its on-screen width is `1px * camera.zoom` — a
      // continuous float, sub-pixel at almost every real zoom level. A
      // browser rasterizes a sub-pixel-wide line via antialiasing
      // (effectively reducing its own opacity to its fractional pixel
      // coverage), and since every node sits at a different fractional
      // screen offset from this same transform, each one crosses that
      // rounding threshold differently — "random" per node, frozen at
      // whatever it lands on once zooming stops. `--canvas-hairline`
      // (read by every node-internal border via `var(--canvas-hairline,
      // var(--stroke-thin))` — the fallback keeps the M9 gallery, which
      // has no scaled ancestor to inherit this from, at the plain
      // unscaled token) is the inverse of this frame's own zoom, so the
      // border's LOGICAL (pre-scale) width divides out to exactly 1 real
      // screen pixel after this transform applies, same as every other
      // hairline border in the app that isn't inside a scaled layer.
      world.style.setProperty('--canvas-hairline', `${1 / camera.zoom}px`)

      const graphNow = getGraphSnapshot()
      const hasUnmeasuredNodes = syncNodeOffsetCache(graphNow.nodes)

      const g = getGesture()

      // A node-drag hasn't committed to the store yet (only on mouseup) —
      // substitute its live (already-snapped) position when building
      // anchors, so cables track the node visually while it's being
      // dragged instead of only jumping once the drag commits.
      const positionsForAnchors =
        g?.kind === 'nodeDrag'
          ? graphNow.nodes.map((n) => {
              const live = g.origins.get(n.id)
              if (!live) return n
              return { id: n.id, x: live.x + g.liveDx, y: live.y + g.liveDy }
            })
          : graphNow.nodes
      const anchors = buildAnchorMap(positionsForAnchors, nodeOffsetCache, camera)
      latestAnchors = anchors

      // Wire-drag hover detection happens BEFORE the main cable list is
      // built, so a "will replace" hit can dim the existing wire it would
      // replace in the same pass (see the main loop below).
      let replacingWireId: string | null = null
      if (g?.kind === 'wireDrag') {
        const fixedAnchor = anchors.get(portKey(g.fromNodeId, g.fromPortId, 'output'))
        if (fixedAnchor) {
          let bestKey: string | null = null
          let bestDist = PORT_HOVER_TOLERANCE
          let bestNodeId = ''
          let bestPortId = ''
          for (const [key, anchor] of anchors) {
            if (!key.endsWith(':input')) continue
            const d = Math.hypot(anchor.x - lastMouseCanvasX, anchor.y - lastMouseCanvasY)
            if (d < bestDist) {
              bestDist = d
              bestKey = key
              const [nodeId, portId] = key.split(':')
              bestNodeId = nodeId
              bestPortId = portId
            }
          }
          if (bestKey) {
            const sourceEndpoint = getEndpoint(g.fromNodeId, g.fromPortId, 'output')
            const targetEndpoint = getEndpoint(bestNodeId, bestPortId, 'input')
            const valid = !!sourceEndpoint && !!targetEndpoint && canConnect(sourceEndpoint, targetEndpoint, getGraphSnapshot().multiplicity)
            const occupied = findWireAtInput(bestNodeId, bestPortId)
            g.hoverNodeId = bestNodeId
            g.hoverPortId = bestPortId
            g.hoverValid = valid
            g.hoverReplacing = valid && !!occupied && occupied.id !== g.detachedWireId
            if (g.hoverReplacing && occupied) replacingWireId = occupied.id
          } else {
            g.hoverNodeId = undefined
            g.hoverPortId = undefined
            g.hoverValid = undefined
            g.hoverReplacing = undefined
          }
        }
      }

      // Splice-hover hit-test for ghost placement, reusing the same
      // tessellation the renderer draws with (nodeEditorRenderer.ts export).
      // Computed BEFORE the main cable list below, same reasoning as the
      // wire-drag "will replace" hover above — so the targeted wire itself
      // can be highlighted in the same pass, not just described in a text
      // hint off to the side (direct feedback: the hint used to be the only
      // feedback at all, always visible even with nothing nearby to insert
      // into — see the hint-label block below for the visibility half of
      // that fix).
      //
      // Direct feedback, second pass: this used to be a flat radius around
      // the mouse point; now it's genuinely "does the ghost's own box touch
      // the cable" (plus SPLICE_HOVER_PADDING's margin) — a much bigger, and
      // more honest, effective target for a Standard-layout node's full
      // ~190px+ card than any single flat radius could be, while a small
      // ghost (Singleton/Decoration) still only gets its own real footprint
      // plus that same margin, not an oversized circle. The gate itself
      // ("does this cable pass through the box at all") is a plain
      // point-in-rectangle test against the cable's existing 25-point
      // tessellation — fine-grained enough relative to a node-card-sized
      // box for any cable of ordinary on-screen length; it isn't exact
      // segment-vs-rectangle intersection, and doesn't need to be for a
      // hover-feedback nicety. Ties (more than one cable's tessellation
      // passing through the box, a real scenario with a dense patch) are
      // broken by distanceToSegment() from the BOX'S OWN CENTER — the ghost
      // may be snapped a little away from the raw mouse point, so its own
      // centre is the more honest "which one is really under it" measure —
      // nearest segment wins, same idiom the wire-drag hover above already
      // uses for its own nearest-port tie-break.
      const currentGhost = getGhost()
      let ghostSpliceValid = false
      if (currentGhost && ghostElRef.current) {
        // Position the ghost BEFORE measuring its box below — otherwise
        // getBoundingClientRect() would read back last frame's position
        // (one rAF tick, ~16ms, stale) instead of where the mouse actually
        // is this frame.
        const worldPos = canvasToWorld(lastMouseCanvasX, lastMouseCanvasY)
        ghostElRef.current.style.left = `${snapValue(worldPos.x, snapSettingsRef.current)}px`
        ghostElRef.current.style.top = `${snapValue(worldPos.y, snapSettingsRef.current)}px`
      }
      // A pasted/duplicated selection is never spliced into a cable.
      if (currentGhost && !currentGhost.fragment && ghostElRef.current) {
        const ghostRect = ghostElRef.current.getBoundingClientRect()
        const canvasRectNow = canvas.getBoundingClientRect()
        const box = {
          left: ghostRect.left - canvasRectNow.left - SPLICE_HOVER_PADDING,
          top: ghostRect.top - canvasRectNow.top - SPLICE_HOVER_PADDING,
          right: ghostRect.right - canvasRectNow.left + SPLICE_HOVER_PADDING,
          bottom: ghostRect.bottom - canvasRectNow.top + SPLICE_HOVER_PADDING,
        }
        const center: Point = { x: (box.left + box.right) / 2, y: (box.top + box.bottom) / 2 }
        const insideBox = (p: Point): boolean => p.x >= box.left && p.x <= box.right && p.y >= box.top && p.y <= box.bottom

        let nearestWireId: string | null = null
        let nearestDist = Infinity
        for (const wire of graphNow.wires) {
          const from = anchors.get(portKey(wire.fromNodeId, wire.fromPortId, 'output'))
          const to = anchors.get(portKey(wire.toNodeId, wire.toPortId, 'input'))
          if (!from || !to) continue
          const points = tessellateCable({ from, to, color: [0, 0, 0], alpha: 1, dashed: false })
          for (let i = 0; i < points.length - 1; i++) {
            if (!insideBox(points[i]) && !insideBox(points[i + 1])) continue
            const d = distanceToSegment(center, points[i], points[i + 1])
            if (d < nearestDist) {
              nearestDist = d
              nearestWireId = wire.id
            }
          }
        }
        ghostSpliceHoverWireId = nearestWireId
        ghostSpliceValid = nearestWireId ? canSplice(nearestWireId, currentGhost.typeId) : false
      } else {
        ghostSpliceHoverWireId = null
      }

      const cables: CableSpec[] = []
      for (const wire of graphNow.wires) {
        if (wire.id === (g?.kind === 'wireDrag' ? g.detachedWireId : undefined)) continue
        const from = anchors.get(portKey(wire.fromNodeId, wire.fromPortId, 'output'))
        const to = anchors.get(portKey(wire.toNodeId, wire.toPortId, 'input'))
        if (!from || !to) continue
        const endpoint = getEndpoint(wire.fromNodeId, wire.fromPortId, 'output')
        // "Will replace" feedback (M10_REVIEW.md §7): the existing wire a
        // committed drop would remove fades rather than looking identical
        // to any other settled wire — ADR-0010 governs the drag-preview
        // cable's own colouring, this only dims the about-to-change one.
        let alpha = wire.id === replacingWireId ? 0.35 : 1
        let color = endpointColorRgb(endpoint)
        let dashed = false
        // Splice-target feedback: the exact wire a placed node would insert
        // into (or, if its type can't splice here, would leave alone) is
        // now visibly distinct on the canvas itself, not just named in the
        // floating hint label — same accent/error vocabulary ADR-0010
        // already uses for wire-drag hover.
        if (wire.id === ghostSpliceHoverWireId) {
          color = ghostSpliceValid ? hexToRgb(tokens.color.accent) : hexToRgb(tokens.color.error)
          alpha = 1
          dashed = !ghostSpliceValid
        }
        const stereo = graphNow.stereoOutputs.get(wire.fromNodeId)?.has(wire.fromPortId) ?? false
        cables.push({ from, to, color, alpha, dashed, stereo })
      }

      if (g?.kind === 'wireDrag') {
        const fixedAnchor = anchors.get(portKey(g.fromNodeId, g.fromPortId, 'output'))
        if (fixedAnchor) {
          const sourceEndpoint = getEndpoint(g.fromNodeId, g.fromPortId, 'output')
          const baseColor = endpointColorRgb(sourceEndpoint)
          let endPoint: Point = { x: lastMouseCanvasX, y: lastMouseCanvasY }
          let color = baseColor
          let alpha = 0.4
          let dashed = false
          if (g.hoverNodeId && g.hoverPortId) {
            const hoverAnchor = anchors.get(portKey(g.hoverNodeId, g.hoverPortId, 'input'))
            if (hoverAnchor) endPoint = hoverAnchor
            if (g.hoverValid) {
              color = baseColor
              alpha = 1
            } else {
              color = hexToRgb(tokens.color.error)
              alpha = 1
              dashed = true
            }
          }
          cables.push({ from: fixedAnchor, to: endPoint, color, alpha, dashed })
        }
      }

      // wiki/plans/UtilMacro.md: a dashed preview cable from the dragged
      // input back to the cursor — always dashed (unlike wireDrag's
      // valid/rejected colouring above), since there's no "target" to
      // validate against yet; the drop only ever either commits on empty
      // space or cancels, never rejects.
      if (g?.kind === 'dragToMacro') {
        const fixedAnchor = anchors.get(portKey(g.nodeId, g.portId, 'input'))
        if (fixedAnchor) {
          const targetEndpoint = getEndpoint(g.nodeId, g.portId, 'input')
          const color = endpointColorRgb(targetEndpoint)
          cables.push({ from: { x: lastMouseCanvasX, y: lastMouseCanvasY }, to: fixedAnchor, color, alpha: 0.6, dashed: true })
        }
      }

      // Ctrl+drag Add: a straight line from the press to the cursor, green
      // over a node it can add, with a "+" riding on the cursor.
      if (g?.kind === 'linkAdd') {
        const color = g.hoverValid ? hexToRgb(tokens.color.gestureValid) : hexToRgb(tokens.color.textSecondary)
        cables.push({ from: { x: g.startX, y: g.startY }, to: { x: lastMouseCanvasX, y: lastMouseCanvasY }, color, alpha: g.hoverValid ? 1 : 0.6, dashed: !g.hoverValid })
        if (linkPlusRef.current) {
          linkPlusRef.current.style.display = 'block'
          linkPlusRef.current.style.left = `${lastMouseRef.current.clientX}px`
          linkPlusRef.current.style.top = `${lastMouseRef.current.clientY}px`
          linkPlusRef.current.classList.toggle('infinite-canvas-link-plus-valid', !!g.hoverValid)
        }
      } else if (linkPlusRef.current && linkPlusRef.current.style.display !== 'none') {
        linkPlusRef.current.style.display = 'none'
      }

      renderer?.render({ cssWidth: size.cssWidth, cssHeight: size.cssHeight, dpr: size.dpr, zoom: camera.zoom, cables })

      if (currentGhost) {
        // Ghost positioning itself now happens earlier, right before the
        // splice hit-test above measures its box — see that block's own
        // comment for why.

        // Direct feedback: the hint used to always show "click to place"
        // even with no wire anywhere nearby — noise for the common case.
        // Now it only appears once the ghost's own box actually touches a
        // wire, and it agrees with the highlighted wire above: "click to
        // insert here" when splice-able, an explicit
        // rejected-state hint when a wire's nearby but its type can't
        // accept this node (no in/out ports at all, or a type mismatch) —
        // the click still places the node, just unconnected in that case
        // (see onClick, M10_REVIEW.md §6/§23). Plain Esc-to-cancel placement
        // needs no ongoing hint at all; that's discoverable once, not on
        // every frame.
        if (ghostSpliceHoverWireId) {
          const text = ghostSpliceValid ? 'click to insert here' : "can't splice here — click to place unconnected"
          if (hintLabelRef.current && text !== lastHintText) {
            hintLabelRef.current.textContent = text
            lastHintText = text
          }
          if (hintLabelRef.current) {
            hintLabelRef.current.style.display = 'block'
            hintLabelRef.current.style.left = `${lastMouseRef.current.clientX + 16}px`
            hintLabelRef.current.style.top = `${lastMouseRef.current.clientY + 16}px`
          }
        } else if (hintLabelRef.current) {
          hintLabelRef.current.style.display = 'none'
        }
      } else if (g?.kind === 'dragToMacro') {
        // wiki/plans/UtilMacro.md: unlike the ghost-splice hint above, this
        // one is unconditional for the whole gesture's duration — there's
        // no "nothing nearby" case to stay quiet for; any empty-space drop
        // always creates a macro, so the hint is always accurate.
        const text = 'release on empty space to create a Macro · Esc to cancel'
        if (hintLabelRef.current && text !== lastHintText) {
          hintLabelRef.current.textContent = text
          lastHintText = text
        }
        if (hintLabelRef.current) {
          hintLabelRef.current.style.display = 'block'
          hintLabelRef.current.style.left = `${lastMouseRef.current.clientX + 16}px`
          hintLabelRef.current.style.top = `${lastMouseRef.current.clientY + 16}px`
        }
      } else if (hintLabelRef.current) {
        hintLabelRef.current.style.display = 'none'
      }

      // Deliberately no `will-change: transform` on the world layer (tried
      // and reverted — direct feedback: it stayed visibly pixelated at rest
      // for a noticeable stretch after zooming, only sharpening again on
      // the next unrelated interaction). GPU-layer-promoting this element
      // caches a rasterized bitmap and scales *that* during a gesture, which
      // is the actual source of the blur — trying to time dropping the
      // promotion to force a fresh rasterize turned out not to be reliably
      // fast on this WebView2 build. Leaving the layer un-promoted means
      // Chromium repaints the DOM content fresh at every scale, all the
      // time — more paint work per frame, but at M10's node counts that's
      // imperceptible, and it's never blurry, ever, with no settle delay to
      // tune. (Transform itself is applied at the top of frame(), before
      // port measurement — see the comment there.)

      if (dirty || getGesture() !== null || getGhost() !== null || hasUnmeasuredNodes) {
        rafHandle = requestAnimationFrame(frame)
      } else {
        looping = false
      }
    }

    // ---- Keyboard ----
    const isTypingTarget = (target: EventTarget | null): boolean => {
      return target instanceof HTMLElement && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA')
    }

    const onKeyDown = (e: KeyboardEvent): void => {
      if (isTypingTarget(e.target)) {
        if (e.code === 'Space') return // let text fields type spaces normally
      } else if (e.code === 'Space') {
        setSpaceHeld(true)
      }
      if (isTypingTarget(e.target)) return

      if (e.key === 'Escape') {
        if (getGhost()) clearGhost()
        else if (getGesture()?.kind === 'wireDrag' || getGesture()?.kind === 'dragToMacro' || getGesture()?.kind === 'linkAdd') setGesture(null)
        setAddMenu(null)
        requestFrame()
        return
      }

      const gestureActive = getGesture() !== null

      if ((e.key === 'Delete' || e.key === 'Backspace') && !getGhost() && !gestureActive) {
        const selected = [...getGraphSnapshot().selection]
        if (selected.length > 0) {
          e.preventDefault()
          deleteNodes(selected)
        }
        return
      }
      if (e.key.toLowerCase() === 'a' && e.shiftKey && !getGhost()) {
        e.preventDefault()
        // No prior mouse position (a keyboard-only session, e.g. Shift+A
        // right after launch) falls back to the viewport centre instead of
        // (0,0) — M10_REVIEW.md §5.
        if (hasMouseMoved) {
          setAddMenu({ x: lastMouseRef.current.clientX, y: lastMouseRef.current.clientY })
        } else {
          const rect = canvas.getBoundingClientRect()
          setAddMenu({ x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 })
        }
        return
      }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z' && !gestureActive) {
        e.preventDefault()
        if (e.shiftKey) redo()
        else undo()
        return
      }
      // ---- wiki/ROADMAP.md stage 0 shortcuts ----
      const mod = e.ctrlKey || e.metaKey
      const key = e.key.toLowerCase()
      const selected = [...getGraphSnapshot().selection]
      // Ctrl+R always: the WebView would otherwise reload the whole UI.
      if (mod && key === 'r') {
        e.preventDefault()
        if (selected.length === 1) {
          const el = container.querySelector<HTMLElement>(`[data-node-instance="${CSS.escape(selected[0])}"]`)
          el?.dispatchEvent(new CustomEvent('bazalt-rename'))
        }
        return
      }
      if (gestureActive) return
      if (mod && !e.shiftKey && key === 'a') {
        e.preventDefault()
        setSelection(getGraphSnapshot().nodes.map((n) => n.id))
        return
      }
      if (mod && !e.shiftKey && key === 'c') {
        if (selected.length > 0) {
          e.preventDefault()
          copyNodes(selected)
        }
        return
      }
      if (mod && !e.shiftKey && key === 'x') {
        if (selected.length > 0) {
          e.preventDefault()
          cutNodes(selected)
        }
        return
      }
      if (mod && !e.shiftKey && key === 'v') {
        const clipboard = getClipboard()
        if (clipboard) {
          e.preventDefault()
          armFragmentGhost(clipboard)
          requestFrame()
        }
        return
      }
      if (mod && !e.shiftKey && key === 'd') {
        // Duplicate follows the cursor like a placement; the clipboard is untouched.
        e.preventDefault()
        const fragment = copyFragment(selected)
        if (fragment) {
          armFragmentGhost(fragment)
          requestFrame()
        }
        return
      }
      if (mod && !e.shiftKey && key === 'y') {
        // A Map from the selected node's main output, placed to its right.
        // (Redo stays on Ctrl+Shift+Z.)
        e.preventDefault()
        if (selected.length === 1) {
          const node = getGraphSnapshot().nodes.find((n) => n.id === selected[0])
          const el = container.querySelector<HTMLElement>(`[data-node-instance="${CSS.escape(selected[0])}"]`)
          if (node) {
            const x = snapValue(node.x + (el?.offsetWidth ?? 200) + 60, snapSettingsRef.current)
            addMapFromMainOutput(node.id, x, snapValue(node.y, snapSettingsRef.current))
          }
        }
        return
      }
      // Single keys arm a placement, exactly like picking from the Add menu.
      if (!mod && !e.altKey && !e.shiftKey) {
        const typeId = key === 'r' ? 'adapt.map' : key === 'a' ? 'math.add' : key === 's' || key === 'm' ? 'math.multiply' : null
        if (typeId) {
          e.preventDefault()
          setAddMenu(null)
          armGhost(typeId)
          requestFrame()
        }
      }
    }
    const onKeyUp = (e: KeyboardEvent): void => {
      if (e.code === 'Space') setSpaceHeld(false)
    }

    // ---- Focus-loss safety net (M10_REVIEW.md 0.2/0.3): no mouseup/keyup
    // is guaranteed if the button/key is released outside the window, or
    // the window loses focus mid-gesture. ----
    const onWindowBlur = (): void => {
      resetInteractionOnFocusLoss()
      if (selectionBoxRef.current) selectionBoxRef.current.style.display = 'none'
      rightClickStart = null
      requestFrame()
    }
    const onVisibilityChange = (): void => {
      if (document.hidden) onWindowBlur()
    }

    /** The wire passing within `tolerance` screen pixels of a canvas-local
        point, if any (the same tessellation the renderer draws). */
    const wireNear = (point: Point, tolerance = 6): string | null => {
      let nearest: string | null = null
      let nearestDistance = tolerance
      for (const wire of getGraphSnapshot().wires) {
        const from = latestAnchors.get(portKey(wire.fromNodeId, wire.fromPortId, 'output'))
        const to = latestAnchors.get(portKey(wire.toNodeId, wire.toPortId, 'input'))
        if (!from || !to) continue
        const points = tessellateCable({ from, to, color: [0, 0, 0], alpha: 1, dashed: false })
        for (let i = 0; i < points.length - 1; i++) {
          const d = distanceToSegment(point, points[i], points[i + 1])
          if (d < nearestDistance) {
            nearestDistance = d
            nearest = wire.id
          }
        }
      }
      return nearest
    }

    /** wiki/plans/Decorations.md §3: a reroute dot dropped into a wire at a
        canvas-local point, centred on it — one undo step. */
    const rerouteWireAt = (wireId: string, canvasX: number, canvasY: number): void => {
      const world = canvasToWorld(canvasX, canvasY)
      spliceInsert(wireId, 'deco.reroute', world.x - 7, world.y - 7)
      requestFrame()
    }

    /** Imports image files at a canvas-local point, cascading several. */
    const importImagesAt = async (files: readonly Blob[], canvasX: number, canvasY: number): Promise<void> => {
      const world = canvasToWorld(canvasX, canvasY)
      let offset = 0
      for (const file of files) {
        if (!file.type.startsWith('image/')) continue
        try {
          const image = await importImage(file)
          await addImageAt(Math.round(world.x + offset), Math.round(world.y + offset), image.mimeType, image.base64, image.width, image.height)
        } catch (error) {
          reportError(`Couldn't add the image: ${error instanceof Error ? error.message : String(error)}`)
        }
        offset += 24
      }
      requestFrame()
    }

    const pickImageAt = (canvasX: number, canvasY: number): void => {
      const input = document.createElement('input')
      input.type = 'file'
      input.accept = 'image/*'
      input.multiple = true
      // Attached while the dialog is open: some WebViews (WebKitGTK) never
      // fire `change` on a file input that isn't in the document.
      input.style.display = 'none'
      document.body.appendChild(input)
      input.onchange = () => {
        void importImagesAt([...(input.files ?? [])], canvasX, canvasY)
        input.remove()
      }
      input.click()
    }

    const onDragOver = (e: DragEvent): void => {
      if (e.dataTransfer?.types.includes('Files')) {
        e.preventDefault()
        e.dataTransfer.dropEffect = 'copy'
      }
    }

    const onDrop = (e: DragEvent): void => {
      const files = [...(e.dataTransfer?.files ?? [])].filter((f) => f.type.startsWith('image/'))
      if (files.length === 0) return
      e.preventDefault()
      const rect = canvas.getBoundingClientRect()
      void importImagesAt(files, e.clientX - rect.left, e.clientY - rect.top)
    }

    const onPaste = (e: ClipboardEvent): void => {
      const target = e.target as HTMLElement | null
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)) return
      const files = [...(e.clipboardData?.items ?? [])].filter((item) => item.kind === 'file' && item.type.startsWith('image/')).map((item) => item.getAsFile()).filter((f): f is File => f !== null)
      if (files.length === 0) return
      e.preventDefault()
      void importImagesAt(files, lastMouseCanvasX, lastMouseCanvasY)
    }

    const onDoubleClick = (e: MouseEvent): void => {
      if (!isCanvasOrWorldTarget(e.target) || closestNodeId(e.target)) return
      const rect = canvas.getBoundingClientRect()
      const wireId = wireNear({ x: e.clientX - rect.left, y: e.clientY - rect.top })
      if (wireId) rerouteWireAt(wireId, e.clientX - rect.left, e.clientY - rect.top)
    }

    // ---- Pointer interaction ----
    const onMouseDown = (e: MouseEvent): void => {
      if (isOwnGestureTarget(e.target)) return

      const canvasRect = canvas.getBoundingClientRect()
      lastMouseCanvasX = e.clientX - canvasRect.left
      lastMouseCanvasY = e.clientY - canvasRect.top
      lastMouseRef.current = { clientX: e.clientX, clientY: e.clientY }
      hasMouseMoved = true

      // Ghost placement captures every left click until placed/cancelled —
      // the actual placement/splice commit happens on the subsequent
      // 'click' event (onClick, below), not here, so a click-drag doesn't
      // also start placing.
      if (getGhost() && e.button === 0) {
        e.preventDefault()
        return
      }
      if (getGhost() && e.button === 2) {
        rightClickStart = { x: e.clientX, y: e.clientY, wasGhostActive: true }
        return // no pan while placing — right-click cancels on mouseup instead
      }

      // Two-button chord (M10_REVIEW.md §1): a gesture (pan/box-select/
      // node-drag/wire-drag) is already in progress from the other button —
      // ignore this second button press entirely rather than letting it
      // hijack/overwrite the first gesture (e.g. right-click-panning on top
      // of an active node-drag, which used to freeze the node mid-move).
      if (getGesture()) return

      if (e.button === 2) {
        // A node/port's own context menu (or overlay UI) owns this click —
        // don't start a pan or offer the Add menu underneath it. Checked
        // the same way the left-click path below does (closestNodeId/
        // closestPortAnchor), not just isCanvasOrWorldTarget — that check
        // alone only distinguishes canvas/world from *overlay* UI, but
        // nodes/ports are themselves descendants of world, so it used to
        // wrongly treat a right-click on a node as a background click and
        // open the Add menu right on top of the node's own context menu.
        if (closestPortAnchor(e.target) || closestNodeId(e.target) || !isCanvasOrWorldTarget(e.target)) {
          rightClickStart = null
          return
        }
        rightClickStart = { x: e.clientX, y: e.clientY, wasGhostActive: false }
        setGesture({ kind: 'pan' })
        panLastX = e.clientX
        panLastY = e.clientY
        return
      }

      const isSpaceLeftDrag = e.button === 0 && getSpaceHeld()
      if (isSpaceLeftDrag) {
        e.preventDefault()
        setGesture({ kind: 'pan' })
        panLastX = e.clientX
        panLastY = e.clientY
        return
      }

      if (e.button !== 0) return

      const port = closestPortAnchor(e.target)
      if (port) {
        e.preventDefault()
        if (port.direction === 'output') {
          // Ctrl+Alt+click on an Audio output listens to it, or stops
          // listening if it already is (wiki/ROADMAP.md stage 0).
          if ((e.ctrlKey || e.metaKey) && e.altKey) {
            const endpoint = getEndpoint(port.nodeId, port.portId, 'output')
            if (endpoint?.port.quantity === 'audio') {
              const worldPos = canvasToWorld(lastMouseCanvasX, lastMouseCanvasY)
              toggleListenAtPort(port.nodeId, port.portId, snapValue(worldPos.x + 40, snapSettingsRef.current), snapValue(worldPos.y + 40, snapSettingsRef.current))
              requestFrame()
            }
            return
          }
          // "Ctrl/Cmd-clicking an output port spawns the viewer matching
          // that port's type, already connected" (design/Visualization/*.png
          // — Ripple, Count, Scope, Scope (Modulation), Gate). A plain click
          // still starts the ordinary wireDrag gesture below; this only
          // intercepts the modifier-held case, and only when the port's
          // live-resolved type (getEndpoint, not its static descriptor —
          // same reasoning NodeCard.tsx's resolvedPortStyle follows) has a
          // viewer at all. graphStore.ts's DEFAULT_VIEWER_BY_PORT_KIND is the
          // one table that decides which.
          if (e.ctrlKey || e.metaKey) {
            const endpoint = getEndpoint(port.nodeId, port.portId, 'output')
            if (endpoint && defaultViewerTypeForPort(endpoint.port)) {
              const worldPos = canvasToWorld(lastMouseCanvasX, lastMouseCanvasY)
              const x = snapValue(worldPos.x + 40, snapSettingsRef.current)
              const y = snapValue(worldPos.y + 40, snapSettingsRef.current)
              createViewerFromPort(port.nodeId, port.portId, x, y)
              requestFrame()
              return
            }
          }
          setGesture({ kind: 'wireDrag', fromNodeId: port.nodeId, fromPortId: port.portId })
        } else {
          const existing = findWireAtInput(port.nodeId, port.portId)
          if (existing) {
            setGesture({ kind: 'wireDrag', fromNodeId: existing.fromNodeId, fromPortId: existing.fromPortId, detachedWireId: existing.id })
          } else {
            // wiki/plans/UtilMacro.md: the M10 "drag out to create a Macro"
            // shortcut is back, now that util.macro is a real registered
            // node type (graphStore.ts's header comment has the retirement/
            // revival history) — only for an unconnected input whose
            // SignalType could sensibly become a macro's value at all
            // (isMacroablePort rules out Audio/Data/Note/Spectral; a
            // Boolean/Event input gets a Bool/Trigger-typed macro).
            const endpoint = getEndpoint(port.nodeId, port.portId, 'input')
            if (endpoint && isMacroablePort(endpoint.port)) {
              setGesture({ kind: 'dragToMacro', nodeId: port.nodeId, portId: port.portId })
            }
          }
        }
        requestFrame()
        return
      }

      const nodeId = closestNodeId(e.target)
      if (nodeId) {
        // Ctrl+drag from a node's body: add it to another node (wiki/ROADMAP.md stage 0).
        if ((e.ctrlKey || e.metaKey) && !e.altKey) {
          e.preventDefault()
          setGesture({ kind: 'linkAdd', fromNodeId: nodeId, startX: lastMouseCanvasX, startY: lastMouseCanvasY })
          requestFrame()
          return
        }
        const current = getGraphSnapshot().selection
        if (e.shiftKey) {
          const next = new Set(current)
          if (next.has(nodeId)) next.delete(nodeId)
          else next.add(nodeId)
          setSelection([...next])
          return
        }
        const alreadySelected = current.has(nodeId)
        const dragIds = alreadySelected ? [...current] : [nodeId]
        if (!alreadySelected) setSelection([nodeId])

        const origins = new Map<string, { x: number; y: number; el: HTMLElement }>()
        const nodesById = new Map(getGraphSnapshot().nodes.map((n) => [n.id, n]))
        container.querySelectorAll<HTMLElement>('[data-node-instance]').forEach((el) => {
          const id = el.getAttribute('data-node-instance')
          if (!id || !dragIds.includes(id)) return
          const n = nodesById.get(id)
          if (n) origins.set(id, { x: n.x, y: n.y, el })
        })
        setGesture({ kind: 'nodeDrag', origins, startClientX: e.clientX, startClientY: e.clientY, primaryId: nodeId, liveDx: 0, liveDy: 0 })
        requestFrame()
        return
      }

      // Opt-in: only the bare canvas/world background may start box-select.
      if (!isCanvasOrWorldTarget(e.target)) return

      // Ctrl/Cmd-click on a wire drops a reroute dot into it (cable management).
      if (e.ctrlKey || e.metaKey) {
        const wireId = wireNear({ x: lastMouseCanvasX, y: lastMouseCanvasY })
        if (wireId) {
          e.preventDefault()
          rerouteWireAt(wireId, lastMouseCanvasX, lastMouseCanvasY)
          return
        }
      }

      setGesture({ kind: 'boxSelect', startX: e.clientX, startY: e.clientY, additive: e.shiftKey, baseSelection: new Set(getGraphSnapshot().selection) })
      if (selectionBoxRef.current) {
        selectionBoxRef.current.style.display = 'block'
        selectionBoxRef.current.style.left = `${e.clientX}px`
        selectionBoxRef.current.style.top = `${e.clientY}px`
        selectionBoxRef.current.style.width = '0px'
        selectionBoxRef.current.style.height = '0px'
      }
    }

    const onMouseMove = (e: MouseEvent): void => {
      const canvasRect = canvas.getBoundingClientRect()
      lastMouseCanvasX = e.clientX - canvasRect.left
      lastMouseCanvasY = e.clientY - canvasRect.top
      lastMouseRef.current = { clientX: e.clientX, clientY: e.clientY }

      const g = getGesture()

      if (g?.kind === 'pan') {
        const dx = e.clientX - panLastX
        const dy = e.clientY - panLastY
        panLastX = e.clientX
        panLastY = e.clientY
        const camera = getCamera()
        setCamera({ zoom: camera.zoom, panX: camera.panX + dx, panY: camera.panY + dy })
        requestFrame()
        return
      }

      if (g?.kind === 'nodeDrag') {
        const zoom = getCamera().zoom
        const dx = (e.clientX - g.startClientX) / zoom
        const dy = (e.clientY - g.startClientY) / zoom
        // Snap only the primary dragged node, then apply that SAME
        // (unsnapped) delta to every other selected node — a multi-drag
        // moves as a rigid group instead of shearing apart when the
        // selection wasn't already grid-aligned relative to itself
        // (M10_REVIEW.md §9).
        const primaryOrigin = g.origins.get(g.primaryId)
        let effectiveDx = dx
        let effectiveDy = dy
        if (primaryOrigin) {
          const snappedX = snapValue(primaryOrigin.x + dx, snapSettingsRef.current)
          const snappedY = snapValue(primaryOrigin.y + dy, snapSettingsRef.current)
          effectiveDx = snappedX - primaryOrigin.x
          effectiveDy = snappedY - primaryOrigin.y
        }
        g.liveDx = effectiveDx
        g.liveDy = effectiveDy
        for (const origin of g.origins.values()) {
          origin.el.style.left = `${origin.x + effectiveDx}px`
          origin.el.style.top = `${origin.y + effectiveDy}px`
        }
        requestFrame()
        return
      }

      if (g?.kind === 'linkAdd') {
        const hover = closestNodeId(e.target)
        g.hoverNodeId = hover && hover !== g.fromNodeId ? hover : undefined
        g.hoverValid = !!g.hoverNodeId && canAddFromNode(g.fromNodeId) && canAddFromNode(g.hoverNodeId)
        requestFrame()
        return
      }

      if (g?.kind === 'boxSelect') {
        const left = Math.min(g.startX, e.clientX)
        const top = Math.min(g.startY, e.clientY)
        const right = Math.max(g.startX, e.clientX)
        const bottom = Math.max(g.startY, e.clientY)
        if (selectionBoxRef.current) {
          selectionBoxRef.current.style.left = `${left}px`
          selectionBoxRef.current.style.top = `${top}px`
          selectionBoxRef.current.style.width = `${right - left}px`
          selectionBoxRef.current.style.height = `${bottom - top}px`
        }

        // Applied live, not just on release (direct feedback: "Selected
        // state (box select) is only applied on box select release. It
        // should already be being applied on selecting.").
        const hits: string[] = []
        container.querySelectorAll<HTMLElement>('[data-node-instance]').forEach((el) => {
          const r = el.getBoundingClientRect()
          if (r.left <= right && r.right >= left && r.top <= bottom && r.bottom >= top) {
            const id = el.getAttribute('data-node-instance')
            if (id) hits.push(id)
          }
        })
        if (g.additive) {
          const next = new Set(g.baseSelection)
          for (const id of hits) next.add(id)
          setSelection([...next])
        } else {
          setSelection(hits)
        }
        return
      }

      // wireDrag/dragToMacro/ghost positions are read fresh from
      // lastMouseCanvasX/Y by the per-frame loop — just make sure a frame
      // is coming.
      if (g?.kind === 'wireDrag' || g?.kind === 'dragToMacro' || getGhost()) requestFrame()
    }

    const onMouseUp = (e: MouseEvent): void => {
      if (e.button === 2) {
        if (getGesture()?.kind === 'pan') setGesture(null)
        const start = rightClickStart
        rightClickStart = null
        if (!start) return
        if (start.wasGhostActive) {
          clearGhost()
          return
        }
        const moved = Math.hypot(e.clientX - start.x, e.clientY - start.y)
        if (moved < RIGHT_CLICK_MOVE_THRESHOLD) {
          setAddMenu({ x: e.clientX, y: e.clientY })
        }
        return
      }

      if (e.button !== 0) return

      const g = getGesture()

      if (g?.kind === 'pan') {
        setGesture(null)
        return
      }

      if (g?.kind === 'linkAdd') {
        if (g.hoverNodeId && g.hoverValid) {
          // The new Add lands midway between the press and the release.
          const a = canvasToWorld(g.startX, g.startY)
          const b = canvasToWorld(lastMouseCanvasX, lastMouseCanvasY)
          addSumOfNodes(g.fromNodeId, g.hoverNodeId, snapValue((a.x + b.x) / 2, snapSettingsRef.current), snapValue((a.y + b.y) / 2, snapSettingsRef.current))
        }
        setGesture(null)
        if (linkPlusRef.current) linkPlusRef.current.style.display = 'none'
        requestFrame()
        return
      }

      if (g?.kind === 'nodeDrag') {
        const updates: { id: string; x: number; y: number }[] = []
        let moved = false
        for (const [id, origin] of g.origins) {
          const nx = origin.x + g.liveDx
          const ny = origin.y + g.liveDy
          if (nx !== origin.x || ny !== origin.y) moved = true
          updates.push({ id, x: nx, y: ny })
        }
        // Click-without-drag on a node that was already part of a
        // multi-selection collapses the selection to just that node
        // (M10_REVIEW.md §9 — explicit decision, common editor convention).
        if (!moved && g.origins.size > 1) setSelection([g.primaryId])
        setGesture(null)
        if (moved) commitNodeMoves(updates)
        return
      }

      if (g?.kind === 'wireDrag') {
        const target = g.hoverNodeId && g.hoverPortId && g.hoverValid ? { nodeId: g.hoverNodeId, portId: g.hoverPortId } : null
        commitWireDrag(g.fromNodeId, g.fromPortId, target, g.detachedWireId)
        setGesture(null)
        return
      }

      if (g?.kind === 'dragToMacro') {
        // Only a true empty canvas/world drop commits — releasing on a
        // node, a port, or any overlay UI (top bar, Add menu) cancels with
        // no effect, same rule wire-drag's own onMouseDown gating already
        // follows for what counts as "empty" (isCanvasOrWorldTarget).
        if (isCanvasOrWorldTarget(e.target)) {
          const worldPos = canvasToWorld(lastMouseCanvasX, lastMouseCanvasY)
          const x = snapValue(worldPos.x, snapSettingsRef.current)
          const y = snapValue(worldPos.y, snapSettingsRef.current)
          createMacroFromPort(g.nodeId, g.portId, x, y)
        }
        setGesture(null)
        return
      }

      if (g?.kind === 'boxSelect') {
        if (selectionBoxRef.current) selectionBoxRef.current.style.display = 'none'
        // A genuine drag already applied its own hits live (onMouseMove,
        // above) — this only covers the case that never generated a single
        // mousemove past the click threshold, which the live-apply logic
        // never runs for at all: a plain click on empty canvas. Clicking
        // outside every node should deselect, the same as most editors
        // (direct feedback) — shift-click on empty space stays a no-op,
        // since a zero-size additive box selects nothing to add.
        const moved = Math.hypot(e.clientX - g.startX, e.clientY - g.startY) > BOX_SELECT_CLICK_THRESHOLD_PX
        if (!moved && !g.additive) setSelection([])
        setGesture(null)
      }
    }

    const onClick = (e: MouseEvent): void => {
      if (e.button !== 0) return
      const currentGhost = getGhost()
      if (!currentGhost) return
      const worldPos = canvasToWorld(lastMouseCanvasX, lastMouseCanvasY)
      const x = snapValue(worldPos.x, snapSettingsRef.current)
      const y = snapValue(worldPos.y, snapSettingsRef.current)
      if (currentGhost.fragment) {
        // Paste / duplicate: the copy's top-left lands on the cursor.
        pasteFragment(currentGhost.fragment, x, y)
        clearGhost()
        return
      }
      if (currentGhost.typeId === 'deco.image') {
        // An image comes from a file: placing one opens the picker.
        clearGhost()
        pickImageAt(lastMouseCanvasX, lastMouseCanvasY)
        return
      }
      if (ghostSpliceHoverWireId && canSplice(ghostSpliceHoverWireId, currentGhost.typeId)) {
        spliceInsert(ghostSpliceHoverWireId, currentGhost.typeId, x, y)
      } else {
        // No wire nearby, or the hovered wire rejected the splice — place
        // unconnected rather than doing nothing (M10_REVIEW.md §6/§23).
        addNode(currentGhost.typeId, x, y)
      }
      clearGhost()
    }

    const onWheel = (e: WheelEvent): void => {
      // A ValueSlider owns wheel-to-adjust on itself — don't also zoom the
      // canvas underneath it (same source-level exclusion as onMouseDown,
      // for the same reason: this listener fires before the slider's own
      // React handler ever gets a chance to stop it).
      if (isOwnGestureTarget(e.target)) return
      // Only zoom when the cursor is over the canvas/world layer itself —
      // lets native scrolling work normally over overlay UI (e.g. the Add
      // menu's own results list) instead of zooming the canvas underneath
      // it. Node DOM elements are siblings, not descendants, of <canvas>,
      // which is why this can't just be `canvas.addEventListener` (that's
      // the actual cause of "can't zoom while hovering a node" —
      // M10_REVIEW.md's direct feedback #1).
      if (!isCanvasOrWorldTarget(e.target)) return
      e.preventDefault()
      const rect = canvas.getBoundingClientRect()
      const cursorX = e.clientX - rect.left
      const cursorY = e.clientY - rect.top

      const camera = getCamera()
      const zoomFactor = Math.exp(-e.deltaY * 0.001)
      const newZoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, camera.zoom * zoomFactor))

      const worldX = (cursorX - camera.panX) / camera.zoom
      const worldY = (cursorY - camera.panY) / camera.zoom
      setCamera({
        zoom: newZoom,
        panX: cursorX - worldX * newZoom,
        panY: cursorY - worldY * newZoom,
      })
      requestFrame()
    }

    const onContextMenu = (e: MouseEvent): void => {
      e.preventDefault()
    }

    resize()
    const resizeObserver = new ResizeObserver(resize)
    resizeObserver.observe(container)

    window.addEventListener('keydown', onKeyDown)
    window.addEventListener('keyup', onKeyUp)
    window.addEventListener('blur', onWindowBlur)
    document.addEventListener('visibilitychange', onVisibilityChange)
    container.addEventListener('mousedown', onMouseDown)
    window.addEventListener('mousemove', onMouseMove)
    window.addEventListener('mouseup', onMouseUp)
    container.addEventListener('click', onClick)
    container.addEventListener('wheel', onWheel, { passive: false })
    container.addEventListener('contextmenu', onContextMenu)
    container.addEventListener('dblclick', onDoubleClick)
    container.addEventListener('dragover', onDragOver)
    container.addEventListener('drop', onDrop)
    window.addEventListener('paste', onPaste)

    requestFrame()

    return () => {
      cancelAnimationFrame(rafHandle)
      resizeObserver.disconnect()
      nodeResizeObserver.disconnect()
      unsubscribeGraph()
      canvas.removeEventListener('webglcontextlost', onContextLost)
      canvas.removeEventListener('webglcontextrestored', onContextRestored)
      window.removeEventListener('keydown', onKeyDown)
      window.removeEventListener('keyup', onKeyUp)
      window.removeEventListener('blur', onWindowBlur)
      document.removeEventListener('visibilitychange', onVisibilityChange)
      container.removeEventListener('mousedown', onMouseDown)
      window.removeEventListener('mousemove', onMouseMove)
      window.removeEventListener('mouseup', onMouseUp)
      container.removeEventListener('click', onClick)
      container.removeEventListener('wheel', onWheel)
      container.removeEventListener('contextmenu', onContextMenu)
      container.removeEventListener('dblclick', onDoubleClick)
      container.removeEventListener('dragover', onDragOver)
      container.removeEventListener('drop', onDrop)
      window.removeEventListener('paste', onPaste)
      requestFrameRef.current = () => {}
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // Ghost just armed: seed its DOM position from wherever the cursor
  // currently is; subsequent frames then track the live cursor via the
  // main render loop.
  useEffect(() => {
    if (!ghost || !ghostElRef.current || !canvasRef.current) return
    const rect = canvasRef.current.getBoundingClientRect()
    const camera = getCamera()
    const canvasX = lastMouseRef.current.clientX - rect.left
    const canvasY = lastMouseRef.current.clientY - rect.top
    ghostElRef.current.style.left = `${(canvasX - camera.panX) / camera.zoom}px`
    ghostElRef.current.style.top = `${(canvasY - camera.panY) / camera.zoom}px`
    requestFrameRef.current()
  }, [ghost])

  return (
    <div className="infinite-canvas" ref={containerRef}>
      <canvas ref={canvasRef} />
      <div className="infinite-canvas-world" ref={worldRef}>
        <GraphSurface
          nodes={snapshot.nodes}
          wires={snapshot.wires}
          selection={snapshot.selection}
          getDescriptor={(typeId) => snapshot.descriptors.find((d) => d.typeId === typeId)}
          multiplicity={snapshot.multiplicity}
          ghost={ghost}
          ghostElementRef={ghostElRef}
          overlayTarget={overlayEl}
        />
      </div>
      <div className="infinite-canvas-overlay" ref={setOverlayEl}>
        {children}
        <div className="infinite-canvas-selection-box" ref={selectionBoxRef} />
        <div className="infinite-canvas-hint-label" ref={hintLabelRef} />
        <div className="infinite-canvas-link-plus" ref={linkPlusRef}>
          +
        </div>
        {addMenu && (
          <AddMenu
            x={addMenu.x}
            y={addMenu.y}
            descriptors={snapshot.descriptors}
            onChoose={(typeId) => {
              setAddMenu(null)
              armGhost(typeId)
              requestFrameRef.current()
            }}
            onClose={() => setAddMenu(null)}
          />
        )}
      </div>
    </div>
  )
})
