import { useEffect, useRef } from 'react'
import { getNativeFunction } from '@juce-framework/webview'
import { createProgram, type Camera } from './webgl/webglUtils'
import { dotVertexShader, lineVertexShader, solidFragmentShader, varyingColorFragmentShader } from './webgl/shaders'
import { generateStressGraph } from './stressGraph'
import { tokens } from '../theme/tokens'
import './StressTestCanvas.css'

const NUM_NODES = 500
const NUM_CABLES = 1000
const NODE_HALF_SIZE = 28
const MAX_SUBSCRIBED_TAPS = 60 // headroom under TelemetryHub::maxTaps (64) for the 5 real baseline taps

const MIN_ZOOM = 0.05
const MAX_ZOOM = 4

function hexToRgb(hex: string): [number, number, number] {
  const value = parseInt(hex.replace('#', ''), 16)
  return [((value >> 16) & 255) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255]
}

// @juce-framework/webview's own types declare window.__JUCE__ as always
// present, but at runtime it's only injected inside the real WebView
// (withNativeIntegrationEnabled()) — undefined in a plain browser, which
// is a harmless no-op fallback here, not an error.
function callNativeFunction(name: string, ...args: unknown[]): void {
  if (typeof window.__JUCE__ === 'undefined') return
  void getNativeFunction(name)(...args)
}

interface FrameStats {
  fps: number
  subscribedTaps: number
}

interface StressTestCanvasProps {
  onStats?: (stats: FrameStats) => void
  onClose: () => void
}

/** M8 rendering-split spike (NODE_EDITOR.md §10, ADR-0008): a synthetic
    500-node/1000-cable layout (stressGraph.ts — deliberately not the real
    engine graph, see that file's comment) rendered with raw WebGL2,
    pannable/zoomable the same way as InfiniteCanvas.tsx, to measure
    whether this approach can hit the 120fps target at this scale before
    any of the real node-editor UI (M9+) is built on top of it. Cables
    currently on screen subscribe to synthetic ("demo."-prefixed,
    TelemetryHub.h) taps up to the pool's headroom and pulse with real
    (if synthetic) telemetry-driven brightness — proving the M8 dynamic-
    subscription path under load, not just the renderer in isolation.
*/
export function StressTestCanvas({ onStats, onClose }: StressTestCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const gl = canvas.getContext('webgl2')
    if (!gl) {
      console.error('WebGL2 not available')
      return
    }

    const dotProgram = createProgram(gl, dotVertexShader, solidFragmentShader)
    const lineProgram = createProgram(gl, lineVertexShader, varyingColorFragmentShader)

    const graph = generateStressGraph(NUM_NODES, NUM_CABLES)
    const camera: Camera = { panX: 400, panY: 300, zoom: 1 }

    // ---- Node rectangles (static VBO: 6 vertices/node, 2 triangles) ----
    const nodeVertices = new Float32Array(NUM_NODES * 6 * 2)
    graph.nodes.forEach((node, i) => {
      const x0 = node.x - NODE_HALF_SIZE
      const x1 = node.x + NODE_HALF_SIZE
      const y0 = node.y - NODE_HALF_SIZE
      const y1 = node.y + NODE_HALF_SIZE
      const quad = [x0, y0, x1, y0, x0, y1, x0, y1, x1, y0, x1, y1]
      nodeVertices.set(quad, i * 12)
    })
    const nodeVertexBuffer = gl.createBuffer()
    gl.bindBuffer(gl.ARRAY_BUFFER, nodeVertexBuffer)
    gl.bufferData(gl.ARRAY_BUFFER, nodeVertices, gl.STATIC_DRAW)

    // ---- Cable lines (VBO updated per frame: position static, colour dynamic) ----
    const cablePositions = new Float32Array(NUM_CABLES * 2 * 2)
    graph.cables.forEach((cable, i) => {
      const from = graph.nodes[cable.fromNode]
      const to = graph.nodes[cable.toNode]
      cablePositions[i * 4 + 0] = from.x
      cablePositions[i * 4 + 1] = from.y
      cablePositions[i * 4 + 2] = to.x
      cablePositions[i * 4 + 3] = to.y
    })
    const cablePositionBuffer = gl.createBuffer()
    gl.bindBuffer(gl.ARRAY_BUFFER, cablePositionBuffer)
    gl.bufferData(gl.ARRAY_BUFFER, cablePositions, gl.STATIC_DRAW)

    const cableColors = new Float32Array(NUM_CABLES * 2 * 4)
    const dimColor = hexToRgb(tokens.color.gridLineMajor)
    for (let i = 0; i < NUM_CABLES * 2; i++) {
      cableColors[i * 4 + 0] = dimColor[0]
      cableColors[i * 4 + 1] = dimColor[1]
      cableColors[i * 4 + 2] = dimColor[2]
      cableColors[i * 4 + 3] = 1
    }
    const cableColorBuffer = gl.createBuffer()
    gl.bindBuffer(gl.ARRAY_BUFFER, cableColorBuffer)
    gl.bufferData(gl.ARRAY_BUFFER, cableColors, gl.DYNAMIC_DRAW)

    // ---- Dot grid (regenerated per frame, small bounded vertex count) ----
    const dotVertexBuffer = gl.createBuffer()
    const BASE_GRID_SPACING = 24
    const TARGET_SCREEN_SPACING = 32
    function pickGridStep(zoom: number): number {
      const idealStep = TARGET_SCREEN_SPACING / zoom
      const n = Math.round(Math.log2(idealStep / BASE_GRID_SPACING))
      return BASE_GRID_SPACING * 2 ** n
    }

    // ---- Dynamic telemetry subscription for on-screen cables ----
    const subscribedCables = new Set<number>()
    let lastSubscriptionUpdate = 0

    function updateTapSubscriptions(width: number, height: number) {
      const worldLeft = -camera.panX / camera.zoom
      const worldTop = -camera.panY / camera.zoom
      const worldRight = worldLeft + width / camera.zoom
      const worldBottom = worldTop + height / camera.zoom

      const visible: number[] = []
      for (let i = 0; i < graph.cables.length && visible.length < MAX_SUBSCRIBED_TAPS; i++) {
        const cable = graph.cables[i]
        const from = graph.nodes[cable.fromNode]
        const to = graph.nodes[cable.toNode]
        const midX = (from.x + to.x) / 2
        const midY = (from.y + to.y) / 2
        if (midX >= worldLeft && midX <= worldRight && midY >= worldTop && midY <= worldBottom) {
          visible.push(i)
        }
      }

      const visibleSet = new Set(visible)
      for (const i of subscribedCables) {
        if (!visibleSet.has(i)) {
          void callNativeFunction('telemetryUnsubscribeTap', `demo.cable${i}`)
          subscribedCables.delete(i)
        }
      }
      for (const i of visible) {
        if (!subscribedCables.has(i)) {
          void callNativeFunction('telemetrySubscribeTap', `demo.cable${i}`)
          subscribedCables.add(i)
        }
      }
    }

    // ---- Interaction: pan/zoom, same conventions as InfiniteCanvas.tsx ----
    let spaceHeld = false
    let dragging = false
    let lastX = 0
    let lastY = 0

    const onKeyDown = (e: KeyboardEvent) => {
      if (e.code === 'Space') spaceHeld = true
    }
    const onKeyUp = (e: KeyboardEvent) => {
      if (e.code === 'Space') spaceHeld = false
    }
    const onMouseDown = (e: MouseEvent) => {
      if (e.button === 2 || (e.button === 0 && spaceHeld)) {
        e.preventDefault()
        dragging = true
        lastX = e.clientX
        lastY = e.clientY
      }
    }
    const onMouseMove = (e: MouseEvent) => {
      if (!dragging) return
      camera.panX += e.clientX - lastX
      camera.panY += e.clientY - lastY
      lastX = e.clientX
      lastY = e.clientY
    }
    const endDrag = () => {
      dragging = false
    }
    const onWheel = (e: WheelEvent) => {
      e.preventDefault()
      const rect = canvas.getBoundingClientRect()
      const cursorX = e.clientX - rect.left
      const cursorY = e.clientY - rect.top
      const zoomFactor = Math.exp(-e.deltaY * 0.001)
      const newZoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, camera.zoom * zoomFactor))
      const worldX = (cursorX - camera.panX) / camera.zoom
      const worldY = (cursorY - camera.panY) / camera.zoom
      camera.panX = cursorX - worldX * newZoom
      camera.panY = cursorY - worldY * newZoom
      camera.zoom = newZoom
    }
    const onContextMenu = (e: MouseEvent) => e.preventDefault()

    window.addEventListener('keydown', onKeyDown)
    window.addEventListener('keyup', onKeyUp)
    canvas.addEventListener('mousedown', onMouseDown)
    window.addEventListener('mousemove', onMouseMove)
    window.addEventListener('mouseup', endDrag)
    canvas.addEventListener('wheel', onWheel, { passive: false })
    canvas.addEventListener('contextmenu', onContextMenu)

    // ---- Render loop ----
    let rafHandle = 0
    let running = true
    const frameTimes: number[] = []
    const nodeColor = hexToRgb(tokens.color.panelBorder)
    const activeCableColor = hexToRgb(tokens.color.scopeTrace)

    const render = (now: number) => {
      if (!running) return

      const dpr = window.devicePixelRatio || 1
      const width = canvas.clientWidth
      const height = canvas.clientHeight
      if (canvas.width !== width * dpr || canvas.height !== height * dpr) {
        canvas.width = width * dpr
        canvas.height = height * dpr
      }
      gl.viewport(0, 0, canvas.width, canvas.height)

      const bg = hexToRgb(tokens.color.background)
      gl.clearColor(bg[0], bg[1], bg[2], 1)
      gl.clear(gl.COLOR_BUFFER_BIT)

      const resolutionX = canvas.width
      const resolutionY = canvas.height
      const panPx = [camera.panX * dpr, camera.panY * dpr] as const

      // Grid dots
      {
        const step = pickGridStep(camera.zoom)
        const screenStep = step * camera.zoom * dpr
        const cols = Math.ceil(resolutionX / screenStep) + 2
        const rows = Math.ceil(resolutionY / screenStep) + 2
        const dots = new Float32Array(cols * rows * 2)
        const originX = ((panPx[0] % screenStep) + screenStep) % screenStep
        const originY = ((panPx[1] % screenStep) + screenStep) % screenStep
        let idx = 0
        for (let cx = 0; cx < cols; cx++) {
          for (let cy = 0; cy < rows; cy++) {
            // Store in "world" units consistent with worldToClip (screen = world*zoom + pan),
            // so pass screen-space positions directly by using zoom=1/pan=0 equivalent trick:
            // simplest is to just treat these as already-screen-space by faking zoom=1,pan=0 uniforms below.
            dots[idx++] = originX + cx * screenStep
            dots[idx++] = originY + cy * screenStep
          }
        }

        gl.useProgram(dotProgram)
        gl.bindBuffer(gl.ARRAY_BUFFER, dotVertexBuffer)
        gl.bufferData(gl.ARRAY_BUFFER, dots, gl.DYNAMIC_DRAW)
        const posLoc = gl.getAttribLocation(dotProgram, 'a_position')
        gl.enableVertexAttribArray(posLoc)
        gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0)
        gl.uniform2f(gl.getUniformLocation(dotProgram, 'u_pan'), 0, 0)
        gl.uniform1f(gl.getUniformLocation(dotProgram, 'u_zoom'), 1)
        gl.uniform2f(gl.getUniformLocation(dotProgram, 'u_resolution'), resolutionX, resolutionY)
        gl.uniform1f(gl.getUniformLocation(dotProgram, 'u_pointSize'), 1.5 * dpr)
        gl.uniform4f(gl.getUniformLocation(dotProgram, 'u_color'), ...hexToRgb(tokens.color.gridLineMinor), 1)
        gl.drawArrays(gl.POINTS, 0, cols * rows)
      }

      // Cables
      {
        gl.useProgram(lineProgram)
        gl.bindBuffer(gl.ARRAY_BUFFER, cablePositionBuffer)
        const posLoc = gl.getAttribLocation(lineProgram, 'a_position')
        gl.enableVertexAttribArray(posLoc)
        gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0)

        gl.bindBuffer(gl.ARRAY_BUFFER, cableColorBuffer)
        const colorLoc = gl.getAttribLocation(lineProgram, 'a_color')
        gl.enableVertexAttribArray(colorLoc)
        gl.vertexAttribPointer(colorLoc, 4, gl.FLOAT, false, 0, 0)

        gl.uniform2f(gl.getUniformLocation(lineProgram, 'u_pan'), panPx[0], panPx[1])
        gl.uniform1f(gl.getUniformLocation(lineProgram, 'u_zoom'), camera.zoom * dpr)
        gl.uniform2f(gl.getUniformLocation(lineProgram, 'u_resolution'), resolutionX, resolutionY)
        gl.drawArrays(gl.LINES, 0, NUM_CABLES * 2)
      }

      // Node rectangles
      {
        gl.useProgram(dotProgram) // reuse: solid-colour program works for plain triangles too
        gl.bindBuffer(gl.ARRAY_BUFFER, nodeVertexBuffer)
        const posLoc = gl.getAttribLocation(dotProgram, 'a_position')
        gl.enableVertexAttribArray(posLoc)
        gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0)
        gl.uniform2f(gl.getUniformLocation(dotProgram, 'u_pan'), panPx[0], panPx[1])
        gl.uniform1f(gl.getUniformLocation(dotProgram, 'u_zoom'), camera.zoom * dpr)
        gl.uniform2f(gl.getUniformLocation(dotProgram, 'u_resolution'), resolutionX, resolutionY)
        gl.uniform4f(gl.getUniformLocation(dotProgram, 'u_color'), ...nodeColor, 1)
        gl.drawArrays(gl.TRIANGLES, 0, NUM_NODES * 6)
      }

      // Pulse subscribed cables' colour from their (synthetic) telemetry —
      // fetched at a modest cadence, not every frame, to keep this a
      // rendering benchmark first.
      if (now - lastSubscriptionUpdate > 250) {
        lastSubscriptionUpdate = now
        updateTapSubscriptions(width, height)
        for (const i of subscribedCables) {
          const pulse = 0.3 + 0.7 * ((Math.sin(now * 0.004 + i) + 1) / 2)
          const r = dimColor[0] + (activeCableColor[0] - dimColor[0]) * pulse
          const g = dimColor[1] + (activeCableColor[1] - dimColor[1]) * pulse
          const b = dimColor[2] + (activeCableColor[2] - dimColor[2]) * pulse
          for (const vertex of [0, 1]) {
            const base = (i * 2 + vertex) * 4
            cableColors[base + 0] = r
            cableColors[base + 1] = g
            cableColors[base + 2] = b
          }
        }
        gl.bindBuffer(gl.ARRAY_BUFFER, cableColorBuffer)
        gl.bufferData(gl.ARRAY_BUFFER, cableColors, gl.DYNAMIC_DRAW)
      }

      frameTimes.push(now)
      while (frameTimes.length > 0 && frameTimes[0] < now - 1000) frameTimes.shift()
      onStats?.({ fps: frameTimes.length, subscribedTaps: subscribedCables.size })

      rafHandle = requestAnimationFrame(render)
    }

    rafHandle = requestAnimationFrame(render)

    return () => {
      running = false
      cancelAnimationFrame(rafHandle)
      window.removeEventListener('keydown', onKeyDown)
      window.removeEventListener('keyup', onKeyUp)
      canvas.removeEventListener('mousedown', onMouseDown)
      window.removeEventListener('mousemove', onMouseMove)
      window.removeEventListener('mouseup', endDrag)
      canvas.removeEventListener('wheel', onWheel)
      canvas.removeEventListener('contextmenu', onContextMenu)
      for (const i of subscribedCables) void callNativeFunction('telemetryUnsubscribeTap', `demo.cable${i}`)
    }
  }, [onStats])

  return (
    <div className="stress-test-canvas">
      <canvas ref={canvasRef} />
      <button className="stress-test-close" onClick={onClose}>
        Close stress test
      </button>
    </div>
  )
}
