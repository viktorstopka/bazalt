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

---

# Node editor phase (M7+)

Planned per `docs/bazalt_node_editor_prompt_v3.md` and grounded in `docs/NODE_EDITOR.md` — read that
document for the design behind every milestone below; it also lists the conflicts/open questions
these milestones resolve. Starts after M6, per the blueprint's own stated sequencing.

## M7 — Domain-aware graph model + command bridge (no visuals)

Extend `engine/`: `SignalType::Boolean`; `PortDescriptor` numeric/primary-output metadata;
`NodeInstance.position` + `properties` (`var`-typed) bag; port-ID-addressed `Connection` (patch
schema v2 migration in `PatchSerializer`); `Node` title/category/layout-variant/icon virtuals
(retrofit the 8 existing M1/M2 nodes); `NodeFactory::describeAll()`. New builtin node types:
`util.voiceSum` (the domain boundary, NODE_EDITOR.md §7), `util.constant`, `util.reroute`,
`util.map`, `util.add`, `util.multiply`, `util.listen`, `util.output`. `GraphCompiler` gains a
domain-partitioning pass (reachability from `util.voiceSum`) replacing today's fully-hardcoded
voice/global split. Wire one `PlanSwapper` per voice plus one for the global plan into
`PluginProcessor`, so a recompile can actually happen after `prepareToPlay` for the first time. New
`plugin/source/GraphEditController` implementing the command list (NODE_EDITOR.md §6) over
`withNativeFunction`/`withEventListener`/`emitEvent`. No canvas/rendering work in this milestone —
commands are driven by a test harness (C++ or a minimal scripted JS client), not real UI.

**Exit criteria**: a scripted sequence of commands (add node, connect, set parameter, delete,
disconnect) against a running `BazaltAudioProcessor` produces the expected compiled audio, verified
the same way M2's proof graphs were (render-cli-style, or a `tests-plugin` equivalent driving
`GraphEditController` directly). A live edit mid-playback swaps in glitch-free (reuses M2's
swap-under-load methodology, now via the real command path instead of a test-only plan swap). An
intentionally-invalid command (e.g. a cycle through a node that can't run per-sample) is rejected,
leaves the prior plan live, and the command's result reports the rejection. Patch save/reload
round-trips schema-v2 fields (position, properties, ID-addressed connections) exactly.

## M8 — Dynamic telemetry + rendering-split benchmark

Extend `TelemetryHub` with `subscribeTap`/`unsubscribeTap` (capped pool, LRU eviction,
NODE_EDITOR.md §9), exposed as commands. Per-frame telemetry budget in `AnalysisThread` (degrade via
drain rate / decimation before ever dropping a tap). Stress-test patch generator (procedurally
builds an N-node/M-cable `NodeGraph` via the M7 command path). Rendering-split spike: a minimal
WebGL canvas (grid + cables + a handful of node-body rectangles, no real interactivity yet) proving
or revising NODE_EDITOR.md §10's WebGL/DOM hypothesis, with an FPS/telemetry overlay.

**Exit criteria**: 500-node/1000-cable stress patch pans/zooms at the display refresh rate (target
120 fps) with the spike's placeholder cable rendering and live (dynamic-tap) activity on every
visible cable; documented FPS and per-frame telemetry throughput numbers; `docs/decisions/
0008-graph-rendering-split.md` and `0009-dynamic-telemetry-subscription.md` written up with real
measurements, not proposals.

## M9 — Descriptor schema end-to-end + component gallery

Full `NodeDescriptor` JSON handed to the UI once at editor load (NODE_EDITOR.md §3) covering all 8
real node types + the M7 utility nodes, plus UI-only mock descriptors for the blueprint's
demonstration needs (marked as mocks). Design-token theme wiring shared between the M5 token file
and the WebGL renderer (`ARCHITECTURE.md` §7's "one place a theme is defined"). Dev-only component
gallery route rendering every node layout variant (standard/horizontal/singleton/decoration), every
port type/glyph, and every control state (default/hover/selected/connected/bypassed/listening/
error) from descriptors alone — no live graph yet.

**Exit criteria**: gallery visually matches `docs/Frame 1 Bazalt.png`'s tokens (colour, glyph,
stroke, spacing) for every variant/state; visual-regression screenshots captured as the baseline for
later milestones; `docs/decisions/0007-node-descriptor-schema.md` written.

## M10 — Canvas navigation, placement, wiring

Builds on M5's empty canvas + M8's proven rendering split: pan (right-drag, space+left-drag),
zoom-on-cursor, box-select (partial-touch mode), fit-view on patch load. Add menu (Shift+A /
right-click, search + category groups from M9's descriptors, keyboard nav, auto-flip on-screen).
Ghost placement (click-to-place, splice-on-wire-hover with highlight, Escape-cancel). Drag-to-wire
with the NODE_EDITOR.md §10 valid/rejected/hover feedback (type-colour-based, no new hues). Wire
drag-off-to-delete, drag-onto-new-port-to-reconnect. Node move/delete/rename/bypass, right-click
context menu. `docs/decisions/0006-command-bridge-transport.md` and
`0010-wire-feedback-colours.md` written.

**Exit criteria**: a patch resembling a subset of `Frame 1 Bazalt.png` (e.g. MIDI Note → SVF →
Master Out) can be built and edited purely via mouse/keyboard against the real engine; save/reload
round-trips positions and wiring exactly; wire hit-testing/hover/selected states match
NODE_EDITOR.md §10.

## M11 — Live visualization in the graph

Cable activity animation (source-type-coloured, dynamic-tap-driven per NODE_EDITOR.md §9). Inline
node previews: waveform, stepped values, envelope with playhead, LFO phase. Output value indicators
on horizontal nodes. Live value readout on a cable-connected parameter control (replacing the
inline slider per blueprint §4). Interpolation between telemetry frames for smooth motion at display
refresh independent of telemetry production rate (`ARCHITECTURE.md` §6.4, now inside real nodes
instead of the M5 analysis panel).

**Exit criteria**: the M8 stress patch, now rendered with real node bodies and live previews (not
placeholders), still hits the 120 fps target with live telemetry on every visible element — the
actual performance validation the blueprint asks for, now proven against the real UI rather than the
M8 spike.

## M12 — Node-type-specific interaction polish

Singleton auto-merge chains (snap/pull-out/insert-between rules, defined and implemented).
Horizontal node layout. Macro node + Constraints popover (Type/Shape/Enum, NODE_EDITOR.md §12 item
10's host-automation-warning behaviour). Listen node + Ctrl/Cmd-click solo-listen (temporary Listen
node via `util.listen`, Ctrl/Cmd-click on Output clears all). Alt-drag Mix/Add/Multiply spawning
(`util.add`/`util.multiply`/`mix.add2`, mixed/incompatible-type behaviour defined). Growable ports
(Mix/Math-style, capped, never below 2). Unwrap (placeholder→real nodes, plain-slider→
`util.constant`) as one undo step. Parameter slider Blender-style behaviour (bounds fill, integer
clamping, horizontal+vertical drag, Shift fine mode, click-to-type, double-click reset, log/linear
from metadata). Decorations: Knob/`util.reroute` (auto-colour from source once connected), Frame
(resize/rename, nodes move with it), Header, Image (resizable/rotatable, drag-drop/paste, embedded
compressed in the patch with a size limit). Computer-keyboard piano + WebView focus-handling
investigation (document where it's available: Standalone by default, plugin TBD per findings).

**Exit criteria**: every interaction in blueprint §6 is demoable; a demo patch recreating
`docs/Frame 1 Bazalt.png` is built, saved, and reload-verified.

## M13 — Assist menu, shell polish, full test/doc pass

Assist menu (`+` button): recipes (declarative node+connection insertion relative to the source
node, 1–2 real examples, one undo step, brief highlight on insert) and port-options exposure. Error
banner (compile/runtime/rejected-command, dismissible). Status bar (active voice count, engine CPU
load replacing the prototype's "Elementary Audio" label, persistent Shift+A hint). Full test pass
per blueprint §8.5: command/undo-coalescing/serialization-round-trip (incl. images)/chaining/Unwrap/
splice/auto-Map/Alt-drag/growable-ports/slider-behaviour unit tests; visual-regression suite
finalized against the M9 gallery baseline; engine tests for bypass, Listen taps, and plan swaps
triggered by edits under load. `docs/NODE_EDITOR.md`, all five new ADRs, and `CLAUDE.md` updated to
match what was actually built, mirroring M6's "docs match reality" pass.

**Exit criteria**: every item in blueprint §8's deliverables list is checked off; `pluginval` strict
level still passes with the node editor active; CI green.
