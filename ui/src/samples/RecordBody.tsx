// wiki/plans/Sampling.md 3a.3: a Record node's body — how much it holds, and
// the button that keeps it: "Freeze" (a Sample node beside it; recording goes
// on) or, for a one-take capture, "Stop & keep" (the take replaces the Record).
import { useEffect, useState } from 'react'
import { freezeRecordNode } from '../graph/graphStore'
import { recordedSeconds } from './samples'
import './SampleBody.css'

function formatSeconds(s: number): string {
  return s < 1 ? `${Math.round(s * 1000)} ms` : `${s.toFixed(1)} s`
}

export function RecordBody({ nodeId, mode }: { nodeId?: string; mode: number }) {
  const [seconds, setSeconds] = useState(0)
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    if (!nodeId) return
    let alive = true
    const poll = () => void recordedSeconds(nodeId).then((s) => alive && setSeconds(Math.max(0, s)))
    poll()
    const timer = window.setInterval(poll, 250)
    return () => {
      alive = false
      window.clearInterval(timer)
    }
  }, [nodeId])

  const once = mode === 1
  return (
    <div className="sample-body sample-body-record">
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
