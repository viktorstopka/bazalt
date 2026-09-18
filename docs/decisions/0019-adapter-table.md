# 0019 — Adapters are real, visible, auto-inserted nodes, shipped incrementally

## Status
Proposed (M16 for the first three adapters, extended through M19–M21 as dependent nodes ship). Not
implemented.

## Context
`SIGNAL_TYPES.md` §5: when `canConnect` (ADR-0018) returns `NeedsAdapters`, the UI inserts real nodes
as a single undoable command — never hidden coercion inside a cable — deterministic, at most two
adapters in a chain, auto-inserted nodes badged until edited. The doc's own table lists ten adapter
pairs. Today, `wireRules.ts` has no adapter concept at all — a binary compatible/incompatible check,
nothing else; no `NeedsAdapters`-equivalent return value exists anywhere. Several of the doc's
referenced adapter nodes don't exist yet as real engine nodes: `Normalise`, `Envelope follower`,
`Sample & Hold` have no precedent at all; `Threshold` exists only as the UI mock
`mock.triggerByThreshold`; `Note gate`/`Note value` can't exist before `Note` is real (ADR-0020);
`Voice Mix` is currently the fused downstream half of `util.voiceSum`, not yet a standalone
insertable node (also ADR-0020). Full analysis: `RECONCILIATION.md` 2.4.

## Decision
Ship the adapter table incrementally, gated on the adapter nodes it depends on existing, rather than
building the full ten-pair matrix in one milestone. First wave (M16, alongside ADR-0018): `Unipolar`/
`Bipolar` → real quantity via `Map` (`util.map`, renamed `adapt.map` as part of this same milestone;
seed `min`/`max` from the destination port's declared range on auto-insert), real quantity →
`Unipolar` via `Normalise` (`adapt.normalise`, new, seeded from the source port's range), `Control` →
`Event` via `Threshold` (`adapt.threshold`,
promotes `mock.triggerByThreshold`, default rising edge at 50% of range per the doc's own table).
Later waves add `Audio` → `Control` via `Envelope follower` (M20), `Control(audio)` → `Control(block)`
via `Sample & Hold` (M20), `Note` → `Event`/`Control` via `Note gate`/`Note value` (after ADR-0020),
`Audio` (poly) → `Audio` (mono) via `Voice Mix` (`instance.voiceMix`, ADR-0020). Auto-insertion as one
undoable command reuses `GraphEditController::applyBatch()` (M8) — no new batching mechanism needed.
`Data` is never an implicit-conversion target, matching the doc's own "anything → Data: rejected"
rule (ADR-0017).

## Consequences
- The adapter table is incomplete between M16 and whichever milestone ships its last dependency
  (M20/M21/ADR-0020) — `canConnect` should return `Reject` rather than a half-built `NeedsAdapters`
  for pairs whose adapter node doesn't exist yet, not a silent gap.
- Every auto-inserted adapter is deterministic (same pair, same seeded values, every time) and
  editable/deletable afterward like any other node — no hidden state anywhere in the chain.
- A conversion requiring more than two adapters is rejected outright; the user builds it explicitly —
  this caps chain complexity by construction, not by convention.
