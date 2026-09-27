# 0026 — Growable port groups: size derived from connections, shipped ports migrated not renamed

## Status
Accepted (M21). Implemented.

## Context
`SIGNAL_TYPES.md` §6 specifies growable ports: a node that takes "N of the same thing" (`math.add`,
`mix.sum`, a boolean gate) declares a port group `in.0…in.N`; ports are stable, removing a middle cable
leaves a hole instead of renumbering, and the UI reveals a fresh empty port once the last one is wired.
M14 added `PortGroup` to `PortDescriptor` (and its JSON/TS mirror) but nothing used it — `math.add`,
`math.multiply` and `mix.sum` were still fixed two-input nodes with ports `a`/`b`, and their headers
deferred the real thing to M21.

Three questions had to be answered, none of which the spec settles:

1. **Where does "how many ports does this node have" live?** A `Node`'s ports come from
   `getInputPorts()`, and the compiler resolves every connection against them, so the count has to reach
   the node before that call.
2. **What happens to the shipped `a`/`b` port ids?** CLAUDE.md rule 3: ids are never renamed once shipped.
3. **How does the audio thread stay safe** when a live edit changes a node's port count?

## Decision
**1. The count is derived, never stored.** `requiredPortGroupCount()` (`PortGroups.h`) is the highest
group index referenced by any connection into the node, plus one, held in `[minCount, maxCount]`.
`GraphCompiler` passes it to `Node::setGroupPortCount()` on each freshly created node *before* reading
its ports. Nothing is added to `NodeInstance` or the patch, so a patch can never disagree with itself,
and a hole (an unwired index between wired ones) simply falls out. An index at or past `maxCount` is
clamped, so the over-range port is then reported as an ordinary unknown port — the right rejection.
Port ids are `idPrefix + canonical decimal` (`in.1`, never `in.01`), so an id names exactly one port.
A node's groups share one index range: `mix.sum`'s `in.N` and `level.N` companion grow together, and
are declared interleaved (`in.0, level.0, in.1, …`) so each source sits beside its own gain.

**2. Migrate, don't rename.** Patch schema v3 adds a v2→v3 migration mapping `a`→`in.0`, `b`→`in.1` on
connections *into* `math.add`, `math.multiply` and `mix.sum` only. `math.subtract`, a genuinely fixed
two-input node, keeps `a`/`b` (a test pins that). v1 patches chain through both steps. This is the
mechanism rule 3 exists to protect: an old patch keeps loading and wiring exactly as before.

**3. Never mutate a live node.** The M17 state pool reuses a node across recompiles, and the audio
thread may still be running the previous plan. A group node is reused only if its required size is
unchanged; wiring the spare port therefore gives that one node a fresh instance (the same trade as
editing its own parameters). `math.add`/`math.multiply`/`mix.sum`/`logic.boolean` are stateless, so
nothing audible is lost.

Supporting choices:
- **Unwired members read a stored value** (`math.add`: 0, `math.multiply`: 1), the identity element,
  so the spare port and any hole never change the result — and that stored value is what the card's
  in-node slider edits, so "A + 5" needs no Constant node. `mix.sum`'s `level` defaults to 1, which
  keeps `a + b` bit-for-bit unchanged (`x * 1.0f == x`); the Karplus-Strong proof graph is identical.
  Unwired audio reads silence, the identity for a sum.
- **`logic.boolean` counts only wired inputs**, using the NaN "unwired fallback" sentinel, because
  AND needs to tell "unwired" from "wired and false".
- **`ExecutionPlan::maxPortsPerNode` 16 → 32**, now public, and enforced by `GraphCompiler` as a compile
  error. `mix.sum`'s 16 inputs carry a level each, and the old limit was only a `jassert`, which
  compiles out of a release build and would have been a stack overrun.
- **`GraphEditController`'s port lookup** sizes its throwaway node for the requested port, so a cable
  dropped on a spare `in.N` validates.
- **The UI mirrors the engine** (`ui/src/graph/portGroups.ts`): `withRevealedGroupPorts()` shows every
  wired index plus one spare (never past the maximum), only for a live instance — the component
  gallery keeps the default minimum. `findPort()` resolves a group member the default descriptor
  doesn't list, so a drop on the spare port isn't a miss.

## Alternatives considered
- **Store the count on `NodeInstance`.** Explicit, but a second source of truth: it can disagree with
  the connections (a count of 4 with only `in.0` wired), needs its own command, migration and undo
  handling, and buys nothing the derivation doesn't already give.
- **Keep `a`/`b` as the first two members** and add `in.2…`. No migration, but every growable node would
  carry an irregular legacy prefix forever, and the catalog specifies `in.0…in.N`.
- **A fixed large port count with the UI hiding unused ones.** Simplest, but every instance pays for 16
  inputs (compile size, `processSample` loop) and it can't express `maxCount`.

## Consequences
- Adding a cable to a group's spare port replaces that node object (see 3). Acceptable while every
  growable node is stateless; a future *stateful* growable node would lose its state at that moment and
  would need either a state-transferring compile or its own decision.
- The UI's reveal rule and the engine's derivation are two hand-synced implementations of one rule,
  like `canConnect.ts`/`CanConnect.cpp`. The engine wins; the UI only decides what to show.
- Two limits, not bugs: a node has one shared index range across all its groups, and the group's
  `autoRevealOnLastConnected = false` is honoured by the UI but the engine never distinguishes it.
- The compiler indexes incoming port ids per node once per compile. A per-node scan of every
  connection would be quadratic — the M8 stress graph is 500 `math.add` nodes against 1000
  connections, roughly half a million string comparisons per compile, and a compile runs per voice
  plus once for the global plan on every command. This is reasoning from the graph's size, not a
  measurement: the indexed version was written first and no timing of the naive one was taken.
