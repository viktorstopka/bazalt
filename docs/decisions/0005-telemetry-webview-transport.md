# 0005 — Binary telemetry transport via WebView resource provider

## Status
Proposed — numbers to be measured and recorded here during M4 before this is "Accepted."

## Context
The brief requires avoiding JSON for high-rate visualization data and measuring/documenting
latency and throughput. JUCE's `WebBrowserComponent` (rewritten in JUCE 8, carried into 9) supports
a resource-provider hook: the embedded browser can `fetch()` a custom-scheme URL and C++ answers
with raw bytes and a MIME type, with no serialization step required on either side.

## Decision (pending measurement)
The UI's `requestAnimationFrame` loop issues a `fetch()` per active tap (e.g.
`bazalt-tap://scope/main`, `bazalt-tap://spectrum/aux1`) each frame; the C++ resource-provider
handler returns the latest published `TelemetryFrame` for that tap as `application/octet-stream`;
JS reads it via `ArrayBuffer`/`DataView`/`Float32Array`. This is pull-based and best-effort-fresh:
a fetch always returns immediately with whatever the analysis thread last published, never blocking
on the engine.

## What M4 needs to measure before this is final
- Round-trip latency of a single `fetch()` to the resource provider, measured from the UI side.
- Sustained throughput/frame rate with all planned taps active simultaneously (main out + 4
  sidechains × {scope, spectrum, meter} = 15 taps).
- Whether per-tap `fetch()` calls scale acceptably or whether a single combined
  "poll all active taps" endpoint is needed to cut per-call overhead.

## Consequences (provisional)
- Visualization freshness is bounded-stale, not sample-accurate-synced — acceptable for scopes/
  spectra/meters, explicitly not acceptable if telemetry were ever repurposed for something
  time-critical (it shouldn't be).
- If measured throughput doesn't hold up with 15 simultaneous taps, the fallback is the combined-
  endpoint variant above; that's a transport-layer change only, not a change to the tap/ring-buffer/
  analysis-thread design in ARCHITECTURE.md §6.1–6.2.
