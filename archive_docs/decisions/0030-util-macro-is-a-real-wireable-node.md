# 0030 — `util.macro` is a real, wireable node; ADR-0015's slot pool stays, its binding mechanism doesn't

## Status
Accepted and implemented, 2026-10-01 (`wiki/plans/UtilMacro.md`). Amends ADR-0015 ("Macro binding
stays parameter/port-ID-addressed; no wireable Macro-node relay") — that decision's real, load-bearing
constraint (a fixed 32-slot `AudioProcessorParameter` pool, never growing/shrinking) is preserved
exactly; only its specific *application* mechanism (a hand-curated `{targetNodeId,
targetParameterId}` side-table poking a foreign node's `setParameter` from outside the graph) is
replaced. ADR-0015 itself is left as written, not retroactively edited — this project's own
convention for an amended ADR (see ADR-0022's relationship to ADR-0017, the one other example of this
in the current set): the new decision states the relationship, the old one stays a historical record
of what was reasoned at the time. `ctest --test-dir build -C Debug`: 483/483 green throughout (engine
+ plugin suites). `npm run build`/`npm run lint` (ui/): clean. Manually verified: the Standalone app
builds and launches on the final tree; the actual drag-to-create gesture needs a human hand on the
WebView's mouse to exercise end-to-end, not yet done as of this ADR landing.

## Context
`util.macro` had been the one open item in the whole M21+M22 node-batch arc since M22 landed
(CLAUDE.md's own note), deferred behind ADR-0015 — "Proposed (M14). Not implemented." Direct
instruction asked for it built for real, with three explicit requirements: (1) a real, graph-native
macro node good enough to actually build patches with; (2) drag an unconnected input port out and
release on empty canvas to auto-create a correctly-typed, correctly-wired macro in one gesture; (3)
every placed macro automatically shows a knob in a panel at the top of the window.

Re-reading ADR-0015's actual reasoning (not just its conclusion) found the real constraint wasn't
"a macro node can never have a wireable output" — it was specifically about VST3's `restartComponent`
behaving inconsistently across hosts if the *host parameter list itself* grows or shrinks as Macro
nodes are placed/deleted. `MacroParameters` (`plugin/source/MacroParameters.h/.cpp`) already creates
exactly 32 `juce::AudioParameterFloat`s once, unconditionally, at construction
(`addParametersTo()`) — that pool's shape never changes regardless of how many `util.macro` nodes
exist in the graph, satisfying ADR-0015's real constraint by construction. A placed `util.macro` node
doesn't add a new host parameter; it *claims* one of the 32 pre-existing ones via its own structural
`slot` parameter (`wiki/NODES.md`'s own `util.macro` entry had specified a `slot` field for exactly
this since before M14, never implemented). Deleting the node frees the slot. The count of live macro
nodes is hard-capped at 32 by the pool's own fixed size.

This reconciliation was independently stress-tested (reading `PluginProcessor::processBlock`,
`PlanSwapper.h`'s audio-thread contract, `GraphCompiler.cpp`'s construction order, and the retired M10
drag-to-macro code directly), which surfaced two real, concrete bugs load-bearing for this decision —
fixed as part of landing it, not left as footnotes:

- **Finding A.** `processBlock`'s non-`monoOnly` branch only ran `MacroParameters::applyToPlans` over
  the per-origin voice plans, never the global-domain plan — a macro wired into anything downstream of
  `instance.sum` (an entirely ordinary patch shape: per-voice synths feeding a shared master filter)
  would have silently never seen its mapping applied, because the global plan used to be fetched only
  inside `finalizeInstanceMixIntoOutput`, *after* `applyToPlans` had already run over the mono/voice
  case. This was always latent (true of the 4 old hardcoded default mappings too, had any of their
  targets ever lived in global-domain content) but became unavoidable once a macro can be wired into
  anything, automatically, by the UI gesture.
- **Finding B.** Creating a macro is necessarily `graphAddNode` immediately followed by a *separate*
  `graphSetParameterValue(slot, N)` command — each one its own full recompile with its own
  slot-collision check (`GraphEditController.cpp`'s one-recompile-per-command architecture,
  CLAUDE.md's own "known interim simplification" note). If `util.macro.slot`'s declared default were a
  real slot number, the `addNode` step alone could collide with an already-placed macro and reject the
  whole creation gesture before the very next command could fix it up.

## Decision
`util.macro` (`engine/include/bazalt/engine/nodes/MacroNode.h`) is a real registered node type: zero
inputs, one `Control` output (`"out"`), structural parameters `slot` (int, range [-1, 31], default
**-1** — a genuine "unclaimed" sentinel, Finding B's fix: excluded from the slot-collision check and
from mapping derivation, so the transient state between `addNode` and the slot-claiming
`setParameterValue` is always safely "unclaimed"), `min`/`max` (unbounded float), `isInteger`
(bool-shaped), `quantity` (the real M14 value-contract enum, `Dimensionless`..`Phase`). Deliberately
**no** `value` parameter in `getParameters()` — it's written only by `MacroParameters::applyToPlans`'s
direct `Node::setParameter()` poke every audio block, never through the ordinary command bridge;
listing it would invite an ordinary `NodeCard` slider fighting host automation every block.
`processSample()`: `out = min + value * (max - min)`.

`GraphEditController::recompileAndPublish()` scans the live graph for `util.macro` nodes on every
successful compile: rejects two nodes claiming the same in-range slot (naming both node ids), and
derives `macroMappings` fresh as `{slot, nodeId, "util.macro.value", 0, 1}` per claimed node —
replacing `setDefaultMacroMappings()`'s old fixed 4-entry table outright (dead code: it targeted node
ids `"osc"/"svf"/"env"` that only ever existed in `buildVoiceProofGraph()`, not the master-out-only
graph that's been the real constructor default since 2026-09-29). `PatchDocument::macroMappings`
(schema v6) is dropped entirely — schema v7 — since the mapping is now fully graph-derived, never a
second, independently-persisted source of truth that could drift from the nodes/connections that are
the real one.

Finding A's fix: `processBlock`'s non-`monoOnly` branch now fetches the global plan once, in the same
place the per-origin voice plans are already fetched (not inside `finalizeInstanceMixIntoOutput`),
runs `applyToPlans` on it there too, and threads that same pointer into
`finalizeInstanceMixIntoOutput` as a new parameter rather than letting it call
`PlanSwapper::getCurrentPlanForAudioThread()` a second time — that call's own documented contract is
"audio-thread, exactly once per `process()` call."

`PluginEditor`'s 4 hardcoded named `WebSliderRelay`/`WebSliderParameterAttachment` members (ADR-0013)
are now `std::vector<std::unique_ptr<...>>`, one per `MacroParameters::numMacros` (32) slot, since a
real `util.macro` node can claim any of the 32, not just the first 4 — `MacroSlider.tsx` (dead code
since the M10 rewrite, never imported) is replaced by `MacroKnob.tsx`: the same relay binding,
knob-styled (circular dial, rotating indicator — decided over true angle-based rotary dragging) and
driven by vertical-only delta-based drag. The relay's own `juce::AudioParameterFloat` range is always
a plain 0..1 identity (`addParametersTo()`), so `MacroKnob.tsx` is handed its real display range
(`min`/`max`/`isInteger`/unit) from the graph mirror, not the relay, and does the `min + raw *
(max - min)` conversion itself — the engine and the UI independently implementing the identical
formula is accepted duplication, not a shared-constant gap, the same way `MacroNode::processSample()`
and `ValueSlider.tsx`'s own raw-storage-vs-displayed-value split already are.

The drag-a-port-out-to-a-Macro gesture (retired at M10 — `addMacroFromPort`/`macroConfigForPort`,
commit `f7f9a2e` introduced it, `492e663` removed it when the canvas went real-graph-only) is revived
as `createMacroFromPort`/`macroConfigForPort` in `graphStore.ts`, upgraded to the real M14 value-
contract fields (`isInteger`/`quantity`/`enumOptions`) that didn't exist at M10 time. One undo step
(`addNode` → claim a free slot as a **separate** command, per Finding B → seed
`min`/`max`/`isInteger`/`quantity`/`unit`/`value` → `connectWithAutoAdapt`), modeled on `spliceInsert`'s
own composite-command shape (ADR-0025). `pickFreeMacroSlot()` also runs on the ordinary `addNode()`
path, so a plain Add-menu'd bare Macro auto-claims a slot too.

Editing an *already-placed* macro's own `slot` parameter (`NodeCard.tsx`) shows a blocking confirm
dialog first — ADR-0015 itself already anticipated this exact requirement in its own Consequences
("`util.macro`'s slot reassignment... changes a live host-automation binding — the UI warning
`VALUE_MODEL.md` §6 already calls for is still required"), now actually built.

## Consequences
- ADR-0004/ADR-0015's real, load-bearing cross-host guarantee is unchanged: the host parameter LIST
  never grows or shrinks, regardless of how many `util.macro` nodes are placed or deleted. This
  decision does not reopen that question.
- ADR-0013's relay mechanism is generalized (4 → 32 slots) but not replaced — still the same
  first-party JUCE `WebSliderRelay`/`WebSliderParameterAttachment` binding, still bound only to the
  fixed pool, never to an arbitrary number of Macro-node instances directly.
- A structural setting is still never bindable (ADR-0014's `isStructural` flag) — unaffected by this
  decision, exactly as ADR-0015 already stated.
- `PatchDocument::currentSchemaVersion` 6→7 (`macroMappings` dropped, `macroValues` kept) —
  `migrateV6ToV7` silently drops any v6 file's `macroMappings` content on load rather than failing to
  parse; nothing before this ever produced any other shape for that field, so nothing of value is
  lost.
- If real host-side needs ever outgrow 32 fixed slots, that is still the separate, larger ADR
  ADR-0015 already deferred it to (VST3 dynamic-parameter-list support) — this decision doesn't touch
  that question either.
