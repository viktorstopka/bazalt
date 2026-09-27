# 0003 — Lock-free ExecutionPlan swap via single-reader epoch reclamation

## Status
Accepted (mechanism, M2). Pool size revised in M7 (see Consequences).

## Context
Graph edits happen off the audio thread (message/compiler thread) but must take effect on the
audio thread without ever locking, blocking, or allocating there. General solutions (hazard
pointers, RCU, `std::atomic<shared_ptr>`) are built for arbitrary multi-reader/multi-writer
scenarios and are heavier — and in the `atomic<shared_ptr>` case, not guaranteed lock-free on all
standard library implementations — than what we actually have: exactly one writer (the compiler
thread) and exactly one reader (the audio thread).

## Decision
A fixed pool of preallocated `ExecutionPlan` slots (4 through M6, 16 from M7 on — see Consequences).
Each plan carries a monotonically
increasing `generation`. The compiler thread builds into a free slot and publishes via
`currentPlan.store(ptr, memory_order_release)`. The audio thread loads the plan pointer exactly
once per `processBlock` call (never mid-block) and, after finishing the block, publishes
`audioThreadEpoch.store(plan->generation, memory_order_release)`. A reclaimer (message-thread
timer, ~50 ms period) frees any slot whose generation is older than both the live plan and
`audioThreadEpoch` — i.e., the audio thread has provably finished with it.

## Consequences
- Graph edits apply at the next block boundary, never inside one — this is what makes "edits never
  glitch audio" a guarantee rather than a hope.
- Per-voice DSP state must live outside the `ExecutionPlan` (in a pool keyed by
  `(voiceIndex, nodeID)`), or every recompile would reset every voice's filter/envelope memory.
  See ARCHITECTURE.md §3.2.
- The reclaimer's 50 ms period bounds how long a retired slot's memory stays allocated after its
  last use — acceptable since the pool is small and fixed, not a source of unbounded growth.
- If a future requirement needs multiple concurrent readers of the plan (e.g. a second audio
  thread, or a preview render happening in parallel with live playback), this scheme needs
  revisiting — it is explicitly single-reader.
- **M7 revision**: `reclaim()` can only free a slot once the audio thread's epoch has advanced past
  it, which requires an actual `process()` call — but M7's `GraphEditController` (NODE_EDITOR.md §6)
  can publish several times in a row with *no* intervening `process()` call at all (several commands
  issued back to back while a host's transport is stopped). The original 4-slot pool proved too easy
  to exhaust in exactly that scenario, silently dropping a publish (caught by a test that issues 5
  edits with zero interleaved `processBlock` calls). Bumped to 16 — still a small, fixed pool, not a
  source of unbounded growth, just sized for a realistic edit burst rather than the MVP's
  no-live-editing baseline. `GraphEditController::recompileAndPublish()` also now calls `reclaim()`
  immediately before each `publish()` rather than relying solely on the plugin's 50ms timer, so
  success doesn't depend on that timer's schedule either.
