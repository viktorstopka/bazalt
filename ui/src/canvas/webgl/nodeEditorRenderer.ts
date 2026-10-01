// WebGL2 cable rendering for the M10 node editor
// (docs/decisions/0008-graph-rendering-split.md's Amendment (M10)). Reuses
// the M8 stress-test spike's shader helpers rather than duplicating them.
// Node bodies are NOT drawn here — they're DOM (NodeCard), positioned by a
// CSS transform InfiniteCanvas.tsx applies to the world layer; this module
// only ever receives already-measured screen-space (CSS px) coordinates, so
// it never needs to know about pan/zoom at all.
//
// No background grid is drawn (removed per direct feedback — "get rid of
// the grid dots in the background"; this was independent of the M8
// stress-test spike's own grid — StressTestCanvas.tsx/stressGraph.ts,
// deleted wholesale in M10 polish once the real editor made the spike's
// questions askable against real content instead). Convention:
// every position this module is given is in CSS pixels, canvas-local.
// Internally cables are drawn with u_pan=(0,0), u_zoom=dpr,
// u_resolution=(deviceWidth, deviceHeight) — i.e. a pure CSS-px-to-device-px
// upscale, never a second independent transform to keep in sync with the
// DOM's own CSS transform (see this milestone's ADR amendment for why
// that's the deliberate answer to the hybrid-sync question).
import { createProgram, hexToRgb } from './webglUtils'
import { cableLineVertexShader, cableLineFragmentShader } from './shaders'
import { tokens } from '../../theme/tokens'

const CABLE_SEGMENTS = 24
const DASH_PERIOD_PX = 10
/** CSS px *at camera.zoom = 1* — a bit thicker than the node border's own
    1px `--stroke-thin` so the cable reads as at least as heavy, per direct
    feedback. Scaled by camera.zoom at draw time (drawCables' `zoom`
    parameter) so it grows/shrinks exactly like the node border does — the
    border is DOM content inside `.infinite-canvas-world`, which gets
    `transform: scale(camera.zoom)` applied to the whole layer, so a
    constant *screen-space* cable width would drift out of proportion with
    it instead of matching (an earlier attempt at a zoom-invariant width
    got this backwards). Rendered as real triangle geometry, not
    `gl.lineWidth` — that call is capped at 1.0 on effectively every
    Windows/ANGLE WebGL implementation (Chromium's default backend here),
    so it was silently a no-op before switching to this.
*/
const CABLE_WIDTH_PX_AT_ZOOM_1 = 2
const MIN_CABLE_WIDTH_PX = 1 // device-space floor so an extreme zoom-out doesn't shrink the cable to nothing, matching how a CSS border effectively hairline-snaps rather than disappearing

export interface Point {
  x: number
  y: number
}

export interface CableSpec {
  from: Point
  to: Point
  color: readonly [number, number, number]
  alpha: number
  dashed: boolean
}

export interface RenderParams {
  cssWidth: number
  cssHeight: number
  dpr: number
  /** camera.zoom — used only to scale cable width to match the (DOM,
      zoom-scaled) node border; cable positions themselves are already
      resolved to screen space before reaching this module (see the header
      comment), so this is the one exception to "never needs to know about
      pan/zoom at all".
  */
  zoom: number
  cables: readonly CableSpec[]
}

/** Cubic bezier control points for a horizontal cable, matching typical
    node-editor conventions: the curve leaves/enters each port horizontally
    regardless of the vertical offset between the two ends.
*/
export function tessellateCable(cable: CableSpec): Point[] {
  const bend = Math.max(40, Math.abs(cable.to.x - cable.from.x) * 0.5)
  const c1: Point = { x: cable.from.x + bend, y: cable.from.y }
  const c2: Point = { x: cable.to.x - bend, y: cable.to.y }

  const points: Point[] = []
  for (let i = 0; i <= CABLE_SEGMENTS; i++) {
    const t = i / CABLE_SEGMENTS
    const mt = 1 - t
    const x = mt * mt * mt * cable.from.x + 3 * mt * mt * t * c1.x + 3 * mt * t * t * c2.x + t * t * t * cable.to.x
    const y = mt * mt * mt * cable.from.y + 3 * mt * mt * t * c1.y + 3 * mt * t * t * c2.y + t * t * t * cable.to.y
    points.push({ x, y })
  }
  return points
}

export interface NodeEditorRenderer {
  render(params: RenderParams): void
}

export function createNodeEditorRenderer(gl: WebGL2RenderingContext): NodeEditorRenderer {
  const cableProgram = createProgram(gl, cableLineVertexShader, cableLineFragmentShader)

  const cableLoc = {
    position: gl.getAttribLocation(cableProgram, 'a_position'),
    color: gl.getAttribLocation(cableProgram, 'a_color'),
    dist: gl.getAttribLocation(cableProgram, 'a_dist'),
    dash: gl.getAttribLocation(cableProgram, 'a_dash'),
    pan: gl.getUniformLocation(cableProgram, 'u_pan'),
    zoom: gl.getUniformLocation(cableProgram, 'u_zoom'),
    resolution: gl.getUniformLocation(cableProgram, 'u_resolution'),
    dashPeriod: gl.getUniformLocation(cableProgram, 'u_dashPeriod'),
  }

  const cablePositionBuffer = gl.createBuffer()
  const cableColorBuffer = gl.createBuffer()
  const cableDistBuffer = gl.createBuffer()
  const cableDashBuffer = gl.createBuffer()

  function drawCables(cables: readonly CableSpec[], deviceWidth: number, deviceHeight: number, dpr: number, zoom: number): void {
    if (cables.length === 0) return

    const positions: number[] = []
    const colors: number[] = []
    const dists: number[] = []
    const dashes: number[] = []
    const halfWidth = Math.max(MIN_CABLE_WIDTH_PX, CABLE_WIDTH_PX_AT_ZOOM_1 * zoom) / 2

    for (const cable of cables) {
      const points = tessellateCable(cable)
      let dist = 0
      const dashFlag = cable.dashed ? 1 : 0
      const [r, g, b, a] = [cable.color[0], cable.color[1], cable.color[2], cable.alpha]

      for (let i = 0; i < points.length - 1; i++) {
        const p0 = points[i]
        const p1 = points[i + 1]
        const dx = p1.x - p0.x
        const dy = p1.y - p0.y
        const segLen = Math.hypot(dx, dy) || 1
        // Perpendicular unit vector, scaled to half the desired width —
        // real quad geometry (two triangles) instead of a native GL line,
        // since gl.lineWidth() can't be trusted to draw anything wider
        // than 1px (see CABLE_WIDTH_PX's own comment).
        const nx = (-dy / segLen) * halfWidth
        const ny = (dx / segLen) * halfWidth

        const aL = { x: p0.x + nx, y: p0.y + ny }
        const aR = { x: p0.x - nx, y: p0.y - ny }
        const bL = { x: p1.x + nx, y: p1.y + ny }
        const bR = { x: p1.x - nx, y: p1.y - ny }

        // Two triangles: (aL, aR, bL) and (aR, bR, bL).
        positions.push(aL.x, aL.y, aR.x, aR.y, bL.x, bL.y, aR.x, aR.y, bR.x, bR.y, bL.x, bL.y)
        for (let v = 0; v < 6; v++) colors.push(r, g, b, a)
        const distEnd = dist + segLen
        dists.push(dist, dist, distEnd, dist, distEnd, distEnd)
        for (let v = 0; v < 6; v++) dashes.push(dashFlag)
        dist = distEnd
      }
    }

    gl.useProgram(cableProgram)

    gl.bindBuffer(gl.ARRAY_BUFFER, cablePositionBuffer)
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(positions), gl.DYNAMIC_DRAW)
    gl.enableVertexAttribArray(cableLoc.position)
    gl.vertexAttribPointer(cableLoc.position, 2, gl.FLOAT, false, 0, 0)

    gl.bindBuffer(gl.ARRAY_BUFFER, cableColorBuffer)
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(colors), gl.DYNAMIC_DRAW)
    gl.enableVertexAttribArray(cableLoc.color)
    gl.vertexAttribPointer(cableLoc.color, 4, gl.FLOAT, false, 0, 0)

    gl.bindBuffer(gl.ARRAY_BUFFER, cableDistBuffer)
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(dists), gl.DYNAMIC_DRAW)
    gl.enableVertexAttribArray(cableLoc.dist)
    gl.vertexAttribPointer(cableLoc.dist, 1, gl.FLOAT, false, 0, 0)

    gl.bindBuffer(gl.ARRAY_BUFFER, cableDashBuffer)
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(dashes), gl.DYNAMIC_DRAW)
    gl.enableVertexAttribArray(cableLoc.dash)
    gl.vertexAttribPointer(cableLoc.dash, 1, gl.FLOAT, false, 0, 0)

    gl.uniform2f(cableLoc.pan, 0, 0)
    gl.uniform1f(cableLoc.zoom, dpr)
    gl.uniform2f(cableLoc.resolution, deviceWidth, deviceHeight)
    gl.uniform1f(cableLoc.dashPeriod, DASH_PERIOD_PX)

    gl.drawArrays(gl.TRIANGLES, 0, positions.length / 2)
  }

  return {
    render({ cssWidth, cssHeight, dpr, zoom, cables }: RenderParams): void {
      const deviceWidth = Math.max(1, Math.round(cssWidth * dpr))
      const deviceHeight = Math.max(1, Math.round(cssHeight * dpr))
      gl.viewport(0, 0, deviceWidth, deviceHeight)

      const bg = hexToRgb(tokens.color.background)
      gl.clearColor(bg[0], bg[1], bg[2], 1)
      gl.clear(gl.COLOR_BUFFER_BIT)

      drawCables(cables, deviceWidth, deviceHeight, dpr, zoom)
    },
  }
}
