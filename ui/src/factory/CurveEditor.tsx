// The curve editor (wiki/plans/DataAndWavetable.md 1b, the EDIT CURVE mockup):
// a pan/zoom grid with the curve's points. Drag a point (selected points move
// together); drag the round handle on a segment to bend it; double-click to
// add a point; Delete removes the selection; right-click a point for its
// segment shape and marker. Snap rounds to the grid. Every drag streams to the
// engine live (`onLive`) and commits once on release (`onCommit`), so it is
// heard while you move and undone in one step.
import { useEffect, useMemo, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import { type CurveDoc, type CurvePoint, type SegmentShape, curvePath, domainEnd, evaluateCurve, sortPoints, valueRange } from './curveModel'
import { tokens } from '../theme/tokens'
import './CurveEditor.css'

interface View {
  x0: number
  x1: number
  y0: number
  y1: number
}

interface Props {
  doc: CurveDoc
  snap: boolean
  onLive: (doc: CurveDoc) => void
  onCommit: (doc: CurveDoc) => void
}

type Drag =
  | { kind: 'points'; startX: number; startY: number; origin: CurvePoint[]; moved: boolean }
  | { kind: 'tension'; index: number; startY: number; origin: number }
  | { kind: 'box'; startX: number; startY: number; x: number; y: number }
  | { kind: 'pan'; startClientX: number; startClientY: number; origin: View }

const MARKERS = ['A', 'D', 'S', 'H', 'R'] as const
const SHAPES: readonly { id: SegmentShape; label: string }[] = [
  { id: 'curve', label: 'Curve' },
  { id: 'smooth', label: 'Smooth' },
  { id: 'hold', label: 'Hold' },
]

function initialView(doc: CurveDoc): View {
  const end = domainEnd(doc)
  const [lo, hi] = valueRange(doc)
  const padX = end * 0.04
  const padY = (hi - lo) * 0.12
  return { x0: -padX, x1: end + padX, y0: lo - padY, y1: hi + padY }
}

/** A grid step near `span / target`, from 1-2-5 steps. */
function niceStep(span: number, target: number): number {
  const raw = span / target
  const power = Math.pow(10, Math.floor(Math.log10(raw)))
  const n = raw / power
  return (n < 1.5 ? 1 : n < 3.5 ? 2 : n < 7.5 ? 5 : 10) * power
}

export function CurveEditor({ doc, snap, onLive, onCommit }: Props) {
  const svgRef = useRef<SVGSVGElement | null>(null)
  const [size, setSize] = useState({ width: 800, height: 400 })
  const [view, setView] = useState<View>(() => initialView(doc))
  const [draft, setDraft] = useState<CurveDoc | null>(null)
  const [selection, setSelection] = useState<ReadonlySet<number>>(new Set())
  const [drag, setDrag] = useState<Drag | null>(null)
  const [menu, setMenu] = useState<{ index: number; clientX: number; clientY: number } | null>(null)
  const spaceHeld = useRef(false)
  const movedPoints = useRef<CurvePoint[]>([])
  const shown = draft ?? doc
  const isTime = shown.timeBase === 'time'

  // Reset the view when switching between a cycle and a time curve.
  useEffect(() => setView(initialView(doc)), [doc.timeBase]) // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    const el = svgRef.current
    if (!el) return
    const observer = new ResizeObserver(([entry]) => setSize({ width: entry.contentRect.width, height: entry.contentRect.height }))
    observer.observe(el)
    return () => observer.disconnect()
  }, [])

  const toPxX = (x: number) => ((x - view.x0) / (view.x1 - view.x0)) * size.width
  const toPxY = (y: number) => size.height - ((y - view.y0) / (view.y1 - view.y0)) * size.height
  const toWorld = (clientX: number, clientY: number) => {
    const rect = svgRef.current!.getBoundingClientRect()
    return {
      x: view.x0 + ((clientX - rect.left) / rect.width) * (view.x1 - view.x0),
      y: view.y0 + (1 - (clientY - rect.top) / rect.height) * (view.y1 - view.y0),
    }
  }

  const end = domainEnd(shown)
  const [yLo, yHi] = valueRange(shown)
  const xStep = isTime ? niceStep(view.x1 - view.x0, 10) : niceStep(view.x1 - view.x0, 16) <= 1 / 16 ? 1 / 16 : 1 / 8
  const yStep = (yHi - yLo) / 8
  const snapX = (x: number) => (snap ? Math.round(x / xStep) * xStep : x)
  const snapY = (y: number) => (snap ? Math.round(y / yStep) * yStep : y)
  const clampX = (x: number) => Math.min(isTime ? Math.max(end, x) : 1, Math.max(0, x))
  const clampY = (y: number) => Math.min(yHi, Math.max(yLo, y))

  const withPoints = (points: CurvePoint[]): CurveDoc => {
    const sorted = sortPoints(points)
    if (!isTime) return { ...shown, points: sorted }
    // An envelope's length follows its last point.
    return { ...shown, points: sorted, length: Math.max(0.001, sorted[sorted.length - 1]?.x ?? 0) }
  }

  // ---- pointer handling ------------------------------------------------------

  const hitPoint = (clientX: number, clientY: number): number => {
    const rect = svgRef.current!.getBoundingClientRect()
    const px = clientX - rect.left
    const py = clientY - rect.top
    let best = -1
    let bestDistance = 9
    shown.points.forEach((p, i) => {
      const d = Math.hypot(toPxX(p.x) - px, toPxY(p.y) - py)
      if (d < bestDistance) {
        best = i
        bestDistance = d
      }
    })
    return best
  }

  const onPointerDown = (e: React.PointerEvent) => {
    if (e.button === 2) return
    ;(e.target as Element).setPointerCapture?.(e.pointerId)
    setMenu(null)
    if (e.button === 1 || spaceHeld.current) {
      setDrag({ kind: 'pan', startClientX: e.clientX, startClientY: e.clientY, origin: view })
      return
    }
    const world = toWorld(e.clientX, e.clientY)
    const tensionIndex = (e.target as Element).getAttribute('data-tension-index')
    if (tensionIndex !== null) {
      const index = Number(tensionIndex)
      setDrag({ kind: 'tension', index, startY: e.clientY, origin: shown.points[index].tension ?? 0 })
      return
    }
    const hit = hitPoint(e.clientX, e.clientY)
    if (hit >= 0) {
      const next = new Set(e.shiftKey ? selection : selection.has(hit) ? selection : [])
      if (e.shiftKey && selection.has(hit)) next.delete(hit)
      else next.add(hit)
      setSelection(next)
      setDrag({ kind: 'points', startX: world.x, startY: world.y, origin: shown.points, moved: false })
      return
    }
    if (!e.shiftKey) setSelection(new Set())
    setDrag({ kind: 'box', startX: world.x, startY: world.y, x: world.x, y: world.y })
  }

  const onPointerMove = (e: React.PointerEvent) => {
    if (!drag) return
    if (drag.kind === 'pan') {
      const rect = svgRef.current!.getBoundingClientRect()
      const dx = ((e.clientX - drag.startClientX) / rect.width) * (drag.origin.x1 - drag.origin.x0)
      const dy = ((e.clientY - drag.startClientY) / rect.height) * (drag.origin.y1 - drag.origin.y0)
      setView({ x0: drag.origin.x0 - dx, x1: drag.origin.x1 - dx, y0: drag.origin.y0 + dy, y1: drag.origin.y1 + dy })
      return
    }
    const world = toWorld(e.clientX, e.clientY)
    if (drag.kind === 'box') {
      setDrag({ ...drag, x: world.x, y: world.y })
      return
    }
    if (drag.kind === 'tension') {
      const tension = Math.max(-1, Math.min(1, drag.origin + (e.clientY - drag.startY) / 120))
      const points = shown.points.map((p, i) => (i === drag.index ? { ...p, tension, shape: 'curve' as const } : p))
      const next = { ...shown, points }
      setDraft(next)
      onLive(next)
      return
    }
    // Move every selected point by the same (snapped) amount.
    const dx = world.x - drag.startX
    const dy = world.y - drag.startY
    const points = drag.origin.map((p, i) => (selection.has(i) ? { ...p, x: clampX(snapX(p.x + dx)), y: clampY(snapY(p.y + dy)) } : p))
    movedPoints.current = points.filter((_, i) => selection.has(i))
    const next = withPoints(points)
    setDraft(next)
    setDrag({ ...drag, moved: true })
    onLive(next)
  }

  const onPointerUp = () => {
    if (drag?.kind === 'box') {
      const [xa, xb] = [Math.min(drag.startX, drag.x), Math.max(drag.startX, drag.x)]
      const [ya, yb] = [Math.min(drag.startY, drag.y), Math.max(drag.startY, drag.y)]
      const inside = shown.points.flatMap((p, i) => (p.x >= xa && p.x <= xb && p.y >= ya && p.y <= yb ? [i] : []))
      setSelection(new Set([...selection, ...inside]))
    }
    if (draft) {
      // Selection follows the moved points through the re-sort.
      if (drag?.kind === 'points') setSelection(new Set(movedPoints.current.map((p) => draft.points.indexOf(p)).filter((i) => i >= 0)))
      onCommit(draft)
      setDraft(null)
    }
    setDrag(null)
  }

  const onDoubleClick = (e: React.MouseEvent) => {
    if (hitPoint(e.clientX, e.clientY) >= 0) return
    const world = toWorld(e.clientX, e.clientY)
    const x = clampX(snapX(world.x))
    const y = clampY(snapY(world.y))
    const next = withPoints([...shown.points, { x, y }])
    setSelection(new Set([next.points.findIndex((p) => p.x === x && p.y === y)]))
    onCommit(next)
  }

  const onWheel = (e: React.WheelEvent) => {
    const world = toWorld(e.clientX, e.clientY)
    const factor = Math.exp(e.deltaY * 0.0015)
    const zoomY = !e.shiftKey
    setView((v) => ({
      x0: world.x - (world.x - v.x0) * factor,
      x1: world.x + (v.x1 - world.x) * factor,
      y0: zoomY ? world.y - (world.y - v.y0) * factor : v.y0,
      y1: zoomY ? world.y + (v.y1 - world.y) * factor : v.y1,
    }))
  }

  const onContextMenu = (e: React.MouseEvent) => {
    e.preventDefault()
    const hit = hitPoint(e.clientX, e.clientY)
    if (hit >= 0) setMenu({ index: hit, clientX: e.clientX, clientY: e.clientY })
  }

  // Keyboard: Delete removes the selection, Space pans, Ctrl+A selects all.
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLElement && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) return
      if (e.code === 'Space') spaceHeld.current = true
      if ((e.key === 'Delete' || e.key === 'Backspace') && selection.size > 0) {
        const remaining = doc.points.filter((_, i) => !selection.has(i))
        if (remaining.length > 0) onCommit(withPoints(remaining))
        setSelection(new Set())
        e.preventDefault()
      }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'a') {
        setSelection(new Set(doc.points.map((_, i) => i)))
        e.preventDefault()
      }
    }
    const onKeyUp = (e: KeyboardEvent) => {
      if (e.code === 'Space') spaceHeld.current = false
    }
    window.addEventListener('keydown', onKeyDown)
    window.addEventListener('keyup', onKeyUp)
    return () => {
      window.removeEventListener('keydown', onKeyDown)
      window.removeEventListener('keyup', onKeyUp)
    }
  })

  const setPointProps = (index: number, change: Partial<CurvePoint>) => {
    const points = doc.points.map((p, i) => {
      if (i !== index) {
        // A marker letter names one point only.
        return change.marker && p.marker === change.marker ? { ...p, marker: undefined } : p
      }
      return { ...p, ...change }
    })
    onCommit({ ...doc, points })
  }

  // ---- drawing -----------------------------------------------------------------

  const grid = useMemo(() => {
    const lines: { x1: number; y1: number; x2: number; y2: number; major: boolean }[] = []
    const xMajor = isTime ? xStep * 5 : 0.25
    for (let x = Math.ceil(view.x0 / xStep) * xStep; x <= view.x1; x += xStep)
      lines.push({ x1: toPxX(x), y1: 0, x2: toPxX(x), y2: size.height, major: Math.abs(x / xMajor - Math.round(x / xMajor)) < 1e-6 })
    for (let y = Math.ceil(view.y0 / yStep) * yStep; y <= view.y1; y += yStep)
      lines.push({ x1: 0, y1: toPxY(y), x2: size.width, y2: toPxY(y), major: Math.abs(y) < 1e-6 })
    return lines
  }, [view, size, xStep, yStep, isTime]) // eslint-disable-line react-hooks/exhaustive-deps

  const segmentHandles = shown.points.flatMap((p, i) => {
    const next = shown.points[i + 1] ?? (isTime ? undefined : { ...shown.points[0], x: shown.points[0].x + 1 })
    if (!next || (p.shape ?? 'curve') !== 'curve' || next.x - p.x < 1e-6) return []
    const mx = (p.x + next.x) / 2
    const my = evaluateCurve(shown, isTime ? mx : mx % 1)
    return [{ index: i, cx: toPxX(mx > 1 && !isTime ? mx - 1 : mx), cy: toPxY(my) }]
  })

  const box = drag?.kind === 'box' ? drag : null
  const menuPoint = menu ? doc.points[menu.index] : undefined

  return (
    <div className="curve-editor">
      <svg
        ref={svgRef}
        className="curve-editor-svg"
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onDoubleClick={onDoubleClick}
        onWheel={onWheel}
        onContextMenu={onContextMenu}
      >
        {grid.map(({ major, ...l }, i) => (
          <line key={i} {...l} className={major ? 'curve-grid curve-grid-major' : 'curve-grid'} />
        ))}
        {/* The domain: one cycle, or the envelope's length. */}
        <rect className="curve-domain" x={toPxX(0)} y={toPxY(yHi)} width={Math.max(0, toPxX(end) - toPxX(0))} height={Math.max(0, toPxY(yLo) - toPxY(yHi))} />
        {shown.loop && (
          <rect className="curve-loop" x={toPxX(shown.loop.start)} y={0} width={Math.max(0, toPxX(shown.loop.end) - toPxX(shown.loop.start))} height={size.height} />
        )}
        <path className="curve-path" d={curvePath(shown, toPxX, toPxY, 512)} stroke={tokens.color.portModulation} />
        {segmentHandles.map((h) => (
          <circle key={`t${h.index}`} className="curve-tension-handle" data-tension-index={h.index} cx={h.cx} cy={h.cy} r={4} />
        ))}
        {shown.points.map((p, i) => (
          <g key={i}>
            {p.marker && (
              <>
                <line className="curve-marker-line" x1={toPxX(p.x)} y1={0} x2={toPxX(p.x)} y2={size.height} />
                <text className="curve-marker-label" x={toPxX(p.x) + 4} y={14}>
                  {p.marker}
                </text>
              </>
            )}
            <circle className={selection.has(i) ? 'curve-point curve-point-selected' : 'curve-point'} cx={toPxX(p.x)} cy={toPxY(p.y)} r={5} />
          </g>
        ))}
        {box && (
          <rect
            className="curve-box"
            x={Math.min(toPxX(box.startX), toPxX(box.x))}
            y={Math.min(toPxY(box.startY), toPxY(box.y))}
            width={Math.abs(toPxX(box.x) - toPxX(box.startX))}
            height={Math.abs(toPxY(box.y) - toPxY(box.startY))}
          />
        )}
      </svg>
      {menu &&
        menuPoint &&
        createPortal(
          <div className="curve-point-menu" style={{ left: menu.clientX, top: menu.clientY }} onPointerDown={(e) => e.stopPropagation()}>
            <div className="curve-point-menu-title">Segment</div>
            {SHAPES.map((s) => (
              <button
                key={s.id}
                className={(menuPoint.shape ?? 'curve') === s.id ? 'curve-point-menu-item active' : 'curve-point-menu-item'}
                onClick={() => {
                  setPointProps(menu.index, { shape: s.id })
                  setMenu(null)
                }}
              >
                {s.label}
              </button>
            ))}
            {isTime && (
              <>
                <div className="curve-point-menu-title">Marker</div>
                <div className="curve-point-menu-row">
                  {MARKERS.map((m) => (
                    <button
                      key={m}
                      className={menuPoint.marker === m ? 'curve-point-menu-item active' : 'curve-point-menu-item'}
                      onClick={() => {
                        setPointProps(menu.index, { marker: menuPoint.marker === m ? undefined : m })
                        setMenu(null)
                      }}
                    >
                      {m}
                    </button>
                  ))}
                </div>
              </>
            )}
          </div>,
          document.body,
        )}
    </div>
  )
}
