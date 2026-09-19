# 0024 — `Note` signal representation: a dedicated per-sample event buffer, not a float port

## Status
Proposed (M18). Implements ADR-0016's deferred "Note remains declared-but-unused pending
ADR-0020/M18" and ADR-0020's "Note... is this node's own interface — built together".

## Context
`GraphCompiler`/`ExecutionPlan` give every ordinary port exactly one `float` slot per sample —
`blockBuffers`/`regionScalars`, one per output port, read through `Node::processSample(inputs[],
outputs[])`'s flat arrays. `SIGNAL_TYPES.md` §2 describes `Note` as a genuinely compound payload
("id, pitch, velocity, pressure, slide, release velocity"), not a single number — it cannot be
squeezed into that one-float-per-port model without either losing information or silently
splitting one logical port into several float ports (which `io.noteIn`'s catalog entry, a single
`notes: Note` output, doesn't do). `Data` (ADR-0016/M15) already established the precedent that a
signal type doesn't have to fit the block-buffer model at all — but `Data`'s cross-thread
publish/reclaim machinery (`DataPublisher`) solves a different problem (a producer on one thread,
a consumer on the audio thread, arbitrary lifetime) that doesn't apply here: `io.noteIn` and
`instance.allocator` are always compiled into the *same* `ExecutionPlan`, on the audio thread,
in the same generation — no cross-thread handoff needed, just a same-block, same-thread channel
with real per-sample resolution (a note landing mid-block must land on the correct sample, not
get rounded to the block boundary — `PluginProcessor::processBlock` already slices each block at
every MIDI event's sample position for exactly this reason, and that slicing is preserved as-is).

## Decision
Give a connected `Note`-typed port its own dedicated buffer of `NoteEvent` structs (`NoteEvent.h`,
new) — one entry per sample, allocated in `ExecutionPlan::noteBuffers`, completely separate from
`blockBuffers`/`regionScalars`. `NoteEvent` for M18 carries exactly what something can actually
drive today: `gate` (bool), `pitch` (float, absolute semitones, continuous — carries bend with no
special path), `velocity` (float), `startEvent`/`stopEvent` (bool, true for exactly the sample a
note-on/off lands on) — `id`/`pressure`/`slide`/`releaseVelocity` are deliberately omitted, the
same narrowing `InstanceAllocatorNode.h` already applied to its own ports in M17 ("add them once
something can actually drive them").

`Node` gains two new, defaulted-no-op virtuals: `produceNoteBlock(NoteEvent* out, int numSamples)`
and `consumeNoteBlock(const NoteEvent* in, int numSamples)`, called by `ExecutionPlan::process()`'s
Block-step handling immediately around the ordinary `processBlock()` call, for any node whose
Note-typed output/input port is actually connected. A `Note`→`Note` connection still participates
in `GraphCompiler`'s ordinary `successors`/SCC scheduling (so producer-before-consumer ordering is
guaranteed exactly like any other edge) but is routed around `incomingSource`/`resolveInput`
entirely — its ordinary float `inputs[]` slot for that port is simply unused (`Silence`). A
Note-typed port inside a detected per-sample region (feedback cycle) is rejected at compile time,
not silently handled wrong — nothing needs that yet.

`instance.allocator`'s existing 9 output ports and its own `noteOn(pitch, velocity)`/`noteOff()`
direct-poke methods are unchanged (ADR-0020's promise kept) — `consumeNoteBlock` just calls the
same `noteOn()`/`noteOff()` internally on detected start/stop edges, so direct C++ pokes (tests,
render-cli-style tools) and real graph wiring are two paths to the same logic, never two logics.
Likewise `env.adsr` gains a `gate` input port and `osc.analog` gains a `pitch` input port, both
using the existing `hasFallbackWhenUnconnected` NaN-sentinel (`DelayNode.h`'s established pattern)
so every graph that leaves them unconnected keeps today's exact external-poke behaviour — no
existing test or `render-cli` output changes.

## Consequences
- A second signal type (`Data`, then `Note`) that isn't a plain float-per-sample buffer is no
  longer a one-off exception — `ExecutionPlan` now has two "doesn't fit blockBuffers" mechanisms
  (`DataPublisher` for cross-thread/arbitrary-lifetime, `noteBuffers` for same-plan/same-block).
  `SignalType.h`'s own comment already flags `SignalType` as mixing compiler-buffer-shape and
  UI/connection concerns (ADR-0016) — this ADR doesn't resolve that, it adds a second real case of
  it, honestly, rather than forcing `Note` into the float model to avoid a second exception.
- Only one `Note`-typed input and one `Note`-typed output per node is supported (`BlockStep` carries
  a single optional note-input/note-output buffer index, not a vector) — no current or near-future
  node needs more than one each; extending to several is additive if that ever changes.
- `io.noteIn`'s `channel`/`mpeMode` structural parameters are schema-only for M18, exactly like
  `instance.allocator.configuration` was in M17 — recorded, not yet behavior-changing (Omni/no-MPE
  is the only real behavior). MPE/multi-channel filtering is out of scope here.
- Global (non-per-note) MIDI data — pitch bend, mod wheel, pedals — is **not** part of `Note`
  (`SIGNAL_TYPES.md` §9's open question 1, answered here in the direction it already proposed):
  pitch bend is injected into `io.noteIn` directly (`injectPitchBend`) and folded into the
  continuous `pitch` field of every sample's `NoteEvent`, rather than becoming its own port or
  connection. `io.control` (mod wheel, CC, sustain) stays entirely separate, unbuilt until its own
  milestone.
