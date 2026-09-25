# 0028 — Host boundary: plain-data `HostInputs`, and a graph with no allocator is mono

## Status
Accepted (M21). Implemented.

## Context
`io.audioIn`, `io.control` and `io.transport` read things only the plugin has: the host's audio buses,
MIDI controller state, the playhead. `engine/` cannot depend on the processor, the buses or the playhead
(CLAUDE.md rule 4), so the data has to cross the boundary as plain values. The one existing crossing,
`io.noteIn` (M18, ADR-0024), does it by the plugin finding the node under the hardcoded id `"noteIn"` and
calling `injectNoteOn()` on it — which silently ignores a note-in the user placed under any other id.

A second problem surfaced with the first audio-input node. Voice plans only ran while a note was held, so an
effect graph (`io.audioIn → filter → out`) never processed anything: with no `instance.allocator` no gate or
pitch reaches any envelope or oscillator, so no graph the editor produced before the user added an allocator
ever made sound in the first place.

## Decision
**1. One plain struct, pushed by the plugin, pulled by opting-in nodes.** `HostInputs`
(`engine/graph/HostInputs.h`) holds the audio bus pointers, controller/pressure/pitch-bend state and the
transport. A node that wants it overrides `wantsHostInputs()`/`setHostInputs()`; `GraphCompiler` collects
those nodes into `ExecutionPlan::hostInputNodes`; the plugin calls `plan->applyHostInputs()` immediately
before each `process()`. The data describes only the range about to be processed — pointers are already
offset to its first sample and the transport advanced to it — so a node copies what it needs and keeps no
pointer. No id or type is hardcoded on the plugin side.

**2. Snapshot the input once per block, before anything writes output.** The main input shares the host
buffer's first two channels with the main output, and the old `processBlock` cleared every channel first —
inputs included, before anything read them. It now snapshots all five stereo buses into a scratch buffer
sized in `prepareToPlay()` (`captureHostInput`) and clears only the output channels. The block is split at
MIDI events, so each sub-range gets a stable, offset view into the snapshot; the global plan runs once over
the whole block.

**3. Controller state is tracked in every mode, omni.** The latest CC/pressure/bend on any channel, updated
in `handleMidiEvent` whether or not any voice exists, so an `io.control` placed later reads the wheel's
current position immediately. The `channel` setting is schema-only, exactly as `io.noteIn`'s is.

**4. The transport falls back to an internal one.** With no host tempo and beat position (the Standalone
app), the plugin runs 120 BPM, playing, from a sample counter, so `io.transport` still moves. That decision
sits in the plugin because it depends on JUCE's playhead; the node reads `HostInputs` and cannot tell.

**5. A graph with no allocator is mono, and runs every block.** DOMAINS.md §7 already says poly-ness comes
from the allocator, so without one there is no poly region. `DomainSplitter` reports `monoOnly` (no
`instance.allocator` and no `instance.mix`); the driver compiles the whole graph once as the global plan and
runs it every block, held note or not. Every other graph is unchanged.

## Consequences
- **`io.noteIn` still uses the hardcoded-id poke.** Note events are sample-accurate and carry gate, pitch
  and velocity; a once-per-range snapshot can't hold that. Folding it in is a follow-up, not done here.
- A graph the editor produces before the user adds an allocator now plays audio effects, and *does not* play
  notes — as before, just no longer silent for the other reason.
- Every bus is assumed stereo (`BusLayout` rejects anything else), so `io.audioIn` has two fixed outputs, not
  a growable group. If a mono or surround bus is ever allowed, that is where it changes.
- `io.control`'s values update at MIDI event boundaries in the voice domain and once per block in the
  global one.
- **A real M18 bug came out of this.** The RT-allocation trap test written for the new path (the first to
  arm the trap around a *held note* at plugin level) fired inside `handleMidiEvent`: three audio-thread call
  sites did `plan->getNodeById ("noteIn")`, and a literal converts to a heap-allocated `juce::String` on
  every call, per voice — on every note-on, note-off and pitch-bend. `getNodeById` was documented "not for
  the audio thread" and used there anyway. Fixed with one `String` built at construction and a shared
  `findNoteIn()`. `triggerVoiceNote` is `noexcept`, so there a regression shows as `std::terminate`
  rather than a catchable failure (RtAllocationTrap.h's caveat). The regression test is `Note-on, pitch bend
  and note-off never allocate on the audio thread`.
