# 0005 — Binary telemetry transport via WebView resource provider

## Status
Accepted — measured during M4 (see Measurements below).

## Context
The brief requires avoiding JSON for high-rate visualization data and measuring/documenting
latency and throughput. JUCE's `WebBrowserComponent` (rewritten in JUCE 8, carried into 9) supports
a resource-provider hook: the embedded browser can `fetch()` a custom-scheme URL and C++ answers
with raw bytes and a MIME type, with no serialization step required on either side.

## Decision
The UI's `requestAnimationFrame` loop issues a `fetch()` per active tap each frame, against
`WebBrowserComponent::getResourceProviderRoot() + "tap/<name>/<scope|spectrum|meter>"` — on Windows
that root is `https://juce.backend/`, so e.g. `https://juce.backend/tap/main/scope`. (§6.3's
`bazalt-tap://scope/main` was illustrative shorthand for this, not a literal scheme: JUCE's
resource provider is one callback per `WebBrowserComponent`, differentiated by URL *path* within a
single virtual origin, not a registered custom URL scheme. In Debug, `Options::withResourceProvider`
is given `allowedOriginIn` set to the Vite dev server's origin so the page — actually loaded from
`localhost:5173` for HMR — can still `fetch()` the resource-provider's origin cross-origin.) The C++
handler (`serveResource`/`serveTap` in `PluginEditor.cpp`) returns the latest published
`TelemetryFrame` for that tap as `application/octet-stream`; JS reads it via
`ArrayBuffer`/`DataView`/`Float32Array`. This is pull-based and best-effort-fresh: a fetch always
returns immediately with whatever the analysis thread last published, never blocking on the engine.

## Measurements
Measured in the Debug Standalone build via a spike benchmark (`ui/src/App.tsx`, a temporary
"Run WebView transport benchmark" button — to be removed once M5 replaces the placeholder UI),
driving the exact `fetch()` path above through the real resource-provider handler, with the audio
device running so taps were actively fed:

- **Single-tap round-trip latency** (`fetch("https://juce.backend/tap/main/scope")`, n=200,
  payload 1056 bytes = 32-byte header + 128 buckets × 2 floats):
  min 0.800 ms, median 1.000 ms, mean 1.183 ms, p95 1.700 ms, max 19.500 ms (one outlier, likely a
  scheduling hiccup — everything else clustered tightly under 2 ms).
- **Sustained throughput**, all 15 taps (main + 4 aux sidechains × {scope, spectrum, meter}) polled
  every round via `Promise.all`, 3-second run: 170.8 rounds/sec, 2562.2 fetches/sec (513 rounds /
  7695 fetches total).

A 60fps UI polling all 15 taps every frame needs only ~900 fetches/sec — well within the measured
~2562 fetches/sec ceiling, with round-trip latency an order of magnitude below a 16.7ms frame
budget. Per-tap `fetch()` calls scale acceptably; the combined-endpoint fallback isn't needed.

## Consequences
- Visualization freshness is bounded-stale, not sample-accurate-synced — acceptable for scopes/
  spectra/meters, explicitly not acceptable if telemetry were ever repurposed for something
  time-critical (it shouldn't be).
- Per-tap fetches are cheap enough that M5's real UI can poll every active tap independently, at UI
  frame rate, without a combined "poll all taps" endpoint. That's a design decision closed by
  measurement, not just deferred.
