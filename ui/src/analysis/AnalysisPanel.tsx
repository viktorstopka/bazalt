import { useEffect } from 'react'
import { TelemetryScope } from './TelemetryScope'
import { seedBaselineTaps, unseedBaselineTaps, type TapName } from '../telemetry/telemetryClient'
import './AnalysisPanel.css'

const CHANNELS: { tap: TapName; label: string }[] = [
  { tap: 'main', label: 'Main' },
  { tap: 'aux1', label: 'Aux 1' },
  { tap: 'aux2', label: 'Aux 2' },
  { tap: 'aux3', label: 'Aux 3' },
  { tap: 'aux4', label: 'Aux 4' },
]

/** M5: oscilloscope, spectrum analyzer, and level meter for main output and
    each of the 4 sidechain inputs (MILESTONES.md M5). Layout is a plain
    grid for now — this is the temporary analysis panel, not the node
    editor's in-graph visualization (NODE_EDITOR.md §11 replaces this with
    live previews inline in the graph; this component's canvases and
    telemetry client are what that work builds on, not what it throws away).
*/
export function AnalysisPanel() {
  // Direct feedback, a real performance bug: these 5 taps x 3 frame types
  // used to be polled unconditionally from app boot (telemetryClient.ts's
  // own startTelemetryPolling(), see its updated comment) regardless of
  // whether this panel was even mounted - it never was, App.tsx's
  // `analysisOpen` is a hardcoded `false`. Now scoped to this component's
  // own lifetime, matching the same subscribe-on-mount/unsubscribe-on-
  // unmount discipline NodePreview.tsx already uses for its own dynamic taps.
  useEffect(() => {
    seedBaselineTaps()
    return () => unseedBaselineTaps()
  }, [])

  return (
    <div className="analysis-panel">
      {CHANNELS.map(({ tap, label }) => (
        <div className="analysis-panel-row" key={tap}>
          <span className="analysis-panel-row-label">{label}</span>
          <TelemetryScope tap={tap} kind="scope" label="Scope" />
          <TelemetryScope tap={tap} kind="spectrum" label="Spectrum" />
          <TelemetryScope tap={tap} kind="meter" label="Meter" />
        </div>
      ))}
    </div>
  )
}
