# 0018 — The engine is the sole authority on connection validity

## Status
Proposed (M16). Not implemented.

## Context
`SIGNAL_TYPES.md` §4: exactly one `canConnect(from, to) -> Ok | NeedsAdapters(chain) | Reject`
function, in C++, generated into the TypeScript mirror the same way node metadata already is; "if UI
and engine ever disagree, the engine wins and the discrepancy is a bug." Today, **zero** connection-
type checking exists anywhere in the engine: `GraphCompiler::compile()` resolves connections by
string-ID lookup only, never reading `SignalType`; `GraphEditController::connect()` only checks that
both node IDs exist; even the UI's own mutation boundary (`graphStore.ts`'s `commitWireDrag()`)
performs no validity check of its own, trusting the caller already gated it via `wireRules.ts`. Every
source consulted for this reconciliation converged on this independently — see `RECONCILIATION.md`
2.3. `ARCHITECTURE.md` §3.3 confirms `SignalType` was designed only as a compiler buffer-shape tag,
never as a connection-validity concept — this is genuinely new engine authority, not formalizing
something already designed-but-unbuilt.

## Decision
Implement `canConnect` as a pure function of two `PortDescriptor`s (post-ADR-0014 value contracts) in
`engine/`. Call it from two places: `GraphCompiler::compile()` (rejects an invalid connection at
compile time, leaving the previous valid plan live — reuses the existing rejection contract from
ADR-0011/ADR-0003, no new "keep the old plan" mechanism needed) and `GraphEditController::connect()`
(rejects a bad command immediately, using the same `CommandResult{success, errorMessage}` shape every
other command rejection already uses, so a bad connection surfaces as an error banner exactly like
any other rejected edit). Export the rule table via the same codegen mechanism ADR-0007 already
established for `NodeDescriptor` JSON, so `ui/src/graph/wireRules.ts` becomes **generated**, not
hand-maintained — the UI's classification-bucket approach (`portUiKind.ts`) is superseded by
generated rules reflecting the real engine function, not an independent reimplementation.

Before enabling hard rejection, audit `ProofGraphs.h` and the default `buildVoiceProofGraph()` against
the new rule — no existing compiling graph should start failing to compile.

## Consequences
- `wireRules.ts`'s current 6-bucket classification (`audio, modulation, value, integer, trigger,
  boolean`) is retired in favour of generated rules once this ships; anything relying on its specific
  bucket names needs updating.
- `graphStore.ts`'s `commitWireDrag()` gap (no validity check at its own mutation boundary) is closed
  as a side effect once `GraphEditController::connect()` enforces `canConnect` — the UI no longer
  needs to be the only thing standing between a bad gesture and an invalid graph.
- This is a prerequisite for ADR-0019 (adapters) — `NeedsAdapters` has nothing to hang off until this
  exists.
- A hand-edited or scripted patch can no longer wire nonsense past the engine, closing
  `ARCHITECTURE_STATE.md`'s originally-flagged gap ("anything constructing the graph outside the UI...
  can wire nonsense today").
