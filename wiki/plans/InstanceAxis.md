# Instance Axis — one plan instead of a plan per voice

**Status:** Proposed, 2026-10-08 — direction agreed with the user, scheduled for
roadmap stage 2 (`wiki/ROADMAP.md`). No code exists yet. Stage 1
(`DataAndWavetable.md`, D2/D4) prepares the model and the card-stack visuals.

---

## 0. Origin

Stage 2 asks for ~100 instances "when it is not a heavy chain", and for the current
limits (8 voices, 4 allocators, 4 sums per allocator) to be understood rather than
just raised. They exist because of how instances are built today, not because of what
an instance is.

## 1. Today

1. `MultiplicityResolver` decides, per node, whether it is mono or poly and which
   allocator its poly signal comes from. **This part is right and stays.**
2. Each allocator's poly part is cut into its own graph and compiled **8 times** — one
   complete `ExecutionPlan` per voice; the rest compiles once as the global plan.
3. Mono nodes a poly part reads are **copied** into every voice plan.
4. Voice Sum is a hand-off between plans: voice plans add their outputs into a buffer,
   the global plan reads it.

Consequences: a fixed voice count; every edit compiles (8 × allocators) + 1 plans; no
instances inside instances; and a "shared" mono LFO feeding the voices is really nine
copies that drift apart in phase (each voice's copy only runs while its voice plays).

## 2. The model

One plan for the whole patch. Every node knows how many **instances** it runs — 1 for
mono, N for poly — keeps its state per instance and runs once per instance: the
channel-lane mechanism (`StereoChannels.md`, `ExecutionPlan`'s lane slots), with the
instance axis next to the channel axis (`DataAndWavetable.md` D2).

- **A mono node runs once** and its value is broadcast to every instance — a shared LFO
  is really one LFO, in phase everywhere.
- **Voice Sum is an ordinary step** that reduces the instance axis (Downmix reduces the
  channel axis).
- **An allocator owns lifecycles, not plans**: which instances are alive, spawning,
  releasing, stealing. `VoiceManager`'s behaviour carries over unchanged; only where an
  instance's state lives changes.
- **Inactive instances are skipped**, so an idle voice costs nothing, as today.
- **The instance count is a property of the allocator** (8, 32, 100…), bounded by
  performance, not by structure. Nested allocators compose axes (`×8 × 20`).

What does not change: node code (still written for one signal), what a user sees of
voices (stealing, release, Voice Sum), patches, and the rules of what connects.

| | Today | Instance axis |
|---|---|---|
| Voices | fixed 8 | set on the allocator |
| Allocators / sums | 4 / 4 per allocator | bounded by CPU |
| An edit compiles | (8 × allocators) + 1 plans | 1 plan |
| A mono LFO into voices | copied, drifts | one, shared |
| Swarm inside a voice | impossible | nested axes |
| An idle voice | free | free (skipped) |

## 3. How it is built without breaking things

- **Two engines side by side.** The new path is built next to the current one and the
  same patches are rendered through both — the existing render tests (Init Patch,
  polyphony, voice render, multiplicity, multiple sums) plus new ones — and the switch
  happens only when the output matches (bit-exact for deterministic patches).
- **Nodes that assume one instance**: none should, since nodes are written mono and
  the channel axis already runs them per lane; any node with shared state (a Data
  publisher, a telemetry tap) gets an explicit per-instance rule.
- **Per-sample feedback regions** must run per instance — own tests.

## 4. Open questions

- Memory: per-instance state × 100 for heavy nodes (reverb, long delays) — a per-node
  instance cost the allocator can show, or a hard cap per node type?
- Scheduling: run instance-major (one instance through the whole chain) or node-major
  (one node across all instances — vectorisable)? Measure both.
- Telemetry: what a viewer inside a poly part shows (every instance overlaid, the
  newest brightest — `DataAndWavetable.md` §6).
- How the stochastic suite's per-instance identity (seeds, "genotypes") rides on the
  instance axis — decided after stage 2's research report.
