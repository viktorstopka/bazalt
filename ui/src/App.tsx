import { useState } from 'react'
import './App.css'

// M4 measurement spike: proves out and quantifies the WebView resource-
// provider transport described in docs/decisions/0005-telemetry-webview-
// transport.md. This is not the real UI (that's M5) — it exists only to
// produce the latency/throughput numbers that ADR needs before it can be
// marked "Accepted", using the exact fetch() path the real UI will use.
const RESOURCE_PROVIDER_ROOT = 'https://juce.backend/'
const TAP_NAMES = ['main', 'aux1', 'aux2', 'aux3', 'aux4']
const FRAME_TYPES = ['scope', 'spectrum', 'meter']
const ALL_TAP_URLS = TAP_NAMES.flatMap((tap) =>
  FRAME_TYPES.map((frameType) => `${RESOURCE_PROVIDER_ROOT}tap/${tap}/${frameType}`),
)

function percentile(sorted: number[], p: number): number {
  const idx = Math.min(sorted.length - 1, Math.floor(p * sorted.length))
  return sorted[idx]
}

async function measureLatency(url: string, iterations: number): Promise<{ durations: number[]; lastByteLength: number }> {
  const durations: number[] = []
  let lastByteLength = 0
  for (let i = 0; i < iterations; i++) {
    const start = performance.now()
    const response = await fetch(url)
    const bytes = await response.arrayBuffer()
    durations.push(performance.now() - start)
    lastByteLength = bytes.byteLength
  }
  return { durations, lastByteLength }
}

async function measureThroughput(urls: string[], durationMs: number): Promise<{ rounds: number; fetches: number; elapsedMs: number }> {
  const start = performance.now()
  let rounds = 0
  let fetches = 0
  while (performance.now() - start < durationMs) {
    await Promise.all(urls.map((url) => fetch(url).then((r) => r.arrayBuffer())))
    rounds += 1
    fetches += urls.length
  }
  return { rounds, fetches, elapsedMs: performance.now() - start }
}

function App() {
  const [running, setRunning] = useState(false)
  const [results, setResults] = useState<string>('')

  async function runBenchmark() {
    setRunning(true)
    setResults('Running...')

    const singleTapUrl = ALL_TAP_URLS[0]
    const { durations, lastByteLength } = await measureLatency(singleTapUrl, 200)
    const sorted = [...durations].sort((a, b) => a - b)
    const mean = durations.reduce((a, b) => a + b, 0) / durations.length

    const throughput = await measureThroughput(ALL_TAP_URLS, 3000)

    const lines = [
      `Single-tap round-trip latency (fetch "${singleTapUrl}", n=${durations.length}, payload ${lastByteLength} bytes):`,
      `  min    ${sorted[0].toFixed(3)} ms`,
      `  median ${percentile(sorted, 0.5).toFixed(3)} ms`,
      `  mean   ${mean.toFixed(3)} ms`,
      `  p95    ${percentile(sorted, 0.95).toFixed(3)} ms`,
      `  max    ${sorted[sorted.length - 1].toFixed(3)} ms`,
      '',
      `Sustained throughput (${ALL_TAP_URLS.length} taps polled per round via Promise.all, ${(throughput.elapsedMs / 1000).toFixed(2)}s run):`,
      `  rounds/sec  ${(throughput.rounds / (throughput.elapsedMs / 1000)).toFixed(1)}`,
      `  fetches/sec ${(throughput.fetches / (throughput.elapsedMs / 1000)).toFixed(1)}`,
      `  total rounds ${throughput.rounds}, total fetches ${throughput.fetches}`,
    ]

    setResults(lines.join('\n'))
    setRunning(false)
  }

  return (
    <div id="placeholder">
      <h1>Bazalt</h1>
      <p>UI placeholder — infinite canvas and analysis panel land in M5.</p>
      <button onClick={runBenchmark} disabled={running}>
        {running ? 'Running WebView transport benchmark…' : 'Run WebView transport benchmark'}
      </button>
      <pre style={{ textAlign: 'left', whiteSpace: 'pre-wrap' }}>{results}</pre>
    </div>
  )
}

export default App
