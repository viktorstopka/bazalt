// wiki/plans/Sampling.md §3.1: the Wavetable factory's "From sample…" — a
// sample (a file, or a Sample node already in the patch, with its region)
// becomes a table of keyframes placed along the sound, so Frame replays its
// change at any pitch. The extraction is the engine's (WavetableExtraction.h);
// the result is an ordinary table, every keyframe editable.
import { useMemo, useState } from 'react'
import { getSnapshot } from '../graph/graphStore'
import { importSampleFile, pickAudioFile, wavetableFromSample, type WavetableMethod } from '../samples/samples'
import { describePitch } from '../nodes/pitchReadout'
import { wavetableFromContent, type WavetableDoc } from './wavetableModel'

const METHODS: { id: WavetableMethod; label: string; title: string }[] = [
  { id: 'auto', label: 'Auto', title: 'Cycle file when it is one, Cycles when it has a pitch, Texture otherwise' },
  { id: 'cycles', label: 'Cycles', title: 'Real cycles of a pitched sound, turned to one phase' },
  { id: 'harmonics', label: 'Harmonics', title: 'The cycles’ harmonics without their phases — the smoothest morphs' },
  { id: 'texture', label: 'Texture', title: 'The colour of an unpitched sound (noise, breath, a room) in one cycle' },
  { id: 'cycleFile', label: 'Cycle file', title: 'A single-cycle file, or 2048-sample frames back to back (Serum-style)' },
]
const FRAME_COUNTS = [4, 8, 16, 32, 64, 256]
const METHOD_NAMES: Record<string, string> = { cycles: 'Cycles', harmonics: 'Harmonics', texture: 'Texture', cycleFile: 'Cycle file' }

interface Source {
  key: string
  label: string
  sampleId: string
  start: number
  end: number
}

export function FromSamplePanel({ onResult, onClose }: { onResult: (doc: WavetableDoc) => void; onClose: () => void }) {
  const sampleNodes = useMemo<Source[]>(
    () =>
      getSnapshot()
        .nodes.filter((n) => n.typeId === 'data.sample')
        .flatMap((n) => {
          const c = (n.content ?? {}) as { sample?: unknown; name?: unknown; start?: unknown; end?: unknown }
          if (typeof c.sample !== 'string' || !c.sample) return []
          return [{ key: n.id, label: n.titleOverride ?? (typeof c.name === 'string' && c.name ? c.name : 'Sample'), sampleId: c.sample, start: Number(c.start ?? 0), end: Number(c.end ?? 0) }]
        }),
    [],
  )
  const [picked, setPicked] = useState<Source | null>(sampleNodes[0] ?? null)
  const [method, setMethod] = useState<WavetableMethod>('auto')
  const [frames, setFrames] = useState(16)
  const [status, setStatus] = useState('')
  const [busy, setBusy] = useState(false)

  const chooseFile = async () => {
    const file = await pickAudioFile()
    if (!file) return
    setStatus('Loading…')
    const imported = await importSampleFile(file)
    if (!imported.success || !imported.id) {
      setStatus(`Couldn't load it: ${imported.errorMessage}`)
      return
    }
    setPicked({ key: `file:${imported.id}`, label: file.name, sampleId: imported.id, start: 0, end: 0 })
    setStatus('')
  }

  const make = async () => {
    if (!picked) return
    setBusy(true)
    setStatus('Listening…')
    const result = await wavetableFromSample(picked.sampleId, method, frames, picked.start, picked.end)
    setBusy(false)
    if (!result.success || !result.content) {
      setStatus(result.errorMessage || 'That didn’t work')
      return
    }
    const how = METHOD_NAMES[result.method ?? ''] ?? ''
    const pitch = typeof result.pitch === 'number' ? ` — ${describePitch(result.pitch).note}` : ''
    setStatus(`${how}${pitch}`)
    onResult(wavetableFromContent(result.content))
  }

  return (
    <div className="wavetable-from-sample" onMouseDown={(e) => e.stopPropagation()}>
      <div className="wavetable-from-sample-row">
        <span>Sound</span>
        <select
          value={picked?.key ?? ''}
          onChange={(e) => setPicked(sampleNodes.find((s) => s.key === e.target.value) ?? picked)}
        >
          {picked && !sampleNodes.some((s) => s.key === picked.key) && <option value={picked.key}>{picked.label}</option>}
          {sampleNodes.map((s) => (
            <option key={s.key} value={s.key}>
              {s.label}
            </option>
          ))}
          {!picked && <option value="">—</option>}
        </select>
        <button className="factory-button" onClick={() => void chooseFile()}>
          File…
        </button>
      </div>
      <div className="wavetable-from-sample-row">
        <span>How</span>
        <div className="wavetable-segmented">
          {METHODS.map((m) => (
            <button key={m.id} className={method === m.id ? 'active' : ''} title={m.title} onClick={() => setMethod(m.id)}>
              {m.label}
            </button>
          ))}
        </div>
      </div>
      <div className="wavetable-from-sample-row">
        <span>Frames</span>
        <div className="wavetable-segmented">
          {FRAME_COUNTS.map((n) => (
            <button key={n} className={frames === n ? 'active' : ''} onClick={() => setFrames(n)}>
              {n}
            </button>
          ))}
        </div>
      </div>
      <div className="wavetable-from-sample-row">
        <button className="factory-button" disabled={!picked || busy} onClick={() => void make()} title="Replace this table with the sound's frames (undo brings it back)">
          Make table
        </button>
        <button className="factory-button" onClick={onClose}>
          Close
        </button>
        <span className="wavetable-from-sample-status">{status}</span>
      </div>
    </div>
  )
}
