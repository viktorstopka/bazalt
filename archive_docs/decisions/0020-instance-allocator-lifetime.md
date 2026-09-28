# 0020 — Instance Allocator / Voice Mix replace `util.voiceSum`; per-instance state pool finally built

## Status
**Implemented (M17-M18), Voice configuration only — corrected 2026-09-28 (09-28-InstanceAllocator
arc); this status line previously still said "Proposed... Not implemented," which had gone stale.**
`instance.allocator`/`instance.mix` are real, separate node types replacing `util.voiceSum`;
`VoiceManager` implements the fade-ramp stealing and generic silence-based freeing this ADR
specifies (`VoiceManager.h`'s `stealFadeSamples`/`updateSilenceAndCheckFinished`); the per-instance
state pool is real as of M17 (`GraphCompiler::compile()`'s `previousPlan` reuse, keyed by node id —
see `CLAUDE.md`'s own note on how this compares to `ARCHITECTURE.md` §3.2's original sketch).

**Two things this ADR decided that were NOT carried out, left deliberately out of scope by the
09-28-InstanceAllocator arc rather than silently forgotten:** (1) Swarm-population/Swarm-transient/
Trigger configurations — explicitly deferred by this ADR's own "Consequences" section, still
deferred, no committed milestone. (2) "Generalize `DomainSplitter` to identify N allocator regions
instead of exactly one fixed node type" — `DomainSplitter.cpp` still hard-rejects a second
`instance.allocator` node today; this generalization was never attempted.

## Context
`DOMAINS.md` proposes two separate boundary node types — **Instance Allocator** (mono→poly, four
configurable spawn sources: Voice/Swarm-population/Swarm-transient/Trigger) and **Voice Mix**
(poly→mono, placeable anywhere, multiple allowed) — plus an elaborate instance-lifetime model
(instances freed only when their per-instance chain is silent, stealing fades rather than cuts).
Today, **one** hardcoded node type, `util.voiceSum`, does both jobs simultaneously; `DomainSplitter`
requires exactly one instance and partitions by pure graph reachability from it — this matches
`NODE_EDITOR.md` §7's own design precisely (doc and code agree with each other), which is exactly
why `DOMAINS.md` diverging from both is the single largest conflict in the whole reconciliation
(`RECONCILIATION.md` 3.1). Separately, `ARCHITECTURE.md` §3.2 specified a per-voice state pool keyed
by `(voiceIndex, nodeID)` *before M2 ever shipped*, explicitly warning it "has to exist from the
first line of compiler code or it becomes an unshippable rewrite later" — it was never built.
`VoiceManager`'s stealing today cuts a stolen voice immediately (no fade); "freed when silent" is
hardcoded to `dynamic_cast` a node literally named `"env"` and check `AdsrNode::isActive()`, which
cuts any per-voice reverb/delay tail rather than letting it ring — directly contradicting the
per-voice-effects reference patch's requirement.

## Decision
Replace `util.voiceSum` with two node types (`NODE_CATALOG.md` §F): `instance.allocator` (opens an
instanced region, one of four configurations, outputs the instance-context ports `DOMAINS.md` §4
specifies) and `instance.voiceMix` (sums/averages live instances back to global domain, placeable
anywhere, multiple allowed). Generalize `DomainSplitter` to identify N allocator regions instead of
exactly one fixed node type. Generalize `VoiceManager` into the allocator's **Voice** configuration
specifically — land Voice-only first (M17); Swarm-population/Swarm-transient/Trigger configurations
are explicitly deferred to a later milestone once Voice is solid, not bundled into this same change.
Build the `(instanceIndex, nodeID)` state pool `ARCHITECTURE.md` §3.2 already specified, generalized
from its original `voiceIndex` naming — this ADR is that pool's implementation, not new design; treat
§3.2 as the spec. Replace the hardcoded `"env"`-node silence check with a generic silence detector
(threshold + hold time measured at `instance.voiceMix`'s input) and replace immediate-cut stealing
with a fade-ramp applied by the allocator to a stolen instance's contribution before real
deallocation.

## Consequences
- This is a structural redesign, not a schema addition — touches `DomainSplitter.cpp`,
  `VoiceManager.h`, `PluginProcessor`'s `numVoices=8` constant and its per-voice `ExecutionPlan`/
  `PlanSwapper` arrays (become dynamic per-allocator instance counts), and `GraphCompiler` (gains a
  real domain-inference pass rather than a pre-compile graph-rewrite step).
- No real user patches exist yet to break (M10's editor is disconnected from the engine) — the
  hardcoded proof graphs and `buildVoiceProofGraph()` are rewritten as part of this work, not broken
  by it.
- `Note` (ADR-0016/M18) and the instance-context ports (`Instance Index`/`Age`/`Random`/`Gate`/
  `Start`/`Stop`/`Position`) are this node's own interface — built together, not as separable
  follow-ups.
- Nested allocators (`DOMAINS.md` §3.7) and the three non-Voice configurations are explicitly out of
  scope for M17 — this ADR only commits to the single-level Voice case landing solidly first.
