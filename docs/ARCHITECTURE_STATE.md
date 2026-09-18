## Current state of the codebase (context, not spec)

Engine (C++/JUCE, real-time audio thread) describes each node type as plain metadata:
`getInputPorts()`, `getOutputPorts()`, `getParameters()` — never called from the audio
thread, only at compile/UI time. This metadata is serialized to JSON (an explicit
allow-list, field by field) and mirrored 1:1 as TypeScript types for the UI.

The node-graph *editor UI* that exists right now is a local, disconnected mock: placing
and wiring nodes in it never touches the real engine graph. It's a rendering/interaction
proof, not validated engine behavior — treat anything about its current node set as
throwaway, per your own instruction.

At the engine level, a connection is just `{fromNodeId, fromPortId, toNodeId, toPortId}`
— raw string IDs, zero type-checking. Every compatibility rule that exists today (what
can plug into what) lives only in the UI mock layer, not enforced by the engine at all.

There is also already a real, separate, shipped "macro" system: 32 fixed host-automation
parameters that map to node *parameters* (not ports) by ID. Whatever new node
architecture you design needs to say explicitly how it relates to this — don't let a
third, different meaning of "macro" accumulate.

## The format a node needs to be described in

For each node type, give:
- **typeId** — stable string (e.g. `"filter.svf"`), assigned once, never renamed. This is
  the patch file's foreign key; changing it breaks every saved patch.
- **title / category / icon** — display only.
- **inputs[] / outputs[]** — each a *port*: `{id, signalType, label, unit, min, max,
  default, isInteger, isLogScale}`. A port is what a cable can attach to.
- **parameters[]** — each a *parameter*: `{id, min, max, default, curve, unit,
  displayName}`. Never cable-connectable, always directly editable in the node, and the
  only thing the host-automation macro pool can bind to.
- **behavior** — plain description of what it computes per sample/block, and what must
  survive `prepare()` (sample-rate-dependent constants) and `reset()`.

Signal types that exist today: Audio; Control (generic numeric — carries both "0-1
modulation" and "real-unit value," no distinction at the type level); Boolean; and Event
— which is what "Trigger" means everywhere else in the UI (Random's Trigger input, the
Trigger-select dropdown, Trigger-by-Threshold's output). Same type, just a different
display name. Trigger/Event is real and obviously stays — it's already load-bearing in
the UI mock. The actual gap: no *real engine* node has an Event-typed port yet — MIDI is
still hardwired straight into voices, bypassing the port system entirely, so Event has
only ever been exercised in the UI mock layer, never compiled/run for real.

`Note` and `Spectral` are the genuinely open ones — both declared in the enum, neither
ever given a concrete meaning anywhere, not even in the mock layer. `Note` was added
ahead of a MIDI/note-data model that was never designed; `Spectral` is an explicitly
reserved placeholder for future FFT-based nodes. Don't treat those two as "an existing
type to match" — they're blank slots, unlike Event/Trigger.

## Mechanics worth stating plainly, so the new design fixes them rather than works around them

1. **Parameter vs. Port is a hard fork, not a spectrum — but there's a validated bridge.**
   A value is either never-connectable-always-editable (Parameter), or connectable
   (Port). Today, "connectable AND host-automatable" is a bolt-on requiring each node's
   own DSP code to hand-check a sentinel and substitute its stored value — implemented
   exactly once in the whole codebase. The resolution direction: don't make every port do
   this. Make ONE dedicated node type — `Macro` — whose entire job is "hold a parameter
   (so it can bind into the 32-slot host macro pool), expose its value as a port." Want
   some other node's input to be DAW-automatable? Wire a Macro into it; don't touch that
   node's code at all. This isn't a new idea grafted on: `util.constant` already exists in
   the real engine as the non-automatable half of exactly this pattern ("no inputs, one
   output holding a fixed value set via parameter") — a Macro is a Constant that also
   binds into the host pool. Important caveat: this bridge is a validated *direction*, not
   existing behavior — the real 32-slot macro pool currently binds only to a plain
   ParameterDescriptor on any node; a Macro node with a real, wireable output port exists
   only as this session's UI mock, not in the engine. It also only resolves the
   *automatable* half of the tension — see the open question below on whether an ordinary
   unconnected port still needs its own inline default.
2. **No first-class "value type."** Frequency/Time/Gain/Pitch are pure convention — a
   shared helper that fills in the same min/max/unit each time. Nothing in the schema
   *is* a Frequency; a Hz-range Control port and a generic 0-1 Control port are
   indistinguishable except by inspecting their numbers. Everything that wants to react
   to "what kind of value is this" (color, dropdown-vs-slider, auto-conversion) currently
   re-derives it by heuristic.
3. **No connection-compatibility enforcement in the engine.** Anything constructing the
   graph outside the UI (a hand-edited patch, a script) can wire nonsense today. Decide
   who's actually authoritative on connection validity.
4. **No enum/discrete-choice value in the schema.** A waveform select or trigger mode is
   either a plain float with an implied mapping, or a bolted-on UI-only string list.
5. **No polyphony/channel-count concept on a port** — currently a UI-only color hint,
   explicitly unfinished.
6. **Fixed-arity ports only** — a node that conceptually wants N inputs (Add, Mix) is
   hardcoded to a fixed count today; growable port count is an acknowledged gap.

## Some things to perfectly think through before creating this system

- Does every value in the graph get ONE canonical representation — id, type, range,
  unit, curve, enum options — used identically by DSP, host automation, UI, and macro
  binding? Or do these stay separate, hand-synced systems the way they are now?
- What's the minimum set of signal types that actually earns its own category, versus
  being "a Control port with different metadata"? What happens automatically (if
  anything) when two related-but-mismatched types meet at a cable?
- Who is the authority on "can these two ports connect" — engine, UI, or both — and what
  happens if they ever disagree?
- Given the Macro-node bridge above resolves "connectable + host-automatable," does an
  ordinary unconnected port still get an inline, no-extra-node default (today's
  dot-state/sentinel convenience), or does *every* default — even a simple one — require
  dropping an explicit Constant/Macro node into the graph? If the former, that's still a
  per-port capability every node needs; if the latter, does the UI auto-place a hidden
  node to preserve the "it just works out of the box" feel?
- How does polyphony flow through a port — a distinct signal type, a channel count on
  the port, or a structural property of the graph (per-voice subgraphs)?
- How far does a generic node-rendering contract (ports + parameters + a handful of
  layout shapes) reach before a node needs a genuinely custom UI (granular, spectral,
  curve editors) — and how is that escape hatch declared, so it doesn't get bolted on
  per-node later?
- Should discrete/enum values and growable-arity ports be core from day one, given real
  node families need both immediately (mode selectors, N-input mixers)?
- Given IDs can never be renamed once shipped, how much of this type system must be
  right from the start versus safe to extend later?