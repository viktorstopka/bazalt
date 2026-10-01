# `util.macro` — a real, wireable macro node

**Status:** Built, 2026-10-01. All 4 code batches (engine, plugin, UI drag-to-
create, UI knob panel) landed and committed; this file is the durable project
record of the plan, written at completion rather than mid-discussion (contrast
`DomainRedesign.md`'s own still-stale "Proposed" status line, a gap this file
deliberately doesn't repeat). The formal decision record is
`archive_docs/decisions/0030-util-macro-is-a-real-wireable-node.md` — this file
keeps the fuller reasoning/narrative; the ADR keeps the terse Decision/
Consequences shape every other ADR uses.

---

## 0. Origin

Direct instruction: a real macro feature, built now, with three explicit
requirements —
1. a real, graph-native macro node good enough to actually build patches with;
2. drag an unconnected input port out and release on empty canvas to
   auto-create a correctly-typed, correctly-wired macro, in one gesture;
3. every placed macro automatically shows a knob in a panel at the top of the
   window.

`util.macro` had been the one open item in the whole M21+M22 node-batch arc
since M22 landed (`CLAUDE.md`'s own note), deferred behind
`archive_docs/decisions/0015-macro-binding-migration.md` — "Proposed (M14).
Not implemented," which explicitly declined `archive_docs/VALUE_MODEL.md` §6's
"wireable Macro node as relay pool" model.

## 1. The reconciliation ADR-0015 needed, not a reversal of it

Re-reading ADR-0015's own reasoning (not just skimming its conclusion) found
the real, load-bearing constraint wasn't "a macro can never have a wireable
output" — it was specifically about VST3's `restartComponent` behaving
inconsistently across hosts **if the host parameter list itself grows or
shrinks** as Macro nodes are placed or deleted.

`MacroParameters` (`plugin/source/MacroParameters.h/.cpp`) already creates
exactly 32 `juce::AudioParameterFloat`s once, unconditionally, at construction
(`addParametersTo()`). That pool's shape never changes, regardless of how many
`util.macro` nodes exist in the graph at any moment — ADR-0015's real
constraint is satisfied by construction, automatically, no matter how the
macro node itself works. A placed `util.macro` doesn't add a new host
parameter at all; it **claims** one of the 32 pre-existing ones via its own
structural `slot` parameter — a field `wiki/NODES.md`'s own `util.macro` entry
had specified since before M14, never implemented until now. Deleting the
node frees the slot. The count of live macro nodes is hard-capped at 32 by the
pool's own fixed size, full stop.

This is a correction of ADR-0015's specific **application mechanism** (a
hand-curated `{targetNodeId, targetParameterId}` side-table that pokes some
other node's `setParameter` from outside the graph, bypassing wires entirely)
— not a reversal of its real decision. The macro node becomes its own mapping
target instead, exposing the bound slot's smoothed value through an ordinary,
freely-wireable `Control` output port.

A dedicated stress-testing pass (reading `PluginProcessor::processBlock`,
`PlanSwapper.h`'s own audio-thread contract, `GraphCompiler.cpp`'s
construction order, and the retired M10 drag-to-macro code directly)
independently confirmed this reconciliation holds, and surfaced two real bugs
that had to be fixed as part of landing it, not left as footnotes.

## 2. Finding A — the global-domain plan never got macro updates

`PluginProcessor::processBlock`'s non-`monoOnly` branch only ever ran
`MacroParameters::applyToPlans` over the per-origin voice plans. Whenever
voices are active **and** the graph has real global-domain content — an
entirely ordinary patch shape, per-voice synths feeding a shared master filter
through `instance.sum` — the global plan was fetched only *inside*
`finalizeInstanceMixIntoOutput`, **after** `applyToPlans` had already run for
that block. Any macro mapping targeting global-domain content would have
silently never been applied. This was always latent (true of the 4 old
hardcoded default mappings too, had any of their targets ever lived in global
content) but became unavoidable once a macro can be wired into anything,
automatically, by a UI gesture.

**Fix:** fetch `globalPlanSwapper.getCurrentPlanForAudioThread()` once, in the
same place the per-origin voice plans are already fetched inside
`processBlock`, run `applyToPlans` on it there too, and thread that same
pointer into `finalizeInstanceMixIntoOutput` as a new parameter instead of
letting it re-fetch — `PlanSwapper.h`'s own documented contract is
"audio-thread, exactly once per `process()` call," and calling it twice would
have been a second, subtler bug layered on the first. Covered by a dedicated
regression test (`tests-plugin/GraphEditControllerTests.cpp`) wiring a macro
into `buildInitPatchGraph()`'s real global-domain `space.pan` node and
confirming the swept value actually reaches it with voices active.

## 3. Finding B — a transient slot-collision hazard

Creating a macro is necessarily `graphAddNode` immediately followed by a
*separate* `graphSetParameterValue(slot, N)` — `graphAddNode` can't seed
parameters. Each command is its own full recompile with its own
slot-collision check (`GraphEditController.cpp`'s one-recompile-per-command
architecture; see `CLAUDE.md`'s own "known interim simplification" note on
why composite gestures aren't yet batched into one recompile generally). If
`util.macro.slot`'s declared default were a real slot number (e.g. `0`), the
bare `addNode` step alone would collide with any already-placed macro at slot
0 and reject outright — aborting the whole creation gesture before the very
next command could ever fix it up.

**Fix:** `util.macro.slot` defaults to **-1**, a genuine "unclaimed" sentinel
(`minValue = -1`), excluded from both the collision check and from
`macroMappings` derivation. Every creation path (the drag gesture, and a
plain Add-menu'd bare Macro) sets the real slot as a *second*, separate
command inside the same undo-grouped gesture — the transient state between
the two commands is always, unconditionally, "unclaimed."

## 4. Decided (direct questions, answered before building)

- **Knob style:** knob-styled (circular, rotating indicator) but driven by
  the existing vertical-drag-changes-value interaction `ValueSlider.tsx`
  already has — not true angle-based rotary dragging. No new drag-
  interaction *algorithm* needed, just a different visual treatment of the
  same delta-based drag math.
- **Slot-reassignment warning:** a blocking confirm dialog when editing the
  `slot` parameter of an *already-placed* macro (never fires during
  creation — the internal slot claim bypasses the UI's commit path
  entirely). Matches this project's established "reject/confirm rather than
  silently do something risky" convention, and is exactly what ADR-0015
  itself already anticipated in its own Consequences section, now actually
  built.
- **Explicitly out of scope:** recreating the 4 old hardcoded default
  mappings as real macro nodes in some future default patch (nothing
  currently depends on them); seeding cosmetic `curve`/`polarity` on a
  created macro (no visible payoff yet — `ValueSlider.tsx` doesn't consume
  `curve`).

## 5. What was built

- **Batch 1 (engine):** `engine/include/bazalt/engine/nodes/MacroNode.h` — a
  real node, zero inputs, one `Control` output, structural `slot`/`min`/
  `max`/`isInteger`/`quantity`, no listed `value` parameter (written only by
  the direct `MacroParameters::applyToPlans` poke, never the command
  bridge — listing it would invite an ordinary `NodeCard` slider fighting
  host automation every block). `PatchDocument::macroMappings` dropped
  (schema v6→v7) — it's graph-derived now, never persisted.
  Commit `a78b53d`.
- **Batch 2 (plugin):** `GraphEditController::recompileAndPublish()` derives
  `macroMappings` from the live graph on every successful compile and
  rejects a slot collision (naming both node ids); Finding A's fix;
  `PluginEditor`'s 4 hardcoded relay/attachment members generalized to
  `std::vector`s, one per all 32 slots. Commit `d210794`.
- **Batch 3 (UI, drag-to-create):** the retired M10 `addMacroFromPort`/
  `macroConfigForPort` gesture (`f7f9a2e` introduced, `492e663` removed),
  revived in `graphStore.ts` as `createMacroFromPort`/`macroConfigForPort`,
  upgraded to the real M14 value-contract fields; a new `dragToMacro`
  gesture kind in `InfiniteCanvas.tsx`/`interactionStore.ts`; the slot-
  reassignment confirm dialog in `NodeCard.tsx`. Commit `a69e84b`.
- **Batch 4 (UI, knob panel):** `MacroSlider.tsx` (M5, dead since the M10
  rewrite) replaced by `MacroKnob.tsx` — same relay binding, knob-styled,
  vertical-drag; a new `.macro-panel` row in `App.tsx`, one knob per claimed
  macro, sorted by slot, rendering nothing with zero macros. Commit
  `f8ef767`.

No UI test runner exists in this project (no vitest/jest, no existing
`*.test.ts*` files) — Batches 3/4 are verified via `npm run build` (tsc
type-check + vite build) and `npm run lint` (oxlint), matching this project's
own established UI verification practice; the engine/plugin batches have real
Catch2 coverage (`ctest`: 483/483 green throughout). The actual drag-and-
release gesture in the live Standalone app needs a human hand on the WebView's
mouse to exercise end-to-end — not yet done as of this file landing.

See `archive_docs/decisions/0030-util-macro-is-a-real-wireable-node.md` for
the formal Decision/Consequences record, and `wiki/NODES.md`'s `util.macro`
entry for the current, user-facing node description.
