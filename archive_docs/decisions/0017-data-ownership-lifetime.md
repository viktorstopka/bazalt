# 0017 — `Data` value ownership and lifetime

## Status
Proposed (M15). Not implemented.

## Context
`SIGNAL_TYPES.md` §2's Data type details section requires: an immutable, reference-counted buffer
with a small header (element type, length, semantic tag such as `modal-set`/`scale`/`wavetable`/
`curve`/`ir`); producers build new buffers on a worker thread and publish them; the audio thread only
swaps a pointer; **never allocated or mutated on the audio thread** (CLAUDE.md rule #2). Nothing like
this exists in the codebase today — confirmed via full-repo search, zero precedent, mock or real
(`RECONCILIATION.md` 2.2). It's a hard prerequisite for `resonator.modal`, `osc.wavetable`,
`sampler.basic`, and `granular.cloud` (`NODE_CATALOG.md` §A/§B) — none of them can be written without
it.

## Decision
`Data` is a reference-counted, immutable value type (`engine::Data` or similar), built via the same
atomic-pointer-swap pattern `ExecutionPlan`/`PlanSwapper` already use for whole-graph state
(ADR-0003) — a `Data` port slot holds an atomically-loaded pointer to the current buffer; a producer
publishes a new buffer by constructing it off the audio thread and swapping the pointer; the old
buffer's refcount drops and it's freed on whichever thread's reclamation pass notices, never
synchronously on the audio thread. Each buffer carries a semantic tag (`modal-set`, `scale`,
`wavetable`, `curve`, `ir`, extensible); a consuming port declares which tags it accepts, and a tag
mismatch is a compile-time `canConnect` rejection (ADR-0018) — never coerced, per `SIGNAL_TYPES.md`
§5's explicit "anything → `Data`: rejected, no implicit construction" rule. `Data` values are
**never** copied per instance inside an instanced region (`DOMAINS.md` §6) — a modal-set or scale
computed once is referenced by every live instance identically.

## Consequences
- `Data` doesn't serialize into a patch the way a numeric parameter does — it's a reference to
  externally-authored content (a baked material table, an imported wavetable) or a reference to
  another node's output, not an inline literal. Patch-format work for `Data` (how a patch references
  a `Data` source, embedded vs. external) is explicitly **not** settled by this ADR and needs its own
  design pass before any node that persists `Data` state ships in a real save file.
- A live curve edit while audio runs (`SIGNAL_TYPES.md` §9 open question #4) needs a crossfade/ramp
  policy at the consuming node, not at the `Data` type itself — deferred to whichever node first
  needs it (a curve-editor source node isn't in the M15–M22 scope).
- `resonator.modal`'s per-instance timbral variation (reference patch 6, the cicada swarm) can't come
  from per-instance-unique `Data` buffers under this rule — `RECONCILIATION.md`/`REFERENCE_PATCHES.md`
  flag this as needing a separate mechanism (per-instance modulation ahead of a shared `Data` table),
  not a `Data`-ownership exception.
