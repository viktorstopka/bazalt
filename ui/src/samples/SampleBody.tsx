// wiki/plans/Sampling.md 3a.1: a Sample node's body — the waveform with its
// region, the file's name and length; "Load…" on an empty node, "Find…" when
// the sample is missing from the library.
import { useEffect, useRef, useState } from 'react'
import { setNodeContent, reportError } from '../graph/graphStore'
import { tokens } from '../theme/tokens'
import { importSampleFile, pickAudioFile, sampleInfo, sampleOverview, type SampleInfo } from './samples'
import './SampleBody.css'

const WIDTH = 180
const HEIGHT = 46
const BUCKETS = 180

interface SampleContent {
  sample?: string
  name?: string
  start?: number
  end?: number
  [key: string]: unknown
}

function formatSeconds(s: number): string {
  return s < 1 ? `${Math.round(s * 1000)} ms` : `${s.toFixed(2)} s`
}

export function SampleBody({ nodeId, content }: { nodeId?: string; content: unknown }) {
  const doc = (content && typeof content === 'object' ? content : {}) as SampleContent
  const id = typeof doc.sample === 'string' ? doc.sample : ''
  const [info, setInfo] = useState<SampleInfo | null>(null)
  const canvasRef = useRef<HTMLCanvasElement | null>(null)

  useEffect(() => {
    let alive = true
    setInfo(null)
    if (id) void sampleInfo(id).then((next) => alive && setInfo(next))
    return () => {
      alive = false
    }
  }, [id])

  const seconds = info?.found ? (info.seconds ?? 0) : 0
  const start = Math.max(0, Number(doc.start ?? 0))
  const end = Number(doc.end ?? 0) > 0 ? Number(doc.end) : seconds

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || !info?.found || !id) return
    let alive = true
    void sampleOverview(id, BUCKETS).then((peaks) => {
      if (!alive) return
      const ratio = window.devicePixelRatio || 1
      canvas.width = WIDTH * ratio
      canvas.height = HEIGHT * ratio
      const g = canvas.getContext('2d')
      if (!g) return
      g.scale(ratio, ratio)
      g.clearRect(0, 0, WIDTH, HEIGHT)
      const mid = HEIGHT / 2
      const column = WIDTH / BUCKETS
      const inRegion = (b: number) => {
        const t = ((b + 0.5) / BUCKETS) * seconds
        return t >= start && t <= end
      }
      for (let b = 0; b < BUCKETS; ++b) {
        const lo = peaks[b * 2] ?? 0
        const hi = peaks[b * 2 + 1] ?? 0
        g.fillStyle = tokens.color.portAudio
        g.globalAlpha = inRegion(b) ? 0.9 : 0.25
        const top = mid - Math.min(1, hi) * (mid - 1)
        const bottom = mid - Math.max(-1, lo) * (mid - 1)
        g.fillRect(b * column, top, Math.max(1, column - 0.3), Math.max(1, bottom - top))
      }
      g.globalAlpha = 1
    })
    return () => {
      alive = false
    }
  }, [id, info, seconds, start, end])

  const load = async (relink: boolean) => {
    const file = await pickAudioFile()
    if (!file || !nodeId) return
    const result = await importSampleFile(file)
    if (!result.success || !result.id) {
      reportError(`Couldn't load the sample: ${result.errorMessage}`)
      return
    }
    // Relinking keeps the region; a new sample starts from the whole file.
    const next = relink ? { ...doc, sample: result.id, name: doc.name || file.name } : { sample: result.id, name: file.name }
    setNodeContent(nodeId, next)
  }

  const button = (label: string, title: string, relink: boolean) => (
    <button
      className="sample-body-button"
      title={title}
      onMouseDown={(e) => e.stopPropagation()}
      onClick={(e) => {
        e.stopPropagation()
        void load(relink)
      }}
    >
      {label}
    </button>
  )

  if (!id)
    return <div className="sample-body sample-body-empty">{button('Load sample…', 'Choose an audio file (or drop one on the canvas)', false)}</div>

  if (info && !info.found)
    return (
      <div className="sample-body sample-body-missing">
        <span className="sample-body-name" title={doc.name}>
          Missing: {doc.name || id}
        </span>
        {button('Find…', 'This sample isn’t in your library — choose the file to relink it', true)}
      </div>
    )

  return (
    <div className="sample-body">
      <canvas ref={canvasRef} className="sample-body-wave" style={{ width: WIDTH, height: HEIGHT }} />
      <div className="sample-body-caption">
        <span className="sample-body-name" title={doc.name}>
          {doc.name || 'Sample'}
        </span>
        <span className="sample-body-length">{info?.found ? formatSeconds(Math.max(0, end - start)) : ''}</span>
      </div>
    </div>
  )
}
