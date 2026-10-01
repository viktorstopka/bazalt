# 0019 — Adapters are real, visible, auto-inserted nodes, shipped incrementally

## Status
Accepted and implemented incrementally (M16 for the first three adapters, extended through
M19–M21 and the 0.x arc as dependent nodes shipped). This Status line went stale — a later
audit (the post-`util.macro`-ship sweep, 2026-10-01) found it still said "Not implemented"
while this very document's own "Amendment (M20)" and "Amendment (0.x arc)" sections below
already described real, shipped adapter nodes (`adapt.map`/`adapt.normalise`/
`adapt.threshold`/`adapt.remap`/`adapt.audioToControl`) in past tense — a direct
self-contradiction within one file, not just staleness against external code.

## Context
`SIGNAL_TYPES.md` §5: when `canConnect` (ADR-0018) returns `NeedsAdapters`, the UI inserts real nodes
as a single undoable command — never hidden coercion inside a cable — deterministic, at most two
adapters in a chain, auto-inserted nodes badged until edited. The doc's own table lists ten adapter
pairs. Today, `wireRules.ts` has no adapter concept at all — a binary compatible/incompatible check,
nothing else; no `NeedsAdapters`-equivalent return value exists anywhere. Several of the doc's
referenced adapter nodes don't exist yet as real engine nodes: `Normalise`, `Envelope follower`,
`Sample & Hold` have no precedent at all; `Threshold` exists only as the UI mock
`mock.triggerByThreshold`; `Note gate`/`Note value` can't exist before `Note` is real (ADR-0020);
`Voice Mix` is currently the fused downstream half of `util.voiceSum`, not yet a standalone
insertable node (also ADR-0020). Full analysis: `RECONCILIATION.md` 2.4.

## Decision
Ship the adapter table incrementally, gated on the adapter nodes it depends on existing, rather than
building the full ten-pair matrix in one milestone. First wave (M16, alongside ADR-0018): `Unipolar`/
`Bipolar` → real quantity via `Map` (`util.map`, renamed `adapt.map` as part of this same milestone;
seed `min`/`max` from the destination port's declared range on auto-insert), real quantity →
`Unipolar` via `Normalise` (`adapt.normalise`, new, seeded from the source port's range), `Control` →
`Event` via `Threshold` (`adapt.threshold`,
promotes `mock.triggerByThreshold`, default rising edge at 50% of range per the doc's own table).
Later waves add `Audio` → `Control` via `Envelope follower` (M20), `Control(audio)` → `Control(block)`
via `Sample & Hold` (M20), `Note` → `Event`/`Control` via `Note gate`/`Note value` (after ADR-0020),
`Audio` (poly) → `Audio` (mono) via `Voice Mix` (`instance.voiceMix`, ADR-0020). Auto-insertion as one
undoable command reuses `GraphEditController::applyBatch()` (M8) — no new batching mechanism needed.
`Data` is never an implicit-conversion target, matching the doc's own "anything → Data: rejected"
rule (ADR-0017).

## Consequences
- The adapter table is incomplete between M16 and whichever milestone ships its last dependency
  (M20/M21/ADR-0020) — `canConnect` should return `Reject` rather than a half-built `NeedsAdapters`
  for pairs whose adapter node doesn't exist yet, not a silent gap.
- Every auto-inserted adapter is deterministic (same pair, same seeded values, every time) and
  editable/deletable afterward like any other node — no hidden state anywhere in the chain.
- A conversion requiring more than two adapters is rejected outright; the user builds it explicitly —
  this caps chain complexity by construction, not by convention.

## Amendment (M20) — two different real quantities compose Normalise+Map, not a bare Reject
Two Control ports with different real (non-Dimensionless, non-normalised) quantities — e.g. Pitch
into a filter's Frequency-quantity Cutoff, pitch-tracking, a standard synthesis technique — were a
hard `Reject` from M16 through M20, flagged in `CanConnect.cpp`'s own comment as "a real, open gap,"
not a deliberate final answer. Direct feedback after M20 made real-quantity ports common enough to
actually hit this pair: rejecting it contradicted this ADR's own premise (real quantities are numeric
ranges that can be rescaled) and the "at most two adapters" ceiling this ADR already set — though the
fix landed as one node, not two. `connectControl()` now inserts `adapt.remap` (NODE_CATALOG.md's own
node — this is its MVP linear form: `in`/`inMin`/`inMax`/`outMin`/`outMax` → `out`; curve-based
morphing between two drawn shapes is that same node's eventual, larger form, not a separate one — see
NODE_CATALOG.md's own `adapt.remap` entry for what it grows into once `data.table`/`Data(curve)`
exist), seeded from both ends at once: `inMin`/`inMax` from the source's own range, `outMin`/`outMax`
from the destination's. `GraphEditController::connectWithAutoAdapt` inserts it as a real, visible,
editable node, exactly like every other adapter — never a hidden/implicit conversion. The 2-step
chain path this ADR's "at most two adapters" ceiling describes stays real and available in
`connectWithAutoAdapt` for whichever future pair actually needs two separate nodes (Envelope
Follower, Sample & Hold, Note gate/value — the later waves above); this particular pair just didn't
turn out to need it. `Dimensionless` stays an unconditional free pass (no adapter, no rescaling) —
whether *that* should also start requiring a remap when ranges differ is a separate, larger question,
not decided here.

## Amendment (0.x arc, 2026-09-29) — `Audio` → `Control` ships via a new node, not `Envelope follower`
This ADR's original Decision text (above) named `Envelope follower` as the eventual auto-insert
target for `Audio` → `Control` (M20's wave). That wave was never executed — `Audio` → `Control`
stayed a straight `Reject` from M16 all the way through the 0.x gap-fixing arc, with `env.follower`
built (M20-era work, unrelated to this ADR) but never wired into `canConnect` at all. Picking this
back up (`wiki/plans/AudioControlBridge.md`, written from a direct conversation about audio-rate
modulation) surfaced a real problem with the original plan: `env.follower` rectifies and smooths by
design (independent attack/release ballistics) — exactly right for amplitude/sidechain-style tracking,
exactly wrong for FM/ring-mod/audio-rate parameter modulation, where the point is to use the source
waveform's *instantaneous* value as the modulator. Auto-inserting `env.follower` on a bare wire-drag
would have silently defeated that entire use case — dragging an oscillator into a filter's cutoff to
get FM-through-the-filter would silently become a smoothed envelope-follow instead of the raw waveform
being reached for.

This revises the original decision rather than fulfilling it: `canConnect` now auto-inserts a new,
purpose-built node, `adapt.audioToControl` ("To Modulation") — mechanical and opinion-free like every
other adapter this ADR governs (Map, Normalise, Threshold, Remap), never a creative DSP choice. It
reads the source waveform's per-sample value, scales it by a `depth` parameter
(`hasFallbackWhenUnconnected`, default 1.0 — full-strength passthrough, the same "unpatched is just as
loud as before" contract `mix.gain.gain` established), and clamps the result into the canonical
Bipolar range. When the destination is a real-quantity port, this is step one of the two-adapter chain
this ADR's own ceiling already allows: `adapt.audioToControl` → `adapt.map`, the latter seeded from
the destination's range exactly like every other `seedFromDestinationRange` step. A stereo source into
a Control-typed port stays a hard `Reject` — `mix.downmix` + this node + `Map` would be three adapters
deep, over the two-adapter ceiling, so it's built by hand instead, same posture stereo→mono held before
the real stereo-cable redesign.

`env.follower` itself is untouched by this amendment — real, correct, and still hand-placed only for
amplitude/sidechain-style tracking. The two nodes now coexist permanently, answering genuinely
different questions ("how loud is this, smoothed" vs. "use the actual waveform"); neither is a
superseded or interim form of the other. `wiki/NODES.System.md` §4's matrix and `wiki/NODES.md`'s
`adapt.audioToControl`/`env.follower` entries carry the day-to-day version of this same reasoning.
