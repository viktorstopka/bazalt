# 0023 — `channels: Mono | Stereo | Inherited` on Audio ports; no multi-channel buffer plumbing yet

## Status
Proposed (M16), amended and the open stereo-representation question resolved (M22 — see the
Amendment below). `PortDescriptor::channels` and `canConnect`'s corresponding rule are implemented;
the actual multi-channel-per-port buffer representation this field implies is explicitly **not**
built, and this ADR says so rather than papering over it.

## Context
`NODE_CATALOG (1).md`'s "Decisions this catalogue settles" section states: *"An Audio port carries a
mono signal. Stereo is made explicitly (two cables, or a `space.pan` producing an L/R pair)...
Ports may declare `channels: 1 | 2 | inherited`; where a node is naturally stereo (reverb, width), it
declares stereo ports."* Neither `VALUE_MODEL.md` nor `SIGNAL_TYPES.md` (the two documents
`RECONCILIATION.md` was written against) mentions channels at all — this is a real, deliberate
extension the bigger catalogue needed, not a gap in the original three docs.

Implementing this surfaced a genuine ambiguity the catalogue text doesn't resolve: most of its own
stereo-capable node entries (`space.reverb`, `io.output`, `resonator.plate`) actually declare **two
separate ports** named `left`/`right`, not one port tagged `channels: Stereo` carrying two channels
in a single buffer. A single-port multi-channel representation and a two-separate-mono-ports
representation are genuinely different engine architectures — `AlignedBuffer`/`ExecutionPlan` today
hardcode one channel per port's buffer everywhere (`buffer.resize(1, maxBlockSize)`, confirmed by
direct reading of `GraphCompiler.cpp`); building real support for the former is a much larger change
than adding a schema field.

## Decision
Add `channels` to `PortDescriptor` as specified (a real, deliberate schema field, defaulting to
`Mono` — matching every port that exists today). Add the corresponding `canConnect` rule: mono into
mono or stereo is `Ok` (mono→stereo is a free broadcast/duplication), stereo into stereo is `Ok`,
stereo into mono is `NeedsAdapters` via `mix.downmix`, `Inherited` on either side is always `Ok`
(forward-looking, no real node uses it yet).

**Explicitly deferred, not built now**: the actual multi-channel-per-port buffer representation a
`channels: Stereo` tag implies. No real node produces one today (correctly — none of the physical
buffer plumbing to back it exists), so `canConnect`'s stereo→mono branch is provable only against
synthetic test descriptors (`CanConnectTests.cpp`), not a real end-to-end graph. `mix.downmix` is
built for real (`NODE_CATALOG (1).md`'s own two-separate-mono-inputs shape: `left`, `right` →
`out`), and is genuinely useful today for any hand-wired two-mono-port stereo signal — but
`GraphEditController::connectWithAutoAdapt` does **not** attempt to auto-insert it, because a 2-in-
1-out node cannot be spliced into a single connection the way `adapt.map`/`adapt.normalise`/
`adapt.threshold` (all 1-in-1-out) can; it rejects with a message pointing at manual insertion
instead of guessing at a shape that doesn't fit.

## Consequences
- `canConnect`'s channels rule is correct and tested in isolation, but has no real end-to-end
  exercise yet — that only becomes possible once a real node declares a genuine multi-channel
  buffer (or once the catalogue's own two-ports convention is adopted instead, which would need
  `canConnect`'s signature to reason about port *pairs*, not the single from/to ports it takes
  today — a bigger change, not attempted here).
- Whichever representation a future milestone actually builds (single multi-channel port vs. a
  `left`/`right` port-pair convention) should be decided explicitly, as its own ADR, before any real
  stereo-capable node (`space.reverb`, `resonator.plate`, etc. — M28 territory) is implemented — not
  discovered ad hoc per node the way the catalogue's own text currently mixes both without comment.
- `mix.downmix` stays real and useful regardless of which representation wins — a two-mono-input
  downmix is meaningful either way (either as the manual adapter for a single-multi-channel-port
  world, or as the natural way to fold a `left`/`right` port pair down to one signal).

## Amendment (M22) — the question is answered: `left`/`right` port pairs, `channels: Stereo` stays unused

This ADR's own Consequences section asked for this decision "before any real stereo-capable node...
is implemented," and named `space.reverb`/`resonator.plate` (M28) as the trigger — `space.pan` and
`space.width` got there first, six milestones earlier than anticipated.

**Decision: every stereo-capable node uses two separate `left`/`right` Audio ports, matching
`DownmixNode`'s existing (M16) input-side precedent and the catalogue's own text for every stereo
entry it lists — `space.reverb`, `resonator.plate`, `sampler.granular`, and now `space.pan` (output)
and `space.width` (input and output).** `PortDescriptor::channels = Channels::Stereo` stays exactly
as this ADR left it: a real, tested-in-isolation schema field with no node using it, and no near-term
plan to build the buffer plumbing it would need (one port carrying two interleaved or dual-pointer
channels — a change to `AlignedBuffer`/`ExecutionPlan` this decision doesn't require and doesn't
motivate building). `mix.downmix`'s existing two-mono-input shape needed no change either way, exactly
as this ADR's own Consequences section already anticipated.

This was the cheaper, zero-new-infrastructure path — `left`/`right` ports are ordinary
`PortDescriptor`s the compiler, `canConnect`, and every UI layer already handle correctly today,
while a real `channels: Stereo` buffer would have meant new `AlignedBuffer` multi-channel storage,
new `ExecutionPlan` scheduling for it, and new UI port-glyph handling for a "this one socket carries
two signals" concept nothing in the node editor currently expresses — real scope no stereo node
actually needs in order to work. Revisit only if a future node's stereo signal must move as one
tightly-coupled unit through generic (type-agnostic) plumbing that a `left`/`right` pair can't express
— nothing built as of M22 has needed that.
