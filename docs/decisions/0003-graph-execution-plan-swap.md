# 0003 — Lock-free ExecutionPlan swap via single-reader epoch reclamation

## Status
Accepted (mechanism); implementation lands in M2.

## Context
Graph edits happen off the audio thread (message/compiler thread) but must take effect on the
audio thread without ever locking, blocking, or allocating there. General solutions (hazard
pointers, RCU, `std::atomic<shared_ptr>`) are built for arbitrary multi-reader/multi-writer
scenarios and are heavier — and in the `atomic<shared_ptr>` case, not guaranteed lock-free on all
standard library implementations — than what we actually have: exactly one writer (the compiler
thread) and exactly one reader (the audio thread).

## Decision
A fixed pool of preallocated `ExecutionPlan` slots (4 for MVP). Each plan carries a monotonically
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
