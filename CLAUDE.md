# Bazalt — project rules

Node-based modular synth, VST3 (JUCE + C++20) with a TypeScript/Vite/React
WebView UI. **Current, active node/architecture reference:** `wiki/NODES.md`
(the node catalog) and `wiki/NODES.System.md` (architecture rules — signal
types, the value contract, the full connection/adapter matrix with real-vs-
aspirational columns, domains, naming philosophy, factories). Current gap
tracking: `wiki/NODES_Gaps.md`. Current per-node build status (Implemented/
MVP/To be implemented, necessity+difficulty, build batches, build-next order):
`wiki/NODES.Status.md`. Current milestone plan: `wiki/MILESTONES.md`
(a `0.x`-numbered arc, separate from the `archive_docs/MILESTONES.md` M-arc
below). Not-yet-decided new capabilities get designed in `wiki/plans/` (one
file per proposal, e.g. `wiki/plans/AudioControlBridge.md`) before any code
exists for them — distinct from `wiki/reports/` (investigative write-ups
answering a specific question) and `wiki/NODES_Gaps.md` (found-defect
tracking for existing behavior). A plan file isn't deleted once it's built —
it stays as the durable project record (see `wiki/plans/UtilMacro.md`'s own
"Status: Built" header, written at completion rather than mid-discussion);
all four files currently in `wiki/plans/` (`AudioControlBridge.md`,
`ControlToAudioBridge.md`, `DomainRedesign.md`, `UtilMacro.md`) are fully
implemented today, same as the proposal they once were before any code
existed. **Historical design docs (M0–M22 era, superseded, not maintained —
don't read for current specs/plan):** `archive_docs/ARCHITECTURE.md`,
`archive_docs/NODE_EDITOR.md`, `archive_docs/MILESTONES.md`,
`archive_docs/decisions/`. Still the right place to look for *why* something
from that era was built the way it was. Read `wiki/NODES.System.md` before
touching anything cross-cutting in the node system — this file is the
condensed day-to-day ruleset, not a replacement for it.

**Milestones through M20 have landed** (M0–M6 = the MVP; M7–M10 = the node
editor's model, telemetry, descriptors and canvas; M14–M18 = the
value/signal/domain system; M19 = the editor wired to the real engine; M20 =
standardized inline visualization). `docs/MILESTONES.md` is the per-milestone
record and `git log` is the source of truth for what's committed — some
headers there carry no "— done" marker even when the work landed (M20 did),
and M12/M13 (interaction polish, assist menu) are still open UI milestones.
**M22 (Basic synthesis: 18 nodes, the Init Patch) is done.** `util.macro`
(from M21, deliberately deferred behind ADR-0015) was the only thing still
open in the whole M21+M22 node-batch arc — now closed: it's a real,
wireable node as of `wiki/plans/UtilMacro.md`/ADR-0030 (ADR-0030 *amends*
ADR-0015's application mechanism, doesn't reverse its real 32-fixed-slot
constraint — see MILESTONES.md's M21/M22 sections for the historical arc
this closed out). Each milestone must build, pass its tests, and be
committed before the next one starts. **A fresh plugin instance opens on a
plain master-out-only graph** (`GraphEditController`'s constructor default,
`ProofGraphs.h::buildMasterOutOnlyGraph()`) — one unconnected `io.output`
node, silent until the user patches something into it. This replaced the
Init Patch as the default on 2026-09-29, on the user's own explicit
instruction ("instead of the initial patch, make a simple master out node
the initial patch"). Both `buildInitPatchGraph()` (M22's real subtractive
synth) and the old M2 proof graph (`buildVoiceProofGraph()`) stay registered
and tested, just aren't the default; don't assume test code that constructs
a bare `BazaltAudioProcessor` is exercising either one without checking
whether it calls `setGraph()` explicitly.

**A parallel `wiki/` gap-fixing arc (milestones `0.x`) started after M22** —
hands-on testing of the running app surfaced real node-design and UI/
architecture gaps M0–M22 didn't catch (redundant/jargon/unsafe node ports,
a Master Out that silently didn't work, stereo-cable design, etc.).
`wiki/MILESTONES.md` is the active plan going forward; `wiki/NODES_Gaps.md`
has the current, reviewed list of what's actually wrong and why — nothing
in it is fixed yet, it's identification pending your review. The `docs/`
folder was renamed `archive_docs/` and is historical reference only.

## The non-negotiable rules

1. **Sound is made in C++, never in JS/TS.** The `ui/` WebView is chrome,
   controls, and visualization only. It never computes audio, never owns DSP
   state, and talks to the engine only through parameters (host-automatable)
   and read-only binary telemetry frames (§6 of ARCHITECTURE.md). If you find
   yourself computing a sample value in TypeScript, stop — that logic belongs
   in `engine/`.

2. **The audio thread never allocates, locks, logs, throws, blocks, or
   deletes an `ExecutionPlan`.** Preallocate everything in `prepare()`. This
   is enforced by a debug-build allocation trap
   (`bazalt::engine::ScopedAudioThreadAllocationTrap`, see
   `engine/include/bazalt/engine/RtAllocationTrap.h`), not just convention —
   if it fires, that's a real bug, not a false positive to silence. Caveat:
   if the offending allocation happens inside a `noexcept` function (e.g.
   `std::vector<T>`'s default constructor under MSVC's debug STL, which
   allocates a small iterator-debug proxy), the trap's exception can't
   propagate and the process terminates instead of throwing catchably —
   still a loud failure, just not one to build a try/catch test around.

3. **Node type IDs, parameter IDs, and port IDs are hand-assigned strings**
   (e.g. `"osc.analog"`, `"filter.svf.cutoff"`), never array indices or enum
   values. In steady state this also means **never renamed once shipped** —
   that's what lets old patches survive refactors, and would be the single
   most expensive mistake to make early once it matters.

   **This "never renamed" half is SUSPENDED as of 2026-09-28, on the user's
   own explicit instruction, not a Claude decision.** Their reasoning,
   verbatim: "This rule only makes sense when we are actually building
   patches and stuff. There is a loooong way to go before that, so right now
   it is just annoying." Nothing real depends on today's port/node/parameter
   ids yet — no distributed patches, no saved user work at stake. While
   suspended: rename a port/node/parameter id freely when it's the right
   call, same as any other identifier in the codebase — no migration
   required, just bump `PatchDocument::currentSchemaVersion` for hygiene if
   the shape changed. **Re-enable trigger, watch for this and raise it
   proactively rather than staying silently suspended forever**: once real
   patches exist that the user (or anyone else) is actually saving and
   relying on — i.e. once patch save/load is a genuine workflow, not just
   test fixtures and this session's own throwaway graphs. The moment that's
   true, this note should be deleted and the rule goes back to being
   unconditional.

4. **`engine/` never depends on `juce_audio_processors`, `juce_gui_basics`,
   or anything plugin/UI-shaped** — only `juce_core`, `juce_audio_basics`,
   `juce_dsp`. This keeps `engine` headless and testable
   (`tools/render-cli` and the Catch2 suite in `tests/` link only `engine`).
   If a change makes `engine` need a plugin/UI dependency, that change is
   wrong — redesign it, don't add the dependency. `tests-plugin/` is a
   separate, plugin-level Catch2 suite (added M3) that deliberately breaks
   this rule on purpose — it tests `PluginProcessor` directly — don't
   confuse the two or move plugin-layer tests into `tests/`.

5. **Graph edits take effect at the next block boundary, never inside one.**
   Don't add code paths that mutate a live `ExecutionPlan` mid-block. A
   rejected/invalid compile must leave the previous valid plan live — never
   let a bad edit reach the audio thread.

6. **No node algorithm may depend on block size for its math** (only for how
   much work it does per call). Sample-rate-dependent constants are derived
   in `prepare()`, never hardcoded. Verify with the block-size-invariance
   test when touching DSP.

7. **Windows x64 only through M6.** macOS build/CI is deliberately deferred
   (see ARCHITECTURE.md §13) — don't add platform-specific code paths in
   `engine/` in anticipation of it; the design already avoids that by
   construction.

## Build / test / render commands

```
# Configure (first time, or after CMakeLists.txt changes)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build everything (engine, plugin VST3 + Standalone, tests)
cmake --build build --config Debug

# Run the Catch2 engine test suite
ctest --test-dir build -C Debug --output-on-failure

# UI dev server (hot reload; plugin debug builds point the WebView here)
cd ui && npm run dev

# UI production build (plugin release builds serve this — see plugin/CMakeLists.txt)
cd ui && npm run build

# Render a fixed tone to WAV, headless — the primary way to *listen to* an
# engine change without opening a DAW. Real patch+MIDI input replaces the
# hardcoded tone once NodeGraph/ExecutionPlan exist (M2/M3).
./build/tools/render-cli/RenderCli_artefacts/Debug/RenderCli.exe out.wav

# Validate the VST3 (pluginval must be installed separately; CI runs this at
# strictness-level 10, the max — .github/workflows/ci.yml, wired in M6)
pluginval --strictness-level 10 --validate ./build/plugin/BazaltPlugin_artefacts/Debug/VST3/Bazalt.vst3

# Plugin-level tests (PluginProcessor behavior: buses, MIDI-to-audio, patch
# state, macro automation) — direct calls, no DAW needed
ctest --test-dir build -C Debug -R PluginTests --output-on-failure
```

## Naming / structure conventions

- C++: JUCE house style — `PascalCase` types, `camelCase` members/functions,
  no Hungarian notation. Match the surrounding file.
- Everything DSP-facing lives under `bazalt::engine`; everything host-facing
  lives under `bazalt` (plugin namespace, no sub-namespace yet — see
  `plugin/source/PluginProcessor.h`).
- TypeScript: standard React/Vite conventions; components in
  `PascalCase.tsx`. High-rate rendering (canvas/WebGL scopes, spectra,
  meters) must run outside React's render cycle — see ARCHITECTURE.md §7.
- One ADR per significant architectural choice, added under
  `archive_docs/decisions/` **as the choice is made**, not retrofitted later
  — the numbered ADR sequence continues in place there; the wiki/archive_docs
  split didn't renumber or relocate it. The current `wiki/` gap-fixing arc
  writes its own architectural decisions straight into `wiki/NODES.System.md`
  instead, per how that specific arc was scoped — don't treat that as a
  general replacement for ADRs elsewhere.

## Known interim simplifications (not bugs, don't "fix" without checking)

- **Fixed, 2026-10-03** — this note used to say the M0 plugin editor's
  release-build WebView always served a hard-coded placeholder HTML string
  via a resource provider, never the real `ui/dist` build, "still true as of
  M6." That gap is closed: `PluginEditor.cpp`'s `findUiDistRoot()`/
  `serveUiDistFile()` now serve a real, built `ui/dist` (`cd ui && npm run
  build`) from disk when one is shipped alongside the binary, fed by
  `plugin/CMakeLists.txt`'s new `CopyUiDist.cmake` post-build step (copies
  `ui/dist` into the VST3 bundle's `Contents/Resources/ui/` and a flat
  sibling `ui/` next to the Standalone executable). The hard-coded
  placeholder still exists and still serves for `/`/`/index.html` only when
  no `ui/dist` was shipped (e.g. a Debug-only checkout that never ran `npm
  run build`) — a real, honest fallback, not the only path anymore. This is
  the "ship `ui/dist` alongside the binary" interim path, not
  ARCHITECTURE.md §7's original full binary-embedded end state (compiled-in
  resources needing no sibling `ui/` folder at all) — that's still real,
  separate future work if a single-file `.vst3` is ever needed. Found and
  fixed in response to a direct, practical need: testing the plugin on a
  different computer, where the Debug build's own `localhost:5173` dev-
  server dependency doesn't help at all. See `wiki/NODES_Gaps.md` for the
  full write-up and `wiki/MILESTONES.md`'s own entry for the build/test
  record.
- `COMPANY_NAME`/`PRODUCT_NAME` in `plugin/CMakeLists.txt` are placeholders
  pending real publisher info.
- `Note` routes through real ports as of M18 (ADR-0024) — `PluginProcessor::
  handleMidiEvent` now translates MIDI into exactly one direct poke
  (`IoNoteInNode::injectNoteOn`/`injectNoteOff`/`injectPitchBend` on each
  voice's `"noteIn"` node), and everything downstream is ordinary graph
  wiring: `io.noteIn`'s Note-typed `"notes"` output feeds `instance.
  allocator`'s `"spawn"` input via a real compiled connection, and
  `allocator`'s own `"pitch"`/`"gate"` outputs feed real `osc.analog`/
  `env.adsr` input ports (both using the `hasFallbackWhenUnconnected`
  NaN-sentinel, so any graph that leaves them unconnected keeps behaving
  exactly like before M18). A connected `Note` port doesn't use
  `ExecutionPlan`'s ordinary `blockBuffers` — it gets a dedicated
  `NoteEvent`-typed buffer (`ExecutionPlan::noteBuffers`, `Node::
  produceNoteBlock()`/`consumeNoteBlock()`) since a note payload (gate,
  pitch, velocity, start/stop) doesn't fit one float per sample. Only one
  `Note` input and one `Note` output per node is supported, and a `Note`
  port inside a feedback cycle is rejected at compile time — neither
  limitation is hit by anything that exists yet. `SignalType::Event` still
  has no real consumer (`allocator`'s own `"start"`/`"stop"` outputs are
  declared but nothing's wired to them) — that's still open, not this
  milestone's job.
- Each voice still gets its own fully independent `ExecutionPlan` (compiled
  `numVoices` times from the same `NodeGraph`) — `ExecutionPlan::nodes` is
  not one shared plan with a separate per-voice state pool keyed by
  `(voiceIndex, nodeID)` the way an earlier reading of ARCHITECTURE.md §3.2
  suggested. `PlanSwapper` is wired into `PluginProcessor` (one per voice +
  one for the global domain, `GraphEditController::recompileAndPublish()`)
  — live edits recompile and swap. State *continuity* across a recompile,
  the part that used to be "still deferred" here, is real as of M17:
  `GraphCompiler::compile()`'s `previousPlan` parameter (see its own doc
  comment in `GraphCompiler.h`) reuses a node's exact `shared_ptr<Node>`
  object — carrying forward filter memory, envelope stage, delay-line
  contents — whenever that node's `(id, type, parameters)` are all
  unchanged from the previous plan (`ExecutionPlan::nodeIdToAppliedParameters`).
  As of 2026-10-04 a VALUE-ONLY edit (a port's fallback value or a
  non-structural parameter) also keeps the running node: the new values
  ride with the new plan (`ExecutionPlan::pendingParameterUpdates`) and the
  audio thread applies them at that plan's first block, so editing a value
  no longer resets filter memory or oscillator phase (no click). A
  structural parameter, or a parameter key appearing/disappearing, still
  builds a fresh node. While a slider is being dragged nothing recompiles
  at all: `plugin/source/LiveParameterEdits.h` streams the value to the
  running nodes and glides to it (~15 ms); the release commits as an
  ordinary edit. This is a real
  `(nodeID)`-keyed reuse mechanism, not literally the separate pool
  ARCHITECTURE.md §3.2 originally sketched — close enough in effect that
  the doc's own gap is considered closed, but don't assume the exact data
  structure it described exists.
- The sidechain "passthrough" (`PluginProcessor::updateAuxLevelsAndPassthrough`)
  mixes each active aux bus into the main output at a fixed -30dB and tracks
  its peak level — a deliberately simple, measurable proof that audio
  reaches the engine from all 4 aux buses (ARCHITECTURE.md §4.1), not real
  sidechain-driven DSP. Until M21 `processBlock` cleared every host-buffer
  channel, aux inputs included, before this read them; it now clears only the
  output channels. Real sidechain use goes through `io.audioIn`'s `bus` setting
  (Aux 1-4 as ordinary Audio signals, ADR-0028) — there are still no aux-typed
  node ports, and no ducking-style node built on it yet.
- **Host boundary (M21, ADR-0028).** `io.audioIn`/`io.control`/`io.transport`
  read a plain `HostInputs` struct the plugin fills per `process()` range
  (`ExecutionPlan::applyHostInputs`), never JUCE types. `io.noteIn` is NOT on
  that mechanism — it's poked directly via `BazaltAudioProcessor::findNoteIn`.
  This note used to say that lookup was still a hardcoded literal id
  (`"noteIn"`), so a note-in placed under another id would be silently
  ignored — true through M21, but fixed during the `09-28-InstanceAllocator`
  arc: `findNoteIn` now resolves by TYPE at compile time
  (`ExecutionPlan::noteInNodeId`, set in `GraphCompiler.cpp` for whichever
  node has type `"io.noteIn"`, whatever its id), the same fix
  `instance.mix`'s own `externalInputNodeId` already had. A note-in placed
  under any id (e.g. the editor's own auto-generated `"node7"`) works
  correctly. A graph with no `instance.allocate.voice` (`MultiplicityResolver`'s
  own `MultiplicityResult::monoOnly` field, replacing `DomainSplitter::monoOnly` as of
  `wiki/plans/DomainRedesign.md`) is one plan run every block, so audio effects work
  but it plays no notes. **Never call `ExecutionPlan::getNodeById ("literal")` on the audio
  thread** — the literal becomes a heap-allocated `juce::String`; pass one built
  beforehand. That exact mistake was in the M18 note path until M21.
- The patch format (`engine/patch/PatchDocument.h`) has no `ui` (view state)
  section — there's no node-graph editor to have pan/zoom/layout state for
  yet. Add that field in the same change as the editor, not before.
- M5's analysis panel runs 15 independent `requestAnimationFrame` loops (one
  per oscilloscope/spectrum/meter canvas in `ui/src/analysis/TelemetryScope.tsx`),
  not one shared/coordinated loop. Fine at M5's fixed count; NODE_EDITOR.md §9
  and M11 already call out that the real node editor's many more dynamic
  previews need a single shared loop instead — don't copy this pattern there.
- The M5 analysis panel (`ui/src/analysis/TelemetryScope.tsx`) still renders
  via Canvas2D, not WebGL — unchanged since M5, still
  `bazalt_node_editor_prompt_v3.md` §3's "build on the M5 canvas rather than
  replacing it unless the benchmark justifies it" territory for that
  specific surface. `ui/src/canvas/InfiniteCanvas.tsx` itself migrated off
  Canvas2D onto WebGL2 in M10 (`ui/src/canvas/webgl/nodeEditorRenderer.ts`),
  per ADR-0008's M8 benchmark and its own stated M10 commitment — the grid
  dots and cables render there now; node bodies stay DOM (`NodeCard`,
  effectively unchanged — see the ADR-0008 Amendment (M10) note below).
- M10 filled in the pan/zoom/grid canvas M5 left empty: box-select
  (partial-touch), fit-view (auto-once when the graph first has nodes, plus
  a toolbar button), node placement/move/select/delete/rename/bypass, a
  right-click node context menu, and drag-to-wire with valid/rejected/hover
  feedback (`docs/decisions/0010-wire-feedback-colours.md`) are all real now
  (`ui/src/canvas/InfiniteCanvas.tsx`, `ui/src/graph/GraphSurface.tsx`). The
  Shift+A/right-click Add menu (`ui/src/graph/AddMenu.tsx`) searches and
  groups the same merged real+mock descriptor catalog the M9 gallery uses.
  Snap-to-grid (`SnapSettings.sizeWorldUnits`) is now read by node drag and
  ghost placement, closing the gap M5's own note used to flag here.
- **The node editor canvas is wired to the real engine as of M19** (ADR-0025)
  — `ui/src/graph/graphStore.ts` mirrors the actual running `NodeGraph`, not
  a local prototype. Placing, wiring, moving, deleting, renaming, and
  bypassing a node all fire real commands (`graphAddNode`/
  `graphConnectWithAutoAdapt`/`graphMoveNode`/`graphDeleteNode`/
  `graphSetProperty`/...) over the M7 bridge (ADR-0006) and only update the
  visible graph once the engine confirms the result — not optimistically
  (ADR-0025's own deviation from `NODE_EDITOR.md` §6, deliberate: a local
  WebView round-trip is fast enough that this costs no perceptible
  responsiveness, and there is then never a local/engine state to
  reconcile). Undo/redo is whole-graph JSON snapshots
  (`graphGetSnapshot`/`graphRestoreSnapshot`), not per-command inverses —
  ADR-0025 has the full reasoning. The canvas is **real-graph-only**: the
  Add menu's catalog no longer merges `mockDescriptors.ts` in (mocks stay in
  the read-only M9 component gallery, `ComponentGallery.tsx`, which fetches
  its own separate copy). The M10 "drag a port out to create a Macro"
  shortcut was retired at M19 for exactly this reason — its target,
  `mock.macro`, had no real engine equivalent — and is back as of
  `wiki/plans/UtilMacro.md`/ADR-0030 (`createMacroFromPort` in
  `graphStore.ts`), now that `util.macro` is a real, wireable node.
  `rename`/`bypass` are real, persisted `NodeInstance.properties` writes
  (`GraphEditController::setProperty`, new). **Bypass is real DSP as of
  docs/CLEANUP.md P1 #1**: `GraphCompiler` resolves `properties["bypassed"]`
  into `BlockStep::bypassed`, and `ExecutionPlan::process()` then skips
  `processBlock()` and copies the node's first declared input straight to
  its primary output. Known limits, not bugs: it's a block-rate step only
  (a bypassed node inside a per-sample feedback region still runs), any
  secondary input is ignored, and a bypassed node's Note ports are not
  forwarded (downstream sees no notes) — bypass is Audio/Control-only for
  now. `move` similarly
  needed a new command (`GraphEditController::moveNode`) that didn't exist
  before M19 — a pure position write, no DSP implications (`GraphCompiler`'s
  state-pool reuse check doesn't compare `position`).
- `ui/src/theme/tokens.ts` is the single source of truth for every colour/
  font/spacing/stroke value (resolves ARCHITECTURE.md §7's "TBD in M5" note
  on how DOM and canvas share one theme definition): `applyTokensToCss()`
  mirrors it onto `:root` as CSS custom properties once at startup
  (`main.tsx`) for DOM/CSS consumers; canvas/WebGL code imports and reads
  `tokens` directly rather than parsing computed CSS values. Add new tokens
  here, not as hardcoded values in components or canvas draw code.
- `ui/vendor/juce-webview/` is a vendored copy of `@juce-framework/webview`
  (JUCE's own `WebSliderRelay`/`getSliderState` JS interop), copied from the
  exact JUCE 9.0.2 source tree rather than installed from the public npm
  registry, so the JS-side protocol can never drift from the pinned C++ side
  — see `ui/vendor/juce-webview/README-BAZALT.md`. Re-sync it (don't
  hand-edit `lib/`) only if the JUCE version pin in ADR-0001 ever changes.
- The M7 command bridge (`GraphEditController`, `PluginEditor::
  withGraphCommands()`'s `graphAddNode`/`graphDeleteNode`/`graphConnect`/
  `graphDisconnect`/`graphSetParameterValue`/`graphSetOutput`/`graphMoveNode`/
  `graphSetProperty`/`graphConnectWithAutoAdapt`/`graphGetSnapshot`/
  `graphRestoreSnapshot` native functions) is real and tested
  (`tests-plugin/GraphEditControllerTests.cpp` drives `GraphEditController`
  directly). As of M19, the UI is a real caller — `ui/src/graph/
  graphCommands.ts` wraps every one of them; see the node-editor-canvas note
  above and ADR-0025 for how the store uses them. `getNodeDescriptors` (M9,
  ADR-0007) was the first native function the UI ever called and is still
  the only read-only one (fetched once by both the canvas and the component
  gallery, each independently).
- `instance.sum`/`instance.allocate.voice`/`MultiplicityResolver`
  (`wiki/NODES.System.md` §5, `wiki/plans/DomainRedesign.md`) are real and
  tested at the engine level (`tests/MultiplicityResolverTests.cpp`), and
  `PluginProcessor`'s starting graph (`buildVoiceProofGraph()`) has both.
  `MultiplicityResolver` replaced `DomainSplitter` outright
  (`wiki/plans/DomainRedesign.md`, all 5 batches landed): the old
  whole-graph reachability pass gave a different answer depending on
  compile order for the exact same graph shape (the redesign's own
  motivating bug) and capped the graph at exactly one allocator/one sum,
  full stop. The new model resolves Scalar-vs-Poly per PORT, per compile,
  tagging each `Poly` value with the producing allocator's own node id
  (`originId`) — up to `MultiplicityResolver::maxOrigins` (4) independent
  allocator/sum pairs now coexist in one graph, each its own physical
  `ExecutionPlan` bundle, each either MIDI-dispatched (`io.noteIn`) or
  internally triggered, never both for the same origin. `instance.sum` was
  renamed from `instance.mix` in the same redesign (Batch 1b, C++ class
  name `InstanceMixNode` unchanged) — a second `instance.sum` reducing the
  SAME origin's Poly signal is still rejected, the one part of the old
  ceiling that was never the bug.
- `GraphEditController::recompileAndPublish()` does a full recompile (8
  voice plans + up to 1 global plan) on **every single command**, even ones
  that are conceptually one user gesture made of several calls (e.g. a
  future splice-insert: disconnect + addNode + 2×connect). NODE_EDITOR.md
  §6 already flags composite operations as "one undo step" for the UI layer
  — this is the matching engine-layer gap: there's no way yet to batch
  several graph mutations into a single recompile+publish. Fine at M7's
  edit rates; revisit if/when a composite command's 4 separate recompiles
  ever prove too slow or too visible as intermediate (invalid-looking)
  states.
- `GraphEditController::applyBatch()` (M8) exists because the stress-test
  generator needed it for real: building a 500-node graph via 750
  individual one-recompile-each commands measured in the tens of seconds;
  batched into one recompile, ~0.2s. Use it for any future bulk/procedural
  graph construction, and remember it's also the mechanism composite UI
  gestures (splice insert, Unwrap, Alt-drag Mix/Add/Multiply,
  NODE_EDITOR.md §6) should use once they're built — don't reach for N
  individual `addNode`/`connect` commands for something that's conceptually
  one user gesture.
- `TelemetryHub`'s tap pool is a **fixed 64 slots** (M8,
  `TelemetryHub::maxTaps`), not a growing map — `subscribeTap`/
  `unsubscribeTap` LRU-evict once full. Nothing generic pushes into a
  dynamically-subscribed tap yet: only the 5 M4 baseline taps (audio
  thread) and M8's synthetic `"demo."`-prefixed stress-test taps
  (`AnalysisThread` generates their content itself, ADR-0009) have real
  pushers. Wiring real per-node/per-connection signals is M11's job, not
  done yet — don't assume subscribing a tap for an arbitrary graph node id
  produces real data today.
- `ui/src/canvas/StressTestCanvas.tsx`/`StressTestCanvas.css`/`stressGraph.ts`
  (M8's rendering-split benchmark spike, ADR-0008 — dev-only scaffolding
  behind App.tsx's "Run stress test (M8)" button, drawing a synthetic
  500-node/1000-cable layout) were deleted wholesale in M10 polish, exactly
  as this note used to say they eventually would be — the spike's own
  measurements are preserved in ADR-0008 itself, the scaffolding isn't
  needed once real node-count/perf questions can be asked against the real
  editor instead. `ui/src/canvas/webgl/` **stays** — `nodeEditorRenderer.ts`
  is the real M10 cable renderer, not spike code, and `shaders.ts`/
  `webglUtils.ts` are its (still real, still used) shader/GL helpers; only
  the spike-only shader exports (`dotVertexShader`, `lineVertexShader`,
  `solidFragmentShader`, `varyingColorFragmentShader`) went with it.
- `ui/src/nodes/NodeCard.tsx` (M9, ADR-0007) renders any `NodeDescriptor`
  (real or mock) as plain DOM/CSS, not the hybrid WebGL-background/DOM-
  overlay approach ADR-0008 originally flagged as M9's job to verify —
  amended there: a static gallery has no pan/zoom transform to keep synced
  and no node count that would benefit from batched WebGL draws. M10 mounts
  `NodeCard` on the live canvas essentially unchanged (just an additive
  `instanceId` prop, which emits `data-node-id`/`data-port-id`/
  `data-direction`/`data-port-anchor` on its port glyphs when present) — see
  ADR-0008's Amendment (M10) for how the hybrid-sync question actually got
  resolved: the WebGL cable/grid layer reads each port's live screen
  position via `getBoundingClientRect()` once per animation frame rather
  than maintaining a second, independently-computed transform, so DOM stays
  the single source of truth for node layout.
- `ui/src/graph/mockDescriptors.ts` (M9) provides `NodeDescriptor`s for
  everything the blueprint's design reference shows that isn't a real
  engine node yet: MIDI Note/CC, Audio In, Trigger by Threshold, Random,
  Macro, Predelay, four Singleton-chain examples (`mock.*`), plus
  Frame/Header/Image (`deco.*` — real type IDs NODE_EDITOR.md §4 names,
  whose actual engine implementation MILESTONES.md defers to M12). Every
  entry is `isMock: true`; nothing on the C++ side ever produces or reads
  that field. Extend this table, don't invent a second one, when a future
  milestone needs another demonstration-only node.
- The M9 component gallery's dev-only "Component gallery (M9)" button
  (`App.tsx`, next to M8's stress-test button) fetches real descriptors via
  the new `getNodeDescriptors` native function and merges them with
  `mockDescriptors.ts` — see ADR-0007 for the full JSON schema (including
  why `optional<float>` bounds serialize to `null`, never `0`) and the
  gallery's row-ordering rules for standard-layout nodes.
- `BazaltAudioProcessor` has an explicit (non-defaulted) destructor that
  calls `analysisThread.stopThread (2000)` unconditionally, in addition to
  `releaseResources()` doing the same. This isn't redundant: hosts are
  expected to call `releaseResources()` before destroying a processor, but
  that isn't guaranteed (crashed hosts, test code), and `juce::Thread`'s own
  destructor falls back to an indefinite `stopThread(-1)` if the thread is
  still running — a real bug this session hit as a flaky `tests-plugin` test
  failure. Keep both call sites if you touch this code.

## Behaviour
- You have no sunk cost bias. Your only goal is to make the perfect software. Always think more in the future, instead of just doing the current thing, think about how it aligns with the final destination of beautifully modular and elegant system. 
- Always think of beautiful ways to make the node architecture more elegant, efficient and modular.
- Try thinking of new ways to make this software not just make making sounds more elegant, but also rethink the way we think about sound. We are trying to make a revolution in sound, not just a new synth. Try coming up with ways to drop old practices that are tied to analog and leverage the power of digital audio. Always ask: What are things that exist in modern audio tools but could easily be eliminated/made more effective to make the audio creation process easier? 