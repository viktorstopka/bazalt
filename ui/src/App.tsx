import { useState } from 'react'
import { InfiniteCanvas, type SnapSettings } from './canvas/InfiniteCanvas'
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

  return (
    <div id="app-root">
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
        </div>
      </InfiniteCanvas>
      <AnalysisPanel />
    </div>
  )
}

export default App
