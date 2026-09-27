# Bazalt — Node system architecture

This is the shared rulebook the node catalog (`wiki/NODES.md`) is written against:
signal types, the value contract, ports vs. structural parameters vs. content, the
domain (mono/poly) system, and — the thing that was scattered across three old docs
and a `.ts`/`.cpp` pair before — the **full connection/adapter matrix**, with a clear
line between "the target design" and "what's real in the engine today."

Supersedes `archive_docs/SIGNAL_TYPES.md`, `archive_docs/VALUE_MODEL.md`,
`archive_docs/DOMAINS.md`. Those stay as historical record; don't read them for
current truth, read this.

---

## 1. Signal types

A signal type answers "what shape of data flows on this cable." Everything else
(units, ranges, whether it's a toggle or a number) is *metadata on the value*, not a
new type — this is deliberate: it keeps the type list small and pushes richness into
one place (§2) instead of a proliferation of special-cased wire kinds.

| Type | What flows | Rate | Notes |
|---|---|---|---|
| `Audio` | one sample per frame, audio range | audio rate | the only type that's always audio rate |
| `Control` | a numeric value (§2) | block or audio rate | carries floats, ints, bools *and* enums |
| `Event` | discrete occurrences with a sample offset | sample-accurate | the UI calls this **Trigger** — one type, two names |
| `Note` | note lifecycle events with a payload | sample-accurate | id, pitch, velocity, pressure, slide, release velocity |
| `Data` | an immutable block of values | updated between blocks | tables, modal sets, scales, curves, wavetables, samples |
| `Spectral` | reserved | — | no meaning assigned yet, connects to nothing |

Deliberate consequences:

- **Boolean and Integer aren't signal types.** They're `Control` with `kind = bool` /
  `int`. The UI colours/glyphs them distinctly by reading the value contract, not by a
  separate wire type.
- **Modulation isn't a signal type.** It's `Control` with quantity `Unipolar` or
  `Bipolar` (§2).
- **Note is a real type, not a MIDI convention.** MIDI must enter the graph through a
  Note-typed port instead of being wired directly into voice allocation — otherwise
  arpeggiators, chord generators and scale quantisers can't exist as ordinary nodes
  sitting *between* MIDI-in and the allocator.
- **`Data` is first class.** A `Data` value is an immutable, reference-counted buffer
  with a small header (element type, length, a semantic **tag** such as `modal-set`,
  `scale`, `wavetable`, `curve`, `sample`, `eq-curve`). Producers build new buffers on
  a worker thread and publish them; the audio thread only ever swaps a pointer —
  **never allocated or mutated on the audio thread.** A consumer declares which tags
  it accepts; wiring the wrong tag in is rejected at compile time, never silently
  reinterpreted.

### Real state, today

All five real types (`Audio`, `Control`, `Event`, `Note`, `Data` for `canConnect`'s
purposes — `Data` itself has no real producer node yet) are implemented in
`engine/src/graph/CanConnect.cpp` / mirrored in `ui/src/graph/canConnect.ts`. `Note`
has exactly two real ports anywhere in the engine right now: `io.noteIn`'s `notes`
output and `instance.allocator`'s `spawn` input (plus polymorphic `util.reroute`,
which can carry a Note cable through unchanged). A connected Note port doesn't use the
ordinary per-sample `blockBuffers` mechanism — it gets a dedicated `NoteEvent`-typed
buffer (`ExecutionPlan::noteBuffers`), and **only one Note input and one Note output
per node is supported today** — a limitation nothing currently built runs into, but
worth knowing before assuming a Note output can freely fan out to several
destinations the way an Audio/Control output can.

---

## 2. The value contract

Every number, switch or choice anywhere in the engine is described by one structure,
owned by the engine and serialised into node descriptors — there is no second,
UI-only notion of what a value is.

```
ValueContract {
  kind          float | int | bool | enum
  quantity      see below
  min, max      optional; absent = unbounded
  default       required
  curve         linear | exponential | logarithmic | custom-ref
  polarity      unipolar | bipolar        // meaningful for normalised quantities
  enumOptions   [{ id, label }]           // required iff kind = enum
  step          optional quantisation step, canonical units
}
```

- **Bounds are optional and their absence is meaningful** — a bounded value gets a
  slider with a fill; an unbounded one gets a drag field with no fill. The UI never
  guesses which.
- **`default` is always present** — it's what an unconnected port feeds (§5 below),
  what double-click resets to, and what a fresh node starts with.
- **`curve` describes perception, not storage** — values are always stored/transmitted
  in canonical units; `curve` only maps a control's 0–1 gesture onto the range.
- **Enum option IDs are part of the patch format.** Renaming a label is safe;
  changing an ID breaks saved patches. Reordering options must never change behaviour
  — nothing may rely on option index (contrast: `instance.allocator.configuration`'s
  *index* genuinely is its stored value today, which is correct only because it's
  also treated as a stable ID by convention — see `NODES_Gaps.md` if this ever needs
  revisiting).

### Quantity — the physical dimension of a value

| Quantity | Canonical unit | Typical display |
|---|---|---|
| `Frequency` | Hz | Hz, kHz |
| `Pitch` | semitones (float, 60 = middle C) | note names, cents |
| `Time` | seconds | ms, s, samples, tempo divisions |
| `Gain` | linear amplitude | dB |
| `Ratio` | dimensionless ratio | ×, % |
| `Unipolar` | 0…1 normalised | % |
| `Bipolar` | −1…1 normalised | % |
| `Count` | integer count | plain number |
| `Phase` | turns (0…1) | degrees |
| `Pressure` | Pa-equivalent, **bipolar** | % (Correction 1 — inhalation ≠ exhalation with a minus sign; a valve's geometry isn't front-to-back symmetric) |
| `Mass` | kg-equivalent | g, kg (Correction 1, physical models) |
| `Length` | metres | mm, cm, m (Correction 1, physical models) |
| `Stiffness` | N/m-equivalent | plain number (Correction 1, physical models) |
| `Dimensionless` | raw number | plain number |

- **One canonical unit per quantity, always.** Display units (ms vs. samples, Hz vs.
  note names, dB vs. linear) are a UI formatting layer and never change what's
  transmitted on a cable.
- `Unipolar`/`Bipolar` are the "modulation" quantities — this distinction, **not**
  the signal type, is what the UI colours (orange for normalised, white for a real
  physical quantity — see §6).
- Quantity drives **default seeding of auto-inserted adapters** (§4): dropping a
  `Unipolar` signal into a `Frequency` port creates a `Map` node already scaled to
  that port's min/max.
- New quantities may be added later (Correction 1 added `Pressure`/`Mass`/`Length`/
  `Stiffness` for physical modelling). Removing or redefining one may not.

---

## 3. Three categories of value: ports, structural parameters, content

A node's data falls into exactly one of three buckets. Getting this categorization
wrong is itself a real class of bug — see `redundant-composable-param` and
`modulation-only-port` in `wiki/NODES_Gaps.md`.

### Ports (the default — almost everything)

There's no separate "parameter" concept for ordinary values. A node declares
**ports**; a port carries a value contract; an unconnected port uses its **inline
default**, editable directly on the node body. This is deliberate, against the
alternative of parameters and ports as two separate lists — that fork forces every
"connectable and automatable" value to be special-cased in node DSP code.

**The inline default is resolved by the graph compiler, not by each node.** When a
port is unconnected, the compiler supplies a constant (a NaN-sentinel the node
detects and substitutes its stored default for, in the current engine's actual
mechanism — `hasFallbackWhenUnconnected` + `defaultValue` on `PortDescriptor`). Node
DSP code should see exactly one shape: a connected input. **A port meant to be
hand-tweakable must declare `hasFallbackWhenUnconnected` with a real `defaultValue`
— leaving it off is a bug, not a style choice**, because it means the port is
unusable until something is wired into it (see `GainNode.h`'s `gain` input in
`NODES_Gaps.md` for the confirmed live instance of getting this wrong).

### Structural parameters

A setting is **structural** if and only if changing it requires reallocation, a
change of graph topology, or a new `prepare()` — table size, oversampling factor,
max mode count, an algorithm variant that swaps the processing kernel entirely.

- Cannot be modulated, has no port.
- May cause an audible discontinuity when changed — the UI should say so.
- Still described by a value contract (usually `enum` or `Count`), so it renders with
  the same controls a port would.
- **Not** bindable to the host macro pool by default.

If a setting doesn't meet this test, it's a port. "It feels like a parameter" is not
a reason — this test is deliberately strict.

### Content (Correction 2 — a third category, not yet built)

A **factory** node (§8) owns an editable *document* — an EQ's band list, a drawn
curve's point list, a 30 MB recording with slice markers. This fits neither ports nor
structural parameters:

```
NodeContent {
  schemaId        stable string, versioned independently of the node type
  schemaVersion   integer, with its own migrations
  payload         structured data (JSON), or an asset reference
  externalized    optional map: element id → node id (see §8, Unwrap)
}
```

- **Not modulatable, not automatable, never bindable to the host macro pool.** To
  modulate part of a factory's content, unwrap that part into a real node (§8).
- Serialised under its own schema version, independent of the node type's own port
  IDs or the patch's top-level version.
- Published like `Data` — rebuilt on a worker thread, swapped atomically, crossfaded
  by the consumer. Never allocates on the audio thread, never clicks.
- A factory may still expose ordinary **ports** for whole-document continuous
  controls (output gain, global tilt, morph amount) — those follow the normal port
  rules. Content is what's *edited*; ports are what's *played*.

---

## 4. Connections: the adapter/`canConnect` matrix

**The engine is the sole authority.** `canConnect(from, to) -> Ok | NeedsAdapters(chain)
| Reject(reason)` is a pure function of two port descriptors, implemented once in
`engine/src/graph/CanConnect.cpp`, hand-mirrored into `ui/src/graph/canConnect.ts` for
live wire-drag prediction. **If the UI and engine ever disagree, the engine wins** —
the UI's answer is prediction only; the engine validates every connection at compile
time, so a rejected/invalid edit can never reach the audio thread.

When `canConnect` returns `NeedsAdapters`, the committing path
(`GraphEditController::connectWithAutoAdapt`) inserts **real, visible nodes** as a
single undoable command — never hidden coercion inside a cable. A chain is at most
two adapters; if a conversion needs more, it's rejected and the user builds it by
hand.

The table below is the **complete scenario matrix** — every `from` × `to` pair and
what actually happens, with a column distinguishing the aspirational full design from
what `CanConnect.cpp` really implements today. Where they differ, the engine's actual
behavior is what a patch experiences; the "target" column is what a later milestone
should close.

| From | To | Outcome | Adapter | Real today? |
|---|---|---|---|---|
| `Audio` (mono) | `Audio` (mono) | Ok | — | ✅ |
| `Audio` (mono) | `Audio` (stereo) | Ok (broadcast — free) | — | ✅ |
| `Audio` (stereo) | `Audio` (mono) | NeedsAdapters | `mix.downmix` (currently **manual only** — the UI predicts the need but doesn't auto-insert it yet) | ⚠️ partial — see §9.5, this stops being a rare edge case under the stereo redesign |
| `Control` (same quantity) | `Control` (same quantity) | Ok | — | ✅ |
| `Control` (`Unipolar`/`Bipolar`) | `Control` (real quantity, e.g. `Frequency`) | NeedsAdapters | `adapt.map`, seeded from the destination port's range/curve | ✅ |
| `Control` (real quantity) | `Control` (`Unipolar`/`Bipolar`) | NeedsAdapters | `adapt.normalise`, seeded from the source port's range | ✅ |
| `Control` (real quantity A) | `Control` (real quantity B, e.g. `Frequency`→`Pitch`) | NeedsAdapters | `adapt.normalise` → `adapt.map` (two-adapter chain) | ✅ |
| `Control` (block-rate) | `Control` (audio-rate) | Ok, free broadcast | — | ✅ (no rate distinction is actually enforced in the current engine — every Control port already accepts either; see `NODES_Gaps.md`) |
| `Event` | `Event` | Ok | — | ✅ |
| `Note` | `Note` | Ok | — | ✅ (only 2 real Note ports exist to test this with — see §1) |
| `Boolean` | `Boolean` | Ok | — | ✅ |
| `Control` | `Event` | NeedsAdapters | `adapt.threshold`, rising edge at 50% of range | ✅ |
| `Event` | `Control` | — | catalog names `env.adsr`(trigger-fed)/`adapt.sampleHold`("Latch") as the manual pattern | ❌ not auto-inserted |
| `Audio` | `Control` | — | catalog names `env.follower` | ❌ not auto-inserted — today this is a straight `reject` unless placed by hand |
| `Note` | `Event` | — | catalog names a `note.gate`-style "Note gate" adapter | ❌ `note.gate` itself isn't built yet (M25) |
| `Note` | `Control` | — | catalog names via `instance.allocator`'s own outputs, or `note.value` | ❌ `note.value` isn't built yet (M25); the allocator path is real but isn't a `canConnect` adapter — it's just wiring the allocator's own output ports |
| `Data` (tag X) | `Data` (accepts tag X) | Ok | — | ✅ |
| `Data` (tag X) | `Data` (accepts tag Y ≠ X) | Reject | "Data tag mismatch" | ✅ |
| `Data` (tag X) | anything non-`Data` | Reject | "Data never converts implicitly" | ✅ |
| anything non-`Data` | `Data` | Reject | same | ✅ |
| `Data(eq-curve)` | `Data(curve)` | — | catalog names `data.eqToCurve` (Correction 2) as an **explicit, one-way** node — never automatic, since the two tags mean genuinely different things (bands-in-dB-across-log-frequency vs. values-across-normalised-axis) | 📋 catalog only |
| `Audio` (poly-domain) | `Audio` (mono-domain, no mix in between) | Reject at compile time | — insert `instance.mix` by hand | ✅ (`DomainSplitter`) |
| `Spectral` | anything | Reject | "Spectral is reserved, not yet implemented" | ✅ |
| type mismatch, no rule above applies | | Reject | "Incompatible signal types with no adapter available yet" | ✅ (the catch-all) |

**Occupied-port behavior (today, real):** the compiler enforces one source per input
— a real, intentional rule, not a bug. Dropping a second cable onto an already-wired
input is currently rejected rather than replacing the existing one; see
`occupied-port-rejects` in `NODES_Gaps.md` for the planned UX fix (auto-disconnect,
not a rule change).

### Growable ports

A node that conceptually takes N inputs (`mix.sum`, `math.add`) declares a **port
group**: `PortGroup { idPrefix, min, max, growPolicy }`. Port IDs within a group are
`prefix.0`, `prefix.1`, … and are **stable** — removing a middle port leaves a hole
that serialisation preserves, it never renumbers the rest. The UI reveals a fresh
empty port once the last one is wired, never below `min` or above `max`.

---

## 5. Domains: mono ↔ poly

A graph is mono everywhere by default. An **Instance Allocator** opens a poly
(instanced) region; a **Voice Mix** closes it back to mono. Everything between the two
runs once *per instance*, with its own state — this is how per-voice distortion,
per-voice delay, and per-voice resonators work.

### `instance.allocator` — one node, several configurations

Voice allocation and "swarm" spawning are the same runtime machinery with a different
event source — one node type, not several that grow apart.

| Configuration | Spawn source | Instance context | Typical use | Built? |
|---|---|---|---|---|
| Voice | a `Note` stream | the note | playing an instrument | ✅ (only one, today) |
| Swarm (population) | fixed count, always live | index + seeded randoms | cicadas, a drone of many bodies | 📋 M28 |
| Swarm (transient) | an `Event` stream | seeded randoms per spawn | bubbles, crackles, sparks, raindrops | 📋 M28 |
| Trigger | an `Event` stream, one instance at a time | payload | percussive one-shots | 📋 M28 |

The `configuration` structural parameter already exists and records a non-zero value
honestly, but only Voice actually runs different behavior today.

### Instance context (the allocator's output ports)

Everything here is `polyOnly`. Common to all configurations: **Instance Index**
(`Count`), **Instance Age** (`Time`), **Random** — a stable random value seeded from
`(patch seed, spawn ordinal)` for the instance's whole lifetime, several independent
ones addressable by index (this is *why* `random1`/`random2` exist on the node
directly rather than being read from an ordinary `random.*` node — a plain
`random.*` node runs once per block for the whole graph and can't reproduce a
per-instance, spawn-ordinal-seeded value; see `NODES_Gaps.md`'s
`instance-scoped-output-needs-review` entry), **Gate** (`Control`, bool), **Start**
and **Stop** (`Event`), **Position** (spatial, swarm configurations). Voice
configuration adds **Pitch**, **Velocity**/**Pressure**/**Slide**/**Release
Velocity**, **Unison Index**/**Unison Detune**.

### Instance lifetime

- An instance is **created** on spawn, reaches **releasing** when its note ends or its
  lifetime expires.
- An instance is **freed only when its per-instance chain is silent**, not when an
  envelope finishes — per-voice reverbs/delays must be allowed to ring out. Silence
  detection sits at Voice Mix's input, with a threshold and hold time.
- Live instance count can therefore exceed held-note count. `maxInstances` bounds the
  live count, not the note count.
- **Stealing** applies to live instances, fades a stolen instance out over a short
  ramp rather than cutting it.
- Buffers are preallocated for `maxInstances`.
- Latency inside an instanced region is identical for every instance, reported once.

### Events across the boundary

- A mono `Event` broadcast into an instanced region fires in **every live instance**
  — the correct default (a global clock ticking all swarm members).
- To fire in one instance only, the event must go through the allocator (a note-on is
  exactly this).
- Events generated *inside* an instance never leave it, except through Voice Mix
  (audio only) or an explicit aggregation node (later feature).
- `Data` is read-only and shared across instances — never copied per instance.

### `instance.mix` — closing the region

Sums or averages live instances back to mono, placeable anywhere, more than one
allowed. Reports per-instance silence back to the allocator, which is what actually
frees a slot. `DomainSplitter` currently supports **exactly one** allocator region and
**exactly one** mix per graph — a second of either is rejected, documented in the
compiler rather than silently assumed.

### Nested allocators

Architecturally natural (a swarm inside a voice) but multiply cost. Supported by the
model; requires explicit opt-in per allocator and the UI showing the multiplied
worst-case instance count. Not built.

---

## 6. How the UI derives its visual language

Nothing in the UI hardcodes a colour rule per node or per type name — everything below
is derived from the value contract:

| Visual | Derived from |
|---|---|
| pink / magenta cable | `signalType = Audio` |
| orange | `Control`, quantity `Unipolar` or `Bipolar` |
| white | `Control`, real-world quantity |
| yellow | `Control`, `kind = int` |
| blue, `?` glyph | `Control`, `kind = bool` |
| violet, `!` glyph | `Event` |
| teal, `♪` glyph | `Note` (Milestone 0.7 — this was the actual cause of the "same colour won't connect" report in `NODES_Gaps.md`: Note fell through to white, colliding with real-quantity Control) |
| *(open)* | `Data` — no colour assigned yet; no real node produces one, so there's nothing to observe against |
| line style | domain (mono vs. poly, §5) |

---

## 7. Naming philosophy

Prefer a plain, human-readable name over inaccessible jargon **wherever the plain
name loses nothing**. This software has no voltages — calling a level-control node
"VCA" imports 1970s analog-synth wiring vocabulary for no reason a user of this
software needs.

This does **not** mean renaming established, widely-understood musical/audio terms —
**LFO**, **ADSR**, **EQ** stay exactly as they are; they're the standard vocabulary a
musician already has, not inside-baseball electronics jargon. The test is: would a
person who's never touched a modular synth, but has used *any* piece of music
software, already know this word? "Envelope," "Filter," "Gain," "Delay" pass. "VCA,"
"VCO," "VCF" don't — they describe a 1970s implementation detail (voltage control)
that has nothing to do with what the node actually does here.

`wiki/NODES.md`'s own catalog entries already use the plain names (`mix.gain` is
titled "Gain" there) — where the running engine's `getTitle()` disagrees with the
catalog, that's a confirmed bug, tracked in `NODES_Gaps.md`, not a naming question
still open.

---

## 8. Factories (Correction 2)

A **factory** is a native node with three things an ordinary node doesn't have:
**content** (§3) it owns and edits, a **custom editor** declared rather than
hardcoded, and a declared **unwrap** expansion into ordinary nodes.

A factory is **not a group** — groups are inlined subgraphs, deferred until after the
catalog ships; factories don't wait for them. A factory compiles like any other node;
unwrap uses the same declarative-expansion mechanism as Assist recipes, not group
inlining.

Two shapes: **Producers** own content and output a `Data` buffer (Curve, Wave,
Material) — unwrap yields a `data.*` producer plus the implied consumer. **Processors**
own content that configures their own DSP and pass audio/notes through (Spectral/EQ,
Sample, Notes) — unwrap yields the equivalent chain of primitives.

### The custom-editor contract

A node descriptor may declare an editor instead of relying on generic port rendering:

```
ui {
  kind        "generic" | "inline" | "custom"
  editorId    stable string, resolved against a registry of editors in the UI
  contentRef  which NodeContent schema the editor edits
  taps        which telemetry taps the editor needs while open
}
```

Editors are deliberately separate per factory (an EQ, a curve editor, a wave editor, a
sample editor have nothing in common in data model or visualisation — one editor
serving all of them produces four bad ones). What's shared is infrastructure only:
the content/`Data` publishing path, canvas gestures (pan, zoom, select, snapping,
undo), the fast render layer, and design tokens. An open editor is a telemetry
subscriber in its own right — subscribed while open, unsubscribed on close.

### Unwrap — the engine-level contract

Turns part or all of a factory into ordinary nodes, so it can be modulated, automated,
rewired.

- **A graph transform, not a UI trick** — the factory descriptor declares its
  expansion as data, applied as one undoable command.
- **Sound-preserving** — rendering before/after must produce the same audio (bit-exact
  for linear factories; statistical comparison for anything chaotic/self-oscillating,
  per Correction 1's own testing note).
- **One-way.** No re-wrap.
- **Two levels** — a single element, or the whole factory.
- **An externalised element leaves a trace** — `content.externalized: <nodeId>`, shown
  greyed in the editor with a link, so the same element never exists in two places at
  once with two truths.
- **Content doesn't unwrap; processing does** — a recording/drawn shape/note pattern
  stays a `data.*` node feeding the resulting chain.
- **A factory mode that can't be expressed by the primitives must not exist.** Either
  the primitive is added first, or the mode isn't offered — a factory is never
  allowed to be more capable than the catalog.

### Namespace note (Correction 2's `stock.*` rename)

`factory.*` is the factory node family itself (`factory.eq`, `factory.curve`, ...).
Shipped *groups* (Karplus-Strong, Water, Cicada Field, Init Patch, ...) therefore move
to **`stock.*`** to avoid the collision — the namespaces are `core.*` (native
primitives, written without the prefix throughout `wiki/NODES.md` per existing
convention), `stock.*` (shipped groups), `user.*` (user groups/saved factory
content), `lab.*` (no stability promise).

Full factory specs (`factory.eq`, `factory.curve`, `factory.wave`, `factory.sample`,
`factory.notes`, `factory.material`) and the new nodes they require (`data.record`,
`data.eqToCurve`) live in `wiki/NODES.md`'s `factory` and `data` sections. None of
this is built yet — it's catalog-only, same status as most of Correction 1.

### What must be decided before building (carried over from Correction 2, still open)

1. The asset store in the patch format (content-addressed blobs, embedded vs.
   referenced, per-patch size budget) — needed before any factory that owns audio
   ships, or patches bloat/invent their own storage.
2. `NodeContent` as a real third category (§3) — changes what "structural" means for
   `data.table`/`seq.steps`, neither of which is built yet, so this is free to land
   correctly from the start rather than a migration.
3. The `ui.custom` descriptor field — cheap now, a per-node bolt-on later.
4. The `stock.*` rename — free today, breaking once a patch references a shipped
   group (none do yet).
5. Unwrap as a declared expansion, not UI-side special-casing — the difference between
   one testable mechanism and six untestable ones.

None of this blocks anything currently built. Factories ship one at a time whenever
their milestone comes up.

---

## 9. Stereo audio: the single-cable redesign (Milestone 0.2)

**Status: scoped, not built.** This section is the engineering design for turning a
stereo signal into one real Audio cable instead of two `left`/`right` mono ports —
your stated preference, reopening a decision the project already made deliberately,
twice (`archive_docs/decisions/0023-audio-port-channels.md` and its M22 Amendment).
Read those first if you want the original reasoning in full; this section doesn't
repeat it, it builds on it and says exactly what changes.

### 9.1 Why this is a reopening, not a bug fix

ADR-0023's Amendment chose `left`/`right` port pairs **specifically because** it was
"the cheaper, zero-new-infrastructure path" — genuine multi-channel-per-port buffer
plumbing would have meant new `AlignedBuffer` storage, new `ExecutionPlan`
scheduling, and new UI port-glyph handling for a concept nothing in the node editor
expressed yet, for zero nodes that needed it at the time. Its own stated bar for
revisiting: "only if a future node's stereo signal must move as one tightly-coupled
unit through generic (type-agnostic) plumbing a left/right pair can't express."

That bar hasn't technically been hit by a new node's requirements — this is being
revisited for a different, legitimate reason instead: **ergonomics**. Two mono ports
per stereo signal means two cables to draw, two sockets to hit, and a stereo signal
that doesn't read as "one thing" on the canvas the way it does in every DAW you've
ever used. That's a real cost worth fixing on its own; this section doesn't pretend
it's fixing a bug.

### 9.2 What's already true today (confirmed by reading the actual code, not assumed)

- **`AlignedBuffer::resize(numChannels, numSamples)`** already takes a channel count
  — it wraps a `juce::dsp::AudioBlock<float>`, a real structure-of-arrays multi-channel
  buffer. Every call site (`GraphCompiler.cpp`, 3 of them) just always passes `1`
  today. **The storage primitive needs zero changes.**
- **`ExecutionPlan::blockBuffers`** is one `AlignedBuffer` **per port**, not per node
  — `BlockStep::inputs`/`outputBufferIndices` are already "one entry per port," and
  every node's `getNumInputPorts()`/`getInputPorts().size()` are already always equal
  (1:1, port-for-port) for every one of the 55 real nodes today, with **no existing
  concept of one port spanning more than one buffer**.
- **`Node::processSample(const float* inputs, float* outputs)`** is a flat array,
  one scalar per port, in port-declaration order. **`Node::processBlock(const float*
  const* inputs, ...)`** is one pointer per port. Neither can represent "two channels
  for this one port" without either (a) the port occupying two array slots instead of
  one, or (b) a deeper signature change (pointer-to-pointer-per-port) that would touch
  every one of the 55 node implementations just to keep compiling.
- **`PortDescriptor::channels`** (`Mono | Stereo | Inherited`) already exists, already
  has a real `canConnect` rule (mono→mono/stereo free, stereo→stereo free, stereo→mono
  needs `mix.downmix`, `Inherited` always `Ok`) — tested in isolation, never exercised
  by a real node.
- **`mix.downmix`** (`left`, `right` → `out`) is real today, and stays exactly as-is —
  useful in both worlds. It is **not** auto-insertable by `connectWithAutoAdapt` (a
  2-in-1-out shape doesn't fit the 1-in-1-out `AdapterStep` splice mechanism) — a real,
  already-known gap that gets more important under this redesign (see §9.5).
  There is no converse `mix.upmix` node and none is needed — mono→stereo is a free
  broadcast per the existing `canConnect` rule.
- **`io.output`/Master Out** has one mono `in`/`out` port; `PluginProcessor` tracks
  exactly one final mono buffer and duplicates it to both physical output channels
  (`PluginProcessor.cpp`: `left[i] += finalMono[i]; right[i] += finalMono[i];`, at
  every one of the 4 places it reads a plan's `finalOutputBufferIndex`). This is
  `archive_docs/CLEANUP.md` Priority 1 #6's already-logged gap — this redesign is what
  actually closes it, not a side effect.
- **Exactly two real nodes** use the `left`/`right` convention today: `space.pan`
  (outputs) and `space.width` (inputs `in.left`/`in.right`, outputs `left`/`right`).
  Everything else in the catalog that's stereo-shaped (`space.reverb`,
  `resonator.plate`, `sampler.granular`, `io.audioIn`'s `channel.0`/`channel.1`) is
  still catalog-only or, for `io.audioIn`, a pre-existing two-separate-mono-ports
  design that predates the `left`/`right` convention entirely.

### 9.3 The chosen design: twin flat slots, not a multi-channel buffer

Two candidate representations exist. This section picks one and says why.

**Rejected: one `AlignedBuffer` per port, with `numChannels = 2` for a stereo port.**
Sounds natural since the storage class already supports it — but it doesn't solve the
real problem. `AudioBlock`'s multi-channel storage is structure-of-arrays (channel 0
and channel 1 are separate contiguous regions), so a single `const float*` still can't
address both — `processSample`/`processBlock`'s signatures would still need to change
to pass something richer per port (a pointer-to-pointer, at minimum), which is the
expensive part regardless of where the samples physically live. This option buys
nothing over the alternative below and costs the same.

**Chosen: a stereo port occupies two consecutive flat slots, each an ordinary mono
`AlignedBuffer` exactly as today.** `Node::processSample`/`processBlock`'s signatures
**do not change at all**. What changes is entirely inside `GraphCompiler.cpp`'s
port-to-slot resolution:

- Today: descriptor list index *N* == flat slot index *N*, always, for every node.
- New: a node's flat slot count is the sum of 1 per `Mono` port and 2 per `Stereo`
  port in `getInputPorts()`/`getOutputPorts()` order; the compiler computes each
  descriptor's **starting** flat-slot index by summing the channel counts of every
  descriptor before it, not by using the descriptor's own list position directly.
- `NodeGraph::Connection` **does not change at all** — one graph-level connection
  (`fromNodeId`/`fromPortId`/`toNodeId`/`toPortId`) still means one logical wire,
  exactly as today. The compiler is what turns a connection between two stereo ports
  into two underlying `InputRef` bindings (slot 0→slot 0, slot 1→slot 1) instead of
  one. **The UI, the patch format's connection list, and 53 of the 55 existing node
  files need zero changes** — this is what makes "twin flat slots" the right choice:
  the blast radius is `GraphCompiler.cpp` plus the two real stereo nodes, not
  everything that touches a `Node`.
- **Mono → stereo** (free broadcast, already the `canConnect` rule): the compiler
  wires the *same* mono source buffer index into *both* of the stereo input's two
  slots. No new node, no new mechanism — just two `InputRef`s pointing at one buffer.
- **Stereo → mono**: still needs `mix.downmix`, exactly as today's rule says — see
  §9.5 for why auto-insertion now actually matters.
- **Telemetry/taps** (`outputBufferIndexByNodeAndPort`,
  `inputSourceBufferIndexByNodeAndPort`, `findTappableBufferIndex`): each currently
  maps a `(nodeId, portId)` to **one** buffer index. For a stereo port this becomes
  two consecutive indices (or a `{firstIndex, channelCount}` pair) — a real, contained
  change to those lookup tables and to `NodePreview.tsx`/the preview render path,
  which needs to decide how to *show* two channels (both overlaid, a real stereo
  meter, or — the honest MVP — channel 0 only with the gap logged, matching how this
  project has shipped partial coverage before rather than blocking on full fidelity).

### 9.4 The real cost: independent per-channel wiring gets harder, not easier

This is the trade-off worth being explicit about, not just the engineering mechanics.
`left`/`right` as two separate ports let a patch wire **genuinely different sources**
into each channel for free — a Haas-effect delay with a different time per side, a
stereo width trick, panning L and R through different filters. A single stereo cable
can't express that directly; the signal moves as one coupled unit by construction.

**This needs two new bridge nodes that don't exist yet, and wouldn't need to if the
old convention stayed:**

- `stereo.split` — one stereo `in` → `left`, `right` (two mono outputs).
- `stereo.combine` — `left`, `right` (two mono inputs) → one stereo `out`.

These aren't optional polish — without them, this redesign is a strict loss of
expressiveness for exactly the patches that most need independent per-channel
control. `mix.downmix`'s existing shape covers the "collapse to mono" half of
`stereo.split`'s job but not the "keep both channels, separately" half — a genuinely
new pair of primitives, small (each is a trivial passthrough/pairing node, Pattern-B
inline DSP, no new math), but real scope this section is naming rather than
discovering mid-implementation.

### 9.5 `mix.downmix` auto-insertion becomes worth building

Today, stereo→mono needing manual `mix.downmix` insertion is a rare edge case (almost
nothing is stereo yet). Once Audio is stereo-by-default, **any** stereo signal
reaching **any** of the many still-mono-only nodes (every filter, `delay.line`,
`shape.*` once built, etc.) hits this path — it stops being an edge case and becomes
an everyday one. `connectWithAutoAdapt`'s 1-in-1-out `AdapterStep` splice mechanism
genuinely can't express a 2-in-1-out auto-insert; this needs either a small, dedicated
extension to that mechanism (a splice shape that consumes a stereo pair and produces
one adapter node) or, more simply, changing `mix.downmix` itself to take **one**
stereo `in` (using this same twin-slot mechanism) instead of two separate mono
`left`/`right` inputs — which would make it fit the existing 1-in-1-out splice
mechanism with no compiler changes at all. **Recommended**: do the latter — redefine
`mix.downmix` as a genuinely stereo-in node under the new convention rather than
building new adapter-chain machinery for a 2-in-1-out shape that would otherwise stay
a one-off special case forever.

### 9.6 Patch migration (schema v5)

Two real nodes need their shipped ports changed: `space.pan` (drop `left`/`right`
outputs, add one stereo `out`) and `space.width` (drop `in.left`/`in.right` inputs and
`left`/`right` outputs, add one stereo `in`/`out`) — plus `mix.downmix` if §9.5's
recommendation is taken (drop `left`/`right`, add one stereo `in`). Per CLAUDE.md rule
3, none of these port IDs get silently renamed — a v4→v5 migration, following the
same pattern the v3→v4 `mix.sum` migration already established:

- If a node's old `left`/`right` (or `in.left`/`in.right`) connections both come from
  a matching, natural pair (the common case — e.g. `space.pan`'s own `left`/`right`
  outputs feeding straight into another stereo-shaped node's inputs), merge them into
  one stereo connection directly.
- If they come from genuinely different, unrelated sources (the asymmetric case §9.4
  exists for), insert a `stereo.combine` node wired from both old sources instead,
  feeding the new stereo input — preserves the old patch's actual behavior exactly,
  never silently collapsing two different signals into one.
- Symmetrically, a downstream consumer that used to read a node's separate `left`/
  `right` outputs independently gets a `stereo.split` inserted after the new stereo
  output, feeding whatever those two old connections fed — same "never silently
  change what an old patch does" discipline as every migration so far.

### 9.7 Implementation waves, once this scoping is approved to build

Not started. Proposed order, each buildable and testable independently:

1. **`stereo.split`/`stereo.combine`** — the bridge primitives, needed before anything
   else so the migration in wave 4 has something to insert.
2. **`GraphCompiler.cpp`**: honor `PortDescriptor::channels` for real — flat-slot
   allocation, mono→stereo broadcast, connection resolution for a stereo port pair.
   Provable against synthetic test node types before touching any real node (the same
   `test.audioConstant`-style approach `GrowablePortsTests.cpp` already uses).
3. **`space.pan`, `space.width`, `mix.downmix`** rewritten onto real stereo ports;
   `io.output`/`io.audioIn` gain real stereo ports; `PluginProcessor`'s 4
   mono-duplicate call sites become real 2-channel reads/writes — this is what
   actually closes `archive_docs/CLEANUP.md` P1 #6.
4. **Schema v5 migration** (§9.6), tested the same way v3→v4 was: hand-written legacy
   JSON fixtures covering the matching-pair case, the asymmetric case, and the
   already-fine (nothing stereo-shaped touched) case, each confirmed to still compile
   and sound the same.
5. **UI**: `canConnect.ts` mirrors the new engine rules; a stereo port gets a visually
   distinct socket (exact treatment — thicker cable, doubled glyph, a small
   indicator — is a design decision for whoever builds this wave, not specified here);
   `NodePreview.tsx`/telemetry taps handle a two-index port (§9.3's "channel 0 only for
   now, gap logged" MVP is an acceptable starting point, matching how this project has
   shipped honest partial coverage elsewhere).

Verification at every wave: the standard project discipline (build, `ctest`,
`pluginval` strictness 10, UI build/lint, Standalone sanity check) plus, for wave 2
specifically, a bit-exact regression proving the whole existing 55-node catalog is
byte-for-byte unaffected (every one of them stays `Mono`-channeled, so this wave
must change nothing about how they compile or sound) before any real node moves onto
`Stereo`.
