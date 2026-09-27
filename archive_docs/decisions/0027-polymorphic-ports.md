# 0027 — Polymorphic ports: type and quantity inherited from what's wired, compiler as the authority

## Status
Accepted (M21; first built as CLEANUP P1 #2 for `util.reroute`). Implemented.

## Context
Several catalog nodes have ports whose type or quantity isn't fixed by the node but by what is wired to
it: `util.reroute` (any plain signal), `logic.select` (any plain type, "output quantity inherited"),
`logic.compare` ("`a`, `b` same quantity required") and `adapt.sampleHold` ("quantity inherited").
`canConnect` (ADR-0018) validates a connection from the two ports' descriptors, and `Node::getInputPorts()`
returns one fixed answer — so a fixed declaration is wrong for these nodes: declared Audio, a Reroute
rejects every Control cable; declared Dimensionless, a compare lets 440 Hz be compared with MIDI note 69.

The first fix (`c4e98f4`, Reroute only) resolved the type inside `GraphCompiler` and looked complete
because its tests called the compiler directly. It wasn't: the editor connects through
`GraphEditController::connectWithAutoAdapt`, which pre-checks `canConnect` against the node's *default*
descriptor, and the UI predicts wire drops the same way — both still said "Audio" and rejected a Control
cable, so no user could use it. That gap is why this ADR exists and why the mechanism below has three
layers, not one.

## Decision
**1. Ports inherit from their sources at compile time.** A node overrides `hasPolymorphicPorts()` and
`resolveIncomingPort(toPortId, source)`. Before any connection is validated, `GraphCompiler` offers each
connection into such a node the target port id and the *full* descriptor of the feeding output, iterating to
a fixed point (a chain of these resolves regardless of declaration order). "Did anything change" is decided
by comparing each node's `(SignalType, Quantity)` port signature before and after, so it also converges when
a node deliberately ignores a source. Nodes are never reused across recompiles (the M17 state pool
would otherwise hand the audio thread a node the compiler is about to mutate, and would keep a stale type
after a cable is removed).

**2. Ties break by port priority, never arrival order.** Sources are offered repeatedly, and a source that is
itself an unresolved Reroute reports its default on an early pass and its real type later — so "first offer
wins" freezes the wrong answer. `InheritingPortsNode::offer(priority, …)` lets the lowest-numbered priority
seen win, with the latest offer *from that priority* applied so chains resolve. By convention priority is
declaration order (`whenTrue` before `whenFalse`, `a` before `b` before `tolerance`), which is exactly what
the UI can mirror without being told. Only plain per-sample sources count (Audio/Control/Boolean/Event); a
Note has the compiler's one-Note-input limit and Data/Spectral are different runtime representations, so
`logic.select` leaves them un-adopted and canConnect rejects them as an ordinary mismatch.

**3. Each port says how it inherits** (`PortDescriptor::polymorphism`: `None` / `Quantity` /
`SignalAndQuantity`), and the node says whether it has any (`NodeDescriptor::hasPolymorphicPorts`). A bool
was tried first and was wrong: `logic.compare`'s values are always Control and adopt only the quantity,
`util.reroute`'s adopt both, and `logic.select`'s Boolean `condition` sits on a polymorphic node yet adopts
nothing.

**4. The compiler is the authority; the command layer stops pre-checking against a wrong default.** For a
connection touching a polymorphic node, `connectWithAutoAdapt` skips its default-descriptor `canConnect`
and calls `connect()`; the compiler's own `canConnect` pass runs on the *resolved* types and still rejects
a real mismatch, with the same reason text, and a rejected command leaves the graph unchanged.

**5. The UI resolves the same way** (`graphStore.endpointFor()`): follow the first declared polymorphic
input that has a wire, recursively (cycle-guarded), adopt the quantity — and the SignalType only for
`signalAndQuantity` ports — so wire-drag prediction and cable colour are right. A `signalAndQuantity` port
with nothing resolvable feeding it is marked `unresolved` and canConnect lets the drop through for the
engine to decide; a `quantity` port simply stays as declared.

## Alternatives considered
- **Per-type variants** (`reroute.audio`, `reroute.control`, …). No mechanism needed, but it multiplies the
  catalog, and `logic.select`/`compare` would need a variant per quantity. Rejected.
- **Make `canConnect` polymorphism-aware at the command layer** instead of skipping it. It would keep
  adapter auto-insertion across these nodes, but it means re-running the compiler's whole resolution
  (chains, priority, fixed point) inside the controller — a second implementation of the authority. Rejected
  for now; the cost is item 1 of Consequences.
- **Store the resolved type on the node instance/patch.** A second source of truth that can disagree with the
  wiring, exactly as with growable-group sizes (ADR-0026). Rejected for the same reason.

## Consequences
- **No adapter is auto-inserted across a polymorphic node.** Wiring a Pitch cable into a `logic.compare`
  whose `a` is a Frequency is rejected with the compiler's reason rather than getting an `adapt.remap`
  spliced in. Acceptable while it's the safer failure (an error, not a silent wrong unit); revisit if it
  gets in the way in practice.
- Two hand-synced implementations of one rule (engine `InheritingPortsNode` + compiler pass; UI
  `endpointFor`), like `CanConnect.cpp`/`canConnect.ts`. The engine wins; a divergence shows up as a drop the
  UI allowed and the engine rejected with an explanation in the error banner, never as a wrong sound.
- One known divergence: the engine treats an unresolved upstream Reroute as its default type (Audio); the UI
  treats it as "unresolved" and permissive. Harmless — the engine decides.
- `Data` never flows through a polymorphic port, and `Note` only through `util.reroute` (which is store-and-
  forward and has one input); select/compare/sampleHold refuse it.
