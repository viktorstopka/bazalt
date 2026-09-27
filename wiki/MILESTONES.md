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

## 0.2 — Stereo, scoped (not built)

Full engineering design written into `wiki/NODES.System.md` §9: the chosen
representation (a stereo port occupies two consecutive flat buffer slots — no change
to `Node::processSample`/`processBlock`'s signatures, no change to 53 of the 55
existing node files, no change to the patch format's connection shape), the exact
`GraphCompiler.cpp`/`PluginProcessor.cpp`/telemetry change surface, the real cost
(independent per-channel wiring needs two new `stereo.split`/`stereo.combine` bridge
nodes that aren't needed today), why `mix.downmix` auto-insertion stops being a rare
edge case and starts mattering, the schema v5 migration plan (including the
asymmetric-old-patch case), and a 5-wave implementation order.

**Not implemented yet** — this milestone was explicitly "scope it out," not "build
it." Waves 1–5 in §9.7 are the next 0.2.x-style work once building starts.

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
