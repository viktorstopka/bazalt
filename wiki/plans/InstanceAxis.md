# Instance Axis — one plan instead of a plan per voice

**Status:** Built (batch 2a), 2026-10-12 — the plugin runs every patch as one plan;
the per-voice engine is gone. Proposed 2026-10-08. §6 says how it was built and what
is still open (§4's single allocator and lifting the 8/4 limits are batch 2b,
`LifeCycle.md`).

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

## 4. One allocator, three questions (proposal, 2026-10-08)

The four allocators (Voice, Swarm (Population), Swarm (Transient), Trigger) differ only
in their answers to three questions, so stage 2 should make them **one allocator** with
an instance count and three choices — the four become Add-menu presets of it ("template
allocators cover any case", `wiki/ROADMAP.md`):

| Question | Choices |
|---|---|
| When is an instance born? | a note / an event / always (alive from the start) |
| How long does it live? | while its gate is held (then until silent) / a fixed lifetime / forever |
| What if the pool is full? | steal the oldest / steal the quietest / ignore the new one |

Every instance is born with an identity: index, a stable seed (its own randomness,
identical on every replay), birth time and age, plus pitch and velocity for a note — the
ground the stochastic suite's per-instance identity builds on. Combinations that exist
nowhere today come for free (born on an event, living while a gate is held). Instances
do not see each other here — interaction between instances (a flock, voices tuning to
each other) is a separate question for stage 2's research. Final shape after that
research; the Merge node (was Voice Sum) closes any of them.

## 6. As built (2a, 2026-10-12)

**By expansion, not by a new compiler.** `InstanceExpansion.h` turns the edited graph
into one graph to compile: every node an allocator's poly part (and its trigger chain —
`io.noteIn`, a clock that spawns it) contains appears once per instance as `id@i`;
everything else appears once. A Merge fed per instance becomes `life.reduce`
(`LifeReduceNode.h`, one input per instance); an output that itself runs per instance
gets an implicit `__output.reduce` in front of it. The unchanged `GraphCompiler`
compiles that — lanes, feedback regions, polymorphic ports, Data and Note wiring and
state carried across recompiles (copies keep stable ids) all work as before.
`ExecutionPlan::applyInstanceLayout` then tags every step and buffer with its
(group, instance), and `'@'` is refused in user node ids.

**The runtime.** Each allocator is an `InstanceGroupRuntime` on the plan: per instance
`run`, `audible`, `fading` + a steal ramp, and the `peak` the reduce measured.
`ExecutionPlan::process()` skips every step of an instance that doesn't run (an idle
voice costs nothing) and `resetInstance()` gives one instance fresh state.
`BazaltAudioProcessor::renderRange` drives the groups from each allocator's
`VoiceManager` exactly as the per-voice engine did — steal fades split the range where
they complete, release ends on silence, swarm populations always run, instance 0 of an
internally triggered allocator always runs so its trigger is heard. Previews tap every
instance's buffer and the plan pushes only the most recently played one.

**Proof, then the switch.** Before the old engine was removed, both engines rendered
the same patches and MIDI (chords, steals, releases; subtractive, stereo, no-Merge
output, Init Patch, swarm population, internally triggered swarm, every factory patch)
and matched bit for bit (the parity test lived in `tests-plugin/` at commit 177cb91);
`tests/InstanceAxisTests.cpp` keeps the engine-level parity case. Then the per-voice
swappers, voice sums and the mono/global/poly branches were deleted: one swapper, one
plan, one render path, for every patch.

**What sounds different, on purpose.**
- A mono LFO (any once-only node) feeding voices is **one shared, free-running LFO**.
  It used to be copied into every voice, where each copy reset on note-on.
- MIDI no longer allocates instances in an internally triggered allocator (the
  per-voice engine did, silently, with nothing to play them).

**Still as before (batch 2b):** 8 instances per allocator, 4 allocators, at most 31
inputs per reduce (a tree for more), the four allocator nodes.

## 5. Open questions

- Memory: per-instance state × 100 for heavy nodes (reverb, long delays) — a per-node
  instance cost the allocator can show, or a hard cap per node type?
- Scheduling: run instance-major (one instance through the whole chain) or node-major
  (one node across all instances — vectorisable)? Measure both.
- Telemetry: what a viewer inside a poly part shows (every instance overlaid, the
  newest brightest — `DataAndWavetable.md` §6).
- How the stochastic suite's per-instance identity (seeds, "genotypes") rides on the
  instance axis — decided after stage 2's research report.
