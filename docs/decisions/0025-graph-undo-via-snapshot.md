# 0025 — Undo/redo via whole-graph snapshots, not a per-command inverse log

## Status
Accepted (M19). Deviates from `NODE_EDITOR.md` §6's originally-proposed design; recorded here as
the point that document is now wrong about, per this project's own convention of amending rather
than silently leaving a stale doc.

## Context
`NODE_EDITOR.md` §6 specifies undo/redo as a client-side log of commands, replayed by their inverse
on undo (`addNode`'s inverse is `deleteNodes`, `connect`'s is `disconnect`, etc.) — reasoned as
needing no engine-side undo-awareness, since every command is already a discrete, serializable op.
By M19, that command surface is bigger than it was when §6 was written: `spliceInsert` (a real M10
interaction, kept — see the separate decision below on `addMacroFromPort`) is itself a composite of
several graph mutations, and a real `connect` may itself expand into an auto-adapter chain
(`GraphEditController::connectWithAutoAdapt`, M16) — each of these needs its own correct inverse
(un-splice must remove the spliced node AND restore the original single wire, not just the last
mutation), multiplying the surface area an inverse-log approach has to get right, with a new inverse
to design and verify for every future composite command too (`unwrap`, decorations, ...).

`GraphEditController::setGraph()` (M8) already exists and does exactly one thing: replace the whole
graph, recompile, publish, roll back the live graph on failure — the same contract every other
command already has.

## Decision
Undo/redo captures the whole `NodeGraph` before a user gesture (a single command, or a short
sequence of them the UI treats as one gesture — `spliceInsert`) as JSON and pushes `before` onto a
client-side history stack once the gesture completes, re-fetching the engine's own confirmed state
afterward rather than assuming the local optimistic state matches it (`ui/src/graph/graphStore.ts`'s
`withHistory()`). Undo calls `graphRestoreSnapshot(before)`; redo replays whatever `after` state
`withHistory()` captured post-gesture (both new native functions, `PluginEditor.cpp`). No new
serialization code is needed for this: `PatchDocument::fromNodeGraph()`/`toNodeGraph()`
(`PatchDocument.h`, already existed) plus the already-shipped `serializePatchToJson`/
`parsePatchFromJson` (`PatchSerializer.h`) round-trip a `NodeGraph` exactly, just wrapped in a
`PatchDocument` with empty `macroMappings`/`macroValues`/default `meta`/`view` — harmless, since
nothing reads those fields back out of a graph-only snapshot. One mechanism for every command, past
and future, including ones that don't exist yet, built entirely from infrastructure M0-M8 already
shipped.

A second, related deviation from §6 made at the same time: every mutating store action **awaits**
its real command's result and only then re-syncs local `nodes`/`wires` from the engine's own
confirmed graph, rather than applying the change optimistically and reconciling afterward as §6's
"Optimistic UI" paragraph proposes. This means there is never a local/engine divergence to reconcile
in the first place — simpler than building real rollback-on-rejection logic, at the cost of each
action's visible effect landing one native-function round-trip later than the input that caused it.
A local WebView call has no network hop, and every action here already only fires once per full user
gesture (a completed drag, not per frame), so this is not perceptible in practice; revisit only if a
real gesture turns out to need instant, no-round-trip feedback.

## Consequences
- A composite gesture (`spliceInsert`: disconnect + addNode + 2×connect) is implemented as several
  sequential real command calls (each its own recompile+publish) rather than one `applyBatch` call —
  accepting the "N recompiles instead of 1" cost `CLAUDE.md`'s own `applyBatch` note already flags as
  a known, accepted gap at today's edit rates, in exchange for not needing a JSON-serializable
  batch-op protocol this milestone doesn't otherwise need. `applyBatch` itself is unchanged and still
  there for whenever a real bulk/procedural use needs it (M8's stress-test generator already does). A
  partial failure mid-sequence (e.g. the disconnect succeeds but `addNode` then fails) still produces
  a coherent, correctly-undoable history entry for free — `withHistory()` diffs whatever the engine's
  state actually ended up as against what it started as, it never needs to know how many sub-commands
  ran or which one stopped the sequence.
- `addMacroFromPort` (M10's "drag a port out to create a Macro" shortcut) is retired, not ported to
  real commands — its target, `mock.macro`, has no real engine equivalent (`util.macro`, ADR-0015, is
  proposed but not implemented), so there is nothing real to wire it to. The M10 interaction and its
  `MacroConfig`/`isMacroablePort` machinery were removed from `graphStore.ts`/`interactionStore.ts`
  rather than kept half-working against a type the canvas can no longer place.
- Undo/redo no longer distinguishes *why* the graph changed — restoring a snapshot can't skip
  re-showing an intermediate state a per-command inverse could in principle preserve. Nothing in this
  project's actual UI needs that today; revisit only if a real gesture needs it.
- An out-of-band engine-originated change (a host automating a mapped macro, mentioned in §6's own
  "Optimistic UI" paragraph) is not yet wired to invalidate or rebase pending undo state — no such
  `emitEvent` exists yet for graph structure; that's a real gap for whenever host-driven graph
  mutation exists, not introduced by this decision.
- `GraphEditController::setProperty()` (new, M19 — see the M19 completion notes) is what backs both
  rename and bypass now, writing into `NodeInstance::properties`; the reused `PatchDocument`/
  `PatchSerializer` round-trip already carried that field faithfully before this decision even needed
  it, so nothing extra was needed here for rename/bypass to survive undo/redo correctly.
