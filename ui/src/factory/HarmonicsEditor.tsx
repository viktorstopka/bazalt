// The harmonics editor (wiki/plans/DataAndWavetable.md 1c, "an FFT draw
// editor for harmonics"): one bar per harmonic, the fundamental first. Drag
// across the bars to paint their levels; right-click (or Alt-drag) clears.
// Streams live while painting and commits once on release, like the curve
// editor. Phases are kept as they are.
import { useEffect, useRef, useState } from 'react'
import { HARMONIC_BARS } from './wavetableModel'
import { tokens } from '../theme/tokens'
import './HarmonicsEditor.css'

interface Props {
  amplitudes: number[]
  onLive: (amplitudes: number[]) => void
  onCommit: (amplitudes: number[]) => void
}

const PAD = { left: 36, right: 12, top: 12, bottom: 26 }

export function HarmonicsEditor({ amplitudes, onLive, onCommit }: Props) {
  const svgRef = useRef<SVGSVGElement | null>(null)
  const [size, setSize] = useState({ width: 800, height: 400 })
  const [draft, setDraft] = useState<number[] | null>(null)
  const last = useRef<{ bar: number; value: number } | null>(null)
  const [hover, setHover] = useState<number | null>(null)

  useEffect(() => {
    const svg = svgRef.current
    if (!svg) return
    const observer = new ResizeObserver(([entry]) => setSize({ width: entry.contentRect.width, height: entry.contentRect.height }))
    observer.observe(svg)
    return () => observer.disconnect()
  }, [])

  const count = Math.max(HARMONIC_BARS, amplitudes.length)
  const shown = draft ?? amplitudes
  const plotW = Math.max(1, size.width - PAD.left - PAD.right)
  const plotH = Math.max(1, size.height - PAD.top - PAD.bottom)
  const barW = plotW / count

  const at = (e: { clientX: number; clientY: number }) => {
    const rect = svgRef.current!.getBoundingClientRect()
    const bar = Math.floor((e.clientX - rect.left - PAD.left) / barW)
    const value = 1 - (e.clientY - rect.top - PAD.top) / plotH
    return { bar: Math.min(count - 1, Math.max(0, bar)), value: Math.min(1, Math.max(0, value)) }
  }

  // Paint from the last point to this one, so a fast drag leaves no gaps.
  const paint = (base: number[], to: { bar: number; value: number }, clear: boolean) => {
    const next = [...base]
    while (next.length < count) next.push(0)
    const from = last.current ?? to
    const steps = Math.abs(to.bar - from.bar)
    for (let s = 0; s <= steps; s++) {
      const t = steps === 0 ? 1 : s / steps
      const bar = Math.round(from.bar + (to.bar - from.bar) * t)
      next[bar] = clear ? 0 : Math.round((from.value + (to.value - from.value) * t) * 1000) / 1000
    }
    last.current = to
    // Trailing silent harmonics are not stored.
    while (next.length > 1 && next[next.length - 1] === 0) next.pop()
    return next
  }

  const onPointerDown = (e: React.PointerEvent) => {
    if (e.button !== 0 && e.button !== 2) return
    e.preventDefault()
    ;(e.target as Element).setPointerCapture?.(e.pointerId)
    const clear = e.button === 2 || e.altKey
    last.current = null
    let current = paint(amplitudes, at(e), clear)
    setDraft(current)
    onLive(current)
    const move = (ev: PointerEvent) => {
      current = paint(current, at(ev), clear)
      setDraft(current)
      onLive(current)
    }
    const up = () => {
      window.removeEventListener('pointermove', move)
      window.removeEventListener('pointerup', up)
      last.current = null
      setDraft(null)
      onCommit(current)
    }
    window.addEventListener('pointermove', move)
    window.addEventListener('pointerup', up)
  }

  return (
    <svg
      ref={svgRef}
      className="harmonics-editor"
      onPointerDown={onPointerDown}
      onPointerMove={(e) => setHover(at(e).bar)}
      onPointerLeave={() => setHover(null)}
      onContextMenu={(e) => e.preventDefault()}
    >
      {[0, 0.25, 0.5, 0.75, 1].map((v) => (
        <g key={v}>
          <line x1={PAD.left} x2={PAD.left + plotW} y1={PAD.top + (1 - v) * plotH} y2={PAD.top + (1 - v) * plotH} className="harmonics-grid" />
          <text x={PAD.left - 6} y={PAD.top + (1 - v) * plotH + 3} className="harmonics-label" textAnchor="end">
            {v.toFixed(2)}
          </text>
        </g>
      ))}
      {Array.from({ length: count }, (_, i) => {
        const value = Math.abs(shown[i] ?? 0)
        const h = value * plotH
        return (
          <rect
            key={i}
            x={PAD.left + i * barW + Math.min(1, barW * 0.15)}
            y={PAD.top + plotH - h}
            width={Math.max(1, barW - Math.min(2, barW * 0.3))}
            height={Math.max(h, 0)}
            fill={tokens.color.portModulation}
            opacity={hover === i ? 1 : 0.8}
          />
        )
      })}
      {[1, 8, 16, 32, 48, 64].filter((h) => h <= count).map((h) => (
        <text key={h} x={PAD.left + (h - 0.5) * barW} y={PAD.top + plotH + 16} className="harmonics-label" textAnchor="middle">
          {h}
        </text>
      ))}
      {hover !== null && (
        <text x={PAD.left + plotW} y={PAD.top + 10} className="harmonics-readout" textAnchor="end">
          Harmonic {hover + 1}: {(Math.abs(shown[hover] ?? 0)).toFixed(3)}
        </text>
      )}
    </svg>
  )
}
