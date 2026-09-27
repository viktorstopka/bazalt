# 0009 — Dynamic telemetry: a fixed, LRU-evicting tap pool, not a growing map

## Status
Accepted (M8).

## Context
M4's `TelemetryHub` held a fixed set of 5 taps (main + 4 sidechains), created once in
`prepareToPlay` and never changed. NODE_EDITOR.md §9 asks for "many small, dynamic taps: per-node
previews, per-connection activity, per-parameter current values... subscribed on demand, driven by
what is visible in the viewport... unsubscribed when off-screen, so cost scales with what the user
sees, not with patch size" — a fundamentally different shape: taps that come and go constantly, tied
to UI state, potentially far more of them than 5.

## Decision
`TelemetryHub` preallocates a **fixed pool of 64 slots** at `prepare()` (`maxTaps`,
`engine/include/bazalt/engine/telemetry/TelemetryHub.h`) — not an `unordered_map` grown at runtime.
`subscribeTap(name)`/`unsubscribeTap(name)` (message-thread only) claim/release slots by name,
LRU-evicting the least-recently-subscribed active slot once the pool is full rather than growing it.
A slot's `Tap*` is stable for the life of the hub, even across being reused for a different name
later — whoever pushes into it caches that pointer once (M4's `tapPointers[]` pattern), never
re-resolves by name. The analysis thread never looks up by name at all: it iterates fixed slot
indices 0..63, reading only an atomic `active` flag cross-thread — this is what makes the whole
thing safe without needing `juce::String` to be readable across threads.

`AnalysisThread` also gained a **per-cycle processing budget** (5ms default,
`AnalysisThread::maxProcessingMsPerCycle`): round-robins the active slot range, and if a cycle would
exceed budget, defers the remainder to the next cycle rather than either blocking longer or dropping
a tap's frame outright — NODE_EDITOR.md §3's "degrade gracefully... rather than dropping frames".
Proven deterministically with a 0ms test budget forcing exactly one slot per cycle
(`tests/AnalysisThreadBudgetTests.cpp`): every subscribed tap still gets a published frame within a
few cycles, none starve.

**Two real gaps this surfaced, both accepted for now:**
- Reusing an evicted slot for a new name while an old pusher hasn't retired yet can cause a few
  stray cross-talk samples until that old pusher stops (documented in `TelemetryHub.h`). Telemetry
  is already bounded-stale/best-effort (ADR-0005); a brief one-frame glitch on reuse doesn't change
  that contract. Revisit if M11's real per-node wiring makes this worse in practice than it is today
  (nothing pushes into arbitrary node taps yet — only the M4 baseline taps and M8's synthetic demo
  taps exist as real pushers so far).
- Nothing generic in the engine "pushes" into a dynamically-subscribed tap unless something already
  knows to (M4's baseline taps: the audio thread; M8's `StressTestCanvas.tsx` demo taps: a synthetic
  generator, see below) — M8 builds the *capacity*, M11 is what wires real per-node/per-connection
  signals into it.

## Measured (M8's rendering spike, ADR-0008, ties the two together)
Up to **6 taps concurrently subscribed and actively pulsing** during the WebGL stress test at 60 fps
with no observed telemetry-related stutter — comfortably under the 64-slot cap, consistent with
"cost scales with what's visible," since only cables inside the current viewport (bounded by a
60-tap headroom under the 64-slot cap, leaving room for the 5 permanent baseline taps) ever
subscribe.

## Synthetic ("demo.") taps
`StressTestCanvas.tsx`'s cables aren't real engine signals (ADR-0008), so nothing would ever push
data into their subscribed taps. `TelemetryHub::subscribeTap()` marks any `"demo."`-prefixed name as
synthetic (an atomic flag, decided at subscribe time so the analysis thread still never needs
`name`); `AnalysisThread::processTap()` generates a simple per-slot waveform for those slots itself
before reading it back — single-threaded write-then-read on the analysis thread, no cross-thread
concern. This is scaffolding for the rendering spike, not a real feature — delete it alongside
`StressTestCanvas.tsx` once M9+ needs telemetry only for real graph content.

## Consequences
- Any future "subscribe a tap for this UI-visible thing" code (M11) uses this exact API — no new
  telemetry-subscription mechanism should be invented alongside it.
- The 64-slot cap is a real, enforced ceiling, not aspirational — a viewport that would want more
  than 64 simultaneously-visible telemetry-bearing elements (unlikely at any zoom level someone
  could actually read individual previews at) starts evicting the least-recently-touched ones. Worth
  revisiting only if M11's real usage patterns show 64 is actually tight.
