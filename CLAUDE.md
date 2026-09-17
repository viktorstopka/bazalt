# Bazalt — project rules

Node-based modular synth, VST3 (JUCE + C++20) with a TypeScript/Vite/React
WebView UI. Full design: `docs/ARCHITECTURE.md`. Node editor design (M7+):
`docs/NODE_EDITOR.md`. Milestone plan: `docs/MILESTONES.md`. Decisions:
`docs/decisions/`. Read `ARCHITECTURE.md` and `NODE_EDITOR.md` before
touching anything cross-cutting — this file is the condensed day-to-day
ruleset, not a replacement for either.

**M0–M8 are done and committed** (M0–M6 = the MVP; M7 = domain-aware graph
model + command bridge; M8 = dynamic telemetry + rendering-split
benchmark — both node-editor-phase milestones from `docs/NODE_EDITOR.md`).
M9 (descriptor schema end-to-end + component gallery) is next. Each
milestone must build, pass its tests, and be committed before the next
one starts.

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
   (e.g. `"osc.basic"`, `"filter.svf.cutoff"`), never array indices or enum
   values, and **never renamed once shipped**. This is what lets old patches
   survive refactors. Silently violating it is the single most expensive
   mistake to make early.

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
  `docs/decisions/` **as the choice is made**, not retrofitted later.

## Known interim simplifications (not bugs, don't "fix" without checking)

- The M0 plugin editor's release-build WebView serves a hard-coded
  placeholder HTML string via a resource provider, not the embedded
  `ui/dist` binary-resource pipeline described in ARCHITECTURE.md §7. Still
  true as of M6 — no milestone through M6 has actually scoped wiring this
  up (a previous version of this note claimed "tracked for M5"; that never
  happened and the claim was wrong, corrected during M6's docs pass). Needed
  before a Release build is usable for anything beyond Debug-only dev.
- `COMPANY_NAME`/`PRODUCT_NAME` in `plugin/CMakeLists.txt` are placeholders
  pending real publisher info.
- Note/Event data still doesn't route through node ports, even now that real
  MIDI input exists (M3). `PluginProcessor::handleMidiEvent` translates MIDI
  straight into `noteOn()`/`setParameter()`/`dynamic_cast<AdsrNode*>` calls
  on each voice's compiled node instances via `ExecutionPlan::getNodeById()`
  — this was a deliberate scoping call (full Note-typed port signal routing
  is a bigger change that only earns its cost once a live graph editor needs
  it), not an oversight. `SignalType::Note`/`Event` still exist in the enum,
  unused by any port.
- Each voice gets its own fully independent `ExecutionPlan` (compiled
  `numVoices` times from the same `NodeGraph`), not a shared plan with
  per-voice state pooled separately as ARCHITECTURE.md §3.2 ultimately
  describes ("per-voice DSP state... lives in a separate pool keyed by
  (voiceIndex, nodeID)"). `PlanSwapper` **is** wired into `PluginProcessor`
  as of M7 (one per voice + one for the global domain,
  `GraphEditController::recompileAndPublish()`) — live edits really do
  recompile and swap now. What's still deferred is state *continuity*
  across a recompile: every recompile builds brand-new `Node` instances
  with fresh DSP state, so editing the graph while a voice is mid-note
  resets that voice's filter/envelope/phase memory. Accepted for M7 (its
  own swap-under-load test only proves no discontinuity *within* one
  `process()` call, same guarantee M2's original test proved — not
  musical-content continuity across edits); the per-(voiceIndex,nodeID)
  state pool ARCHITECTURE.md §3.2 describes is the eventual fix, still not
  built.
- The sidechain "passthrough" (`PluginProcessor::updateAuxLevelsAndPassthrough`)
  mixes each active aux bus into the main output at a fixed -30dB and tracks
  its peak level — a deliberately simple, measurable proof that audio
  reaches the engine from all 4 aux buses (ARCHITECTURE.md §4.1), not real
  sidechain-driven DSP. Ducking/modulation-style sidechain use needs
  aux-typed node ports, which don't exist yet.
- The patch format (`engine/patch/PatchDocument.h`) has no `ui` (view state)
  section — there's no node-graph editor to have pan/zoom/layout state for
  yet. Add that field in the same change as the editor, not before.
- M5's analysis panel runs 15 independent `requestAnimationFrame` loops (one
  per oscilloscope/spectrum/meter canvas in `ui/src/analysis/TelemetryScope.tsx`),
  not one shared/coordinated loop. Fine at M5's fixed count; NODE_EDITOR.md §9
  and M11 already call out that the real node editor's many more dynamic
  previews need a single shared loop instead — don't copy this pattern there.
- The M5 canvas (`ui/src/canvas/InfiniteCanvas.tsx`) and analysis panel both
  render via Canvas2D, not WebGL, deliberately — this is what
  `bazalt_node_editor_prompt_v3.md` §3 means by "build on the M5 canvas
  rather than replacing it unless the benchmark justifies it." The WebGL/DOM
  split proposed in NODE_EDITOR.md §10 is unconfirmed until M8 actually
  benchmarks it; don't assume WebGL is needed before that milestone does.
- M5's `InfiniteCanvas` only implements pan/zoom/grid/a snap-settings toggle
  — box-select and fit-view are real interactions but belong to the node
  editor phase (blueprint §6.1, `docs/MILESTONES.md` M10), where there are
  actually nodes to select/fit around. The snap-to-grid checkbox exists and
  is wired to state, but nothing reads `sizeWorldUnits` yet for the same
  reason.
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
  `graphDisconnect`/`graphSetParameterValue` native functions) is real and
  tested (`tests-plugin/GraphEditControllerTests.cpp` drives
  `GraphEditController` directly), but **no UI calls it yet** — nothing in
  `ui/` imports `getNativeFunction` for these. Don't assume a JS-side
  caller exists; that's M10+.
- `util.voiceSum`/`DomainSplitter` (NODE_EDITOR.md §7, ADR pending for the
  domain-partitioning design) are real and tested at the engine level
  (`tests/DomainSplitterTests.cpp`), but `PluginProcessor`'s starting graph
  (`buildVoiceProofGraph()`) has no `util.voiceSum` node — the global-domain
  code path (`BazaltAudioProcessor::finalizeVoiceSumIntoOutput`,
  `hasGlobalDomain`) is real but dormant until a graph actually adds one via
  a command.
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
- `ui/src/canvas/StressTestCanvas.tsx`, `stressGraph.ts`, and
  `ui/src/canvas/webgl/` are M8's rendering-split benchmark spike (ADR-0008)
  — dev-only scaffolding reachable via App.tsx's "Run stress test (M8)"
  button, drawing a synthetic (not real-engine) 500-node/1000-cable layout.
  Same fate as the M4 `ui/src/App.tsx` benchmark spike: delete wholesale
  once M9/M10's real node editor exists, don't try to preserve or build on
  it.
- `BazaltAudioProcessor` has an explicit (non-defaulted) destructor that
  calls `analysisThread.stopThread (2000)` unconditionally, in addition to
  `releaseResources()` doing the same. This isn't redundant: hosts are
  expected to call `releaseResources()` before destroying a processor, but
  that isn't guaranteed (crashed hosts, test code), and `juce::Thread`'s own
  destructor falls back to an indefinite `stopThread(-1)` if the thread is
  still running — a real bug this session hit as a flaky `tests-plugin` test
  failure. Keep both call sites if you touch this code.
