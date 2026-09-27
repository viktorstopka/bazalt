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

---

# Sound design system phase (M14+) — revision 2

Rewritten against the expanded `docs/NODE_CATALOG.md` (rewritten 2026-09-18, 107 nodes across 25
families — the original draft catalog this milestone sequence was first written against had 46).
Replaces this document's original M14–M22 draft entirely: that draft's three broad "waves"
don't fit a catalog more than double the size, and it didn't account for two things settled since:
the editor UI needs to actually be wired to the engine before node batches start (otherwise
"testing a batch" means staring at `render-cli` WAV output, not playing with it), and node previews
need a standardized, descriptor-driven system before any node ships one, per your own instruction
not to hand-code visualization per node.

**Process change from the original draft, explicit because it changes what "exit criteria" means
from M21 onward**: M14–M20 are one-time foundational work with real automated exit criteria, same as
every earlier milestone in this document. **From M21 on, the real gate is you playing with the batch
in the Standalone app and signing off** — automated tests are a floor (nothing ships broken), not the
finish line. Batch contents past the next one or two are expected to shift based on what you find;
treat the M21–M28 breakdown below as a dependency-ordered plan, not a fixed contract.

**Relationship to the already-existing M11–M13**: M11 (live visualization) is fully superseded by
M20 below — its scope (waveform/stepped-value/envelope/LFO-phase previews) is a strict subset of
M20's generalized system; don't build M11 as originally scoped. M12's Macro node and growable ports
are superseded by M14/M16 below, built for real at the schema level instead of UI-only. M13 (assist
menu, shell polish, full test pass) is **not** superseded, just deferred — a recipe menu and a full
test pass are worth more against a real catalogue than an empty one; revisit it once M21–M28 are
substantially through.

Sequencing: foundation (M14–M18, per `RECONCILIATION.md` §4's riskiest-first ordering, unchanged in
substance from the original draft) → make the editor actually usable (M19) → make previews generic
(M20) → node batches, most-dependency-free-first (M21–M28) → groups, last, once the primitives they
compose from exist and have been played with (M29).

## M14 — Value contract & port/parameter unification

Extend `engine/`: unify `PortDescriptor`/`ParameterDescriptor` around a shared value-contract
payload (`kind`, `quantity`, `curve`, `polarity`, `enumOptions`, `step`, `softMin`/`softMax`,
`isStructural`) per `RECONCILIATION.md` 1.1/1.3/1.5/1.7 — additive fields, defaulted so every
existing aggregate-init keeps compiling, same pattern as `hasFallbackWhenUnconnected`. Migrate
`osc.basic.shape` from a bare float to a real `kind=enum` value (the first end-to-end test of the
new contract). Keep the `hasFallbackWhenUnconnected` NaN-sentinel mechanism as-is (`RECONCILIATION.md`
1.4 — the design doc's "compiler bakes a constant" alternative is explicitly rejected). Resolve
`RECONCILIATION.md` 1.2 (Macro binding direction, ADR-0015) as its own decision point before writing
`util.macro`. **Also do the real-code ID renames now, in this milestone, while the blast radius is
smallest**: `util.add`→`math.add`, `util.multiply`→`math.multiply`, `util.map`→`adapt.map`,
`util.output`→`io.output`, `osc.basic`→`osc.analog`, `delay.basic`→`delay.line`,
`amp.vca`→`mix.gain`, `mix.add2`→`mix.sum`, `util.listen`→`view.listen`, `noise.burst`→`excite.burst`
(`NODE_CATALOG (1).md`'s full rename list — mechanical, touches `ProofGraphs.h`,
`PluginProcessor.cpp`'s default macro mappings, and the handful of tests referencing these literals;
a grep-based CI check confirming no old string survives is part of this milestone's exit criteria,
not a later one). Update `NodeDescriptorJson.cpp` and regenerate the TypeScript mirror via the
ADR-0007 codegen path, not by hand. **Also add the `PortGroup` schema concept now** (`SIGNAL_TYPES.md` §6:
`{idPrefix, min, max, growPolicy}`, stable `prefix.N` IDs, holes preserved on removal) — missed in
the first pass of this milestone's scope, caught before any code was written around the gap. Schema
only: `math.add`/`math.multiply`/`mix.sum` (renamed this milestone from `util.add`/`util.multiply`/
`mix.add2`) stay fixed-2-input for now, same runtime behaviour as before the rename — actually wiring
`PortGroup` into a real variable-arity node (`Node`'s `numInputs` becoming instance-level instead of
a compile-time constant, `GraphCompiler`'s slot allocation handling a variable count,
`processSample` summing however many are wired) is real, separate work that belongs in M21 (Batch A)
when these nodes are built for real per `NODE_CATALOG.md`, not retrofitted here as an afterthought.

**Exit criteria**: all 16 existing node headers compile against the extended structs and renamed
IDs; `osc.basic.shape`/now `osc.analog.shape` round-trips as a real enum through JSON → TS with no UI
code change needed beyond reading the new field; a Catch2 test asserts every existing
`ParameterDescriptor`/`PortDescriptor` literal still produces the same effective value contract it
did before; `render-cli` output for the existing proof graphs is bit-identical before/after; a grep
over `engine/`/`plugin/`/`tests/`/`tests-plugin/` finds zero occurrences of any pre-rename typeId
string.

## M15 — `Data` type, and how it lives in a patch

New `engine::Data` — immutable, reference-counted buffer with a small header (element type, length,
semantic tag), built off the audio thread, audio-thread side only ever swaps a pointer. Add `Data` to
`SignalType`, `NodeDescriptorJson.cpp`, the TS mirror. **Settle the patch-serialization question
ADR-0017 explicitly punted on, since the new catalog leans on it immediately**: a `Data` value in a
patch is either (a) a reference to another node's output (`data.table`/`data.scale`/`data.material`
feeding a consumer — no new serialization concept, it's just a connection), or (b) content a node
*owns and edits itself* (`adapt.remap`'s drawn curve, `seq.steps`'s own step data) — decide now
whether (b) is stored as the owning node's own parameter-adjacent state (serialized inline in that
node's patch entry) or is secretly always a hidden, auto-created `data.table` instance wired in
behind the scenes (keeping exactly one mechanism, at the cost of every such node compiling an extra
hidden node). Recommend the latter — one mechanism, no second serialization path — but this is a real
design call, flag it back before committing. Wire "consumer declares accepted tags, mismatched tag
rejected" into a standalone Catch2 test directly against the `Data` type (M16 gives it real
`canConnect` teeth).

**Exit criteria**: a Catch2 test builds a `Data(modal-set)` buffer on a worker thread, publishes it,
confirms the audio thread's read is allocation-free and torn-free under concurrent republish; a
second test confirms tag mismatch is rejected; the owned-vs-referenced serialization decision is
written up (a short ADR-0022, or folded into ADR-0017 as an amendment) before any M21+ batch that
needs it (`adapt.remap`, `seq.steps` — both Batch D, M24) starts.

## M16 — `canConnect`, adapters, and channels — done

New pure `canConnect(from, to) -> Ok | NeedsAdapters(chain) | Reject` (`engine/include/bazalt/engine/graph/CanConnect.h`/`.cpp`),
called from `GraphCompiler::compile()` (rejects `NeedsAdapters` exactly like `Reject` — a connection
reaching the compiler must already be directly compatible) and from a new
`GraphEditController::connectWithAutoAdapt()` (resolves real port descriptors via `NodeFactory`,
calls `canConnect`, and for a single-step 1-in-1-out chain builds + wires the adapter node via
`applyBatch` — one recompile, one undo step). Shipped three adapters, not four: `adapt.map` (already
existed, renamed M14), `adapt.normalise` (new), `adapt.threshold` (new, promotes
`mock.triggerByThreshold`) — Sample & Hold stays deferred to M20 alongside Envelope Follower per
ADR-0019's own incremental plan; the original draft of this milestone's text listed it here by
mistake. Added `channels: Mono | Stereo | Inherited` on Audio ports (ADR-0023) and built
`mix.downmix` for real (`left`/`right` → `out`).

**Real scope correction found while implementing, not before**: `wireRules.ts` is **not** regenerated
this milestone — ADR-0007's codegen path reflects *data* (a `NodeDescriptor`), and `canConnect` is
*logic*; turning it into a callable UI-facing thing (whether that's a generated TS mirror or a native-
function query) is naturally M19's job ("wire the editor to the real engine"), not M16's. `canConnect`
is real, tested, and enforced engine-side; the UI still predicts via its own `wireRules.ts` guess
until M19. Also found and fixed: `mix.downmix`'s real shape (2-in-1-out) doesn't fit
`connectWithAutoAdapt`'s single-splice model the way `adapt.map`/`adapt.normalise`/`adapt.threshold`
(all 1-in-1-out) do — it's flagged by `canConnect` but not auto-inserted; rejected with a message
pointing at manual insertion instead of silently guessing at a shape that doesn't fit (ADR-0023 has
the full reasoning, including a real, previously-unnoticed ambiguity in `NODE_CATALOG (1).md` itself
about whether "stereo" means one multi-channel port or two separate mono ports).

**A real bug caught by this milestone's own tests, worth remembering**: the first implementation of
`ok()` returned a default-constructed `CanConnectResult`, whose in-class default is `Reject` (the
safe default for anyone who forgets to set it) — so `ok()` silently returned `Reject` for every
legitimate connection, `prepare()` failed silently (`jassert` with no debugger attached), and every
downstream `processBlock()` call dereferenced a never-published plan. Caught immediately by the full
suite going from 111 green to 21 failing/crashing; fixed by constructing the result explicitly rather
than relying on `{}`.

**Exit criteria**: a Catch2 suite (`CanConnectTests.cpp`) exercises every `Ok`/`NeedsAdapters`/`Reject`
branch pairwise across `SignalType`s and across the three `channels` values — done. `connectWithAutoAdapt`
rejects an incompatible pair with a descriptive error and leaves the prior plan live, connects directly
when already `Ok`, and inserts+seeds a real `adapt.map` node end to end for a synthetic
Unipolar→`Time` connection (`ConnectWithAutoAdaptTests.cpp`) — done. The stereo→mono/`mix.downmix`
case is proven only against a synthetic test-only stereo node (no real one exists yet, ADR-0023) and
confirmed to reject-with-reason rather than falsely auto-insert — done, but honestly scoped short of
"auto-inserts," matching the correction above. All 131 engine+plugin tests green; UI `tsc`/`vite build`/
`oxlint` clean.

## M17 — Domain generalization: Instance Allocator, Voice Mix (Voice configuration only) — done

Replaced `util.voiceSum` with `instance.allocator`/`instance.mix` (`RECONCILIATION.md` 3.1–3.4,
`InstanceAllocatorNode.h`/`InstanceMixNode.h`, `VoiceSumNode.h` deleted). Generalized `VoiceManager`
into the allocator's **Voice** configuration specifically — Swarm/Trigger configurations are still
Batch H (M28), not touched here. Built the node-reuse mechanism `ARCHITECTURE.md` §3.2 specified and
never shipped: `GraphCompiler::compile()`'s `previousPlan` parameter reuses a node's exact
`shared_ptr<Node>` object (state and all) across a recompile when its `(id, type, parameters)` are
unchanged. Replaced the hardcoded `"env"`-node silence check with a generic peak-level silence
detector (`VoiceManager::updateSilenceAndCheckFinished()`); replaced immediate-cut stealing with a
256-sample fade ramp (`VoiceStage::Stealing`).

**Delivered vs. originally planned exit criteria**: three of the four shipped as stated — a per-voice
delay tail audibly outlasts its envelope's own release (`tests-plugin/VoiceRenderTests.cpp`), a
stolen ringing instance fades rather than clicks (`tests/VoiceManagerTests.cpp`), and voice state
survives a live recompile that doesn't touch the sounding voice's nodes
(`tests/GraphCompilerTests.cpp`'s two M17 state-pool cases). **"N-allocator-region coverage" did
not ship as originally worded** — `DomainSplitter` still supports exactly one `instance.allocator`
and one `instance.mix` per graph and rejects a second of either
(`tests/DomainSplitterTests.cpp`'s multi-allocator-rejection case), the same single-boundary
limitation `util.voiceSum` already had. True multi-region support needs `ExecutionPlan` to carry more
than one designated output, which doesn't exist yet; deferred rather than silently assumed done. Also
found and fixed along the way: a real cross-thread mutation race in the first state-pool draft
(reusing a node's object while still calling `setParameter()` on it), caught by the existing
concurrent-edit test before it shipped.

## M18 — `Note` as a real port type, MIDI rewired — done

`io.noteIn` (new) translates MIDI into real `Note` events; `PluginProcessor::handleMidiEvent`'s
direct pokes onto "osc"/"env" are gone, replaced by exactly one direct poke onto `io.noteIn`
(`injectNoteOn`/`injectNoteOff`/`injectPitchBend`) — everything downstream is real graph wiring:
`io.noteIn.notes` → `instance.allocator.spawn` (a genuine compiled `Note` connection, ADR-0024) →
`allocator`'s existing `pitch`/`gate` outputs → new real input ports on `osc.analog`/`env.adsr`.
`buildVoiceProofGraph()` now includes `noteIn`/`allocator` for the first time (M17 shipped
`instance.allocator` but never actually placed it in the running graph).

A `Note`-typed port doesn't fit `ExecutionPlan`'s one-float-per-port model (a note payload is gate +
pitch + velocity + start/stop together) — `ADR-0024` gives it its own per-block `NoteEvent` buffer
(`ExecutionPlan::noteBuffers`, `Node::produceNoteBlock()`/`consumeNoteBlock()`), routed around the
ordinary `blockBuffers`/`incomingSource` machinery but still participating in ordinary
producer-before-consumer scheduling. Caught and fixed along the way: a real, previously-latent
stack-array overrun — `InstanceAllocatorNode` has always had 9 output ports but `ExecutionPlan`'s
fixed per-step scratch arrays were sized for 8, and M17 never actually ran it through `process()` to
notice; `maxPortsPerNode` is now 16 in both `ExecutionPlan.cpp` and `Node.h`.

**Exit criteria — both met**: the existing MIDI-driven proof graph, rebuilt using `io.noteIn` →
`instance.allocator` → the rest, produces audio indistinguishable from before (same tests, same
render-cli tone, same RMS thresholds — `osc.analog`/`env.adsr`'s new ports fall back to their old
direct-`setParameter`/`noteOn()` behaviour whenever left unconnected, so nothing that doesn't wire
them regresses); a pitch-bend render (`tests-plugin/VoiceRenderTests.cpp`) confirms the continuous
`pitch` port needs no special-cased path for bend — it's folded into `io.noteIn`'s output like any
other continuous value change.

## M19 — Wire the editor to the real engine — done

Replaced `ui/src/graph/graphStore.ts`'s local-only mutations with real calls through the M7 command
bridge (`ui/src/graph/graphCommands.ts`) for every M10 interaction: place (`graphAddNode`), wire
(`graphConnectWithAutoAdapt`), move (`graphMoveNode`, new — no command for this existed before),
delete (`graphDeleteNode`), rename/bypass (`graphSetProperty`, new generic command — see below), plus
splice-insert (a real composite of disconnect+addNode+2×connect, one undo step). Kept
`InfiniteCanvas.tsx`'s/`GraphSurface.tsx`'s interaction logic unchanged, exactly as CLAUDE.md's own
prior note said to. Replaced `ui/src/graph/wireRules.ts`'s classification-bucket guess with
`ui/src/graph/canConnect.ts`, a hand-mirrored port of the real `CanConnect.cpp` decision logic (ADR
-0018's "generated" is aspirational the same way `NodeDescriptorJson.cpp`/`descriptorTypes.ts` are —
two hand-synced representations, not a codegen tool).

Two real scope decisions made and recorded as ADRs before implementing, since the milestone's own
one-line description didn't anticipate them: undo/redo is whole-graph JSON snapshots
(`graphGetSnapshot`/`graphRestoreSnapshot`), not `NODE_EDITOR.md` §6's per-command inverse log
(ADR-0025); and the canvas became **real-graph-only** — the M10 demo seed graph was entirely
`mock.*` nodes with no engine backing and no concept of "the audio output" at all, so it's retired
in favour of loading the actual running graph on open. `mock.*` stays in the read-only M9 component
gallery only. The M10 "drag a port out to create a Macro" shortcut is retired outright (its target,
`mock.macro`, has no real engine equivalent — `util.macro`/ADR-0015 isn't built).

**Exit criteria**: you place a node from the Add menu, wire it to another real node, and hear the
result change live in the Standalone app, with zero code changes in between — mechanically true
(every interaction is a real command now), not yet manually confirmed in the app by ear — that's
real hands-on testing only you can do. Undo/redo works against the real graph (ADR-0025's snapshot
mechanism, not a local-only snapshot). A rejected connection surfaces the engine's real rejection
reason in a new top-bar error banner. Not done, honestly: **bypass has no audio effect** (the
property is real and persisted, but `GraphCompiler`/`ExecutionPlan` doesn't read it yet) — a real gap
for whenever generic node bypass gets designed, not silently assumed solved. Every action awaits the
engine's confirmation before updating the display, rather than `NODE_EDITOR.md` §6's proposed
optimistic-then-reconcile flow (ADR-0025) — imperceptible at today's local-WebView round-trip speed,
but a real, deliberate simplification worth knowing about if it ever isn't.

## M20 — Standardized visualization system — done

A small, closed taxonomy of preview kinds — start with what M21/M22 need (Scope, Spectrum, Meter,
EnvelopeWithPlayhead, PhaseMarker), extend the taxonomy later rather than front-loading all of it.
Extend `NodeDescriptor` (JSON + TS mirror) with a `previews[]` field: each entry names a kind and
which tap id(s) feed it, declared in the node's own C++ header alongside its ports/parameters —
**nothing about a specific node's preview is hardcoded in `NodeCard.tsx`**, per your instruction.
Build one shared per-frame update loop replacing M5's 15 independent `requestAnimationFrame` loops
(closing the gap CLAUDE.md/NODE_EDITOR.md §9 already flag), draining every visible node's subscribed
taps and feeding whichever generic component is mounted for its declared kind. Port M5's existing
oscilloscope/spectrum/meter canvases into this system as the first three kinds (refactor, not new
DSP-adjacent work); build EnvelopeWithPlayhead and PhaseMarker new, since Batch B needs them
immediately. `view.scope`/`view.spectrum`/`view.meter`/`view.listen` (Batch A, M21) are the explicit
placeable nodes that reuse these same generic components to preview an arbitrary point in the graph.

**Exit criteria**: adding a preview declaration to a node's C++ header (e.g.
`{PreviewKind::EnvelopeWithPlayhead, "out"}` on `env.adsr`) is sufficient on its own to make that
preview render correctly — no `NodeCard.tsx` edit for that specific node. The M8 stress-test's
60fps-at-500-nodes number is re-measured with the shared loop active and doesn't regress.

**Delivered, with two honest deviations from the text above.** Waveform/Spectrum/Meter are real end to
end — `previews[]` declared in a node's C++ header, one shared render loop, viewport-gated tap
subscription, per-tap frame-type selection — and `osc.analog`/`mix.gain` declare real previews, so the
"a header declaration, zero `NodeCard.tsx` edits" criterion is met. **Not built:** `ShapeWithPlayhead`
(the one kind covering `EnvelopeWithPlayhead`, `PhaseMarker`, wavetable and `data.table` previews) and
the other declared-but-unproduced kinds — deferred by the M20 plan until a real node needs them, so
`env.adsr` has no preview yet. **Not re-measured:** the 60fps-at-500-nodes criterion; its harness was
deleted in M10 and only two node types declare previews, so there is no condition to measure (ADR-0008,
M20 amendment).

---

## Node batches (M21–M28)

Dependency-ordered so each batch only needs infrastructure and nodes that already exist. Every
batch's real exit gate is you testing it live in the Standalone app; the "testable target" column is
what you'd actually patch to try it, usually a `REFERENCE_PATCHES.md`/`NODE_CATALOG.md` Part B entry.

| # | Batch | Nodes (107 total, none dropped) | Testable target |
|---|---|---|---|
| M21 | Core plumbing, math, logic, adapters | `math.*` (11), `logic.*` (5), `adapt.map/normalise/threshold/sampleHold` (4), `util.*` (3), `mix.gain/sum/crossfade/downmix` (4), `io.*` (5, `io.audioIn` built with its full N-channel port-group behavior from the start, not deferred), `view.*` (4) — 36 nodes, zero DSP risk | Validates the whole pipeline (schema, `canConnect`, command bridge, generic previews) end to end on trivial nodes before anything harder depends on it working. Build a scope+meter+gain-staging test patch by hand in the app. |
| M22 | Basic synthesis | `osc.analog/sine` (2), `filter.svf/onepole/ladder/allpass/shelf/peak/dcBlock` (7), `env.adsr/follower` (2), `random.stepped/drift` (2), `space.pan/width` (2), `delay.line` (1), `instance.allocator` (Voice, wired for real) + `instance.mix` (2) — 18 nodes | The full **Init Patch** factory group — an ordinary subtractive synth, playable via keyboard in the Standalone app. First milestone that's genuinely "does this feel good to play," not just "does it compile." |
| M23 | Shaping + non-cyclic physical modeling | `shape.*` (5), `noise.colored/dust` (2), `excite.impulse/burst/pluck/contact` (4, the four *without* a `feedback` port), `resonator.modal/comb` (2), `data.material/analyseModes` (2) — 15 nodes | **Struck Body** and **Crackle**/**Scrape** factory groups — proves `Data(modal-set)` end to end without yet touching cyclic feedback. |
| M24 | Data authoring & shared curves | `data.table/scale/lookup`, `adapt.remap` (4), `note.quantize` (1), `lfo.shape` (1, full shape/morph), `env.curve` (1), `seq.steps/euclid` (2), `clock.*` (3) — 12 nodes | **Scale Quantize** group + a hand-drawn LFO/envelope shape audibly working — proves the "curves are shared data" idea for real, not just on paper. |
| M25 | Note-stream family + analysis | `note.gate/value/transpose/chord/hold/select/humanize/filter/assemble` (9), `analysis.*` (4) — 13 nodes | **Arpeggiator**, **Chord** groups, and a single-channel audio-to-note test (`analysis.onset`+`pitch`→`note.assemble`) — the hexaphonic-guitar reference patch's hard part, one string at a time before all six. |
| M26 | Coupled physical modeling + benchmark | `excite.mallet/stickSlip/breath` (3, `feedback` ports wired for real this time), `resonator.string/tube/plate` (3), `filter.formant` (1) — 7 nodes, plus a dedicated per-sample-region CPU benchmark at real voice counts (concern flagged separately — most physical-modeling patches become cyclic once `feedback` is real, unlike everything before this batch) | **Bowed String**, **Breath/Wind**, full **Struck Body**. Exit criteria includes measured CPU numbers at 8+ voices, not just "it compiles" — same bar M8 set for the rendering split. |
| M27 | Samplers, wavetable, file loading | `osc.wavetable` (1), `sampler.player/granular` (2), `data.load` (1) — 4 nodes | Load a real sample, play it pitched and granulated. |
| M28 | Space effects + swarm/trigger configs | `space.reverb/diffuser` (2), `instance.allocator`'s Swarm-population/Swarm-transient/Trigger configurations (same typeId, new behavior, generalizing M17's Voice-only scope) | **Water**, **Crackle** (as a real swarm, not the single-instance stand-in from M23), **Cicada**, **Cicada Field** groups — the transient/persistent swarm reference patch, for real. |

### M21 progress (Batch A) — done except `util.macro` (deliberately deferred)

**Wave 1 — done, 10 new nodes (25 of the 36 now exist; 15 already did):** `math.subtract/divide/abs/minmax/power/modulo/slew`, `mix.crossfade`,
`logic.not/toggle`. Fixed-arity nodes needing no new infrastructure; each header records its own design
calls (`math.power` is sign-preserving so a bipolar input never goes NaN; `math.modulo` is floored;
`math.slew` is an exponential lag with per-direction time constants, sample-rate-derived; `math.divide`'s
`safeZero` is a parameter, not a port — a Boolean port with an unwired fallback gets a dot in the UI
with no control to change it).

**Wave 2 — done: growable port groups, end to end (26 of the 36 exist).** `math.add`, `math.multiply`,
`mix.sum` (with a per-input `level` companion) and the new `logic.boolean` take 2..16 inputs
(`in.0..in.N`). The group size is derived from the connections, never stored; the compiler never reuses a
group node whose size changes; `maxPortsPerNode` is 32 and now a real compile error. The shipped `a`/`b`
port ids were migrated, not renamed (patch schema v3). The node card reveals a spare port after the last
wired one. Design and trade-offs: **ADR-0026**. Verified in the running Standalone app: a saved schema-v2
graph loads with its `math.add` cables on `In 1`/`In 2` and a third spare row.

**Wave 3 — done: type/quantity-inheriting ports (29 of the 36 exist).** `logic.select` (any plain signal
type; `condition` stays a fixed Boolean), `logic.compare` (`a`/`b`/`tolerance` share one quantity, so
comparing a Frequency with a Pitch is a compile-time rejection instead of comparing 440 with 69) and
`adapt.sampleHold` (quantity inherited, exponential `glide`). Built by generalising the polymorphic-port
hook first written for `util.reroute` (`resolveIncomingPort(toPortId, source)`, per-port
`PortDescriptor::polymorphism`, priority-by-declaration-order). Design: **ADR-0027**. This also
completed CLEANUP P1 #2 properly — the Reroute fix had only worked inside `GraphCompiler`, and the
command layer and the UI still rejected a Control cable into it.

**Wave 4 — done: the host boundary (32 of the 36 exist).** `io.audioIn` (Main or Aux 1–4, two stereo
outputs), `io.control` (CC / mod wheel / pressure / pitch bend / sustain, smoothed) and `io.transport`
(`beat` Event, `tempo`, `playing`, `position`; an internal 120 BPM transport when the host gives no
playhead). Built on a plain-data `HostInputs` struct the plugin pushes and opting-in nodes read, so `engine/`
still knows nothing about JUCE's processor (CLAUDE.md rule 4). Also: a graph with no `instance.allocator` is
now compiled once and run every block, so an audio effect (`io.audioIn → … → out`) actually processes, and
`processBlock` now snapshots the host input *before* clearing the output — it used to clear inputs first.
Design: **ADR-0028**. The RT-allocation trap test written for this path found a real M18 bug (a `juce::String`
built from a literal on every note-on/off/pitch-bend); fixed and regression-tested. 258/258 tests, pluginval
strictness 10 SUCCESS. `io.noteIn` is deliberately not migrated onto `HostInputs` (ADR-0028).

**Wave 5 — done: `view.scope`/`view.spectrum`/`view.meter`, and their settings are real (36 of the 36 exist).**
Each has one input and no outputs; its preview is declared on that input, and the engine resolves it to the
buffer wired in (`ExecutionPlan::inputSourceBufferIndexByNodeAndPort`/`findTappableBufferIndex`). `scope` and
`meter` take any plain signal via the ADR-0027 inherited-port mechanism; `spectrum` is Audio-only.
`AnalysisThread` was rebuilt around a `TapSettings` block per hub slot, filled from the live node's
`getPreviews()` on every attach — so a viewer's parameters (`scope`'s time window and free/rising-edge
trigger, `spectrum`'s FFT size 512–8192/tilt/averaging, `meter`'s Peak/RMS/True-Peak) are what
`AnalysisThread` actually applies, and every real enum parameter (not just these three nodes') now renders
as a dropdown instead of an integer slider — the UI never read `enumOptions` before this. Design and the
full build log: **ADR-0029**.

Two real bugs surfaced only by verifying the finished feature live in the Standalone app, both fixed as part
of this wave, neither specific to it: a falsy-zero check (`!frameType`) silently disabled every Waveform
preview since M20, including `osc.analog`'s own; and `osc.analog` was silent DC with nothing wired to its
pitch, because `PolyBlepOscillator` starts at 0 Hz and only a voice-graph allocator (which always drives
pitch) ever called `setFrequency()` before — invisible until M21's mono-graph path let an oscillator run on
its own. See ADR-0029's step 4 for both. 283/283 tests, `npm run build`/`lint` clean.

**`util.macro` is the only thing left of Batch A, deferred past M21 on purpose** (decision 2026-09-20).
ADR-0015 is still Proposed; it commits to the fixed 32-slot host-automation pool plus a slot-addressed node
and deserves its own decision before M22.

**Known UI gap, not M21's to fix:** the UI never reads the engine's `enumOptions`, so every real enum
parameter (`math.round`/`math.minmax` mode, `mix.downmix` mode, `mix.crossfade` law, `osc.analog` shape)
renders as an integer slider instead of a labelled dropdown.

## M29 — Groups

`DOMAINS.md` §8: Group Input/Output nodes, inline-and-flatten compiler pass before domain inference,
domain-signature computation, `factory.*`/`user.*`/`lab.*` namespaces (native nodes keep their
existing flat prefixes, per `NODE_CATALOG.md`'s namespace note). Build all fifteen
`NODE_CATALOG.md` Part B groups for real: Karplus-Strong, Scale Quantize, Arpeggiator, Chord, Bubble,
Water, Crackle, Scrape, Cicada, Cicada Field, Breath/Wind, Bowed String, Struck Body, Hex Guitar
Front End, Init Patch.

**Exit criteria**: all fifteen groups compile, inline correctly (compiled plan matches the equivalent
hand-built graph modulo naming), and are indistinguishable by ear from their native-graph
equivalents; a group placed in an incompatible domain context is rejected before the user hears
anything wrong; "make unique" detaches a library-referenced instance cleanly.
