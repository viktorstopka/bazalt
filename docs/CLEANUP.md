# Cleanup audit — post-M14–M20 consistency pass

## Why this exists

M14 through the last few sessions' worth of hands-on-testing fixes (M17's domain
split, M18's Note ports, and everything folded into this session under the
loose label "M20" — modulatable ports, `adapt.remap`/`math.round`/`math.clamp`,
the optimistic-update rewrite, the cable-rendering race fixes, the port-color
classifier fix) moved fast and were driven by real bugs found through actual
use, not by a front-loaded design pass. That's the right way to build this,
but it leaves a real cost: comments, ADRs, and milestone write-ups that
described the system as it was *before* a later fix, and code that was
written against an assumption a subsequent change quietly invalidated.

This audit (three parallel read-only passes — engine/plugin C++, UI
TypeScript, and every markdown design doc cross-checked against actual code)
exists to find that drift before it compounds, ahead of the real M20 (the
standardized visualization system — a properly-planned session of its own,
not this document's concern).

Nothing in this document has been fixed yet. It's the plan; execution is a
separate pass, prioritized below.

---

## Priority 1 — real bugs, not just stale docs

These affect actual behavior today, independent of any documentation issue.

### 1. `bypassed` has zero read sites (the one you explicitly flagged)
Confirmed via full grep across `engine/` and `plugin/`: `GraphEditController::
setProperty` writes and persists `bypassed`, but nothing in `GraphCompiler`,
`ExecutionPlan`, or `PluginProcessor` ever reads it. Toggling bypass changes
only what's stored and displayed — CLAUDE.md already says this, so it's a
known gap, not a new discovery, but it's the top of your own priority list.

Two real implementation options, scoped:
- **(a) Runtime skip-and-passthrough (recommended MVP).** Add `bool bypassed`
  to `ExecutionPlan::BlockStep`, set at compile time from
  `NodeInstance::properties["bypassed"]`. At `process()` time, a bypassed
  step copies its designated "primary" input buffer straight to its output
  buffer instead of calling `processBlock()`. Cheap, no scheduling changes.
  Needs one convention decision: which input counts as "primary" (port index
  0, or an input-side mirror of `isPrimaryOutput`), and secondary inputs are
  silently ignored while bypassed.
- **(b) Compile-time splice-around.** `GraphCompiler` removes a bypassed node
  from the schedule entirely and rewires its consumers to read directly from
  whatever fed its primary input — closer to "as if the node didn't exist,"
  but touches the scheduling/dependency-resolution code. Needed only if
  bypass must also work inside a per-sample feedback region.

Recommend (a) first; revisit (b) only if a real patch needs to bypass a node
inside a feedback loop.

### 2. `RerouteNode` can no longer reroute non-Audio signals
`RerouteNode.h`'s own doc comment claims "the engine doesn't type-check
connections at all today" — false since M16's `canConnect()` started
rejecting incompatible types at compile time. Real consequence, not just a
wrong comment: both of Reroute's ports are hardcoded `SignalType::Audio`, so
`canConnect` now rejects wiring it into a Control/Boolean/Note/Event
signal — the node's whole purpose (a generic passthrough) is broken for
every signal type except Audio. The comment itself predicted this exact risk
("revisit if a future node genuinely needs a type-polymorphic port") and
nobody came back when the risk materialized. Needs either a real
type-polymorphic port mechanism or, more simply, splitting into
per-signal-type reroute variants (or documenting Audio-only as the
deliberate scope and renaming/relabeling accordingly).

### 3. `ThresholdNode`'s unconnected slider is cosmetic-only
`threshold` declares `hasFallbackWhenUnconnected = true`, so the UI renders a
draggable slider when it's unconnected. But `setParameter()` is a literal
no-op and `processSample()` hardcodes the fallback to `0.5f` regardless of
what the slider shows. Dragging it changes the saved/displayed number with
**zero effect on the actual sound**. This is exactly the bug class M20 fixed
for ADSR/Oscillator/SVF/OnePoleFilter (`isnan(input) ? storedValue : input`,
with `setParameter` actually updating `storedValue`) — Threshold just never
got the same treatment. Small, mechanical fix once picked up.

### 4. `MapNode`'s "in" port has no declared quantity — a silent-garbage-in path
`MapNode`'s "in" port declares `minValue=0/maxValue=1` but no
`Quantity::Unipolar`, while `NormaliseNode`'s output (the thing that's
supposed to feed it) explicitly declares `Quantity::Unipolar`. Because
`canConnect`'s rule is "same-or-Dimensionless quantity is Ok," Map's
Dimensionless "in" port will accept a raw Frequency or Pitch value directly,
with **no adapter inserted at all** — a 3000 Hz value gets silently read as
if it were already 0-1 and clamped to 1.0. This is the same class of
unit-confusion bug the whole `adapt.remap` fix was built to prevent for
*different* quantities meeting each other, but it slips through here because
one side is untyped. Fix: declare `Quantity::Unipolar` on Map's "in" port.

---

## Priority 2 — stale comments (low risk, easy, but actively misleading)

Each of these describes behavior that a *later* commit already changed,
without anyone updating the comment. Ordered by how central/likely-to-mislead
the file is.

- **`engine/include/bazalt/engine/graph/NodeGraph.h`** — top-level class
  comment: "No node-graph editor UI exists yet (M2 builds the two proof
  graphs directly in C++)." Wrong since M10 (canvas exists), definitely wrong
  since M19 (canvas is wired to the real engine). One of the most central
  types in the engine; worth fixing first among the doc-only issues.
- **`engine/include/bazalt/engine/nodes/InstanceAllocatorNode.h`** — describes
  `spawn` as "not wireable/functional yet" and "M18 replaces the poke with
  real Note-port delivery" in future tense. M18 shipped this session —
  `io.noteIn` is real and wired. Needs rewriting in past tense.
- **`engine/include/bazalt/engine/nodes/MapNode.h`** — says seeding happens
  "at insertion time (a UI-side concern)". It's actually done by
  `GraphEditController::connectWithAutoAdapt`, C++, not the UI. Compare
  `NormaliseNode.h`'s sibling comment, which correctly attributes it to
  `CanConnect.h`/`AdapterStep` — the two adapters' own docs now disagree with
  each other about the same mechanism.
- **`ui/src/nodes/ValueSlider.tsx`** (lines ~32-36) — "Range is the caller's
  choice — for now... that's deferred to when the real node architecture is
  designed." This session's `hardMin`/`hardMax` split already did that work;
  this specific paragraph was missed when `NodeCard.tsx`'s equivalent comment
  got updated in the same change.
- **`ui/src/graph/mockDescriptors.ts`** — header comment says "the real
  engine registry (16 types as of M8)" — it's ~24 now.
- **`CLAUDE.md`**'s M19 command-bridge bullet — "every mutating store action
  awaits real command confirmation before updating the display... a local
  WebView round-trip is fast enough that this costs no perceptible
  responsiveness" is false since commit `b5bde7c` made every `graphStore.ts`
  action locally optimistic, specifically because the round trip *was*
  perceptible (the glitch bug). Needs rewriting to describe the current
  optimistic-apply-then-reconcile design and why.
- **`docs/MILESTONES.md`**'s M19 exit criteria — same claim, same fix needed:
  "imperceptible at today's local-WebView round-trip speed... a real,
  deliberate simplification worth knowing about if it ever isn't" — it
  wasn't imperceptible, and that's exactly what got fixed.
- **`docs/decisions/0019-adapter-table.md`**'s M20 amendment heading — still
  reads "two different real quantities compose Normalise then Map," but the
  body correctly describes the final single-node `adapt.remap` design
  (the two-node chain was an intermediate design, replaced within the same
  session before anyone but the two of us ever saw it). Self-contradicting
  at a glance; just needs the heading retitled to match the body.

---

## Priority 3 — needs an amendment, not just a wording fix

The underlying decision was reasonable when made; a real, since-confirmed
deviation from it was never recorded. Per this project's own convention
(amend, don't silently leave a doc stale — see ADR-0008's and ADR-0019's own
amendment sections as the precedent), each of these needs a dated amendment
paragraph, not a silent rewrite.

- **`docs/decisions/0025-graph-undo-via-snapshot.md`** — the snapshot-based
  undo/redo decision itself is still correct and unaffected. But its "second,
  related deviation" section (the "awaits... rather than applying
  optimistically... not perceptible in practice" paragraph) is precisely the
  premise the glitch bug disproved. No amendment exists yet — needs one
  recording the reversal to locally-optimistic updates and the real bug that
  forced it.
- **`docs/SIGNAL_TYPES.md`** §5's adapter table — has no row for "two
  different real quantities." This confirms the `adapt.remap` fix is a
  genuinely new capability, not a previously-specified-but-unbuilt pair from
  the original ten. Needs a new row: Control real quantity → Control
  different real quantity → Remap, seeded inMin/inMax from source,
  outMin/outMax from destination.
- **`docs/NODE_CATALOG.md`**'s `adapt.remap` entry — describes the eventual
  curve/`Data(curve)`-based node (ports `in`/`curve`/`curveB`/`morph`/
  `amount`), not what shipped (`in`/`inMin`/`inMax`/`outMin`/`outMax`, no
  curve support). You explicitly approved the linear version as "the MVP that
  grows into the curve one" — the catalog itself needs a note saying so, or
  someone reading it will think the current node has ports it doesn't and is
  missing ports it actually has.

---

## Priority 4 — real design questions, not obviously "fix it and move on"

These need a decision from you, not a mechanical correction — flagging them
here rather than picking an answer myself.

- **`AdsrNode`'s envelope `out` port has no declared `Quantity`.**
  `docs/REFERENCE_PATCHES.md` already documents it, informally, as
  `Control·Unipolar` — and this session's port-color classifier fix flipped
  its rendered color from (incorrectly) orange to (correctly-by-default-but-
  not-by-declaration) white. The *actually* correct fix is declaring
  `Quantity::Unipolar` on the port for real, not relying on the classifier's
  default. Same question applies to `SvfFilterNode`'s new `resonance` port
  (M20, this session) — `cutoff` correctly got `Quantity::Frequency` via
  `ValueTypes::frequencyPort`, but `resonance` shipped with no quantity at
  all, while `NODE_CATALOG.md` describes it as `Unipolar·0-1` rather than the
  shipped raw-Q `0.01-10` range. Worth a pass declaring real quantities on
  every M20-touched port that's still missing one, not just these two — but
  I'd want your call on resonance's actual intended semantics (raw Q vs.
  normalized) before touching its range.
- **`adapt.map`/`adapt.normalise` are narrower than `NODE_CATALOG.md`
  specifies.** The catalog describes a fuller shape (`inLow`/`inHigh`/
  `outLow`/`outHigh`/`curve`/`shape` for Map; `low`/`high` as real ports for
  Normalise) than what M16 actually shipped (a single seeded min/max
  parameter pair each, not ports). This predates this session, but it's
  directly relevant now: the catalog already specified something close to
  what `adapt.remap` ended up being. Worth deciding whether Map/Normalise
  should eventually converge toward Remap's shape, or stay as the simpler,
  narrower originals with Remap covering the general case.
- **`mock.triggerByThreshold`/`mock.sumVoices` in `mockDescriptors.ts`** now
  have real engine counterparts (`adapt.threshold` M16, `instance.mix` M17)
  — `ThresholdNode.h`'s own comment says it "promotes the UI-only
  `mock.triggerByThreshold` to a real engine node," implying the mock should
  have retired then. Not clearly safe to delete outright without checking
  whether the M9 gallery's real+mock merge would leave a visible gap in a
  layout-variant demo they're serving a purpose for — needs a quick look at
  the gallery's actual rendered output before deciding.

---

## Confirmed solid — no action needed

The honest other half of this audit. Checked carefully, found correct:

- **`graphStore.ts`** — every exported mutating action re-checked for the
  lazy-read-after-optimistic-mutation bug class that hit `commitWireDrag`
  once already. None of the others have it; each either uses only its own
  parameters inside the async gesture or captures what it needs before
  calling `withHistory`, matching the correct pattern. Header comment is
  accurate, no stale "not truly optimistic" language remains.
- **`InfiniteCanvas.tsx`** — no leftover TEMP DEBUG code (confirmed via
  grep). The node-removed-and-re-added-with-the-same-id edge case for the
  settle-frames mechanism is handled correctly (cache cleanup is atomic
  across all three tracking maps).
- **Port rendering/coloring is a real single source of truth** —
  `classifyPortUiKind`/`portUiStyle` and the `hardMin`/`hardMax` convention
  are used consistently everywhere a port or parameter renders (`NodeCard`,
  `InfiniteCanvas`'s cable coloring, the M9 gallery via `NodeCard` reuse) —
  no second classifier or clamp path exists anywhere else in the UI.
- **`GraphCompiler.cpp`'s Note-port routing and per-sample-region rejection**
  for Note ports touching a feedback cycle — verified still correctly wired
  against every current node type.
- **`resolveInput()`'s `hasFallbackWhenUnconnected` handling is fully
  generic** — no per-node-type branching, confirmed it automatically covers
  every M20 port (ADSR/Oscillator/SVF/OnePoleFilter/Remap/Round/Clamp) with
  zero `GraphCompiler` changes ever needed for a new one.
- **`CanConnect.cpp`'s adapter-chain logic and `connectWithAutoAdapt`'s
  1-or-2-step insertion loop** (including `adapt.remap`'s special
  4-parameter seeding branch) — no leftover assumption anywhere that only one
  step is possible.
- **No actual dead code found.** `util.voiceSum`/`VoiceSumNode` grep hits are
  all legitimate historical-lineage comments in currently-real files, never a
  reference to something that doesn't exist. `ui/src/graph/wireRules.ts` is
  fully deleted, not just retired-and-lingering — no stale imports anywhere.
- **`docs/VALUE_MODEL.md`, `DOMAINS.md`, `RECONCILIATION.md`,
  `ARCHITECTURE.md`, `NODE_EDITOR.md`** — skimmed for anything this session's
  changes might contradict; nothing found.

---

## Two small structural notes, not bugs

- **`ExecutionPlan::maxPortsPerNode` and `Node.h`'s copy of the same constant**
  are two independently-declared `= 16`s with a "keep in sync" comment
  linking them, no shared source of truth. This is the exact bug class that
  already bit once (M17's 9-output overrun before the 8→16 bump). Comfortable
  headroom today; worth collapsing into one shared constant so the next node
  needing more ports can't silently update only one copy.
- **`GraphNode.error` (UI) is a real, demoed feature with no live producer.**
  It's read by `GraphSurface.tsx` and rendered by `NodeCard.tsx`'s error
  badge, and demoed with a hardcoded string in the M9 gallery — but nothing
  on the actual live canvas ever sets it (e.g. a domain-splitter rejection
  localized to one node). Only the global `lastError` banner is wired today.
  Not urgent, just an unwired capability worth knowing exists.

---

## Suggested order of attack

1. Priority 1's four real bugs (bypass, Reroute, Threshold, Map's untyped
   input) — each is small and self-contained once picked up.
2. Priority 2's stale comments — pure text fixes, no risk, do in one pass.
3. Priority 3's three amendments — write them alongside whichever code change
   they document (the ADR-0025 amendment can be written any time; the other
   two are pure doc edits).
4. Priority 4's design questions — bring back to you before touching anything,
   since each one is a real "which way do we want this" call, not a
   correction.
