# Bazalt — Architecture Proposal (v0.1, pre-implementation)

Status: **proposal, awaiting approval**. No code has been written against this document yet.

This document describes the architecture for the Bazalt MVP and the foundation it leaves for the
full product. It is organized around the non-negotiables in the kickoff brief. Where a decision is
genuinely open (cost, legal, or preference — not something I can resolve from the spec), it is
called out in §13 rather than silently decided.

---

## 1. Repository layout

```
bazalt/
  engine/           pure C++ DSP + graph runtime library (no plugin/UI dependency)
  plugin/           JUCE AudioProcessor wrapper: buses, state, WebView bridge
  ui/               TypeScript + Vite (+ React) frontend
  tools/            offline render CLI, other headless utilities
  tests/            Catch2 test suites (engine-focused)
  themes/           design-token theme files (JSON/CSS custom properties)
  docs/
    ARCHITECTURE.md
    MILESTONES.md
    decisions/      one ADR per significant choice, numbered
  CLAUDE.md         project rules for future sessions (written in M0)
  CMakeLists.txt    top-level, ties engine/plugin/tools/tests together
```

`engine/` is a static library target with its own `CMakeLists.txt` and can be built and tested
completely headlessly (`tools/render-cli` links only `engine`, never `plugin`). `plugin/` links
`engine` plus JUCE's plugin-client modules and owns everything host-facing.

**Dependency rule, enforced by CMake target design, not convention:** `engine` may depend on
`juce_core`, `juce_audio_basics`, and `juce_dsp` (data structures — `AudioBuffer`, `MidiBuffer`,
`SmoothedValue`, FFT, oversampling — not framework glue). It must never depend on
`juce_audio_processors`, `juce_gui_basics`, or anything plugin/UI-shaped. This is what makes
"headless and testable" true rather than aspirational.

---

## 2. Threading model

Five thread roles, each with a single owner and a documented handoff mechanism. This is the spine
everything else hangs off.

| Thread | Owns | Never does |
|---|---|---|
| **Audio thread** (host RT thread) | Running the current `ExecutionPlan`; per-voice/global DSP state; writing telemetry into tap ring buffers; reading smoothed parameter/macro values | Allocate, lock, log, throw, block, delete an `ExecutionPlan` |
| **Message thread** (JUCE) | Editor lifecycle, WebView bridge, parameter changes arriving from host/UI, triggering graph edits, patch load/save | Touch DSP state directly, block on the audio thread |
| **Graph compiler thread** (worker, spun up by plugin) | Turning `NodeGraph` edits into a new `ExecutionPlan`; all allocation for the new plan | Touch the currently-live plan's memory while the audio thread may still read it |
| **Analysis thread** (worker) | Draining tap ring buffers; FFT, peak decimation, envelope snapshots; producing binary telemetry frames | Block the audio thread; allocate per-frame (preallocated scratch buffers, reused) |
| **WebView renderer thread** (Chromium/WebKit-owned, outside our control) | rAF-driven Canvas2D/WebGL rendering, polling telemetry frames, React chrome | Any DSP; anything audio-thread-adjacent |

Data flows in one direction per concern, never round-trips synchronously:

```
Host MIDI/audio + params ──▶ Audio thread ──▶ tap ring buffers ──▶ Analysis thread
                                  ▲                                       │
                          ExecutionPlan                          binary frames
                          (atomic, epoch-                                │
                           reclaimed)                                    ▼
                                  │                              WebView (pull via
NodeGraph edits ──▶ Compiler thread                              resource-provider
(from UI intents,                                                 fetch, rAF loop)
 message thread)
```

No mutex ever sits between the audio thread and any other thread. The two cross-thread handoffs
(plan swap, telemetry frames) are both single-writer/single-reader and lock-free by construction
(§3.2, §6.1) — that's deliberate; it means we never need a general-purpose lock-free queue library,
just two small, provably-correct SPSC mechanisms.

---

## 3. Graph runtime

### 3.1 Model vs. execution plan

- **`NodeGraph`** — the editable representation: nodes (stable string ID, type, parameter values,
  UI position), ports, connections. Lives on the message thread. Mutating it never touches audio.
- **`ExecutionPlan`** — an immutable, flat, cache-friendly compiled artifact: topological node
  order, SoA buffer assignments, per-voice/global partition, per-sample region descriptors
  (§3.4). Built once per edit by the compiler thread, then published.

The MVP does not ship a node-graph *editor* (per the brief), but it does ship a real `NodeGraph` →
`ExecutionPlan` pipeline: the hardcoded demo graph is expressed as a `NodeGraph` and compiled
through the same path a future UI-driven edit would use. This is the point of building the runtime
now — there is no shortcut version to later throw away.

### 3.2 Lock-free swap and reclamation

A fixed pool of plan slots (4 for MVP) is preallocated on the compiler thread's own memory. Each
`ExecutionPlan` carries a monotonically increasing `generation`.

- Compiler thread builds a new plan into a free slot, then
  `currentPlan.store(newPlanPtr, memory_order_release)`.
- Audio thread loads the plan pointer **once per `processBlock` call** — never mid-block — and
  runs the entire block against it, then
  `audioThreadEpoch.store(plan->generation, memory_order_release)`.
- A reclaimer (message-thread timer, ~50 ms) frees any slot whose generation is older than both
  the current plan and `audioThreadEpoch`, meaning the audio thread has provably moved past it.

This is single-reader/single-writer epoch-based reclamation — simpler than general hazard-pointer
schemes because there is exactly one audio-thread reader. Consequence, stated explicitly: **graph
edits take effect at the next block boundary**, never inside one. That's what makes "graph edits
must never glitch audio" true by construction rather than by care.

**Voice-state continuity across recompiles** (needed once live editing exists): per-voice DSP state
(filter memory, envelope stage, delay-line contents) is *not* embedded in the `ExecutionPlan`. It
lives in a separate pool keyed by `(voiceIndex, nodeID)`. A recompile that keeps a node's ID keeps
its state; a new node gets fresh state; a removed node's state is retired with the old plan. This
is what prevents "add an unrelated node" from resetting every filter's memory. MVP's hardcoded
graph doesn't need this in practice, but the plan/state split has to exist from the first line of
compiler code or it becomes an unshippable rewrite later.

### 3.3 Signal types

`Audio`, `Control` (same buffer shape as audio — control/modulation may run at full sample rate,
e.g. FM/PM — but distinct type tag so the compiler and UI can reason about range/units), `Event`
(sample-accurate discrete: note on/off, gate, param changes — offset + payload, same shape as
`juce::MidiBuffer`), `Note` (MPE-ready per-voice stream: note ID, channel, initial velocity,
continuous pitch bend / pressure / timbre CC74 as `Control` sub-streams). A `Spectral` tag is
reserved in the enum now, unimplemented, for future frequency-domain nodes.

### 3.4 Per-voice / global domains and feedback

- **Per-voice graph**: instantiated once per active voice (bounded by max polyphony, allocated in
  `prepare`); has independent state. Typical MVP path: note event → oscillator → SVF → ADSR-gated
  amp.
- **Global graph**: runs once per block after the voice mixer sums per-voice outputs. Handles
  master processing, global LFOs, sidechain taps.
- **Crossing the boundary**: voice→global is the implicit voice-sum. Global→voice (e.g. a global
  LFO modulating every voice's filter) is allowed: every per-voice graph instance reads the same
  global `Control` buffer for that block, computed once. This mirrors Phase Plant's
  generators/lanes split named in the brief.

**Single-sample feedback.** The compiler detects cycles in a domain's dependency graph. A node set
forming a cycle cannot be scheduled as ordinary whole-block ops (there's no valid topological
order); instead the compiler groups it into a **per-sample region** — a sub-graph compiled into a
sample loop and wrapped as one opaque node in the outer block-rate schedule. Inside the region,
participating nodes are called via `processSample(Frame&)` instead of `processBlock`; the
cycle-breaking edge implicitly holds one sample of delay (this is the physically correct behavior
for e.g. a Karplus-Strong loop, not a runtime artifact — worth being explicit about in the UI later
so users don't read it as latency to "fix"). A node that only implements `processBlock` cannot
legally sit inside a cycle; if the compiler finds one there, the compile is rejected and the
**previous valid plan stays live** — a bad edit never reaches the audio thread.

**MVP proof**: a tiny Karplus-Strong string (delay line → one-pole damping filter → excitation mix,
one feedback edge) built from generic graph primitives (`Delay`, `OnePoleFilter`, `Mix`), compiled
into a per-sample region, rendered to WAV via the CLI. This is the concrete test that the mechanism
is real and not just a diagram.

### 3.5 Voice management

Fixed voice pool sized at `prepare()` (no runtime allocation). Stealing policy for MVP: steal the
oldest voice in release stage, falling back to oldest overall — simple, deterministic, revisit once
there's a reason to. Each voice carries a note ID (not just a MIDI channel/number pair) so a future
unison mode can map one triggered note to N render voices without changing the voice-state model —
the mapping is `NoteID → [voiceIndex...]` from day one, even though MVP always maps 1:1.

### 3.6 Node interface

Every node implements: declared ports (typed per §3.3) and parameters (id, range, skew, unit,
default, display string — enough for the UI to build a control without knowing the DSP);
`prepare(sampleRate, maxBlockSize, busLayout) / reset() / process(...)`; state
serialization (get/set, used by patch save/load); and tap registration (§6) for default preview
points. UI-facing metadata and DSP implementation are fully decoupled: the UI can describe a node
generically from its metadata alone, which is what makes third-party/user node packs conceivable
later without a UI rewrite.

**Stable identifiers.** Node type IDs, parameter IDs, and port IDs are hand-assigned strings
(`"osc.basic"`, `"filter.svf.cutoff"`), never array indices or enum values, and never renamed once
shipped. This is the one rule that makes old patches survive refactors; violating it silently is
the single most expensive mistake to make early, so it's called out here rather than assumed.

---

## 4. Host integration

### 4.1 Bus layout

Main stereo input (so Bazalt works as an effect), four auxiliary stereo sidechain input buses
(separate VST3 buses, independently host-activatable), main stereo output. Buses are described
generically enough (`N optional aux stereo inputs`) that adding outputs later doesn't change the
shape of the code. Any aux bus the host hasn't activated is treated as present-but-silent inside
the engine (a zeroed scratch buffer is substituted) rather than special-cased per call site — nodes
downstream never need to know whether a bus is "real."

### 4.2 MPE-ready note data

The note/voice model carries per-note pitch bend, pressure, and timbre (CC74) as continuous
`Control` streams from day one, whether or not MPE input mode is enabled. MVP wires standard MIDI
in (channel, note, velocity, channel-wide pitch bend/CC) into this same model with per-note streams
simply following the channel-wide value; turning on true MPE later is a routing change in the input
adapter, not a data-model change.

### 4.3 Host automation vs. a dynamic graph

**Decision**: a fixed pool of N host-exposed macro parameters (plain `AudioProcessorParameter`s,
`Macro 1..N`, N = 32 for MVP — arbitrary, cheap to change, see §13). Each macro optionally maps to
one or more node-parameter targets (target = stable param ID + range + curve), and the mapping
table is part of the patch. Node parameters with no macro mapping are still fully saved/restored in
the patch, just not host-automatable.

This is the same shape Kilohearts snap-ins and most modular-graph plugins use, for the same reason:
VST3 does technically support restructuring a host's parameter list at runtime
(`restartComponent` with parameter-list-changed flags), but host support for *that* is inconsistent
enough that depending on it for the MVP would be building quality infrastructure on a foundation
that isn't there yet. Fixed macros work identically in every host, today. Revisiting per-host
dynamic parameters later (once we know which hosts we actually care about) is a plugin-layer change
only — it doesn't touch the engine.

### 4.4 Patch format

Versioned JSON: `schemaVersion`, `graph` (nodes with stable IDs/types/params/positions,
connections), `macros` (mapping table), `ui` (view state — pan/zoom, any node-editor layout once it
exists), `meta` (name/author/timestamps). A migration registry runs `vN → vN+1` functions in
sequence on load; for MVP there is exactly one version, but the dispatcher exists from the start so
"add a migration" is a five-minute task forever, not a project. **Plugin state = patch**, no
separate serialization format for `getStateInformation`.

---

## 5. DSP quality infrastructure

- **Oscillators**: PolyBLEP band-limited classic waveshapes for MVP. Interface separates phase
  accumulation (double precision — see precision policy below) from waveform generation, so a
  mipmapped band-limited wavetable implementation slots in later behind the same port/parameter
  contract without touching callers.
- **Oversampling**: a reusable stage wrapping `juce::dsp::Oversampling` (polyphase IIR/FIR
  halfband), 2x/4x/8x selectable, latency read via `getLatencyInSamples()` and reported to the host
  via `setLatencySamples()`. Not exercised by the MVP's hardcoded graph's audio path, but built and
  unit-tested now so any future nonlinear node can wrap itself in it trivially.
- **Filters**: TPT (topology-preserving transform / Zavalishin) SVF as the first filter — solves
  its own zero-delay feedback internally, stays stable under audio-rate cutoff/resonance
  modulation. All future filter topologies follow the same TPT discretization approach.
- **Smoothing**: every parameter and every macro-mapped target is smoothed (`juce::SmoothedValue`
  or an equivalent per-sample ramp) before it reaches DSP. No raw parameter value is ever read
  directly inside a `process` call.
- **Sample-accurate events**: `processBlock` splits into sub-blocks at each MIDI/parameter event's
  sample offset, so a note-on or automation point always lands on the exact sample, never quantized
  to a block boundary.
- **Block-size / sample-rate invariance**: no node algorithm may depend on block size for its math
  (only for how much work it does per call) — verified by the block-size-invariance test (§9).
  Sample-rate-dependent constants (filter coefficients, envelope times) are derived in `prepare`,
  never hardcoded.
- **Precision**: float32 for the signal path throughout. Double precision specifically for phase
  accumulators and any long-running timers/counters where float32 error would accumulate audibly
  over minutes of playback. This split is documented per-use, not applied blindly.
- **Real-time safety, enforced not just intended**: preallocate everything in `prepare`; a
  debug-build allocation trap (overridden `operator new/delete` with a thread-local "on audio
  thread" flag that asserts on entry) catches accidental allocation; `juce::ScopedNoDenormals` (or
  manual FTZ/DAZ) per block; a NaN/Inf guard at the final output stage that replaces bad samples
  with silence and raises a (non-blocking, lock-free) fault flag the UI can surface, rather than
  ever sending NaN to the host's speakers.
- **SIMD-friendly layout**: structure-of-arrays, aligned buffers (`juce::dsp::AudioBlock` or a thin
  aligned-allocation wrapper) from the start. No SIMD kernels in the MVP; the data layout is chosen
  so adding them later is a local change, not a restructure.

---

## 6. Telemetry and visualization pipeline

### 6.1 Taps

A **tap** is a named, preallocated SPSC ring buffer (float32, power-of-two capacity) that any point
in the graph can write raw samples into from the audio thread. Overflow policy is
overwrite-oldest — visualization tolerates dropped samples, never tolerates the audio thread
blocking. The same tap mechanism serves both a node's own default preview point and a future
dedicated preview node (scope/spectrum/goniometer/meter) wired anywhere in the graph — there is
only one tap API, not two.

### 6.2 Analysis thread

A single non-RT worker drains active taps and produces compact binary **telemetry frames**:
oscilloscope min/max decimation pairs (resolution-independent of zoom level), windowed FFT
magnitude spectrum (Hann, 2048-point for MVP, with display-side smoothing/averaging), and
meter/envelope snapshots with proper ballistics. Each frame: a small header (tap ID, frame type,
sample rate, sequence number, payload length) plus a raw float32 payload. Frames for a given tap
are published into a small ring (3 slots) via the same atomic-index-swap pattern as §3.2, so the
consumer never reads a half-written frame — no mutex here either.

### 6.3 Transport to the WebView

Binary, not JSON, per the brief. The UI's rAF loop issues a `fetch()` to a custom scheme handled by
`WebBrowserComponent`'s resource provider (e.g. `bazalt-tap://scope/main`); the C++ handler returns
the latest published frame for that tap as `application/octet-stream`, and JS reads it directly via
`ArrayBuffer`/`DataView`/`Float32Array` — no serialization step on either side. This is pull-based
from the UI, which is what keeps the guarantee bidirectional: the engine never blocks on the UI
(it just publishes into a ring and moves on), and the UI never blocks on the engine (a fetch always
returns immediately with whatever is latest, at the cost of *bounded staleness* rather than
sample-accurate sync — acceptable for visualization, and the actual latency/throughput numbers get
measured and written up in an ADR during M4, not assumed).

### 6.4 Rendering

All scope/spectrum/meter rendering is Canvas2D/WebGL, driven by `requestAnimationFrame`, with
interpolation between telemetry frames to stay smooth at 60–120 Hz display refresh independent of
the (lower, and non-fixed) telemetry production rate. This code has no dependency on React state —
it reads directly from the latest fetched frame buffer each rAF tick.

---

## 7. UI architecture

React is used for layout/chrome (panels, controls, future node-editor scaffolding) — anything that
changes at UI-interaction rate, not audio rate. The infinite canvas grid, and every scope/spectrum/
meter, is a Canvas2D/WebGL surface owned outside React's render cycle, driven by its own rAF loop,
reading from a plain (non-React-state) store that the telemetry and parameter-bridge code writes
into. This is the concrete mechanism behind "high-rate rendering must run outside the framework's
render cycle" — React never re-renders because a telemetry frame arrived.

**Theming**: design tokens (CSS custom properties) loaded from theme files; components and
canvas/WebGL renderers both read the same token set (the canvas code reads computed-style values or
a parallel JS token object generated from the same source file, TBD in M5) so there is exactly one
place a theme is defined.

**Dev/release asset loading**: debug builds point `WebBrowserComponent` at the Vite dev server
(hot reload); release builds embed the built `ui/dist` assets via JUCE's binary-resource/embedded
provider. This switch is a build-type check, not a runtime flag.

---

## 8. Testing and tooling

- **Catch2** in `tests/`, linked against `engine` only: aliasing measurement (render a high-pitch
  saw, FFT, assert alias energy under a threshold), SVF frequency response vs. the analytical
  curve, block-size invariance (render at sizes 1, 7, 64, 512, compare to a tight tolerance),
  sample-rate coverage (44.1 kHz–192 kHz), NaN/Inf stress (extreme parameters + modulation),
  compiler correctness (topological order, cycle → per-sample-region routing, invalid-cycle
  rejection), and a plan-swap-under-load test (replace the plan mid-stream, assert no discontinuity
  above a threshold at the seam).
- **RT-safety**: the debug allocation trap from §5, run as part of the normal debug test suite. It
  is the primary RT-safety net for MVP CI, since RealtimeSanitizer's practical support is
  Clang/macOS-first and macOS CI is deferred (see below) — revisit RTSan once macOS CI exists.
- **Offline render CLI** (`tools/render-cli`): patch + MIDI file → WAV, headless, deterministic.
  This is the primary way to *listen to* a change; it exists starting in M1.
- **pluginval**, strict level, scripted.
- CI build scripts for Windows x64 through the MVP. **macOS (universal binary) is deferred**
  (decided 2026-09-16, see §13): the architecture and CMake structure make Windows/macOS symmetric
  by design (no platform-specific code paths in `engine/`), but macOS build/CI/`pluginval` are not
  exercised until a later milestone, once mac access exists. Treat every macOS-specific claim in
  this document as "designed for, not yet verified" until then.

---

## 9. Extensibility already accounted for

- **CLAP/AU later**: the plugin wrapper is a thin adapter over a format-agnostic `GraphEngine`
  interface (`prepare/process/getState/setState`). JUCE's `AudioProcessor` is itself one such
  adapter; a CLAP wrapper is another adapter over the same `GraphEngine`, not a parallel engine.
- **Unison later**: voice model already keys by note ID → list of render voices (§3.5).
- **Spectral signals later**: enum slot reserved now (§3.3), unimplemented.
- **Third-party/user node packs later**: node metadata/implementation decoupling (§3.6) plus
  stable string IDs are the two preconditions; nothing else needs to change to make a node type a
  loadable unit later.

---

## 10. Architecture decision records

Short ADRs live in `docs/decisions/`, one per significant choice, written as the choice is made
rather than retrofitted. Seeded now, before any code:

- `0001-juce-version-and-license.md`
- `0002-engine-juce-module-boundary.md`
- `0003-graph-execution-plan-swap.md`
- `0004-macro-parameter-host-automation.md`
- `0005-telemetry-webview-transport.md`

---

## 11. Risks

- **WebView binary transport performance is unverified.** The resource-provider approach is
  standard for JUCE 8/9 but the actual achievable frame rate/latency for several simultaneous taps
  (main out + 4 sidechains, each with scope + spectrum + meter) hasn't been measured. M4 starts
  with a throughput spike before building the full analysis pipeline around it.
- **Per-sample feedback regions add real compiler complexity** for a payoff (physical modeling)
  that's explicitly out of scope for the MVP's visible feature set. The brief asks for this
  deliberately ("design it now"); flagging that it's the single highest-complexity piece of the
  MVP relative to what it visibly delivers.
- **Cross-platform build validation is deferred, not resolved.** macOS build/CI is out of scope
  until a later milestone (decided 2026-09-16). The architecture avoids platform-specific code in
  `engine/` by design, but that's a design intent, not a verified fact, until it's actually built
  and tested on macOS.
- **JUCE 9 is very new** (first stable release 9.0.0 was 2026-07-21, current 9.0.2). It's the right
  target per "latest stable major," but it also means less field experience to lean on if we hit an
  edge case in `WebBrowserComponent` or `dsp::Oversampling`. Mitigated by pinning an exact tag via
  `FetchContent` (not tracking a moving branch) so upgrades are a deliberate, tested step.

---

## 13. Open questions

**Resolved (2026-09-16):**

1. ~~JUCE licensing model~~ — **commercial license**, Bazalt is closed-source. See ADR-0001.
2. ~~macOS hardware/CI access~~ — **deferred for the MVP**; Windows x64 only through M6, macOS
   build/CI added in a later milestone once access exists.

**Still open — proceeding with the stated defaults unless told otherwise:**

3. RealtimeSanitizer isn't exercised at all for MVP CI now that macOS CI is deferred (§8); the
   debug allocation trap is the only automated RT-safety net until macOS CI exists. Flagging in
   case that gap matters sooner than M6.
4. Macro parameter pool size (proceeding with default: 32).
5. Exact JUCE version pin (proceeding with: 9.0.2, latest stable as of this writing).

---

## 14. What this MVP does *not* build

Per the brief: no node-graph editor UI, no node library beyond the hardcoded proof graph, no preset
browser, no CLAP/AU wrappers, no unison, no true polyphonic MPE input mode (the data model supports
it; the input adapter doesn't expose a toggle yet). All of these are explicitly designed for, not
designed against.
