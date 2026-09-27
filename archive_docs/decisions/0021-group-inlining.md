# 0021 — Groups compile by inlining/flattening before domain inference; native namespace unchanged

## Status
Proposed (M22, last in the sound-design-system sequence). Not implemented.

## Context
`DOMAINS.md` §8: a group is a subgraph with named exposed ports, behaving like an ordinary node from
every outside perspective; compiled by inlining and flattening, recursively, before domain inference;
carries a domain signature (`mono→mono`, `any→any`, `mono→poly`, `poly→mono`); stored either embedded
in the patch or as a versioned library reference with an explicit "make unique" to detach; namespaced
`core.*` (native, frozen IDs), `factory.*` (shipped groups), `user.*` (user groups), `lab.*`
(experiments, no stability promise). Zero precedent exists anywhere in engine or plugin code today —
confirmed via full-repo search for group/subgraph/composite/inlining concepts; this is the single
largest piece of *net-new* (non-redesign) work in the whole reconciliation (`RECONCILIATION.md` 3.6).
Separately: the doc's `core.*` prefix doesn't match the actual shipped convention — every existing
native node uses a flat category prefix (`osc.*`, `filter.*`, `util.*`, `delay.*`, `env.*`, `amp.*`,
`mix.*`, `noise.*`) with no `core.` wrapper, and per CLAUDE.md rule #3 those IDs are permanent
(`NODE_CATALOG.md`'s namespace note).

## Decision
Build groups last, after the native catalogue (`NODE_CATALOG.md` Part A) ships in waves — matching
the planning prompt's own instruction that riskiest foundations land first and groups land last, and
matching `DOMAINS.md` §9's own framing of Part B's factory groups as proving the primitive set is
sufficient, not as something to ship day one. A new compiler pass inlines and flattens every group
instance (recursively) before domain inference (ADR-0020) runs — at run time a group doesn't exist
and costs nothing; per-sample feedback regions may cross group boundaries exactly as they cross
ordinary node boundaries today (ADR-0011). The compiler computes each group's domain signature and
rejects placement in an incompatible context with the same precision as an ordinary node, before the
user hears anything wrong. Storage: embedded-copy is the default; library-reference with a pinned
version is opt-in, and "make unique" detaches an instance so a later library update never silently
changes it.

**Namespace deviation from `DOMAINS.md` §8, adopted deliberately**: native nodes keep their existing
flat category-prefix convention, not `core.*` — that prefix is reserved exclusively for the group
system going forward. `factory.*`/`user.*`/`lab.*` are adopted as written for groups, since nothing
existing conflicts with them.

## Consequences
- The six `NODE_CATALOG.md` Part B groups (Karplus-Strong, Scale Quantize, Arpeggiator, Bubble,
  Crackle, Scrape) are built for real in M22, alongside the two native nodes
  (`data.quantize`, `note.holdMemory`/`note.harmonizer`) the gap list identified as blocking Scale
  Quantize and Arpeggiator — closing those gaps and proving group sufficiency happen together.
- "Write it natively when..." / "ship as a group when..." (`DOMAINS.md` §9's own decision rule,
  including its "~20 nodes or ~3× CPU" rule of thumb) governs every future native-vs-group choice
  from this point on — no separate ADR needed per node, this one establishes the standing rule.
- A group's own preview tap declaration (`DOMAINS.md` §8 — "groups must not be the only nodes
  without live visualisation") needs wiring into the M11-era telemetry-tap mechanism
  (`TelemetryHub`'s 64-slot pool, ADR-0009) — flagged here, not separately specified, since it reuses
  that existing mechanism rather than needing a new one.
