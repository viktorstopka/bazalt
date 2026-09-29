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

**A second, related bug found live during verification** (same user-reported symptom —
"instance.allocator's gate shows 0 movement" — on a real patch built through the normal editor
workflow): `PluginProcessor::findNoteIn` looked up the plan's `io.noteIn` node by a **hardcoded
instance id**, `"noteIn"` — but the editor's own Add-menu auto-generates ordinary ids (`"node2"`,
`"node3"`, ...) for a placed node, never that specific string. Every graph built entirely through
the UI therefore never delivered a single MIDI note to its allocator, silently. Fixed the same way
`ExecutionPlan::externalInputNodeId` already solves an identical problem for `instance.mix`:
`GraphCompiler::compile()` now resolves the id of whichever node has type `"io.noteIn"` once, at
compile time (`ExecutionPlan::noteInNodeId`, new field) — `findNoteIn` reads that instead of a
literal. A first version of the regression test for this (wiring `osc.analog`'s pitch straight from
the allocator and checking the raw oscillator) passed even with the bug deliberately reintroduced —
`osc.analog` free-runs off `instance.allocator.pitch`'s already-valid default (60.0f) the moment its
voice slot is marked active (`VoiceManager::noteOn()`, called unconditionally in `handleMidiEvent`
*before* `findNoteIn` is ever reached), completely independent of whether the noteIn poke this fix
targets actually succeeds. Corrected by gating a `mix.gain` VCA through a real `env.adsr` fed from
`instance.allocator.gate` — gate only ever becomes true via a real `noteOn()` reaching the
allocator, so silence genuinely means "the note never arrived." That corrected version does catch
the mutation (`rms == 0.0` with the fix reverted).

**Verified:** 370/370 tests green (up from 365 — the rewritten `DomainSplitterTests.cpp` case
split into two, the new `tests-plugin` literal-repro case, and the `findNoteIn`-by-type regression
test). Mutation-tested at both layers for the reachability fix (forced the check to a constant
`false`, confirmed both the engine-level `DomainSplitterTests.cpp` cases and the plugin-level
real-audio test caught it) and separately for the `findNoteIn` fix (reverted to the hardcoded id,
confirmed the corrected regression test — not the first, flawed version — catches it).
`pluginval --strictness-level 10`: the "Parameter thread safety" timeout recurred on this same
change too; already isolated once this session (`git stash` comparison, confirmed environmental),
passed clean on retry both times.

**Also folded in** (same file, directly related): `archive_docs/decisions/
0020-instance-allocator-lifetime.md` still said "Status: Proposed (M17). Not implemented" — false,
Voice mode shipped M17-M18. Corrected honestly, including naming that the ADR's own "generalize
`DomainSplitter` to N allocator regions" decision was never carried out and stays out of scope here
(still a hard `instanceAllocatorCount > 1` rejection) — real, separate, larger future work, named
so it isn't silently lost.

**A third bug found live, this time in the fix above** (same symptom again, reported against the
user's own real, mid-build patch — `noteIn → allocator`, allocator's `gate` output not yet wired to
anything past a `view.glance` tap, Master Out fed by something else entirely): the reachability fix
above set `monoOnly = true` whenever the allocator's chain didn't reach the designated output — but
`monoOnly` isn't just a rendering-path switch, it's also what `PluginProcessor::handleMidiEvent`
gates *all* MIDI dispatch on (`if (monoOnlyGraph.load(...)) return;`). A real allocator, honestly
wired to a real `noteIn`, stopped receiving MIDI at all the moment its own output wasn't reachable
yet — exactly the mid-build state a user *inspecting* an allocator's gate via Glance, before wiring
it further, is in. Not a Glance bug, not a MIDI-settings bug: a real gap in this same milestone's
first pass.

**The fix:** decoupled two questions that had been conflated into one boolean. Whether a real
`instance.allocator` gets per-voice plans and real MIDI dispatch is now unconditional — `monoOnly`
stays `false` whenever an allocator genuinely exists, full stop. Reachability now answers a
separate question only: whether an "independent global region" needs to run *alongside* the voice
plans to produce the actual audible output, when the voice domain doesn't reach it. This reuses the
existing `hasGlobalDomain`/`globalGraph`/`PlanSwapper` machinery (previously exercised only by the
`instance.mix`-bridged case), distinguished by `instanceMixNodeId` being empty for the new,
unbridged case (`DomainSplitter.h`'s `hasGlobalDomain`/`instanceMixNodeId` comments updated to
document both cases). `PluginProcessor::finalizeInstanceMixIntoOutput` gained the matching branch:
when `hasGlobalDomain` is true but `externalInputNodeId` is empty, the global plan runs
unconditionally every block (never fed a voice sum) and its own output is what's audible — the same
role `monoOnly` used to play alone. `GraphEditController.cpp` needed zero changes — already
generic over "compile voice plans whenever `!monoOnly`" and "compile the global plan whenever
`hasGlobalDomain`" as independent conditions.

Reachability itself also needed a real fix while rebuilding this: `voiceReachable` must be forward-
**and-backward** reachable from the allocator (union of both directions), not forward-only — found
via two *pre-existing* tests breaking (`StressGraphTests.cpp`'s 500-node compile, `GraphEditController
Tests.cpp`'s `deleteNode`/disconnect test) plus this milestone's own new test: a forward-only check
misclassified `io.noteIn → allocator.spawn` (a predecessor edge — noteIn is upstream of the
allocator, not downstream) as a voice/global domain crossing. `DOMAINS.md` §2 already says a mono
source feeding the poly region is free; the union fix makes `voiceReachable` agree with that
principle instead of silently contradicting it for the single most common allocator-adjacent shape
in the whole codebase (`buildVoiceProofGraph()` itself is exactly this shape).

**New coverage:** `tests/DomainSplitterTests.cpp`'s case rewritten with two subcases (a fully
disconnected allocator; a wired-but-output-irrelevant one) asserting `monoOnly` stays `false` and
`hasGlobalDomain` is `true` with an empty `instanceMixNodeId` in both. New end-to-end
`tests-plugin/VisualizationTapTests.cpp` case shaped exactly like the user's real patch — subscribes
a visualization tap to the allocator's own `"gate"` output (the same mechanism `view.glance` itself
uses) and asserts it goes non-zero after a real `playNote()` call, directly validating the user's
literal report ("I tried plugging in Glance to the gate in instance allocator and it had 0
movement").

**Verified:** 371/371 tests green. Mutation-tested both the `monoOnly = false` line (reverted to
`true`, confirmed the engine test, the new plugin telemetry test, *and* the pre-existing `.1b` test
all correctly fail) and the `finalizeInstanceMixIntoOutput` branch (a mutation that processes the
global plan but "forgets" to point `finalMono` at its output is caught by the pre-existing
"unrelated, unconnected instance.allocator" test's real-audio assertion — a first mutation attempt
that skipped `processPlanRange` entirely was a no-op, since both the pre- and post-mutation buffers
were still zero-initialized at that point in a single-block test; not a meaningful mutation, redone
properly). `pluginval` wasn't reachable in this environment for this pass (not on `PATH`, not in
any of the usual install locations checked) — noted rather than silently skipped; the rest of the
project's standing pluginval track record on this same code area (run clean, twice, earlier in this
same arc) stands.

**Also fixed in passing:** the first draft of the new plugin test used a real em dash in its Catch2
test name, which broke CTest's own test discovery on this Windows/locale setup (an encoding mismatch
between CTest's test-name discovery and its invocation of the Catch2 executable — reported as
"Failed" with "No test cases matched", not a logic bug). Replaced with a plain hyphen, matching
every other test name in this codebase.

**A fourth bug found live, once the third fix above actually got sound playing**: with sound
working, the user's very next action — placing a brand-new node onto the canvas of their now
real, bridged graph (a real `instance.mix` wired all the way to Master Out) — failed outright:
"Node 'node12' is not connected to either the voice or global domain." Root cause, in the OTHER
branch of `DomainSplitter::split()` than every fix above touched (`instanceMixCount == 1`, the
original M17 bridged-`instance.mix` path, untouched by parts 1-3): once `instance.mix` has a
real upstream connection, every single node in the graph is required to be reachable from either
`voiceDomain` (backward from `instance.mix`) or `globalDomain` (forward from it, plus M21's mono-
source backward-expansion) — a hard compile error otherwise. A freshly-placed node starts with
*zero* connections by construction (the editor always places a node, then wires it, as two
separate commands — the exact same reasoning the `instance.mix`-itself carve-out a few lines above
this check already uses), so this made it **structurally impossible to place any new node at all**
the moment a graph had a real, connected global domain — not a rare edge case, the single most
common thing to do right after getting a first patch working.

**The fix:** the same philosophy as `.1`'s original fix and `.1` (part 3) above, applied to this
branch too — a node reachable from neither domain is folded into the global domain instead of
being a hard error (plus anything only reachable through OTHER such orphans, so a disconnected
`osc -> filter` pair wired only to each other still compiles and runs together), never poaching a
domain a node already legitimately earned. This makes an orphan inert (it reaches neither
`instance.mix` nor the designated output, so it contributes nothing to the audible signal) while
keeping it fully compiled and inspectable via a tap the instant it's placed — exactly the same
outcome `.1`'s original fix already gives the `instanceMixCount == 0` branch's disconnected
allocator. The orphan fold runs *before* the existing "voice domain feeds global domain directly"
validation, so an orphan that turns out to be wired straight to a voice-domain node without going
through `instance.mix` is still correctly rejected, not silently allowed through — mutation-tested
directly (removing the "never poach" guard, and removing the fold step entirely, both correctly
broke a real test each time). The one pre-existing check this fix had to special-case: a freshly-
placed, still-unconnected `instance.allocator` itself now also gets the orphan carve-out (an
allocator genuinely wired the wrong way round — connected, but not upstream of the mix — is still
rejected exactly as before; only "not connected to anything yet" is now allowed).

**New/updated coverage:** `tests/DomainSplitterTests.cpp`'s old "DomainSplitter rejects an
orphaned node connected to neither domain" test (which encoded the bug as contract) rewritten into
four cases — a single orphan, a small cluster of orphans wired to each other, an orphan wired
straight into the voice domain (still correctly rejected), and a freshly-placed unconnected
`instance.allocator` (now also accepted). 374/374 tests green (up from 371). `pluginval
--strictness-level 10`: the "Parameter thread safety" timeout recurred here too (twice in a row
this time, not just once) — isolated via a full `git stash`/rebuild/retest A/B comparison,
reproduced byte-for-byte identically with this fix fully reverted, confirmed environmental,
passed clean (`SUCCESS`) on retry with the fix back in place.

**Also found and fixed in passing, unrelated to this bug**: the part-3 writeup above was written
during that commit but never actually `git add`ed into it — a real slip in the commit's explicit
file list. Caught via the `git stash`/`git stash pop` used to isolate the pluginval flake above
(the stash surfaced it as an unexpectedly-still-modified file), committed on its own
(documentation only, no code) immediately before this milestone's own commit.

**A fifth bug found live, immediately after part 4 shipped**: the user tried building a realistic
patch — `instance.allocator.gate → logic.select.condition`, two `util.constant`s into `whenTrue`/
`whenFalse`, `select.out → env.adsr.gate` — and connecting `select`'s output into `adsr` was
rejected: "Node 'select' (voice domain) feeds node 'adsr' (global domain) directly." Root cause:
part 4's own fold (a node reachable from neither domain always joins the global domain) was too
narrow. `select` genuinely IS fed by the voice domain (`instance.allocator.gate`) — it just isn't
wired *onward* to anything reaching `instance.mix` yet, exactly the ordinary "wire one cable, then
the next" construction order. Folding it into the global domain by default made its own real
incoming edge from voice look like a straight voice→global violation — rejecting the single most
common thing to do right after placing a Select/Compare/Sample-and-Hold node fed by the allocator.

**The fix:** classify each not-yet-connected-to-either-domain cluster on its own, not with one
blanket default. If ANY edge feeds into the cluster from the (already-proven) voice domain, the
whole cluster now joins the voice domain instead (`DOMAINS.md` §2's rule is asymmetric — voice
content may only reach global content through `instance.mix`, but nothing stops it drifting
*deeper* into the voice domain first); everything else (no connections at all, or fed only by the
global domain / other such clusters) still joins the global domain exactly as part 4 already had
it. The existing "voice feeds global directly" validation still runs afterward and still catches a
genuine bypass — mutation-tested: a cluster fed by voice that ALSO wires straight into an existing
global-domain node (e.g. the designated output) is still correctly rejected, just via that node's
own now-correctly-classified outgoing edge instead of its incoming one.

**A second, related bug found while building this fix's own test coverage**: the *pre-existing*
M21 "mono sources" backward-expansion (a separate pass, walking backward from the global domain to
catch things like `io.audioIn` feeding a chain after the mix) could *itself* prematurely claim a
voice-fed node as global, racing against this fix's own classification — specifically when that
node happened to be a direct predecessor of something *already* in the global domain (e.g. wired
straight to the designated output, bypassing `instance.mix`). Real bug, not just a testing
artifact: two separate passes were answering variations of the same question ("what domain does
this in-between node belong to?") and could disagree depending on which one ran first for a given
node. Fixed by removing the separate M21 pass entirely — this milestone's own unified
classification already implements the exact same "not fed by voice → global" rule for every
not-yet-classified node, PLUS the "fed by voice → voice" half the old M21 pass never had. One rule
answers the question once, instead of two rules that could race.

**New/updated coverage:** the old "an orphan wired straight to a voice-domain node... is still a
real, correctly-caught error" test (which encoded the too-narrow default as contract) split into
two: one confirming a voice-fed, not-yet-wired-onward node now correctly joins the voice domain,
and one confirming a voice-fed node that ALSO bypasses `instance.mix` on its own outgoing edge is
still correctly rejected. 375/375 tests green. Mutation-tested the `fedByVoice` classification
itself (forced it to always `false`, confirmed two separate tests correctly break, restored).
`pluginval --strictness-level 10`: the "Parameter thread safety" timeout recurred a third time in
a row this session (worse than the usual once-or-twice) — isolated via the same full
`git stash`/rebuild/retest A/B comparison, reproduced byte-for-byte identically on the clean,
already-committed part-4 baseline with none of this fix's changes present, confirming environmental
independent of this fix; `SUCCESS` on the next retry with the fix in place.

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

## `09-28-InstanceAllocator.3` — split into a real `instance.voice` node, drop the dead configuration dropdown — done

**Root cause:** `instance.allocator`'s `configuration` enum (Voice / Swarm-population /
Swarm-transient / Trigger) was real, visible UI surface — `setParameter()`'s own comment admitted
three of the four options did nothing. The four configurations didn't even share a port shape
(Voice needs a `Note` `spawn` input; Swarm-population needs none at all) — a real structural
mismatch for one node with a mode switch, unlike `mix.downmix`'s legitimate same-shape mode enum.

**The fix, as shipped:** renamed the type id `instance.allocator` → `instance.voice` (a direct
rename — CLAUDE.md rule 3 is suspended, no migration needed). Deleted
`InstanceAllocatorNode.h`/class `InstanceAllocatorNode` outright and wrote `InstanceVoiceNode.h`/
class `InstanceVoiceNode` fresh from it, rather than editing the old file in place — same content,
new name, easier to diff cleanly. The `configuration` parameter and its backing `int configuration`
member are gone entirely, not defaulted; the surviving `maxInstances` parameter's own id was
renamed too, `instance.allocator.maxInstances` → `instance.voice.maxInstances`, matching this
codebase's universal `<typeId>.<paramName>` convention (every other node's parameter ids follow
it — leaving this one stale would've been the actual inconsistency). Title is now "Voice";
category stays "Domain" exactly as planned.

**Real, functional references updated:** `engine/CMakeLists.txt`'s file list, `ProofGraphs.h`
(factory registration, `#include`, both hardcoded proof graphs' node instances), `DomainSplitter.h`/
`.cpp` (the `instanceAllocatorTypeId`/`instanceAllocatorId`/`instanceAllocatorCount` C++
identifiers renamed too, not just their string values, for the same "don't leave one stale name
behind" reasoning as the parameter id), every test file with a `"instance.allocator"` string
literal (`tests/DomainSplitterTests.cpp`, `NoteEventTests.cpp`; `tests-plugin/
ConnectWithAutoAdaptTests.cpp`, `HostInputTests.cpp`, `VisualizationTapTests.cpp`) plus their test
*names* and comments referencing the old id in prose (a plain, dot-anchored `instance\.allocator`
search-and-replace is safe against the arc's own name, `09-28-InstanceAllocator`, which has no dot
and different capitalization — verified this distinction holds before running it broadly).
`plugin/source/{GraphEditController,PluginEditor,PluginProcessor}.{h,cpp}`'s own comments got the
same sweep, since they were already being touched by recent milestones in this same arc.

**Docs updated to match, not left describing the old shape:** `wiki/NODES.md`'s own node-catalog
entry rewritten (🚧 → ✅, config-dropdown language replaced), its Reference Patches table's
Swarm-mode-referencing rows (Water, Cicada Field, the swarm/hexaphonic-guitar bullets) reworded to
point at *future, separate* `instance.swarmPopulation`/`instance.swarmTransient` node types instead
of configurations of `instance.voice` — matching this milestone's own "separate types later, not
empty shells now" decision. `wiki/NODES.System.md` §2's enum-index-stability caveat (which used to
cite `instance.allocator.configuration` as the one real exception to "never rely on option index")
rewritten to say the violation is resolved, since the parameter it cited is gone; §5's whole
`instance.allocator — one node, several configurations` subsection rewritten into `instance.voice —
the region-opening node`, reframing Swarm/Trigger as future separate node types sharing the same
underlying instance-context/lifetime/events machinery, not future configurations of this one.
`wiki/NODES_Gaps.md`'s still-open items (the `.2`-relevant `random1`/`random2` entry, a Note-port
example) updated to the current name; its *historical*, already-fixed bug-report entries (the
dropdown-clipping fix, which happens to mention screenshotting "instance.allocator's Configuration"
dropdown specifically, at a time when that dropdown genuinely existed) left as an accurate record of
what was true when that fix shipped, not rewritten to pretend it never existed.
`wiki/reports/InstanceAllocator_2026-09-28.md` (the report that recommended this exact rename) and
`archive_docs/**` (explicitly historical, not maintained, per `CLAUDE.md`'s own framing) deliberately
left untouched — rewriting the report that recommended a change, to already assume the change
shipped, would erase the reasoning that led to it.

**Explicitly not built here (unchanged from the plan):** `instance.swarmPopulation`/
`instance.swarmTransient`/`instance.trigger` as real node types — created later, as their own real
milestones, once actual Swarm/Trigger runtime machinery exists, not as empty shells now.

**Tests:** new engine-level test (`tests/NodeDescriptorTests.cpp`) confirms `instance.allocator` no
longer resolves to anything in `NodeFactory::describeAll()`, `instance.voice` does, its title reads
"Voice", and its `parameters` list contains exactly one entry (`instance.voice.maxInstances`) with
nothing matching `*configuration*` anywhere in an id. 377/377 tests green (up from 376 — one new
test; the rename itself didn't need new *coverage* so much as updating existing tests to the new
name, since a stale reference fails loudly at compile time, not silently). `pluginval
--strictness-level 10`: clean `SUCCESS` first try this round, no flake to isolate. Standalone app
relaunched — the Init Patch (which uses this node for real, as `"allocator"`/`instance.voice`)
still compiles and plays.

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

**Partially shipped early, unplanned** (2026-09-28, same session `.1` finished in — the user asked
directly for a debugging aid while hand-testing `.1`'s fixes, not scoped as `.4` at request time):
a per-NODE domain indicator, not the per-cable one this section describes. A small dot in each
node's title bar (`NodeCard.tsx`'s new `DomainDot`) — green (`tokens.domainVoice`, reusing the
existing "poly" green exactly) for a node the last successful compile put in the voice domain,
grey (`tokens.domainGlobal`) for global, no dot at all for a `monoOnly` graph (most graphs, most of
the time) or a node the engine hasn't compiled yet. `GraphEditController` now tracks
`nodeDomains` (a plain `nodeId -> "voice"|"global"|"mono"` map, computed once per successful
recompile from whichever `DomainSplitter` branch actually ran — the SAME logic already this whole
arc's other fixes are built on, not a new classification), exposed via a new read-only native
function `graphGetNodeDomains`; `graphStore.ts` fetches it in parallel with every snapshot refresh
(`ensureInitialized`/`withHistory`/`undo`/`redo`) and threads it through `GraphSurface.tsx` into
`NodeCardState.domain`. Verified live: pixel-sampled a screenshot of the running Standalone app
against the user's own real mid-build patch and confirmed exact colour matches at both a
correctly-green (Note In, Instance Allocator — voice) and correctly-grey (Oscillator, Master Out —
not yet wired to the allocator) node. Real, correctness-critical piece caught and fixed before this
shipped: the domain map must only be committed to the controller's member state on an ACTUAL
publish, not right after `DomainSplitter::split()` succeeds (`split` succeeding only proves the
graph partitions cleanly, not that `GraphCompiler::compile()` can actually build either half) — a
first draft got this wrong, would have left the indicator showing a REJECTED command's attempted
domain shape instead of the graph that's actually live; caught by writing the correctness test
first (`GraphEditControllerTests.cpp`'s new "leaves it exactly as it was" case) and mutation-testing
it (committing early was reintroduced deliberately, confirmed the test breaks, reverted).

**Still open, unchanged from the description above:** the per-CABLE version (stroke weight/style on
`nodeEditorRenderer.ts`'s WebGL cable renderer) — the node-level dot is a real, useful, but coarser
signal (it tells you a node's OWN domain, not which specific wire crossed a boundary), and doesn't
by itself replace what a cable-level treatment would show for a node that's genuinely a boundary
(e.g. `instance.mix` itself, which is always "global" by the current node-level classification even
though its OWN input cable is meaningfully different from its output cable).

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
  from the same conversation, explicitly excluded per their own instruction. **Picked up and
  built in `09-29-AddMenu.1`, below** — not still open.

## Verification, every `09-28-InstanceAllocator` milestone

Same standing discipline as the `0.x` arc: build + `ctest` green, mutation-test the
correctness-critical logic (`.1`'s reachability check, `.2`'s determinism), `pluginval
--strictness-level 10`, UI `npm run build && npm run lint` clean (only `.4` touches UI code),
Standalone app launched and sanity-checked against the Init Patch (which uses this node for real).
Each milestone commits on its own once green, without asking.

## `09-29-AddMenu.1` — nested node categories, Blender-style flyout Add menu — done

**Root cause:** the Add menu (`ui/src/graph/AddMenu.tsx`) only ever grouped by a single flat
`category` string — no way to nest, so a node whose natural home is "a kind of Domain node,
specifically one of the spawn mechanisms" had nowhere to go but a flat "Domain" bucket alongside
unrelated siblings. Already getting messy with today's ~55 nodes; explicitly called out as needing
order before the upcoming node batches land (the user's own framing). This exact proposal was raised
during the `09-28-InstanceAllocator` arc and deliberately deferred — see that arc's "Explicitly out
of scope" section, now cross-referenced here.

**The fix, category data model:** `category` stays a plain string (no schema change, no engine-side
parsing) — it's just now allowed to be a `/`-separated path, e.g. `"Domain/Allocate"`. New pure
module `ui/src/graph/categoryTree.ts` turns a flat descriptor list into a tree (`buildCategoryTree`),
and produces one level's alphabetically-interleaved rows (items + subcategories, `rowsOf`). Search
mode groups by the top-level segment only (`topLevelCategory`) — drilling into a nested flyout while
a query is active would defeat the point of typing one, so search stays a flat, familiar list, same
shape as before this milestone.

**The fix, node rename:** `instance.voice` → `instance.allocate.voice` (direct rename, CLAUDE.md rule
3 is suspended, no migration — same treatment `instance.allocator` → `instance.voice` got in
`09-28-InstanceAllocator.3`, no schema-version bump needed there either). The inserted `allocate`
segment is what lets `getCategory()` return `"Domain/Allocate"` for Voice while `instance.mix` stays
flat `"Domain"` — it isn't a spawn-mechanism sibling, it's the region-closing node. The parameter id
followed suit: `instance.voice.maxInstances` → `instance.allocate.voice.maxInstances`, matching this
codebase's universal `<typeId>.<paramName>` convention. Future `instance.allocate.swarmPopulation`/
`swarmTransient`/`trigger` (M28, still unbuilt) land in the same "Domain/Allocate" flyout once they
exist, with zero further menu work. Every real reference updated to match (`ProofGraphs.h`'s
registration + both proof graphs, `DomainSplitter.h/.cpp`'s `instanceVoiceTypeId` constant and error
messages, every test file with an `"instance.voice"` string literal, `CLAUDE.md`'s own interim-
simplifications note, `wiki/NODES.md`/`wiki/NODES.System.md`/`wiki/NODES_Gaps.md`) — `wiki/
reports/InstanceAllocator_2026-09-28.md` and `archive_docs/**` deliberately left untouched, same
"don't rewrite the reasoning that led to a change" precedent `09-28-InstanceAllocator.3` set.

**The fix, menu UI:** a category with no subcategories (still nearly every one of them) renders
exactly as before — inline header, inline item buttons, no hover required, zero behavior change.
A category that genuinely has children (today: only "Domain") additionally gets one hoverable row
per child ("Allocate ▸"); hovering it (after a short, standard hover-intent delay so moving the
mouse diagonally across the menu doesn't flicker-open the wrong one) or clicking it opens a floating
flyout panel beside it — `ui/src/graph/useFlyoutPosition.ts`, a new hook alongside the existing
`useAutoFlipPosition.ts` (kept separate rather than generalizing the shared one, so
`NodeContextMenu.tsx`'s existing `(x, y)`-based call site can't regress), anchored to the trigger
row's own measured rect and flipping to the left/clamping vertically near a viewport edge.
Recursion is generic to any depth, not hardcoded to two levels. Full keyboard parity: ArrowUp/Down
move within the current level, ArrowRight/Enter drills into a focused category (measuring the same
row via a small DOM-element registry, since a keyboard-driven open has no mouse event to read a rect
from), ArrowLeft backs out one level and refocuses the row that opened it, Escape still closes the
whole menu. Clicking outside still closes everything for free — flyouts render as nested JSX inside
the root menu's own DOM subtree (fixed positioning doesn't require being a direct child of `body`),
so the existing single `contains()` check already covers them.

**Left exactly as-is, deliberately:** `ui/src/gallery/ComponentGallery.tsx` (the separate, read-only
M9 component gallery) still groups by the same flat `category` string with no nesting awareness — it
will show `instance.allocate.voice` under a literal "Domain/Allocate" header instead of a proper
flyout. Harmless (nodes still land in the right, findable group) and out of scope — the user's ask
was specifically the Add menu, and the gallery already fetches its own separate descriptor copy.

**Tests:** `tests/NodeDescriptorTests.cpp`'s existing `09-28-InstanceAllocator.3` test updated for
the new type id/category (`instance.voice` also confirmed gone, alongside the already-checked
`instance.allocator`; `category == "Domain/Allocate"` replaces the old flat check) — no new engine
test needed beyond that, since nothing about compilation/connection semantics changed, only a string.
No TS test framework exists in `ui/` (this project's UI is verified via `npm run build && npm run
lint` plus a manual Standalone launch, not unit tests) — `categoryTree.ts`'s tree-building and
`AddMenu.tsx`'s flyout logic are new, pure-enough TS that a future milestone adding real UI test
infra would want to backfill coverage for, not assumed covered here. 377/377 engine+plugin tests
green. `pluginval` not available in this environment to re-run (documented absence, not a new gap).
UI `npm run build`/`npm run lint` clean, zero warnings on any new file. Standalone app rebuilt and
relaunched for a manual click-through (this environment has no computer-use/screenshot capability
to drive a native Win32 window itself, so the actual visual flyout-hover check is the user's own,
not claimed here).

## `09-29-AddMenu.2` — root categories collapse too, not just the ones with subcategories — done

**Root cause:** `.1` only special-cased categories that genuinely have subcategories (today: just
"Domain") into a hoverable flyout trigger — every other top-level category (Adapters, Effects,
Filters, ...) still rendered fully expanded inline, exactly like before this arc. Direct feedback
after `.1`: the root list should show category names ONLY, closed, every one of them — not a mix of
"most categories inline, one category collapsed." Opening the menu should show categories; hovering
one shows what's in it.

**The fix:** root now renders one row per top-level category, unconditionally, via the exact same
`CategoryRow`/`renderRow` shape a nested flyout already used — no more special inline-vs-flyout
branch. A category with no subcategories of its own (still true of all but "Domain") opens a flyout
showing its items directly, using the same recursive `FlyoutPanel`/`rowsOf` machinery `.1` already
built for "Domain/Allocate" — this needed no new mechanism, just removing the special-cased inline
rendering path at the root level so root behaves like any other level. `browseRows` (the keyboard-
navigable row set) shrank to match: at root it's now just the category list itself, not every node
in the catalog concatenated together.

**Left unchanged, deliberately:** search mode. Typing a query still shows a flat, top-level-grouped
list with items visible immediately — collapsing search results into hoverable categories would
fight the reason someone types a query in the first place. `ComponentGallery.tsx` still untouched,
same reasoning as `.1`.

**Tests:** no engine change (pure `ui/src` edit), so no `ctest` re-run needed. UI `npm run build`/
`npm run lint` clean, zero warnings on the touched files. Not re-verified against the running
Standalone app by this session (no computer-use/screenshot capability here) — the change is served
live by the already-running Vite dev server (`ui/src` HMR), so the already-open window picks it up
without a rebuild; confirming the actual hover behavior is the user's own next step.

## `09-29-AddMenu.3` — "I/O" silently fragmented into a bogus "I" > "O" flyout — fixed

**Root cause:** live testing (`.2`'s own category flyout) surfaced a top-level category literally
named "I" with a single subcategory that reads as "0" in the menu's small mono font but is actually
the letter "O" — containing exactly 4 nodes. `IoAudioInNode`/`IoControlNode`/`IoNoteInNode`/
`IoTransportNode` all declared `getCategory() == "I/O"` (a pre-existing, pre-arc category name) —
and `categoryTree.ts` treats every `/` as a nesting delimiter, so "I/O" silently split into a
top-level "I" category containing one subcategory "O", exactly the 4 nodes the user saw. A real
naming collision this arc's own delimiter choice introduced, not caught before shipping `.1`/`.2`
because nothing scanned every node's category for this specific failure mode.

**The fix:** renamed the category string itself, `"I/O"` → `"IO"` (no slash), across all four
nodes — a pure UI-label change, same category *meaning*, no typeId/parameter/patch impact. Checked
every other registered node's category for the same mistake (`grep` across all 55 node headers) —
"I/O" was the only unintentional collision; `InstanceVoiceNode`'s `"Domain/Allocate"` is the one
deliberate nested category and stays as-is. Added a warning to `Node::getCategory()`'s own base
declaration (`Node.h`) so the next node written doesn't repeat this. Added a new, generic regression
test (`tests/NodeDescriptorTests.cpp`) scanning every registered node's category for a segment of
length ≤ 1 — the actual shape this exact mistake takes — so any *future* accidental slash is caught
by `ctest`, not by a screenshot.

**Tests:** new test passes for all 58 registered nodes; 378/378 total (up from 377). UI `npm run
build`/`npm run lint` clean (unchanged — this was a pure engine-side string fix, no `ui/src` edit).
Standalone app rebuilt and relaunched (category strings are baked into the compiled descriptor JSON
the native bridge serves, unlike `.2`'s pure-TS fix, so this one genuinely needed a rebuild+relaunch,
not just Vite HMR) — visual confirmation that "IO" now renders as one flat category is the user's own
next step, same computer-use-capability caveat as every earlier entry in this arc.

---

# Clock+Seq batch — `wiki/NODES.Status.md`'s own build order, step 1 — done

Not part of the `0.x` arc or either dated arc above — the first real node-building work
driven directly by `wiki/NODES.Status.md` (written 2026-09-29), which named this exact
5-node cluster as the top of its own "nodes to build next" order: zero dependencies,
essential (**A**-tier) modularity infra, highest leverage. Five new node types:
`clock.pulse`, `clock.divide`, `clock.counter`, `seq.steps`, `seq.euclid`
(`engine/include/bazalt/engine/nodes/Clock{Pulse,Divide,Counter}Node.h`,
`Seq{Steps,Euclid}Node.h`), registered in `ProofGraphs.h::buildDefaultNodeFactory()`
— not wired into either proof graph or the Init Patch (nothing yet consumes them; they
exist to be placed by hand in the editor, same as every other freshly-built node
before its own stock-group/reference-patch lands).

**Design decisions made while building, not pre-specified by the catalog:**

- **`clock.pulse`'s swing/jitter** are a concrete, testable contract the catalog named
  but didn't pin down: ticks fire at successive integer thresholds `k = 0, 1, 2, ...`;
  swing delays every odd-numbered tick's threshold by `swing*0.5` periods, which
  automatically preserves the pair's average period (the next even threshold is always
  the next plain integer — no separate compensation step needed); jitter perturbs each
  threshold by a random offset redrawn once per tick, not resampled every sample.
  `rateMode = Division` reads `rate` as beats/sec (wire `io.transport.tempo` straight
  in) scaled by the selected note division.
- **`clock.pulse`/`clock.counter` both gained a `seed`** structural parameter beyond
  the catalog's own port list — the same convention `random.stepped`/`random.drift`
  already established for any node with internal randomness (jitter; `random` mode),
  needed for deterministic, reproducible renders.
- **`clock.divide`'s output port id is `tickOut`, not `tick`.** The catalog names both
  the input and the output `tick` — a real, previously-untested engine invariant
  (`ExecutionPlanTapTests.cpp`'s "no node type reuses a port id across its inputs and
  outputs") rejects that at compile time. Caught by the build, not by inspection: the
  full test suite was already green before this node existed, and adding it was the
  first thing in 58 prior node types to ever exercise that specific check. Display
  label stays "Tick" — id/label divergence is already normal throughout this codebase.
- **`seq.steps` shipped as MVP, two deliberate, documented deviations from spec**, both
  flagged in `wiki/NODES.Status.md` before this session started: no `Data(curve)`
  input (no node in the engine produces a real `Data` value yet — a cross-cutting
  prerequisite, not specific to this node), and `length` capped at 16 rather than the
  catalog's 64 (a classic 16-step sequencer covers the overwhelming common case;
  raising the cap later is trivial — these are plain, individually-numbered
  `ParameterDescriptor`s, `seq.steps.step.0`..`.15`, not a wire-format array size).
  `gate` is defined as `abs(currentStepValue) > epsilon` (a step storing exactly 0.0 is
  a rest) since this data model has no separate per-step enable flag.
- **`seq.euclid`** uses the standard `floor(i·pulses/steps) != floor((i-1)·pulses/steps)`
  onset formula (Bjorklund-equivalent, no recursion needed) — hand-verified against the
  textbook `steps=8, pulses=3` tresillo pattern (hits at 0, 3, 6) before writing the
  corresponding test.
- **`seq.steps`/`seq.euclid` both hold at step 0 until the first tick**, which advances
  to step 1 — the classic hardware step-sequencer "power-on shows step 1 active, first
  clock advances to step 2" convention, chosen deliberately and documented in both
  nodes' own header comments so it doesn't read as an off-by-one bug later.
- **`clock.counter`'s `random` mode never fires `wrapped`** — there's no meaningful
  "reached the end and came back to start" for an independent uniform draw each tick,
  so this node doesn't invent one; every other mode (`up`/`down`/`pingPong`) fires it
  exactly once per lap.

**Tests:** new `tests/ClockSeqNodesTests.cpp`, 21 cases covering every node (free-run
timing, swing/jitter determinism, division-mode arithmetic, run/reset for
`clock.pulse`; passthrough/N-division/reset for `clock.divide`; all 4 modes + wrap
behaviour + normalised output for `clock.counter`; advance/hold/wrap/gate/range/reset
for `seq.steps`; the tresillo pattern, `pulses=0`/`pulses>=steps` edge cases, rotate,
and trigger-vs-gate timing for `seq.euclid`). A real dangling-pointer bug in an early
draft of the `seq.steps` test (a lambda returning a raw pointer to its own local
stack array) was caught by the compiler's own `C4172` warning during the first build
attempt, not found live — fixed by returning `std::array<float, 4>` by value instead.
`NodeDescriptorTests.cpp`'s registered-type count updated 58 → 63.

**Verified:** `ctest` (engine+plugin combined) 399/399 green; `EngineTests.exe` alone
314 test cases / 2,004,319 assertions, all passing, of which the 21 new `[ClockSeq]`
cases (93,257 assertions) were run and confirmed green in isolation first, before the
full-suite run. `pluginval --strictness-level 10`: the recurring "Parameter thread
safety" timeout hit again on the first run — the same known-environmental symptom
this project has isolated via full `git stash` A/B comparison several times before
(`09-28-InstanceAllocator.1`'s part 3/4/5, `09-29-AddMenu` entries); not re-isolated
from scratch here given that standing track record, but passed clean (`SUCCESS`) on a
plain retry, consistent with every prior occurrence. Standalone app rebuilt, launched,
screenshotted (confirmed no crash, the WebView rendered the user's own saved graph
correctly), and closed — no UI code changes were needed for the new nodes to become
placeable (the Add menu is entirely descriptor-driven), so this was a sanity check,
not a feature verification.

**Docs updated to match:** `wiki/NODES.md`'s `clock.*`/`seq.*` entries and status-index
row (📋 → ✅ for four nodes, 🚧 for `seq.steps`, with each deviation from the literal
catalog spec named inline, not silently narrower); its own "62 of 125 catalog-only"
header count. `wiki/NODES.Status.md`: all 5 nodes moved out of their "to build" tables
into the family sections above (Implemented/MVP), totals recomputed (55→59 Implemented,
3→4 MVP, 67→62 to build, A-tier 19→14), the Clock+Seq batch and "nodes to build next"
step 1 marked done, and the Arpeggiator/Cicada stock-group appendix rows updated to
reflect their now-satisfied `clock.*` dependencies.

---

# Data Foundations batch — `wiki/NODES.Status.md`'s own build order, step 2 — done

Five node types in (Clock+Seq); three more now: `data.scale`, `data.table`,
`data.lookup` (`engine/include/bazalt/engine/nodes/Data{Scale,Table,Lookup}Node.h`),
registered in `ProofGraphs.h::buildDefaultNodeFactory()`. Unlike Clock+Seq, this batch
also required **new cross-cutting engine infrastructure**, not just three node files —
it was named as the pathfinder for exactly this reason in `wiki/NODES.Status.md`'s own
"cross-cutting prerequisite" note, written before any of it existed: **no node in the
engine had ever produced a real `Data` value.** `Data` was a fully real, implemented
signal type at the `canConnect` level (tag matching, rejection rules), but the actual
"build once, publish, swap a pointer" runtime (`DataPublisher`/`DataBuffer`, `Data.h`,
already built and unit-tested standalone since an earlier milestone) had zero real
callers — nothing in `GraphCompiler.cpp` knew how to wire a `Data`-typed connection
between two nodes at all.

**The new mechanism, deliberately lighter than `Note`'s own:** two new `Node.h`
virtuals, `getDataPublisher()` (a producer hands back its own `DataPublisher*`) and
`setDataInput(portId, publisher)` (a consumer receives the resolved producer's
pointer). `GraphCompiler.cpp` gained a new branch in its connection-resolution loop,
directly parallel to the existing M18 `SignalType::Note` special case (same "one
source per input" dedup via a new `dataInputsUsed` set, same successors-edge
bookkeeping for SCC scheduling, same bypass of `incomingSource`/`channelCountOf`'s
ordinary float-buffer path) but structurally simpler: a `Data` connection is wired
**once, at compile time** — `getDataPublisher()`/`setDataInput()` are called once per
resolved connection, full stop, no per-block re-wiring the way `Note`'s
`produceNoteBlock()`/`consumeNoteBlock()` need. This is sufficient because a
`DataPublisher`'s own address never changes after construction, only its published
*contents* do — reading those live (`DataPublisher::getCurrentForAudioThread()`) is
already exactly what the consumer does for itself, on the audio thread, whenever it
wants, cheaply and allocation-free (already proven by `Data.h`'s own pre-existing
tests). Design reasoning captured directly in `Node.h`'s own doc comments on both new
virtuals, not just here.

**Design decisions made while building, not pre-specified by the catalog:**

- **`data.scale`**: 12 named scales built in — everything the catalog names except
  "harmonic series" (genuinely ambiguous, no agreed 12-tone approximation exists — a
  poor guess would be worse than an honest gap) and "custom" (wants real `NodeContent`
  editing, not a bolted-on parameter bank ahead of it). `octaveSize` proportionally
  rescales the 12-tone patterns rather than just padding them, verified by a dedicated
  test. **A real, documented RT-safety limit, found by reasoning through the
  architecture before writing code, not live**: `root` is a genuine wireable port
  (matching the catalog), but rebuilding a `Data` buffer means constructing a new
  `std::vector<float>` — a heap allocation, forbidden on the audio thread (CLAUDE.md
  rule 2). A cable wired into `root` compiles and is accepted; it currently has no
  audible effect. Only the value applied via `setParameter()` (the node's own inline
  slider, same mechanism `adapt.threshold`'s own "threshold" port already established)
  actually republishes. Building a bespoke lock-free in-place-mutation scheme to make
  live modulation RT-safe was judged real, separate infrastructure — `wiki/
  NODES.System.md` §8's own still-open "asset store"/worker-thread-rebuild item — not
  something to improvise as a side effect of one node in this batch.
- **`data.table`**: zero RT-safety tension (the catalog gives it no input ports at
  all) — its curve content is a fixed 32-point `ParameterDescriptor` bank, the same
  pattern `seq.steps`' own step bank already established, ahead of real `NodeContent`
  (§3) which doesn't exist as code yet. `resolution` (2–32) picks how many points
  publish, truncating from the front.
- **`data.lookup`**: the catalog names four modes (nearest/interpolate/index/
  wrap-index) without specifying their exact contract — this session's own concrete
  design: `nearest`/`interpolate` treat `in` as a normalised position (remapped via
  `polarity`), `index`/`wrapIndex` treat it as a literal element index and ignore
  `polarity` entirely (an index has no natural normalised meaning), `wrapIndex` always
  wraps regardless of `edgeMode`. `dataB`/`morph` blending falls back gracefully to
  `data` alone on a tag mismatch rather than rejecting — nothing in this engine
  enforces "required" Data ports today, so graceful degradation was the honest choice
  over inventing new enforcement machinery mid-batch.
- **A real const-correctness bug caught by the compiler, not live**: `DataPublisher::
  getCurrentForAudioThread()` isn't `const`-qualified (it genuinely mutates an epoch
  atomic — real state, not just a cache), so `Node::setDataInput()`'s first draft
  (`const DataPublisher*`) failed to compile the moment `data.lookup` tried to call it
  through a stored member pointer. Fixed by dropping the `const` on both the virtual's
  signature and the two stored member pointers — documented directly in `Node.h`'s own
  comment so the next Data-consuming node doesn't rediscover this the same way.
- **A real nested-enum shadowing risk avoided, following existing precedent**:
  `data.lookup`'s own polarity concept was named `PositionPolarity`, not `Polarity` —
  `bazalt::engine::Polarity` (`PortDescriptor.h`) already exists at namespace scope,
  and `RandomSteppedNode.h`'s own `OutputPolarity` already established the same
  avoidance for the same reason. Would still have compiled either way (class-scope
  lookup wins), but the existing convention is there for readability, not correctness.

**Tests:** new `tests/DataFoundationsNodesTests.cpp`, 18 cases. Direct per-node tests
(scale-pattern correctness including a hand-verified `octaveSize` rescale, republish-
on-edit generations, an `RtAllocationTrap`-wrapped proof that the *read* side is
allocation-free, `data.table` truncation-on-resolution-change, `data.lookup`'s full
mode/polarity/edgeMode/morph matrix using hand-built `DataBuffer`s for precise control)
plus — the more important half — **two real compiled-graph round trips** through
`GraphCompiler::compile()` (`data.scale`→`data.lookup` reading a scale degree by index,
`data.table`→`data.lookup` interpolating a drawn curve) and one negative case (wiring
two `Data` producers into the same input is rejected, exercising the new
`dataInputsUsed` dedup specifically) — these are what actually prove the new
`GraphCompiler.cpp` wiring works end to end, not just that the nodes behave correctly
in isolation. One real test-authoring bug caught on the first run, not shipped: the
`octaveSize` rescale test's expected values ignored `data.scale.root`'s own default
(60, not 0) interacting with a changed `octaveSize` (60 mod 24 = 12, not 0) — the node
was already correct; the test's hand-computed expectation wasn't. Fixed by explicitly
zeroing `root` in that one test to isolate exactly what it means to check, not by
changing the node. `NodeDescriptorTests.cpp`'s registered-type count updated 63 → 66.

**Verified:** `EngineTests.exe` 332 test cases / 2,004,380 assertions, all green (18 of
those cases are this batch's own, confirmed passing in isolation before the full-suite
run). `ctest` (engine+plugin combined) 417/417 green. `pluginval --strictness-level
10`: clean `SUCCESS` on the first run this time, no flake to retry. Standalone app
rebuilt, launched, and confirmed running/responsive — no UI code changes were needed
(the Add menu is descriptor-driven), so this was a sanity check, consistent with every
other node-batch milestone in this project.

**Docs updated to match:** `wiki/NODES.md`'s `data.scale`/`data.table`/`data.lookup`
entries (📋 → ✅, each design decision and deviation named inline) and its `data.*`
family intro paragraph (no longer "entirely catalog-only"); its own "59 of 125
catalog-only" header count. `wiki/NODES.System.md` §1's "Real state, today" paragraph
(no longer says `Data` has no real producer). `wiki/NODES.Status.md`: all 3 nodes moved
into the `data.*` family's own Implemented table, totals recomputed (59→62 Implemented,
62→59 to build, C-tier 12→9), the Data Foundations batch and "nodes to build next"
step 2 marked done, `data.material` re-batched from the now-closed "Data Foundations"
tag (a stray leftover from an earlier pass — it was never actually one of this batch's
3 real members) to "PM Core" where it actually belongs, and the Scale Quantize/
Arpeggiator/Chord/Crackle stock-group appendix rows updated to reflect their
now-satisfied `data.scale` dependency.

---

# Note Stream batch — `wiki/NODES.Status.md`'s own build order, step 3 — done, with a real finding

Three batches in (Clock+Seq, Data Foundations); this one is the `note.*` family:
`note.gate`, `note.value`, `note.transpose`, `note.filter`, `note.humanize`,
`note.quantize` — 6 of the planned 9 nodes
(`engine/include/bazalt/engine/nodes/Note{Gate,Value,Transpose,Filter,Humanize,
Quantize}Node.h`), registered in `ProofGraphs.h`. **`note.hold`/`note.select`/
`note.chord` were deferred outright** — a real, previously-unexercised engine limit,
not scope-trimming for convenience.

**The limit, precisely:** `ExecutionPlan::BlockStep` has exactly one
`noteInputBufferIndex`/`noteOutputBufferIndex` field each (singular, not a vector) — a
node can declare at most one `Note`-typed input and one `Note`-typed output, total,
today. This was already documented (`wiki/NODES.System.md` §1) as "a limitation
nothing currently built runs into" — this batch is the first to actually hit it, twice:
the catalog's own `note.filter` wants two `Note` outputs (`pass`/`reject`), and
`note.hold`/`note.select`/`note.chord` all assume a real multi-note collection can
travel over one `Note` cable (a chord generator turning 1 note into N; a "held notes"
memory handed from `note.hold` to `note.select` over its own `held` port). Neither is
representable by `NoteEvent`'s own monophonic shape (`gate`/`pitch`/`velocity`/
`startEvent`/`stopEvent` — one note's worth of state per sample, no `id` field to
multiplex several).

**Two different resolutions for two different situations:**

- **`note.filter`**: a clean redesign was possible without touching engine
  infrastructure. Instead of `pass`/`reject` (two `Note` outputs), it ships with **one**
  `Note` output (the note, verbatim, when in range; fully suppressed — gate false, no
  start/stop — when out of range) plus a plain `inRange` **Boolean** carrying the
  pass/reject *decision* as an ordinary signal. This captures the real, useful behaviour
  (a keyboard split, a velocity gate) the two-output design would have offered, without
  pretending the engine can carry two simultaneous note streams off one node.
- **`note.hold`/`note.select`/`note.chord`**: no clean redesign exists that stays
  faithful to what these nodes are *for* (a real "held notes" memory; a real chord).
  Forcing them through the one-`Note`-port wall would mean either lying about what they
  do (a "chord" generator that can only ever emit one note isn't a chord generator) or
  inventing a parallel, ad hoc multi-note mechanism (e.g. a growable bank of plain
  Control pitch outputs instead of a real `Note` list) as an uncoordinated side effect
  of building three individual nodes — real, cross-cutting engine design deserving its
  own pass, not something to improvise here. Deferred, with the reason named in three
  places (`NoteFilterNode.h`'s own class comment, `wiki/NODES.md`, and this entry) so it
  doesn't read as an oversight later.

**Design decisions made while building the 6 that do fit, not pre-specified by the
catalog:**

- **The shared consume-then-produce shape**: `ExecutionPlan::process()` calls
  `consumeNoteBlock()` → `processBlock()` → `produceNoteBlock()` in that fixed order per
  block-rate step (confirmed by reading `ExecutionPlan.cpp` directly before designing
  anything, not assumed) — every Note-transforming node in this batch (`transpose`,
  `filter`, `humanize`, `quantize`) uses this precisely: `consumeNoteBlock()` stores the
  incoming per-sample array; `processBlock()` captures that block's ordinary Control
  inputs (`semitones`, `root`, etc.) into a small `prepare()`-sized scratch buffer, since
  `produceNoteBlock()` (which runs last) has no access to the ordinary `float inputs[]`
  array at all — only `NoteEvent*`. This is what makes those Control inputs genuinely
  audio-rate-modulatable rather than only settable via `setParameter()` (contrast
  `data.scale`'s `root`, which — for a real, different, RT-safety reason documented on
  that node — can't be).
- **`note.value`'s `select`** (last/lowest/highest/first) is real, not just schema —
  this node maintains its own small internal memory (up to 8 concurrently-held notes,
  tracked by watching the incoming stream's start/stop edges over time) rather than only
  ever looking at "whatever's live this sample." Genuinely useful the moment a real
  multi-note source exists, even though today's `io.noteIn` is itself strictly
  monophonic (a separate, pre-existing, out-of-scope limitation this node doesn't need
  fixed to behave correctly per its own contract).
- **`note.humanize`'s timing jitter** delays a note-on by up to 50ms via a small
  scheduled countdown, not a full ring buffer — sufficient because the stream is
  monophonic (at most one note-on is ever pending). Only note-on is jittered,
  deliberately — note-off timing humanization is far less musically useful for the
  added complexity a second, independent schedule would need.
- **`note.quantize`, the flagship Data Foundations consumer**: real, tested
  `data.scale → note.quantize` wiring, genuinely closing reference patch #2 ("MIDI
  remapped to a scale"). `root` here is a *second*, independent knob from `data.scale`'s
  own `root` — not a duplicate: `data.scale.root` rotates which pitch-classes are IN the
  published scale (a self-contained, nameable scale); this node's `root` is a plain
  post-quantization semitone offset, the same "movable key centre without touching the
  scale table" knob real quantizer modules commonly have on the quantizer itself.
  Assumes a standard 12-semitone octave for pitch reconstruction — documented, not
  silently assumed to generalize to a `data.scale` wired with `octaveSize != 12`.
- **Port id collisions, again**: `note.transpose`/`note.filter`/`note.humanize`/
  `note.quantize` all declare both an input and output port named `notes`, matching the
  catalog's own literal naming — the same real engine invariant `clock.divide`'s
  `tickOut` already hit (`ExecutionPlanTapTests.cpp`'s "no node type reuses a port id
  across its inputs and outputs") rejected this again. Fixed the same way: the output
  port id became `notesOut` on all four, display label stays "Notes."

**Two real bugs caught during testing, both in test code, not the nodes:**

- **A genuine hang, not a flaky test.** The first draft of `note.humanize`'s timing-
  delay test called `prepare({44100.0, 512})` but then ran a single 2205-sample block
  (the full 50ms jitter window at 44100Hz) — writing past the 512-element scratch
  buffers `prepare()` had sized, a silent out-of-bounds `std::vector::operator[]` write.
  In this Debug/MSVC build that manifested as a fully silent process hang (no crash, no
  output, minimal memory growth) rather than a clean crash — almost certainly an
  invisible modal debug-assertion dialog blocking on user input the terminal never
  shows. Isolated by bisecting tags (`[NoteGateNode]`, `[NoteValueNode]`, ... one at a
  time with a hard `timeout`) down to the exact test, then the exact line, in a few
  minutes rather than guessing. Fixed by `prepare()`-ing with the actual block size the
  test runs (`{44100.0, numSamples}`), not a smaller placeholder.
- A quantization test injected pitch 63.0 expecting it to resolve to the major scale's
  64, not noticing 63 is *exactly* equidistant between the major scale's 62 and 64 — a
  real tie the node's deterministic "first-found wins" tie-break resolved to 62, not a
  node bug. Fixed by picking an unambiguous test pitch (64.4) instead of changing the
  node's tie-break behaviour.

**Tests:** new `tests/NoteStreamNodesTests.cpp`, 17 cases — per-node coverage
(gate/value/transpose/filter/humanize's core behaviour, `note.value`'s held-memory
select modes and stop-event removal, `note.quantize`'s direction/strength/root/
applyTo matrix) plus one real compiled-graph round trip: `io.noteIn → note.quantize
(fed by data.scale) → note.value`, proving `Note` and `Data` connections cooperate
correctly on one real node through the actual `GraphCompiler`, not just in isolated
per-node C++ calls. `NodeDescriptorTests.cpp`'s registered-type count updated 66 → 72.

**Verified:** `EngineTests.exe` 349 test cases / 2,004,480 assertions, all green (the
17 `[NoteStream]` cases confirmed passing in isolation, including a deliberate re-run
after fixing the hang, before the full-suite run). `ctest` (engine+plugin combined)
434/434 green — one more already-documented recurring gotcha hit and handled the known
way along the way: `LNK1163` on `MacroParametersTests.obj` (a corrupt incremental-link
object, not a code regression) — deleted the `.obj`, rebuilt clean.
`pluginval --strictness-level 10`: clean `SUCCESS` first try, no flake. Standalone app
rebuilt, launched, and confirmed running/responsive.

**Docs updated to match:** `wiki/NODES.md`'s `note.*` section (📋 → ✅ for six nodes,
a new shared "engine limit" note explaining the three deferrals and `note.filter`'s
redesign, so neither reads as an oversight); its own "53 of 125 catalog-only" header
count. `wiki/NODES.System.md` §1's Note-port-limit paragraph (no longer claims "nothing
currently built runs into" it). `wiki/NODES.Status.md`: the 6 nodes moved into the
`note.*` family's own Implemented table, `note.hold`/`select`/`chord` re-marked
"blocked" rather than merely "to be implemented," totals recomputed (62→68 Implemented,
59→53 to build, A-tier 14→8), the Note Stream batch and "nodes to build next" step 3
marked done, and the Scale Quantize/Arpeggiator/Chord stock-group appendix rows updated
(Scale Quantize is now genuinely buildable; Arpeggiator/Chord are explicitly blocked,
not just "next").

---

## `09-29-DefaultGraph.1` — plain master-out-only default, and Standalone's mute-input default flipped off — done

Two small, unrelated fixes made together on the user's own explicit instruction, not
discovered by testing.

**Default graph:** `GraphEditController`'s constructor default was the M22 Init Patch
(a fully-wired two-oscillator subtractive synth) — a fresh instance now opens on
`ProofGraphs.h::buildMasterOutOnlyGraph()` instead: one unconnected `io.output` node,
silent until something's patched into it. `buildInitPatchGraph()` and
`buildVoiceProofGraph()` are both untouched and stay registered/tested — neither is
anyone's default now, same relationship the M2 proof graph already had after M22
demoted it. This had a wider test blast radius than it looked: **13** individual
test cases across 5 files (`InitPatchTests.cpp`, `VoiceRenderTests.cpp`,
`HostInputTests.cpp`, `VisualizationTapTests.cpp`, `GraphEditControllerTests.cpp`)
implicitly relied on
a bare `BazaltAudioProcessor`'s default graph being playable (real audio from a
note-on, an `"allocator"` node existing, etc.) without ever calling `setGraph()`
explicitly — each now sets `buildVoiceProofGraph()`/`buildInitPatchGraph()` up front,
same discipline CLAUDE.md's own note already asked for. `InitPatchTests.cpp` also
gained a new first test proving the actual new default (one node, genuinely silent).

**Standalone mute-input default:** JUCE's stock Standalone app
(`juce_audio_plugin_client_Standalone.cpp` → `StandaloneFilterApp`) hardcodes "mute
audio input to avoid a feedback loop" ON on a fresh settings file — a literal `true`
buried in vendored JUCE (`build/_deps`, re-fetched, not ours to edit), hit constantly
since CLAUDE.md's own testing rule is "always build+launch the Standalone app."
Fixed via JUCE's own sanctioned escape hatch: `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`
(`plugin/CMakeLists.txt`) plus a new `plugin/source/StandaloneApp.cpp` — a near-verbatim
copy of the stock class (which is `final`, so can't be subclassed) that seeds
`shouldMuteInput = false` into the settings file once, only when no prior choice is
recorded, before JUCE ever reads it. Purely a changed *default*: a user's own later
toggle of the app's own "Feedback Loop" checkbox is read normally afterwards and never
overwritten here.

**Tests:** no new test files; 9 existing plugin tests updated (see above) plus one new
case in `InitPatchTests.cpp`.

**Verified:** built and tested together with the Audio → Control Bridge batch below,
not as a separate isolated run — see that entry's own Verified section for the actual
numbers (446/446). The first combined `ctest` run after both batches landed caught
all 9 of this batch's default-graph test fixes as real failures (proving they'd
actually been relying on the old default, not a defensive rewrite done on guesswork)
alongside the one real bug the other batch found; every one of those 10 failures is
individually accounted for across this entry and the next. Standalone app rebuilt,
launched via its own custom entry point, confirmed running (no immediate crash) and
closed cleanly — the "mute input" checkbox's own visual state is the user's own next
confirmation step, same computer-use caveat every entry in this file already carries.

**Docs updated to match:** `CLAUDE.md`'s "A fresh plugin instance opens on the Init
Patch" claim, corrected to describe the new default and point at both demoted graphs.

---

# Audio → Control Bridge — `wiki/plans/AudioControlBridge.md`, implemented — done

Not part of the `0.x` arc or either dated arc above — a standing **plan**
(`wiki/plans/AudioControlBridge.md`, written 2026-09-28) picked up and built whole on
the user's own explicit instruction ("better to have it now, rather than later").
Full design reasoning lives in the plan itself; this entry is the build record.

**New node: `adapt.audioToControl`** ("To Modulation", `AudioToControlNode.h`) — reads
a raw waveform's instantaneous per-sample value and hands it out as an ordinary
Bipolar Control signal scaled by `depth` (`hasFallbackWhenUnconnected`, default 1.0 —
the same "unpatched is just as loud as before" contract `mix.gain.gain` established).
Deliberately not `env.follower`: that node rectifies and smooths on purpose, exactly
wrong for the motivating use case (FM/ring-mod/audio-rate parameter modulation, where
the instantaneous waveform value IS the modulator).

**`CanConnect.cpp`:** a new `Audio -> Control` branch — mono sources auto-insert
`adapt.audioToControl` alone (destination is a modulation-range quantity) or chained
into `adapt.map` (destination is a real quantity, e.g. `Frequency`), seeded from the
destination's own range exactly like every other `seedFromDestinationRange` step. This
is the first genuinely 2-adapter chain `CanConnect.cpp` has ever assembled and had
actually spliced+compiled end to end (the earlier "two real quantities" case collapsed
to one node, `adapt.remap`, per ADR-0019's M20 amendment, so the 2-step splicing path
in `connectWithAutoAdapt` had never really been exercised before this). A stereo
source stays a hard `Reject` ("insert `mix.downmix` first") — a 3-adapter chain would
exceed this codebase's own two-adapter ceiling. `ui/src/graph/canConnect.ts` mirrors
the same rule for live wire-drag prediction.

**This revises ADR-0019**, not fulfils it: the ADR's original M16 text named
`env.follower` as the eventual auto-insert target for `Audio -> Control`, a wave that
was never executed. Auto-inserting `env.follower` on a bare wire-drag would have
silently defeated the actual motivating use case, so `archive_docs/decisions/
0019-adapter-table.md` gained a new Amendment section recording the revision and why,
matching the M20 amendment already living in that same file — `env.follower` itself is
untouched, still real, correct, and hand-placed only for amplitude/sidechain tracking.

**A real, previously latent bug found and fixed along the way:** `adapt.map`'s own
"in" port was hardcoded `Quantity::Unipolar`, so `CanConnect.cpp`'s own already-written
"Unipolar/Bipolar -> real quantity via Map" rule (M16) had been silently dead code for
the Bipolar half specifically ever since — a genuinely Bipolar source (`random.stepped`/
`random.drift`, or this bridge) spliced into `adapt.map.in` was rejected outright by
`GraphCompiler`'s own re-validation of the actual compiled graph, since nothing had
ever exercised that exact path end to end before this bridge did. Fixed by making
`adapt.map`'s "in" polymorphic on quantity (`PortPolymorphism::Quantity`, the same
mechanism `adapt.sampleHold` already uses) — Unipolar by default, adopting Bipolar
when that's genuinely what's wired, with `processSample()` rescaling −1…1 into 0…1
first in that case rather than clamping the whole negative half away.

**Tests:**
- `tests/CanConnectTests.cpp` — the new pure-function rule (1-step, 2-step, stereo
  reject), plus the heterogeneous-pairs catch-all test repointed at `Note -> Control`
  (the pair it originally exercised, `Audio -> Control`, is no longer a bare reject).
- `tests/UtilityNodeTests.cpp` — `MapNode`'s new polymorphism/Bipolar-rescale behaviour.
- `tests/AudioControlBridgeTests.cpp` (new) — `AudioToControlNode` unit behaviour
  (depth scaling, unconnected-depth fallback, clamping) plus two real compiled-graph
  integration tests: a raw waveform driving another oscillator's `phaseMod` through
  the bridge produces genuine, measurable audio-rate phase modulation (compared
  sample-for-sample against an independently-computed unmodulated reference), and
  `depth` genuinely scales that effect (depth=0 is bit-identical to a plain sine).
- `tests-plugin/ConnectWithAutoAdaptTests.cpp` — real end-to-end auto-insertion: the
  1-step case, the 2-step case (with seeding verified), and the stereo reject.
- `tests/NodeDescriptorTests.cpp` — registered-type count updated 72 → 73.

**Verified:** `EngineTests.exe` 358 test cases / 2,005,036 assertions, all green.
`ctest` (engine+plugin combined) 446/446 green. UI `npm run build` clean (`canConnect.ts`
mirror). Standalone app rebuilt, launched, confirmed running, and closed cleanly.

**Docs updated to match:** `wiki/NODES.md` (`adapt.audioToControl`'s own entry,
`env.follower`'s entry cross-referencing it, `adapt.map`'s entry noting the Bipolar
fix, the family status table, and the "53 of 126 catalog-only" header count — this is
the one node in the whole catalog that was never catalog-only, added whole). `wiki/
NODES.System.md` §4's matrix (three new/revised `Audio -> Control` rows). `wiki/
NODES.Status.md` (`math.*`/`logic.*`/`adapt.*` 21/21 → 22/22, totals recomputed
68→69 Implemented, 125→126 total). `archive_docs/decisions/0019-adapter-table.md`'s
new Amendment.

---

# `note.assemble` — the Note Stream follow-up — done

Not part of the `0.x` arc or either dated arc above — a direct design session on the
Note Stream family's own remaining gaps (three concrete "I can't do X" reports) named
`note.assemble` as the single highest-leverage node missing, and it was built on the
spot rather than waiting for its originally-planned slot (`wiki/NODES.Status.md`'s
own "Analysis+Assemble" batch, behind `analysis.pitch`).

**The diagnosis, for the record:** every other `note.*` node only reshapes a `Note`
stream that already exists (`io.noteIn`, real host MIDI, was the only thing that could
ever produce one). Two of the three reported gaps turned out to already have a real
answer with existing ✅ nodes, just undiscoverable: scale-quantizing a plain Control
signal works today via `data.lookup` (mode Nearest, polarity Bipolar) fed straight from
`data.scale`, and a fully custom scale (mixolydian ♭6, anything) works today by feeding
`data.lookup` a hand-drawn `data.table` curve instead of `data.scale` — `data.lookup`
already accepts either tag. The third — no way to synthesize a `Note` from clock/
random/data primitives at all — was real, and `note.assemble` is the fix.

**New node: `note.assemble`** (`NoteAssembleNode.h`) — turns a plain `trigger` Event +
a tracked `pitch` into a real `Note` stream. Zero `Note` inputs, one `Note` output —
comfortably inside the existing one-`Note`-port-per-node engine limit, no redesign
needed. Own design decisions: `trigger` retriggers legato even while already held (no
forced stop first, matching `IoNoteInNode`'s own MIDI convention); `pitch` tracks
continuously while held (vibrato/bend, or an algorithmically modulated pitch);
`velocity` is captured once at the trigger instant; a note ends on explicit `release`
OR `confidence` dropping below `confidenceGate` (the audio-pitch-tracking case, which
has no discrete note-off of its own) — with neither wired, `confidence`'s own
unconnected fallback (1.0) never drops, so a purely generative patch holds the note
until an explicit `release`, ordinary MIDI semantics, no invented auto-timeout.

**A real, previously-latent doc bug found while implementing this**: `Node.h`'s own
`produceNoteBlock()` doc comment said it runs "immediately before `processBlock()`" —
backwards. `ExecutionPlan.cpp`'s actual call site has always run it AFTER, which is
exactly what makes a node like this one possible: `processBlock()` reads this block's
ordinary Event/Control inputs and updates held-note state, then `produceNoteBlock()`
(right after, same block) reads that freshly-updated state — zero added latency, not
the one-block delay "before" would have implied. Comment corrected in place.

**A real bug this session's own history had already named once, hit again**: the
first test run hung silently with no crash and no output — this codebase's own
recurring MSVC-Debug-STL failure mode (an invisible modal assertion dialog blocking on
user input the terminal never shows, first documented in the Note Stream batch's own
entry above). Root cause this time: the test helper never called `NoteAssembleNode::
prepare()` before `processBlock()`, so its preallocated scratch buffer (sized in
`prepare()`, CLAUDE.md rule 2) was still empty — an out-of-bounds `vector::operator[]`.
Caught by noticing ctest's own retry behavior kept spawning fresh hung `EngineTests.exe`
processes each pointed at the next `NoteAssembleNode` test case, one per test, rather
than by any visible error text. Fixed in the test helper, once, for every case using it.

**Tests:** `tests/NoteStreamNodesTests.cpp` gained 7 new cases — 6 direct-node
(start/stop semantics, legato retrigger, continuous-pitch/captured-velocity, the
confidenceGate suppression, the confidence-drop auto-release, and the fully-generative
nothing-wired case) plus one real compiled-graph integration test (`clock.pulse`'s own
tick driving `note.assemble`, read back through `note.value`). `tests/
NodeDescriptorTests.cpp`'s registered-type count updated 73 → 74.

**Verified:** `EngineTests.exe` 365 test cases / 2,005,079 assertions, all green.
`PluginTests.exe` 89 test cases / 60,287 assertions, all green (454 combined). Standalone
app rebuilt and relaunched.

**Docs updated to match:** `wiki/NODES.md` (`note.assemble`'s own entry, the `note.*`
family's "7 of 10 built" count, the "52 of 126 catalog-only" header count). `wiki/
NODES.Status.md` (`note.*` section, the Analysis batch's row, the Hex Guitar Front End
dependency row, totals recomputed 69→70 Implemented, 53→52 to build, A-tier 8→7).

---

# Domain redesign — `wiki/plans/DomainRedesign.md`, implemented whole — done

Not part of the `0.x` arc or either dated arc above — a standing **plan**
(`wiki/plans/DomainRedesign.md`, written 2026-09-29) picked up and built whole on the
user's own explicit instruction ("I want you to launch the full implementation of
DomainRedesign.md"), following the plan's own §10 "approved via plan mode" 5-batch
sequence exactly. Full design reasoning (why the old model actually failed, the
`Multiplicity` type, the origin-tagging, the unison/ring-mod questions) lives in the
plan itself; this entry is the build record. `wiki/NODES.System.md` §5 was rewritten
wholesale to match (not left as a stale description of the old model) — read that for
the settled architecture, this entry for what changed and what broke along the way.

**The one-line summary:** `DomainSplitter` (whole-graph reachability, order-dependent,
exactly-one-allocator) is gone, replaced by `MultiplicityResolver` (per-node,
per-compile Scalar/Poly resolution, up to 4 simultaneous independent voice regions).
Physically, nothing about `ExecutionPlan`'s runtime shape changed — still N=8
independent plans per voice-origin via `PlanSwapper`+`VoiceManager` — only how nodes
get sorted into buckets before compile changed (§10.1's own "compile-time
reclassification, not a runtime rewrite" framing).

**Batch 1 — `MultiplicityResolver` replaces `DomainSplitter`.** New
`engine/include/bazalt/engine/graph/MultiplicityResolver.h`/`.cpp`: origin detection,
fixed-point forward propagation (origin-mismatch → reject with a real, named-node
error message), `instance.sum` input-must-be-Poly validation, backward-inclusion (an
origin's own Scalar upstream trigger source gets duplicated into that origin's own
voice-bucket graph, since two independently-scheduled `ExecutionPlan`s share no
buffers), and a `globalMembers` heuristic (output node, OR disconnected, OR has ≥1
Scalar-Scalar edge — not simply "every unresolved node"). `DomainSplitter.h`/`.cpp`
deleted outright. Two real bugs found and fixed while building this: (1) an origin's
voice-bucket output was always retargeted to the allocator's own raw gate signal even
when the graph's REAL designated output already lived inside that same bucket (e.g.
`buildVoiceProofGraph()`'s "amp" node) — silently swapping the compiled plan's real
audible signal for a Boolean gate; (2) when the designated output resolved Poly, the
global-bucket graph was never compiled at all, so `GraphCompiler`'s own validation
(invalid ports, growable-group overflow) silently never ran against newly-added
orphan nodes — fixed by decoupling "should this compile" from "should this be the
audible output" (`ensureGlobalGraphHasAValidOutput()`, new).

**Batch 1b — `instance.mix` → `instance.sum`, plus a real MIDI-independence bug.**
Rule 3 (never-rename-ids) is suspended for this codebase, so the rename needed no
migration — just `PatchDocument::currentSchemaVersion` bumped to 6 for hygiene (C++
class name `InstanceMixNode` deliberately unchanged). **A critical, previously-latent
bug found here, not before**: `spawnEventsThisBlock` increments on every noteOn/
noteOff regardless of source (one counter for both directions, by design), including
an ORDINARY note arriving via real `io.noteIn` wiring one render call after
`triggerVoiceNote()`'s own internal poke — the first cut of internal-trigger detection
misread this as an internal trigger and spawned a phantom extra voice at the same
pitch. Caught by `InitPatchTests.cpp`'s polyphony RMS test showing the resulting chord
QUIETER than expected (destructive phase interference from a duplicate, phase-offset
oscillator — not louder, the naive expectation). Fixed by gating the whole
internal-trigger mechanism on `voicePlans[0] != nullptr &&
voicePlans[0]->noteInNodeId.isEmpty()` — only an origin with NO real `io.noteIn`
wired ever runs it.

**Batch 2 — real multi-origin runtime.** `PluginProcessor`/`ExecutionPlan` generalized
from one hardcoded external-input slot to a map, so up to `MultiplicityResolver::
maxOrigins` (4) allocator/sum pairs can coexist and run simultaneously in one graph,
each its own independent physical plan bundle, each either MIDI-dispatched or
internally-triggered (never both — the Batch 1b fix is what makes this safe).

**Batch 3 — `mix.sum` folded into `math.add`; `math.multiply` actually fixed.**
`mix.sum` (`MixNode.h`) and `math.add` (`AddNode.h`) were almost line-for-line the
same node once `mix.sum`'s `level.N` was already removed (Milestone 0.5) — the only
real differences were Audio-vs-Control ports and a stored-fallback convenience an
unwired Audio input never needed. `MixNode.h` deleted; `math.add`/`math.multiply` both
gained real `PortPolymorphism::SignalAndQuantity` (same mechanism `util.reroute`/
`logic.select` already use). SignalType keeps `InheritingPortsNode`'s strict
lowest-priority-wins rule; **Quantity is deliberately lenient** — a real design bug
caught by `HostInputTests.cpp`'s own "combine io.control + io.transport through
math.add" case: giving Quantity the same strict rule broke a legitimate pre-existing
graph, since `canConnect`'s strict real-quantity matching had never actually been
enforced on `math.add`'s inputs before it became polymorphic. Fixed with a
per-port-index "unanimous agreement or fall back to Dimensionless" rule instead,
recomputed fresh on every `resolveIncomingPort()` call. **A real documentation bug
also found and fixed here**: `wiki/NODES.md` had claimed `math.multiply` "already
doubles as ring-mod" for audio-rate signals — verified false against the real source;
its ports were fixed Control until this batch.

**Batch 4 — engine/plugin: `maxInstances` enforced; per-port multiplicity + badge
data.** `VoiceManager::setMaxActiveVoices()`/`getMaxActiveVoices()` now actually
enforce `instance.allocate.voice.maxInstances` (`findIdleVoice()`/`stealVoice()`
respect it) — closing a real, confirmed gap: this parameter was declared and editable
since M17 but read nowhere, the pool always hardcoded to the full `numVoices`
regardless of it. `VoiceManager::getActiveVoiceCount()` (a relaxed atomic, recomputed
on every stage change) backs the badge's live numerator with zero audio-thread work
on the reading side. `GraphEditController` gains `getPortMultiplicity()` (per PORT,
not per node — `instance.sum`'s own mixed shape needs no UI-side special casing this
way) and `getOriginBundleIndices()` (read fresh every call, since the badge's numbers
change on every voice on/off, far more often than a recompile). New native function
`graphGetNodeMultiplicity` (alongside the existing `graphGetNodeDomains`, kept —
still real, tested, complementary info).

**Batch 4 — UI: colour split, instance-count badge, `DomainDot` removed.**
`tokens.ts`: `portAudioPoly` (`#40FF69`) added as a real, decided reversal of this same
file's own earlier "don't give Multiplicity its own hue" stance — a direct, explicit
user call ("very tricky in plugging each other... we can revert back later");
`portPoly` (the old mock-only placeholder) and `domainVoice`/`domainGlobal`/
`domainMono` all retired. `portUiKind.ts`: `PortUiKind`'s `'audio'` splits into
`'audio-scalar'`/`'audio-poly'`, both real `PORT_UI_STYLE` entries; new
`resolvePortIsPoly()` (live per-port data first, the mock-only `isPolyPlaceholder`
fallback second) replaces the ad-hoc override that used to sit outside the classifier
at 7 call sites across `NodeCard.tsx`/`ComponentGallery.tsx`/`InfiniteCanvas.tsx`.
`graphStore.ts`: `GraphSnapshot.domains` → `GraphSnapshot.multiplicity`, full
prop-chain rename through `GraphSurface.tsx`. `NodeCard.tsx`/`NodeCard.css`:
`DomainDot` (a title-bar dot) deleted outright; new `InstanceCountBadge` renders as a
real top-right corner badge (`position: absolute` off `.node-card` itself — a
correction against the plan's own "same corner" framing, since `DomainDot` never
actually sat in a corner). `canConnect.ts`: new `hasOriginMismatch()` guard in
`canConnect()` — the wire-drag-prediction mirror of `MultiplicityResolver`'s real
"fed by two different voice allocators" rejection, living here as an extra guard
rather than inside `canConnectPorts()` (which stays a pure `CanConnect.cpp` mirror,
since multiplicity is a separate whole-graph pass `CanConnect.cpp` has no notion of).

**Batch 5 — docs.** `wiki/NODES.md` (`instance.sum` rename, `math.add`/
`math.multiply` polymorphism notes, `mix.sum` entry removed). `wiki/NODES.System.md`
§5 rewritten wholesale around per-node resolution (a genuine duplicate leftover
sub-section from an earlier, incomplete edit pass was found and removed while doing
this — pre-existing, not introduced by this batch, confirmed by diffing against the
last commit). `wiki/NODES_Gaps.md` gained a new Part 4 documenting the
`maxInstances-never-enforced` and MIDI-independence-dispatch findings as closed.
`wiki/NODES.Status.md`'s node-count header was corrected against
`tests/NodeDescriptorTests.cpp`'s own authoritative running count (77 types/74 files,
not the stale 58/56 it had drifted to independently of this redesign) while fixing
the `instance.sum` rename and `mix.sum` removal. `CLAUDE.md`'s "Known interim
simplifications" bullets naming `instance.mix`/`DomainSplitter`/the one-allocator
ceiling rewritten to describe the new model, not left stale.

**Tests:** `tests/MultiplicityResolverTests.cpp` (new, ~20 cases, replacing
`DomainSplitterTests.cpp` — including the exact §0 motivating repro: `env.adsr` →
`mix.gain` connects regardless of compile order now). `tests/VoiceManagerTests.cpp`
(new cases: `maxInstances` enforcement + live count). `tests-plugin/
DomainRedesignTests.cpp` (new: per-port multiplicity including `instance.sum`'s mixed
shape, end-to-end `maxInstances` enforcement through a real 3-note chord, two
independent MIDI-independent origins both audibly playing). Several existing test
files updated for the rename/removal (`InitPatchTests.cpp`, `HostInputTests.cpp`,
`ConnectWithAutoAdaptTests.cpp`, `GrowablePortsTests.cpp`'s migration tests now
correctly assert a literal `"mix.sum"` fails to load, matching Rule 3's suspension).

**Verified:** `ctest` (both suites) 471/471 green — 376 engine + 95 plugin test
cases, reconfirmed at the end of the whole 5-batch sequence, not just per-batch. UI
`npm run build` (`tsc -b && vite build`) clean, no errors. Manual Standalone
verification (badge only on Poly nodes with a correct live count, Poly Audio cables
`#40FF69`, Scalar Audio cables pink, `DomainDot` gone everywhere) is the user's own
next step — same computer-use caveat every entry in this file already carries.

**Docs updated to match:** see the Batch 5 paragraph above — this entry itself,
`wiki/NODES.md`, `wiki/NODES.System.md`, `wiki/NODES_Gaps.md`, `wiki/NODES.Status.md`,
and `CLAUDE.md` were all updated in the same pass as this record.
