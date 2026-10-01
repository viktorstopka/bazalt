# 0031 — Stereo is one real `Channels::Stereo` port with real buffer plumbing, not a `left`/`right` port pair

## Status
Accepted and implemented, 2026-09-28 (`wiki/NODES.System.md` §9, `wiki/MILESTONES.md`'s `0.2 —
Stereo — done` entry). Amends ADR-0023's own "Amendment (M22)" — that amendment's decision (every
stereo-capable node uses two separate `left`/`right` Audio ports; `channels: Stereo` stays an
unused schema field) is reversed, not extended. ADR-0023 itself is left as written, not
retroactively edited — this project's own established convention for an amended ADR (ADR-0030's
own precedent, and ADR-0022's relationship to ADR-0017 before it): the new decision states the
relationship; the old one stays a historical record of what was reasoned at the time. Found stale
during a post-`util.macro`-ship sweep (2026-10-01): ADR-0023's own text, read on its own, actively
asserts the *opposite* of the real, shipped architecture, with nothing in `wiki/NODES.System.md` §9
(the doc that correctly describes the current design) ever cross-referencing it.

## Context
ADR-0023's M22 Amendment picked `left`/`right` port pairs over real `Channels::Stereo` buffer
plumbing specifically to avoid new `AlignedBuffer`/`ExecutionPlan`/UI-port-glyph infrastructure it
judged nothing yet needed — "revisit only if a future node's stereo signal must move as one
tightly-coupled unit through generic (type-agnostic) plumbing... nothing built as of M22 has needed
that."

That trigger condition arrived almost immediately. Direct feedback questioned why `space.pan` still
needed two separate cables for what is conceptually one signal, after a first attempt at a narrower
point-fix (Master Out gaining a second channel as a special case, every other node keeping separate
`left`/`right` ports) still left `space.pan`/`space.width` themselves two-cabled. The user's own
explicit instruction ("build the full redesign now") authorized reopening ADR-0023's own M22
decision rather than extending the point-fix further.

## Decision
A stereo signal is one real Audio cable, catalog-wide — `GraphCompiler.cpp` now backs `canConnect`'s
already-existing mono/stereo rules (mono→mono, mono→stereo free broadcast, stereo→stereo,
stereo→mono needs `mix.downmix`) with real per-channel buffers: a `Channels::Stereo` port occupies
two flat buffer slots, resolved once per compile, with zero change to `Node::processSample`/
`processBlock`'s own signatures. Six nodes were redesigned onto one stereo port per side —
`space.pan`, `space.width`, `io.output`, `mix.downmix` (now a genuine 1-in-1-out node, so
`connectWithAutoAdapt` can finally auto-insert it, closing a second, related gap this ADR's own
Decision section used to flag as impossible), `stereo.split`, `stereo.combine`. The constructor
default graph wires `pan.out` → `masterOut.in` as one cable — a fresh plugin instance opens playing
genuinely panned stereo, not mono duplicated to both speakers.

A real bug found during implementation, not by inspection: `Node.h`'s default `processBlock()` sized
its scratch loop from descriptor port *counts*, not flat channel counts — a node with one Stereo
output but no override would silently leave its second channel uninitialized (caught by a synthetic
test reading back classic uninitialized-debug-memory garbage). Fixed by adding
`Node::getNumInputChannels()`/`getNumOutputChannels()`, defaulting to the existing port-count
methods (a no-op for every node that never overrides them).

CLAUDE.md rule 3 ("port ids never renamed once shipped") was suspended at the time this landed, on
the user's own explicit instruction (see that rule's own note) — which is what made this redesign
tractable without migration-bridging code: all six nodes' port ids changed directly port-for-port
(e.g. `left`/`right` → one `in`/`out`), no v4→v5 data migration written (schema version still
bumped to 5 for hygiene, with a trivial version-only migration so old patches still parse).

## Consequences
- ADR-0023's own base Decision (`PortDescriptor::channels` as a schema field, `canConnect`'s
  mono/stereo rules) is unchanged and still correct — only its M22 Amendment's specific *answer*
  (which representation wins) is reversed. The base ADR's Consequences section even already
  anticipated this exact possibility: "whichever representation a future milestone actually builds...
  should be decided explicitly, as its own ADR."
- `mix.downmix`'s own two-mono-input shape, which ADR-0023 worried would need `canConnect` to reason
  about port *pairs* under a real-channels world, turned out not to: it became a real 1-in-1-out
  node instead (one Stereo input, one Mono output), the simpler of the two paths ADR-0023's own
  Consequences section already named as possible outcomes.
- Deliberately out of scope here, same as this redesign's own milestone recorded: tap/preview
  lookups on a stereo port read channel 0 only; no visually distinct stereo cable in the UI (not
  needed — `channels` was already unread anywhere in `ui/src` except `canConnect.ts`); the mono-only
  no-allocator render path untouched.
- Any future ADR or doc that cites ADR-0023 for "how stereo works in this engine" should be
  corrected to cite this ADR and `wiki/NODES.System.md` §9 instead — ADR-0023 is historical context
  for *why* a `channels` field exists on `PortDescriptor` at all, not a description of the current
  stereo architecture.
