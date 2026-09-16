import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'
import { applyTokensToCss } from './theme/tokens'
import { startTelemetryPolling } from './telemetry/telemetryClient'

applyTokensToCss()
startTelemetryPolling()

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
