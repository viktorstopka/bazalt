// wiki/plans/Sampling.md 3a.3/3a.5: a Record node's body — its live waveform
// with the write head moving, how much it holds, and the button that keeps it:
// "Freeze" (a Sample node beside it; recording goes on) or, for a one-take
// capture, "Stop & keep" (the take replaces the Record). Drawing only — the
// waveform is the min/max per stretch the engine keeps as it writes.
import { useEffect, useRef, useState } from 'react'
import { freezeRecordNode } from '../graph/graphStore'
import { tokens } from '../theme/tokens'
import { recordInfo, type RecordInfo } from './samples'
import './SampleBody.css'

const WIDTH = 160
const HEIGHT = 36

function formatSeconds(s: number): string {
  return s < 1 ? `${Math.round(s * 1000)} ms` : `${s.toFixed(1)} s`
}

function draw(canvas: HTMLCanvasElement, info: RecordInfo) {
  const ratio = window.devicePixelRatio || 1
  if (canvas.width !== WIDTH * ratio) {
    canvas.width = WIDTH * ratio
    canvas.height = HEIGHT * ratio
  }
  const g = canvas.getContext('2d')
  if (!g) return
  g.setTransform(ratio, 0, 0, ratio, 0, 0)
  g.clearRect(0, 0, WIDTH, HEIGHT)
  const buckets = Math.floor(info.peaks.length / 2)
  if (buckets === 0) return
  const mid = HEIGHT / 2
  const column = WIDTH / buckets
  g.fillStyle = tokens.color.portAudio
  for (let b = 0; b < buckets; ++b) {
    // A filling buffer shows only what it holds; a full ring all of it.
    const recorded = info.wrapped || (b + 0.5) / buckets <= info.filled
    if (!recorded) continue
    const lo = info.peaks[b * 2] ?? 0
    const hi = info.peaks[b * 2 + 1] ?? 0
    const top = mid - Math.min(1, hi) * (mid - 1)
    const bottom = mid - Math.max(-1, lo) * (mid - 1)
    g.globalAlpha = 0.85
    g.fillRect(b * column, top, Math.max(1, column - 0.3), Math.max(1, bottom - top))
  }
  g.globalAlpha = 1
  // The write head.
  const x = Math.round(info.head * WIDTH) + 0.5
  g.strokeStyle = tokens.color.textPrimary
  g.beginPath()
  g.moveTo(x, 0)
  g.lineTo(x, HEIGHT)
  g.stroke()
}

export function RecordBody({ nodeId, mode }: { nodeId?: string; mode: number }) {
  const [seconds, setSeconds] = useState(0)
  const [busy, setBusy] = useState(false)
  const canvasRef = useRef<HTMLCanvasElement | null>(null)

  useEffect(() => {
    if (!nodeId) return
    let alive = true
    const poll = () =>
      void recordInfo(nodeId).then((info) => {
        if (!alive) return
        setSeconds(Math.max(0, info.seconds))
        if (canvasRef.current) draw(canvasRef.current, info)
      })
    poll()
    const timer = window.setInterval(poll, 100)
    return () => {
      alive = false
      window.clearInterval(timer)
    }
  }, [nodeId])

  const once = mode === 1
  return (
    <div className="sample-body sample-body-record">
      <canvas ref={canvasRef} className="sample-body-wave" style={{ width: WIDTH, height: HEIGHT }} />
      <div className="sample-body-caption">
        <span>{['Ring', 'Once', 'Loop'][mode] ?? 'Ring'}</span>
        <span>{formatSeconds(seconds)}</span>
      </div>
      {nodeId && (
        <button
          className="sample-body-button"
          disabled={busy || seconds <= 0}
          title={once ? 'Keep this take as a Sample node, in the Record’s place' : 'Copy what it holds now into a Sample node beside it'}
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => {
            e.stopPropagation()
            setBusy(true)
            void freezeRecordNode(nodeId, once).finally(() => setBusy(false))
          }}
        >
          {once ? 'Stop & keep' : 'Freeze'}
        </button>
      )}
    </div>
  )
}
