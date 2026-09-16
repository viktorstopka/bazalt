# 0011 — Feedback cycles via Tarjan SCC + implicit-delay per-sample regions

## Status
Accepted (built in M2, `GraphCompiler`/`ExecutionPlan`).

## Context
`ARCHITECTURE.md` §3.4 requires that a feedback loop in the graph (e.g. a Karplus-Strong delay
loop) compile into working audio, not get rejected outright, and that the one-sample delay a real
feedback loop needs falls out of the mechanism rather than being special-cased per node. A node set
forming a cycle has no valid topological order, so it can't be scheduled as ordinary whole-block
steps the way the rest of the graph is.

## Decision
`GraphCompiler` finds strongly-connected components (Tarjan's algorithm) in the graph's dependency
edges. A trivial SCC (one node, no self-loop) schedules as a normal block-rate `Step`. A non-trivial
SCC becomes a `PerSampleRegionStep`: its nodes run via `processSample()` in a tight per-sample loop,
wrapped as one opaque step in the outer block-rate schedule.

The one-sample delay isn't injected as an extra buffer or an explicit "delay node" — it falls out of
scheduling order. Within a region, the compiler orders nodes via DFS postorder, then **reverses**
that order for execution, with one deliberate exception: the single edge that closes the cycle is
scheduled so its consumer runs *before* its producer for that sample. Reading "whatever's currently
in the region's persistent scalar" for that one edge therefore naturally returns last sample's
value — every other (forward) edge in the region still reads this sample's freshly written value.
No edge is tagged "this one delays"; the delay is a consequence of read-before-write ordering on
exactly the edge that made the graph cyclic in the first place.

A node that only implements `processBlock()` (i.e. `supportsPerSample() == false`) cannot legally
sit inside a per-sample region. If the compiler finds one there, the whole compile is rejected —
`GraphCompiler` itself has no notion of "keep the old plan," that's `PlanSwapper`'s job (ADR-0003),
but the contract is that a rejected compile must never reach the audio thread.

## Consequences
- No separate "feedback delay" node type exists or is needed — the mechanism is generic and applies
  to any cycle the compiler finds, not just the Karplus-Strong proof graph it was built against.
- The one-sample delay is the physically correct behaviour for a real feedback loop (this is what a
  real analog/digital feedback path does), not a compiler artifact — worth surfacing this framing to
  users later (NODE_EDITOR.md) rather than letting them read it as latency to eliminate.
- This is real, non-obvious compiler complexity for a payoff (physical modeling / self-oscillating
  feedback structures) that's mostly invisible in the MVP's shipped feature set — `ARCHITECTURE.md`
  §11 flagged this trade-off going in; recording it here now that it's built and proven (Karplus-
  Strong renders correctly via `render-cli`, per M2's exit criteria) closes that risk out.
- Any future node type that's fundamentally block-rate-only (e.g. an FFT-based node) must leave
  `supportsPerSample()` false and accept that it can never participate in a feedback loop — this is
  a real, permanent constraint on that class of node, not a temporary limitation.
