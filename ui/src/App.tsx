import { useState } from 'react'
import { InfiniteCanvas, type SnapSettings } from './canvas/InfiniteCanvas'
import { StressTestCanvas } from './canvas/StressTestCanvas'
import { AnalysisPanel } from './analysis/AnalysisPanel'
import { MacroSlider } from './controls/MacroSlider'
import './App.css'

const MACROS = [
  { relayName: 'oscShape', label: 'Osc Shape' },
  { relayName: 'filterCutoff', label: 'Filter Cutoff' },
  { relayName: 'filterResonance', label: 'Filter Resonance' },
  { relayName: 'envRelease', label: 'Env Release' },
]

/** M5: infinite canvas (empty) + analysis panel + the 4 temporary controls
    the M3 hardcoded voice graph already exposes as macros. The real node
    editor (NODE_EDITOR.md, M10+) replaces the empty canvas's contents and
    this control strip with the actual graph UI — the canvas, telemetry
    client, and design tokens underneath are what that work builds on.
*/
function App() {
  const [snapSettings, setSnapSettings] = useState<SnapSettings>({ enabled: true, sizeWorldUnits: 24 })
  const [stressTestActive, setStressTestActive] = useState(false)
  const [stressStats, setStressStats] = useState({ fps: 0, subscribedTaps: 0 })

  return (
    <div id="app-root">
      {stressTestActive ? (
        <StressTestCanvas onStats={setStressStats} onClose={() => setStressTestActive(false)} />
      ) : (
        <InfiniteCanvas snapSettings={snapSettings}>
          <div className="top-bar">
            <span className="top-bar-title">Bazalt</span>
            <div className="top-bar-macros">
              {MACROS.map((macro) => (
                <MacroSlider key={macro.relayName} {...macro} />
              ))}
            </div>
            <label className="top-bar-snap">
              <input
                type="checkbox"
                checked={snapSettings.enabled}
                onChange={(e) => setSnapSettings((s) => ({ ...s, enabled: e.target.checked }))}
              />
              Snap to grid
            </label>
            <button className="top-bar-stress-test" onClick={() => setStressTestActive(true)}>
              Run stress test (M8)
            </button>
          </div>
        </InfiniteCanvas>
      )}
      {stressTestActive && (
        <div className="stress-test-stats">
          {stressStats.fps} fps · {stressStats.subscribedTaps} taps subscribed
        </div>
      )}
      {!stressTestActive && <AnalysisPanel />}
    </div>
  )
}

export default App
