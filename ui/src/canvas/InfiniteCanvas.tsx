import { useEffect, useRef, useState, type ReactNode } from 'react'
import { tokens } from '../theme/tokens'
import './InfiniteCanvas.css'

interface Camera {
  panX: number
  panY: number
  zoom: number
}

const MIN_ZOOM = 0.1
const MAX_ZOOM = 8
const BASE_GRID_SPACING = 24 // world units between dots at zoom == 1
const TARGET_SCREEN_SPACING = 32 // px — grid step doubles/halves to stay near this on screen

/** Picks a screen-space-stable grid step: BASE_GRID_SPACING * 2^n, chosen so
    the on-screen spacing (step * zoom) stays close to TARGET_SCREEN_SPACING
    at any zoom level, rather than dots merging into noise when zoomed out
    or vanishing when zoomed in.
*/
function pickGridStep(zoom: number): number {
  const idealStep = TARGET_SCREEN_SPACING / zoom
  const n = Math.round(Math.log2(idealStep / BASE_GRID_SPACING))
  return BASE_GRID_SPACING * 2 ** n
}

function drawDotGrid(ctx: CanvasRenderingContext2D, width: number, height: number, camera: Camera) {
  const step = pickGridStep(camera.zoom)
  const screenStep = step * camera.zoom

  const originX = camera.panX % screenStep
  const originY = camera.panY % screenStep

  ctx.fillStyle = tokens.color.gridLineMinor
  for (let x = originX; x < width; x += screenStep) {
    for (let y = originY; y < height; y += screenStep) {
      ctx.fillRect(x - 0.5, y - 0.5, 1, 1)
    }
  }

  // Every 4th line drawn brighter as a "major" landmark, screen-space stable
  // the same way the dot step itself is.
  const majorStep = screenStep * 4
  const majorOriginX = camera.panX % majorStep
  const majorOriginY = camera.panY % majorStep
  ctx.fillStyle = tokens.color.gridLineMajor
  for (let x = majorOriginX; x < width; x += majorStep) {
    for (let y = majorOriginY; y < height; y += majorStep) {
      ctx.fillRect(x - 0.75, y - 0.75, 1.5, 1.5)
    }
  }
}

export interface SnapSettings {
  enabled: boolean
  sizeWorldUnits: number
}

interface InfiniteCanvasProps {
  snapSettings: SnapSettings
  children?: ReactNode
}

/** Empty pan/zoom canvas (MILESTONES.md M5) — no nodes yet, that lands with
    the node editor phase (NODE_EDITOR.md, M10). Camera state lives in a ref,
    not React state (ARCHITECTURE.md §7): panning/zooming redraws the canvas
    every frame via its own rAF loop without ever triggering a React
    re-render, which is what keeps this smooth independent of React's
    render cycle. `snapSettings` is accepted but unused by anything yet —
    there's nothing to snap until M10 places real nodes; the setting exists
    now so the UI for it doesn't need inventing later.
*/
export function InfiniteCanvas({ children }: InfiniteCanvasProps) {
  const containerRef = useRef<HTMLDivElement | null>(null)
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const cameraRef = useRef<Camera>({ panX: 0, panY: 0, zoom: 1 })
  const [, forceLayout] = useState(0) // only to re-measure on mount; render itself reads the ref

  useEffect(() => {
    const canvas = canvasRef.current
    const container = containerRef.current
    if (!canvas || !container) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    let rafHandle = 0
    let spaceHeld = false
    let dragging = false
    let lastX = 0
    let lastY = 0

    const resize = () => {
      const dpr = window.devicePixelRatio || 1
      const width = container.clientWidth
      const height = container.clientHeight
      canvas.width = width * dpr
      canvas.height = height * dpr
      canvas.style.width = `${width}px`
      canvas.style.height = `${height}px`
    }

    const render = () => {
      const dpr = window.devicePixelRatio || 1
      const width = canvas.width / dpr
      const height = canvas.height / dpr

      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.fillStyle = tokens.color.background
      ctx.fillRect(0, 0, width, height)
      drawDotGrid(ctx, width, height, cameraRef.current)

      rafHandle = requestAnimationFrame(render)
    }

    const onKeyDown = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement | null
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA')) return
      if (e.code === 'Space') spaceHeld = true
    }
    const onKeyUp = (e: KeyboardEvent) => {
      if (e.code === 'Space') spaceHeld = false
    }

    const startDrag = (x: number, y: number) => {
      dragging = true
      lastX = x
      lastY = y
    }

    const onMouseDown = (e: MouseEvent) => {
      const isRightDrag = e.button === 2
      const isSpaceLeftDrag = e.button === 0 && spaceHeld
      if (isRightDrag || isSpaceLeftDrag) {
        e.preventDefault()
        startDrag(e.clientX, e.clientY)
      }
    }

    const onMouseMove = (e: MouseEvent) => {
      if (!dragging) return
      const dx = e.clientX - lastX
      const dy = e.clientY - lastY
      lastX = e.clientX
      lastY = e.clientY
      cameraRef.current.panX += dx
      cameraRef.current.panY += dy
    }

    const endDrag = () => {
      dragging = false
    }

    const onWheel = (e: WheelEvent) => {
      e.preventDefault()
      const rect = canvas.getBoundingClientRect()
      const cursorX = e.clientX - rect.left
      const cursorY = e.clientY - rect.top

      const camera = cameraRef.current
      const zoomFactor = Math.exp(-e.deltaY * 0.001)
      const newZoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, camera.zoom * zoomFactor))

      // Keep the world point under the cursor fixed on screen across the zoom change.
      const worldX = (cursorX - camera.panX) / camera.zoom
      const worldY = (cursorY - camera.panY) / camera.zoom
      camera.panX = cursorX - worldX * newZoom
      camera.panY = cursorY - worldY * newZoom
      camera.zoom = newZoom
    }

    const onContextMenu = (e: MouseEvent) => e.preventDefault()

    resize()
    const resizeObserver = new ResizeObserver(resize)
    resizeObserver.observe(container)

    window.addEventListener('keydown', onKeyDown)
    window.addEventListener('keyup', onKeyUp)
    canvas.addEventListener('mousedown', onMouseDown)
    window.addEventListener('mousemove', onMouseMove)
    window.addEventListener('mouseup', endDrag)
    canvas.addEventListener('wheel', onWheel, { passive: false })
    canvas.addEventListener('contextmenu', onContextMenu)

    rafHandle = requestAnimationFrame(render)
    forceLayout((n) => n + 1)

    return () => {
      cancelAnimationFrame(rafHandle)
      resizeObserver.disconnect()
      window.removeEventListener('keydown', onKeyDown)
      window.removeEventListener('keyup', onKeyUp)
      canvas.removeEventListener('mousedown', onMouseDown)
      window.removeEventListener('mousemove', onMouseMove)
      window.removeEventListener('mouseup', endDrag)
      canvas.removeEventListener('wheel', onWheel)
      canvas.removeEventListener('contextmenu', onContextMenu)
    }
  }, [])

  return (
    <div className="infinite-canvas" ref={containerRef}>
      <canvas ref={canvasRef} />
      <div className="infinite-canvas-overlay">{children}</div>
    </div>
  )
}
