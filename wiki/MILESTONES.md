# Wiki milestones (0.x) — node & architecture gap-fixing arc

Not a continuation of `archive_docs/MILESTONES.md`'s M-numbering. That record stays
accurate for M0–M22 (shipped, real, unchanged) — `git log` remains the source of
truth for what's actually committed. This is a **separate, parallel plan**, numbered
`0.x`, for the specific arc kicked off by hands-on testing of the running app
surfacing real node-design and UI/architecture gaps M0–M22 didn't catch. See
`wiki/NODES_Gaps.md` for what was found and `wiki/NODES.System.md` §4/§6/§7 for the
architecture questions this arc reopens (the connection matrix's real-vs-aspirational
gap, the visual-language color table, the stereo-cable question).

Each milestone builds, passes its tests, and is committed before the next one starts
— same discipline `archive_docs/MILESTONES.md`'s M-arc used.

## 0.0 — Wiki scaffold — done

`docs/` → `archive_docs/` (git-mv, history preserved). New `wiki/NODES.md` (replaces
`NODE_CATALOG.md`, folds in both correction docs critically — not verbatim),
`wiki/NODES.System.md` (architecture rules + the full connection/adapter matrix, real
vs. aspirational columns), `wiki/NODES_Gaps.md` (0.1's output), `wiki/MILESTONES.md`
(this file). `CLAUDE.md`'s doc pointers repointed at `wiki/`.

## 0.1 — `wiki/NODES_Gaps.md`: generalize + scan every node — done

Every mistake you named, generalized into a named category, checked against all 55
real node headers. Confirmed: `redundant-composable-param` (`mix.sum`),
`jargon-naming` (`mix.gain`'s "VCA" title — confirmed; `filter.svf`'s "SVF Filter" —
flagged, lower confidence), `modulation-only-port` (`mix.gain.gain`, confirmed
high-severity; several `math.*`/`adapt.*` primary inputs, confirmed lower-severity),
`hardcoded-trigger` (`excite.burst`), `single-type-preview-coverage` (nuanced —
`view.scope`/`meter` already take Control/Boolean/Event; built-in automatic previews
are the actual Audio-biased gap). Root-caused the Master Out bug precisely
(`graphSetOutput` has zero UI call sites — confirmed by grep, not guessed). Flagged
`instance.allocator.random1`/`random2` as likely-intentional, for your confirmation
rather than treated as broken. Three items still need live repro before a fix is
designed: dropdown clicks, Note-port connectivity, Reroute connectivity.

## 0.3 — Master Out / output designation, fixed — done

`graphSetOutput` was already real and tested on the native side, just uncalled from
`ui/src`. Added the JS wrapper (`ui/src/graph/graphCommands.ts`), then wired it into
every gesture that can land a connection on a node's input
(`addWire`/`commitWireDrag`/splice-insert in `ui/src/graph/graphStore.ts`): if the
destination node's `typeId` is `io.output`, the graph's output designation is
auto-set to that node's primary output port right after the connect succeeds — no
new engine mechanism, no separate step the user has to know about. Also added a
general "Set as Output" right-click action (`NodeContextMenu.tsx`/`GraphSurface.tsx`)
for designating any node's output explicitly, not just Master Out's.

## 0.4 — Connection-replace UX — done

Fixed at the source rather than in the UI: `GraphEditController::connect()` and
`connectWithAutoAdapt()`'s final connection now call a new
`replaceExistingInputConnection()` helper that removes any existing connection
already targeting that `(toNodeId, toPortId)` pair before adding the new one —
`GraphCompiler`'s "Input port already connected" check still exists as a safety net,
it just never fires on an ordinary user gesture anymore. Covers direct connects, the
adapter-chain path, and the polymorphic-endpoint (Reroute) path, since all three
funnel through `connect()`. New regression test
(`tests-plugin/GraphEditControllerTests.cpp`, "Connecting into an already-wired input
replaces the old connection instead of being rejected") — mutation-checked: reverted
the fix, confirmed the test fails with the old rejection, restored it.

**Verified:** 354/354 tests green (353 + the new one), `pluginval --strictness-level
10` SUCCESS, `npm run build`/`npm run lint` clean, Standalone app launches with no
regressions (screenshot-checked). Interactive wire-drag re-verification of the exact
UI gesture is still worth doing by hand when convenient — not attempted here given
this environment's documented unreliability with synthetic mouse input into WebView2
content (see the project memory's M10 note); the engine-side fix is proven by a real,
mutation-checked test, and the UI-side wiring reuses the same `callCommand`/
`getNativeFunction` path every other already-working graph command uses, with the
exact native-function name confirmed to match on both sides by direct source reading.

## 0.5 — Node-level fixes from the confirmed `NODES_Gaps.md` entries — done (the four confirmed ones)

Done at the user's explicit go-ahead ("go ahead with 0.5, I'll review the gaps doc
after") — the review is still pending and still meaningful; these were the four
findings already at high confidence, not a reason the review no longer matters.

- **`mix.gain`** (`GainNode.h`): title "VCA" → "Gain" (`jargon-naming`); `gain` input
  gained a real unconnected default, unity/1.0 (`modulation-only-port`) — unpatched is
  now genuinely "just as loud as before" instead of multiplying by NaN.
- **`mix.sum`** (`MixNode.h`): `level.N` removed (`redundant-composable-param`) — now a
  plain sum; use a real `mix.gain` node for per-input level. **Patch schema v4** +
  a real v3→v4 migration (`PatchDocument.h`/`PatchSerializer.cpp`) splices a `level.N`
  that was ever touched (non-default constant, or itself connected to a modulator)
  into a real, visible `mix.gain` node at load time — never silently dropped. A
  `level.N` left at its default needs no migration at all.
- **`excite.burst`** (`NoiseBurstNode.h`): real `trigger : Event` + `duration` input
  ports (`hardcoded-trigger`) — any Event source can start it now, not just a direct
  C++ poke (which still exists, for tests/tools, and now shares one implementation
  with the real path).

**Left untouched, deliberately** — the lower-confidence items `NODES_Gaps.md` itself
flagged as needing your judgment, not blindly fixed alongside the confirmed ones:
`filter.svf`'s "SVF Filter" title, the `math.*`/`adapt.*` no-fallback pattern
(`subtract`/`abs`/`minmax`/etc.), and `instance.allocator`'s `random1`/`random2`
(reads as intentional design, not a mistake).

**Verified:** 355/355 tests green (one new regression test for the migration, with a
real bug in the migration itself caught and fixed along the way — it originally only
discovered `level.N` values that had a stored parameter, missing ones that existed
only as a connection target; the test caught this before it shipped). `pluginval
--strictness-level 10` SUCCESS (one transient "Parameter thread safety" timeout
investigated via a stash-based isolation check — reproduced identically with these
changes fully reverted, confirmed environmental/scheduling, not a regression; passed
cleanly on retry with the changes back in place). Standalone app sanity-checked,
no regressions.

## 0.2 — Stereo — done

Went through three passes before landing: a scoping-only design (never built), a
narrow point-fix (Master Out gained a second channel via a special case, every other
node kept separate `left`/`right` ports), then — on the user's explicit request to
"build the full redesign now" after questioning why `space.pan` still needed two
cables — the real thing, which supersedes and replaces the point-fix rather than
sitting alongside it. Full detail in `wiki/NODES.System.md` §9.

**What shipped:** a stereo signal is one real Audio cable, catalog-wide.
`GraphCompiler.cpp` now backs `canConnect`'s already-existing mono/stereo rules
(mono→mono, mono→stereo free broadcast, stereo→stereo, stereo→mono needs downmix)
with real per-channel buffers — a `Channels::Stereo` port occupies two flat buffer
slots, resolved once per compile, with zero change to `Node::processSample`/
`processBlock`'s signatures (proven first against synthetic node types in
`tests/GraphCompilerTests.cpp`, before any real node was touched). Six real nodes
redesigned onto one stereo port per side: `space.pan`, `space.width`, `io.output`,
`mix.downmix` (now also auto-insertable by `connectWithAutoAdapt`, closing a second,
related gap), `stereo.split`, `stereo.combine`. The Init Patch wires `pan.out` →
`masterOut.in` as one cable — a fresh plugin instance opens playing genuinely panned
stereo.

**A real bug found during implementation, not by inspection:** `Node.h`'s default
`processBlock()` sized its scratch loop from descriptor counts, not flat channel
counts — a node with one Stereo output but no override would silently leave its
second channel uninitialized. Caught by a synthetic test reading back
`-431602080.0f` (classic uninitialized-debug-memory pattern). Fixed by adding
`Node::getNumInputChannels()`/`getNumOutputChannels()`, defaulting to the existing
port-count methods (a no-op for every node that never overrides them).

**CLAUDE.md rule 3 ("port ids never renamed once shipped") is suspended**, on the
user's own explicit instruction — see that rule's own note in CLAUDE.md for the full
reasoning and the re-enable trigger. This is what made the redesign tractable without
elaborate migration-bridging code: every one of the six nodes' port ids changed
directly, no v4→v5 data migration was written (schema version still bumped to 5 for
hygiene, with a trivial version-only migration so old patches still parse).

**Deliberately out of scope** (§9.4 has the full list): tap/preview lookups on a
stereo port read channel 0 only; no visually distinct stereo cable in the UI (not
needed — `channels` was already unread anywhere in `ui/src` except `canConnect.ts`);
the mono-only no-allocator render path untouched.

**Verified:** 365/365 tests green (new `[Stereo]`-tagged synthetic compiler tests
proving flat-slot allocation directly; real-node coverage across
`tests-plugin/HostInputTests.cpp`, `tests/SpaceNodesTests.cpp`,
`tests-plugin/ConnectWithAutoAdaptTests.cpp`). A real mutation-testing pass on the
mono→stereo broadcast logic crashed on a debug assertion rather than silently
passing, confirming it's load-bearing. `pluginval --strictness-level 10` SUCCESS. UI
`npm run build`/`npm run lint` clean with zero `ui/src` changes. Standalone app built,
launched, and sanity-checked against the redesigned Init Patch.

## 0.3 — Master Out / output designation, fixed

Wire `graphSetOutput` into the UI: auto-call it when a cable is dropped onto
`io.output`'s input, plus a general "Set as Output" context-menu action on any node's
output. No new engine mechanism — the bridge command already exists and is tested.

## 0.4 — Connection-replace UX

Wiring into an already-occupied input auto-disconnects the old cable first, instead
of rejecting the new one. Locate the exact UI drop-handler call site.

## 0.5 — Node-level fixes from the confirmed `NODES_Gaps.md` entries

Executed only after your review of 0.1's findings corrects/confirms them. Expected
shape: `mix.gain` title fix, `mix.gain.gain` gets a real unconnected default,
`mix.sum`'s `level.N` removed in favor of auto-inserted `mix.gain`, `excite.burst`
gets a real `trigger : Event` port. Exact scope finalized after your review.

## 0.6 — Minimal preview nodes for non-Audio types — done

Built exactly the shape you described: `view.glance`, a new node
(`ViewGlanceNode.h`) using a new `NodeLayoutVariant::Glance` — no title, no
parameter list, just an input glyph, a compact live preview, and an output glyph.
Polymorphic (Audio/Control/Boolean/Event, same mechanism `util.reroute`/`view.scope`
already use) and a real passthrough (unlike `view.scope`/`meter`/`spectrum`, which
only tap a wire from the side, this one has a real output and splices directly into
an existing cable). New UI layer end to end: `descriptorTypes.ts`'s
`NodeLayoutVariant` union, `NodeDescriptorJson.cpp`'s serializer, `NodeCard.tsx`'s
`GlanceBody` (reusing `SingletonGlyph`, which turned out to already be generic
enough to reuse rather than singleton-specific), and matching CSS.

**Left open, deliberately** (see `wiki/NODES_Gaps.md`): whether specific nodes
(`env.adsr`, `random.stepped`, ...) should gain an *automatic* built-in preview the
way `osc.analog`/`mix.gain` already do — a separate, smaller question this milestone
didn't try to answer; `view.glance` closes the "no minimal way to look at a non-Audio
signal at all" gap on its own.

**Verified:** 358/358 tests green (3 new: a polymorphic type/quantity-adoption +
passthrough test, a through-the-real-compiler splice test, and a JSON
layout-variant/preview round-trip test — `tests/InheritingPortsTests.cpp`,
`tests-plugin/NodeDescriptorJsonTests.cpp`). `pluginval --strictness-level 10`
SUCCESS (one transient "Parameter thread safety" timeout on the first run, passed
clean on retry — the same known-environmental symptom isolated during 0.5, not
re-investigated from scratch given the prior isolation already covers it). UI
build/lint clean. Standalone app sanity-checked.

## 0.7 — Live UI bug fixes — done

Both original working theories in `wiki/NODES_Gaps.md` turned out wrong once actually
checked against the real code — neither bug was where it looked like it would be.

- **Dropdowns not opening**: not a pointer-capture/canvas-drag conflict
  (`InfiniteCanvas.tsx`'s `isOwnGestureTarget()` already excludes `.trigger-select`
  correctly, and its raw `click` listener no-ops with no placement ghost active). The
  real cause: `.trigger-select`'s CSS copied `.value-slider`'s `overflow: hidden`
  wholesale — load-bearing there (clips the slider's fill bar), but it silently
  clipped `.trigger-select-menu` (a DOM child, positioned below the 20px pill) to zero
  visible height instead. The click handler and `open` state were never broken.
  Fixed by dropping that one property.
- **Note-port "same color won't connect"**: not a Note-buffer fan-out limitation
  (`GraphCompiler.cpp`'s `noteInputsUsed` guard is a plain "one source per input" rule,
  identical to every ordinary connection — Note fan-*out* was never restricted). The
  real cause: `portUiKind.ts`'s classifier had no case for `SignalType::Note` at all,
  so it silently rendered the exact same white a real-quantity Control port uses —
  every rejection the user saw was a genuinely correct type mismatch, just wearing a
  borrowed color. Fixed with a new, distinct `'note'` kind (teal, `♪` glyph) in the one
  shared classifier every consumer (node cards, the gallery legend, the WebGL cable
  layer) already reads from.

**Verified live**, without synthetic mouse input (this environment doesn't reliably
deliver it to WebView2 content, and UI Automation doesn't reliably expose that content's
tree either — both documented in earlier sessions): temporarily forced
`TriggerSelect`'s `open` state to `true` in code, screenshotted the Standalone app, saw
both dropdowns render their full option lists unclipped, reverted immediately. Same
screenshot also showed `instance.allocator`'s `spawn` port in the new teal, visibly
distinct from the white ports beside it. UI build/lint clean both before and after the
revert.

## Verification, every wave from 0.2 on

Build + `ctest` green, `pluginval --strictness-level 10`, `cd ui && npm run build &&
npm run lint` clean, build+launch the Standalone app to confirm by ear/eye (never
browser testing). Commit each milestone once green, without asking.

---

# `09-28-InstanceAllocator` — a separate arc, its own numbering

Not part of the `0.x` sequence above — a self-contained arc scoped from a real bug report
(`wiki/reports/InstanceAllocator_2026-09-28.md` has the full reasoning, code citations, and
alternatives considered for every milestone below; this is the plan, not a restatement of the
report). Numbered by date + feature name rather than sequentially, since it's parallel to, not
part of, the `0.x` arc's own progression. **`.1` is done; `.2`–`.4` are not yet built** — the
rest was written as the approved forward plan, same pattern the `0.x` arc itself used when it
started, and stays that way until each ships.

**The bug, precisely:** adding an `instance.allocator` node to a graph — even completely
disconnected from anything — silently converts the *entire* graph from "always-on, runs every
block" to "per-voice, only runs while a voice is triggered." `DomainSplitter::split()`'s own
shortcut is the cause: when no `instance.mix` node exists, the whole graph becomes the "voice
graph" unconditionally, with no check on whether the graph's designated output is actually
reachable from the allocator.

**Not needed to fix this:** the PhasePlant-style "a voice-triggered part and a separate always-on
part, both audible at once" coexistence the user asked about is already fully supported today via
`instance.mix` (already real, already tested — a voice chain and an independent global source both
feeding one `mix.sum`, bridged only through `instance.mix`). What's actually broken is narrower:
the *no-`instance.mix`* case, where an allocator's mere existence shouldn't matter to content it
isn't wired to.

## `09-28-InstanceAllocator.1` — the reachability fix — done

**Root cause:** `DomainSplitter.cpp`'s `instanceMixCount == 0` branch sets `result.monoOnly =
(instanceAllocatorCount == 0)` and always returns the *whole* graph as `voiceGraph` — never
checking whether the graph's designated output is reachable from the allocator at all.

**The fix:** when `instanceMixCount == 0` and `instanceAllocatorCount == 1`, compute forward
reachability from the allocator's own node id using the file's own existing `reachableFollowing()`
helper (already used elsewhere in the same file, just not in this branch, and not forward from an
allocator yet), and set `monoOnly = (graph.getOutputNodeId() is NOT in that reachable set)`.
Everything else in this branch — `voiceGraph = graph` unconditionally, `hasGlobalDomain = false` —
stays exactly as today. No change to `DomainSplitResult`'s shape, `PluginProcessor.cpp`, or
`GraphEditController.cpp` needed: a disconnected allocator sitting inside a `monoOnly`-dispatched
plan simply never receives a `noteOn()` poke (MIDI dispatch only targets per-voice plan slots), so
it sits inert — exactly matching its own real contribution to the audible signal.

**Tests:** `tests/DomainSplitterTests.cpp`'s `"A graph with no instance.allocator and no
instance.mix is a mono graph; one with an allocator is not"` currently encodes the bug as
contract (asserts `monoOnly` becomes `false` for a disconnected allocator) — rewritten to assert
`monoOnly` stays `true`, plus a new case in the same test for the legitimate "fully wired `noteIn →
alloc → osc → output`, no `instance.mix`" graph, asserting `monoOnly` is correctly `false` there.
Every other existing `DomainSplitterTests.cpp` case confirmed untouched (none exercise the
`instanceMixCount == 0` branch). New `tests-plugin` case matching the literal repro: `osc.sine →
io.output` plus a disconnected `instance.allocator`, asserting real audio reaches the host output.

**Verified:** 369/369 tests green (up from 365 — the rewritten `DomainSplitterTests.cpp` case
split into two, plus the new `tests-plugin` literal-repro case). Mutation-tested at both layers:
forced the fix's reachability check to a constant `false`, confirmed both the engine-level
`DomainSplitterTests.cpp` cases AND the plugin-level real-audio test caught it (the plugin one
failing with the exact symptom — `rms == 0.0`, genuine silence — not just a flag mismatch).
`pluginval --strictness-level 10`: one "Parameter thread safety" timeout, isolated via
`git stash` (reproduced identically with this milestone's changes fully removed — confirmed
environmental, not a regression), passed clean on retry with the changes restored.

**Also folded in** (same file, directly related): `archive_docs/decisions/
0020-instance-allocator-lifetime.md` still said "Status: Proposed (M17). Not implemented" — false,
Voice mode shipped M17-M18. Corrected honestly, including naming that the ADR's own "generalize
`DomainSplitter` to N allocator regions" decision was never carried out and stays out of scope here
(still a hard `instanceAllocatorCount > 1` rejection) — real, separate, larger future work, named
so it isn't silently lost.

## `09-28-InstanceAllocator.2` — `random1`/`random2` real determinism

**Root cause:** `InstanceAllocatorNode::prepare()` calls `random.setSeedRandomly()` — reseeded
randomly per plugin-instance-lifetime, not from a real patch-level seed. Directly contradicts the
one stated reason these ports exist as allocator-owned state rather than a plain `random.*` node
(`archive_docs/DOMAINS.md` §4): "the same patch, the same MIDI, the same seed produce bit-identical
output... required for the offline render CLI to be a useful regression tool." Today it doesn't.

**The fix:** add `instance.allocator.seed` (structural parameter, matching `random.drift`/
`random.stepped`'s existing `seed` convention — integer, fixed default, not time-based). Stop
maintaining a persistent `juce::Random` member seeded once at `prepare()`; instead, at each
`noteOn()`, draw `random1`/`random2` from a `juce::Random` constructed fresh from
`hashCombine(seed, instanceIndex)`, so the value is a pure function of (patch seed, spawn ordinal)
— no dependency on wall-clock time, call order, or elapsed blocks.

**Tests:** a real regression test compiling the same graph twice from a clean `prepare()`, same
seed, asserting `random1`/`random2` are bit-identical across runs; a second case with a different
seed asserting the values differ. Mutation-checked (temporarily revert to `setSeedRandomly()`,
confirm the new test catches the non-determinism, revert back).

## `09-28-InstanceAllocator.3` — split into a real `instance.voice` node, drop the dead configuration dropdown

**Root cause:** `instance.allocator`'s `configuration` enum (Voice / Swarm-population /
Swarm-transient / Trigger) is real, visible UI surface today — `setParameter()`'s own comment
admits three of the four options do nothing. The four configurations don't even share a port shape
(Voice needs a `Note` `spawn` input; Swarm-population needs none at all) — a real structural
mismatch for one node with a mode switch, unlike `mix.downmix`'s legitimate same-shape mode enum.

**The fix:** rename the type id `instance.allocator` → `instance.voice` (direct rename — CLAUDE.md
rule 3 is suspended, no migration needed; every real reference updated: `ProofGraphs.h`'s factory
registration and Init Patch, `DomainSplitter.cpp`'s `instanceAllocatorTypeId` constant, every test
graph that builds one, `wiki/NODES.md`). Remove the `configuration` parameter and its backing
member entirely — one real configuration means a single-option enum is UI clutter, not a choice.
Rename the source file/class too (`InstanceAllocatorNode.h` → `InstanceVoiceNode.h`, class
`InstanceVoiceNode`) rather than leaving an `Allocator`-named class behind a `voice` type id. Title
becomes "Voice" — category stays "Domain" (the Add menu's existing category grouping, confirmed
already real in `AddMenu.tsx`, gives the context, not the node's own name).

**Explicitly not built here:** `instance.swarmPopulation`/`instance.swarmTransient`/
`instance.trigger` as real node types — created later, as their own real milestones, once actual
Swarm/Trigger runtime machinery exists, not as empty shells now (which would just recreate the
same dead-surface problem this milestone fixes).

**Tests:** every existing `"instance.allocator"` string reference updated (a stale one fails at
graph-compile time with a clear "unknown node type" error, not silently — mechanical/grep-driven).
New test confirming `getParameters()` no longer exposes a `configuration`-named parameter. Full
suite green, `pluginval`, Standalone sanity check that the Init Patch (which uses this node) still
plays.

## `09-28-InstanceAllocator.4` — a visual indicator for voice-domain cables

**Root cause:** no way to see, by looking at the graph, which cables run per-voice versus once —
only discoverable by mentally tracing allocator reachability, or hitting a compile error after the
fact. `archive_docs/DOMAINS.md` §11's own open-questions list already asked this in the original
design pass, years ago, and already leaned toward an answer: *"It must not collide with the type
palette, so the proposal is line style or weight rather than colour."*

**The fix** (more design-open than `.1`–`.3` — exact visual treatment decided during this
milestone, not pre-specified here): surface per-node domain membership (voice / global) through
the existing `NodeInstance`/descriptor bridge, the same way `bypassed` already rides along — no new
per-connection concept needed, since a cable's domain is derivable from the domain of the nodes it
connects (both ends agree except exactly at the `instance.mix` boundary). Render the distinction in
`ui/src/canvas/webgl/nodeEditorRenderer.ts`'s existing cable-drawing code as a stroke-weight (or
style) difference, per `DOMAINS.md`'s own steer away from color.

**Sequenced last deliberately:** it's describing `.1`–`.3`'s own output (domain-splitting is only
*correct* as of `.1`; the node types it's labeling are only honest as of `.3`).

## Explicitly out of scope for this whole arc

Named so nothing is silently dropped, not because any of it is wrong:
- Lifting `DomainSplitter`'s one-allocator-per-graph limit (ADR-0020's own un-carried-out
  decision) — real, larger, separate work.
- `note.*`/`clock.*` (arpeggiator/chord generation) and Swarm-population/Swarm-transient/Trigger's
  real runtime behavior — entirely unbuilt, large, M28-territory features the report answered
  questions about but did not recommend building now.
- A random node seedable from `instanceIndex` (the report's answer to "could sample-and-hold +
  random nodes fully replace `random1`/`random2`") — a real, buildable idea, explicitly not
  recommended as urgent in the report, not included here.
- The Add-menu category-nesting / node-id-naming-convention question — the user's own proposal
  from the same conversation, explicitly excluded per their own instruction.

## Verification, every `09-28-InstanceAllocator` milestone

Same standing discipline as the `0.x` arc: build + `ctest` green, mutation-test the
correctness-critical logic (`.1`'s reachability check, `.2`'s determinism), `pluginval
--strictness-level 10`, UI `npm run build && npm run lint` clean (only `.4` touches UI code),
Standalone app launched and sanity-checked against the Init Patch (which uses this node for real).
Each milestone commits on its own once green, without asking.
