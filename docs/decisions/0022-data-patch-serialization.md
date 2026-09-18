# 0022 — A node's own authored `Data` is always a hidden `data.table`, never a second serialization path

## Status
Proposed (M15). Not implemented — no node yet exists to serialize this way (`data.table` itself is
M24/Batch D). This decision governs how that milestone builds it, and amends ADR-0017, which
explicitly left this question open.

## Context
ADR-0017 established `Data` as a reference-counted, immutable buffer type but deliberately punted on
how it lives in a saved patch: *"a `Data` value doesn't serialize into a patch the way a numeric
parameter does... patch-format work for `Data`... is explicitly not settled by this ADR."*
`NODE_CATALOG (1).md`'s node catalogue leans on this immediately in two different shapes:

- **Referenced**: `resonator.modal`'s `modes` input is fed by a separate producer node
  (`data.material`'s output) over an ordinary connection — no new serialization concept, it's exactly
  like any other port connection; the patch already knows how to save "node A's output feeds node
  B's input."
- **Owned**: `adapt.remap` and `seq.steps` both carry *their own* authored `Data(curve)` content
  directly ("otherwise the node's own drawn curve is used" / "otherwise the node's own editable step
  data") with no visible producer node in the graph at all.

Building a second serialization mechanism for the owned case (a raw `Data` blob attached inline to
the *consuming* node's own patch entry) would mean the patch format needs to know how to save two
fundamentally different kinds of thing under `Data`: a connection, and a blob. That's exactly the
kind of "two places describe the same concept" drift `VALUE_MODEL.md`'s own opening paragraph warns
against for values generally.

## Decision
There is no owned case at the patch-format level. A node with "its own" curve/data is, underneath,
always a `data.table` (or the equivalent producer for whatever tag is involved) instance wired into
that node's input — the UI presents it as if it were local ("draw a curve right here"), but the
graph and the patch only ever see an ordinary two-node connection. Concretely: when `adapt.remap` is
placed with no external `curve` connection, the system auto-creates a hidden `data.table` node,
wires its output into `adapt.remap`'s `curve` input, and hides that node from the visible graph
(analogous to, but not the same mechanism as, `SIGNAL_TYPES.md` §5's auto-inserted adapters — this
one is permanent scaffolding for the node's own control, not a one-time conversion). Deleting or
disconnecting reveals it like any other node if the user ever needs to; "detach" isn't a special
operation, it's just making the existing hidden connection visible.

Patch serialization therefore needs exactly one mechanism for `Data`: node instances plus
connections, already fully specified. No second schema, no blob-shaped field anywhere.

## Consequences
- Every node with "its own" `Data` content compiles one extra hidden node into the graph. This is a
  real, non-zero cost (`NODE_CATALOG (1).md`'s own worked examples — `adapt.remap`, `seq.steps`, and
  by extension `env.curve`, `lfo.shape`'s `shape`/`shapeB` when locally authored) but bounded and
  cheap relative to the alternative of a second serialization path living forever in the patch
  format.
- The UI must render this hidden node's editor (the curve-drawing widget) as if it belonged to the
  visible node — a UI-layer responsibility, not a schema one. Out of scope for this ADR; flagged for
  whichever M24 UI work builds `adapt.remap`'s actual editor.
- A future "make unique" / "detach" operation on a *group* (`DOMAINS.md` §8) and "reveal the hidden
  data.table" are conceptually similar (both turn implicit structure into explicit, visible nodes)
  but are not the same mechanism — don't conflate them when either is built.
- This decision must land (as an actual compiler/command-bridge mechanism, not just this ADR) before
  M24 starts, since `adapt.remap` and `seq.steps` are that milestone's own scope.
