# Bazalt — MVP Milestone Plan

Each milestone must build, pass its tests, and be committed before the next starts. "Done" for a
milestone means the exit criteria below, not just "code exists."

## M0 — Skeleton & build

Repo scaffolding: top-level `CMakeLists.txt`, JUCE pinned via `FetchContent` (exact tag, see
ADR-0001), Catch2 via `FetchContent`. Empty `engine` static lib target. Empty JUCE plugin
(`plugin/`) producing VST3 + Standalone, no DSP yet — silence in, silence out. `ui/` scaffolded with
Vite + TypeScript (+ React), placeholder page. `CLAUDE.md` written (RT-safety rules, "sound in
C++" rule, build/test/render commands, naming conventions). CI script for Windows x64 (macOS is
deferred for the MVP — see ARCHITECTURE.md §13 — added in a later milestone).

**Exit criteria**: `cmake --build` succeeds on Windows; VST3 loads and passes `pluginval` at a
basic level with silence; Standalone launches and shows the placeholder WebView page; `ctest` runs
(even with zero tests) cleanly.

## M1 — Engine core & DSP quality infrastructure

`AlignedBuffer`/SoA types, `SmoothedValue`-based parameter smoothing helper, TPT SVF filter,
PolyBLEP band-limited oscillator, `Oversampling` wrapper around `juce::dsp::Oversampling` with
correct latency reporting, denormal protection, debug-build RT-allocation trap, NaN/Inf guard at
output. `tools/render-cli` skeleton: renders a fixed tone to WAV headlessly, proving the
engine-only build path works end to end with no plugin/UI dependency.

**Exit criteria**: Catch2 suite passes — aliasing measurement, SVF frequency response vs.
analytical curve, block-size invariance (1/7/64/512), sample-rate coverage (44.1–192 kHz), NaN/Inf
stress. `render-cli` produces a WAV I can listen to.

## M2 — Graph runtime

`NodeGraph` editable model, `Node` interface (ports/params metadata, prepare/reset/process,
serialization, tap registration hooks), compiler (topological sort, SoA buffer assignment, cycle
detection → per-sample region routing), lock-free `ExecutionPlan` swap + epoch reclamation,
per-voice/global domain split, voice manager (fixed pool, basic stealing, note-ID-keyed for future
unison). Hardcoded MVP graph (note → PolyBLEP osc → SVF → ADSR-gated amp → out) expressed as a
`NodeGraph` and run through the compiler. Karplus-Strong per-sample-region proof (delay + one-pole
filter + excitation, one feedback edge) as a second hardcoded graph.

**Exit criteria**: both hardcoded graphs render correct audio via `render-cli` from a test MIDI
file (I'll tell you what to listen for). Compiler tests pass (topo-sort correctness, cycle →
per-sample routing, invalid-cycle rejection keeps prior plan live). Swap-under-load test shows no
discontinuity above threshold at a mid-stream plan swap.

## M3 — I/O buses, patch format, macros

VST3 bus layout: main stereo in/out + 4 aux stereo sidechain ins, graceful handling of
host-deactivated aux buses. MPE-ready note/voice data model wired to real MIDI input. Patch format
v1 (JSON, schema version, migration dispatcher scaffolded) — plugin state = patch. Macro parameter
pool (default 32, see ARCHITECTURE.md §4.3) mapped to the hardcoded graph's oscillator shape,
filter cutoff/resonance, and envelope; host automation of a macro is audible and reflected in
engine state. Sidechain passthrough + level proof for each of the 4 aux inputs.

**Exit criteria**: `pluginval` passes with all buses, including hosts that don't activate aux
buses. A DAW automation lane on a macro parameter produces audible, click-free change. Patch
save/reload round-trips exactly (bit-identical parameter/macro state).

## M4 — Telemetry pipeline

SPSC tap ring buffers, analysis thread (FFT via `juce::dsp::FFT`, min/max peak decimation, meter
ballistics), binary `TelemetryFrame` format, `WebBrowserComponent` resource-provider transport
spike (measure latency/throughput first, before committing to the final tap count/frame rate —
write up actual numbers in `docs/decisions/0005-telemetry-webview-transport.md`). Taps wired to
main output and all 4 sidechain inputs.

**Exit criteria**: measured, documented latency/throughput numbers for the WebView transport.
Analysis thread never blocks the audio thread (verified by the RT-safety checks from M1 running
concurrently with telemetry active).

## M5 — UI canvas & analysis panel

Infinite node canvas (pan, zoom centered on cursor, dot/line grid, snapping settings) — empty,
no nodes. Analysis panel: oscilloscope, spectrum analyzer, and level meter for main output and each
of the 4 sidechain inputs, Canvas2D/WebGL, 60–120 fps with interpolation between telemetry frames.
Temporary controls (osc shape, filter cutoff/resonance, envelope) bound through the full round trip
(UI → C++ → smoothed DSP, host automation reflected back to the UI). Design-token theme file, one
default theme, no hardcoded colors/sizes in components.

**Exit criteria**: canvas is smooth at high refresh rates under pan/zoom; all 4 sidechain scopes +
spectra + meters update live and truthfully against injected test signals; a host automation move
on a bound parameter visibly updates the corresponding UI control.

## M6 — Testing/tooling hardening

`pluginval` at a strict level scripted into CI. RT-safety checks (allocation trap; RTSan where the
platform supports it) wired into CI, documented where they can't run (see ARCHITECTURE.md §13).
Full pass on `docs/decisions/` — every significant choice made across M0–M5 has an ADR, not just
the ones seeded up front. `docs/ARCHITECTURE.md` updated to match what was actually built, not just
what was proposed here.

**Exit criteria**: CI green on Windows for build + full test suite + `pluginval`. This is the MVP
done for the platforms in scope; macOS build/CI/pluginval and RealtimeSanitizer coverage are a
follow-on milestone once mac access exists (deferred 2026-09-16, see ARCHITECTURE.md §13).
