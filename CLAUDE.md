# Bazalt — project rules

Node-based modular synth, VST3 (JUCE + C++20) with a TypeScript/Vite/React
WebView UI. Full design: `docs/ARCHITECTURE.md`. Milestone plan:
`docs/MILESTONES.md`. Decisions: `docs/decisions/`. Read `ARCHITECTURE.md`
before touching anything cross-cutting — this file is the condensed
day-to-day ruleset, not a replacement for it.

Currently on **M2** (graph runtime) per `docs/MILESTONES.md`; M0 (scaffolding)
and M1 (engine core/DSP infra) are done and committed. Each milestone must
build, pass its tests, and be committed before the next one starts.

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
   wrong — redesign it, don't add the dependency.

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

# Validate the VST3 (pluginval must be installed separately)
pluginval --strictness-level 1 --validate ./build/plugin/BazaltPlugin_artefacts/Debug/VST3/Bazalt.vst3
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
  `ui/dist` binary-resource pipeline described in ARCHITECTURE.md §7. Wiring
  real `ui/dist` embedding is tracked for M5.
- `COMPANY_NAME`/`PRODUCT_NAME` in `plugin/CMakeLists.txt` are placeholders
  pending real publisher info.
