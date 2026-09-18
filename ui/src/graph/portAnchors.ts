// Port-anchor geometry for the M10 node editor's WebGL cable layer.
//
// Each node's port offsets (relative to the node's own world-space origin,
// i.e. what `node.x`/`node.y` already is) are measured from the DOM ONCE —
// on mount and again only if that node's own ResizeObserver reports its
// size actually changed (title renamed, descriptor swapped) — rather than
// every animation frame (M10_REVIEW.md §23's retrospective, rendering item
// 2: a full querySelectorAll+getBoundingClientRect sweep every frame is
// pure waste once panning/idle, since a port's position *relative to its
// own node* never changes, only the node's `(x, y)` and the camera do).
// InfiniteCanvas.tsx owns the cache (one Map<nodeId, Map<portKey, offset>>)
// and calls measureNodeLocalPortOffsets() only when a node is first seen or
// its ResizeObserver fires; every other frame recomputes each port's screen
// position from the cached offset with plain arithmetic via
// offsetToScreenAnchor(). This keeps the same "DOM/CSS defines the real
// layout" principle NodeCard.tsx's own instanceId comment describes — the
// cache is *seeded* from a real measurement, never hand-computed/guessed —
// it just stops re-measuring what hasn't changed.
import type { Camera } from '../canvas/webgl/webglUtils'

export interface PortAnchor {
  x: number
  y: number
}

export function portKey(nodeId: string, portId: string, direction: string): string {
  return `${nodeId}:${portId}:${direction}`
}

function localPortKey(portId: string, direction: string): string {
  return `${portId}:${direction}`
}

/** A few CSS px, measured at cache time and thus scaling with the glyph
    itself at any later zoom — how far *inside* an input glyph's own outer
    edge the cable anchor lands, rather than landing exactly on it. The
    glyph is a text character ("→"), whose rendered ink starts somewhere
    inside its own bounding box (font-dependent left-side bearing, not
    controllable via CSS padding alone) — this overlap has to reach past
    that bearing, not just cover getBoundingClientRect()'s own sub-pixel
    rounding, so it's larger than a pure rounding margin would need. Capped
    at half the glyph's own width (measureNodeLocalPortOffsets, below) so
    it can never overshoot the smallest glyph (the 8px unconnected dot).
    The glyph's background is opaque, so any overlap here is never visible.
*/
const INPUT_ANCHOR_OVERLAP_PX = 4

/** Scans one node element's own port/singleton glyphs and returns each
    one's offset from the node's world-space origin (top-left corner, i.e.
    `(node.x, node.y)`), in world units — independent of the current camera,
    so the result stays valid across zoom changes until the node's own DOM
    layout actually changes.

    Anchor point is direction-asymmetric, per direct feedback: an input
    glyph anchors just inside its OUTER edge (`rect.left + overlap`) — the
    glyph's own opaque background already occludes anything a cable draws
    past that point (it paints on top of the WebGL canvas), so this is the
    closest an anchor can get a cable to actually touching the glyph,
    rather than stopping noticeably short of it. An output glyph anchors at
    its CENTRE (the node's true border) — this is the original,
    already-correct behaviour; a previous attempt at pushing this outward
    by a fixed gap only widened it, since the "gap" people were already
    seeing there was itself just this same occlusion effect, not empty
    space this file was leaving on purpose. Don't reintroduce an explicit
    output-side gap.
*/
export function measureNodeLocalPortOffsets(nodeEl: HTMLElement, zoom: number): Map<string, PortAnchor> {
  const offsets = new Map<string, PortAnchor>()
  const nodeRect = nodeEl.getBoundingClientRect()
  const elements = nodeEl.querySelectorAll<HTMLElement>('[data-port-anchor]')
  elements.forEach((el) => {
    const { portId, direction } = el.dataset
    if (!portId || (direction !== 'input' && direction !== 'output')) return
    const rect = el.getBoundingClientRect()
    // Never overlap past the glyph's own midpoint, however small it is
    // (the 8px unconnected dot) — half its width is a hard ceiling.
    const overlap = Math.min(INPUT_ANCHOR_OVERLAP_PX, rect.width / 2)
    const edgeX = direction === 'input' ? rect.left + overlap : rect.left + rect.width / 2
    offsets.set(localPortKey(portId, direction), {
      x: (edgeX - nodeRect.left) / zoom,
      y: (rect.top + rect.height / 2 - nodeRect.top) / zoom,
    })
  })
  return offsets
}

/** Resolves one cached local offset (from measureNodeLocalPortOffsets) plus
    a node's current world position through the current camera into a
    canvas-local screen-space point — the per-frame arithmetic that replaces
    a DOM read.
*/
export function offsetToScreenAnchor(nodeX: number, nodeY: number, offset: PortAnchor, camera: Camera): PortAnchor {
  return {
    x: camera.panX + (nodeX + offset.x) * camera.zoom,
    y: camera.panY + (nodeY + offset.y) * camera.zoom,
  }
}

/** Builds the full per-frame anchor map InfiniteCanvas.tsx's renderer and
    hit-testing both read, from the node-offset cache it owns.
*/
export function buildAnchorMap(
  nodes: ReadonlyArray<{ id: string; x: number; y: number }>,
  offsetCache: ReadonlyMap<string, Map<string, PortAnchor>>,
  camera: Camera,
): Map<string, PortAnchor> {
  const anchors = new Map<string, PortAnchor>()
  for (const node of nodes) {
    const localOffsets = offsetCache.get(node.id)
    if (!localOffsets) continue
    for (const [key, offset] of localOffsets) {
      const [portId, direction] = key.split(':')
      anchors.set(portKey(node.id, portId, direction), offsetToScreenAnchor(node.x, node.y, offset, camera))
    }
  }
  return anchors
}
