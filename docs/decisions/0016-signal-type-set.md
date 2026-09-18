# 0016 — Signal type set: add Data, defer folding Boolean into Control

## Status
Proposed (M14 for schema groundwork, M15 for `Data` specifically). Not implemented.

## Context
`SIGNAL_TYPES.md` §2 proposes six types — `Audio, Control, Event, Note, Data, Spectral` — with
Boolean and Integer explicitly **not** among them ("they are `Control` with `kind=bool`/`int`").
`engine/include/bazalt/engine/graph/SignalType.h` today has six *different* members: `Audio,
Control, Event, Note, Spectral, Boolean` — no `Data`, and `Boolean` exists where the doc says it
shouldn't. `Boolean` was added deliberately at M7 with a stated rationale in its own header comment:
a genuinely different buffer-level contract (no smoothing, no skew, one bit of state), not a UI
colour distinction. `ARCHITECTURE.md` §3.3 frames `SignalType` overall as a pure compiler
buffer-shape tag, decoupled from value semantics — under the *current* compiler design, folding
`Boolean` into `Control` would require `GraphCompiler`'s buffer-allocation logic to read `kind=bool`
from the (pre-ADR-0014) value contract at compile time, which it has no mechanism to do yet. Full
analysis: `RECONCILIATION.md` 2.1, 2.2.

## Decision
Add `Data` as a seventh member of `SignalType`, matching `SIGNAL_TYPES.md` §2 exactly (immutable,
reference-counted, semantic-tagged buffer — see ADR-0017 for ownership/lifetime detail). Do **not**
fold `Boolean` into `Control` in this pass. Revisit after ADR-0014 (value contract) ships: once
`GraphCompiler` can read `kind` from a real value contract at compile time, re-evaluate whether
`Boolean`'s distinct buffer representation can be derived from `kind=bool` instead of a separate
`SignalType` member — recommend keeping `SignalType::Boolean` as a reasoned buffer-shape
optimisation and amending `SIGNAL_TYPES.md` §2 accordingly, unless that compiler work turns out to
be trivial once profiled.

## Consequences
- `SignalType` stays a 7-member enum (`Audio, Control, Event, Note, Data, Spectral, Boolean`) rather
  than the doc's proposed 6, until/unless the Boolean question is revisited post-ADR-0014.
- `SIGNAL_TYPES.md` §2's own text needs a documented amendment once this is settled either way — it
  currently states a rule the codebase has a reasoned, shipped exception to.
- `Event`/`Trigger` naming is **not** a conflict — the doc's "pick one name for the codebase, UI name
  is Trigger" is already how the code and UI mock layer behave (`Event` internally, "Trigger"
  displayed). No action needed there.
- `Note` remains declared-but-unused pending ADR-0020/M18 (Instance Allocator, real MIDI rewiring) —
  this ADR only settles the type list, not Note's runtime meaning.
