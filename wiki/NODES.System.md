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

All five real types (`Audio`, `Control`, `Event`, `Note`, `Data`) are implemented in
`engine/src/graph/CanConnect.cpp` / mirrored in `ui/src/graph/canConnect.ts`. `Data` got
its first two real producers and its first real consumer in the Data Foundations batch
(`data.scale`/`data.table` produce, `data.lookup` reads) — before that, only
`canConnect`'s tag-matching rules were real; the actual publish/swap runtime
(`DataPublisher`, `Data.h`) had never been exercised by a live node. `GraphCompiler.cpp`
wires a `Data` connection once, at compile time, via `Node::getDataPublisher()`/
`setDataInput()` — a much lighter mechanism than `Note`'s own per-block
`produceNoteBlock()`/`consumeNoteBlock()`, since a `Data` buffer changes only on a
discrete edit, never mid-block; the consumer just holds the raw `DataPublisher*` and
reads `getCurrentForAudioThread()` for itself whenever it likes. `Note`
has exactly two real ports anywhere in the engine right now: `io.noteIn`'s `notes`
output and `instance.allocate.voice`'s `spawn` input (plus polymorphic `util.reroute`,
which can carry a Note cable through unchanged). A connected Note port doesn't use the
ordinary per-sample `blockBuffers` mechanism — it gets a dedicated `NoteEvent`-typed
buffer (`ExecutionPlan::noteBuffers`), and **only one Note input and one Note output
per node is supported today** (a Note OUTPUT can still fan out to several destinations
freely — this limit is about how many Note-typed ports one node can declare, not about
connection multiplicity). The Note Stream batch was the first real work to actually hit
this: the catalog's own `note.filter` wants two Note outputs (`pass`/`reject`), and
`note.chord`/`note.hold`/`note.select` all assume a multi-note signal one `Note` cable
can't carry — `note.filter` got a clean one-output redesign, the other three were
deferred outright rather than forced through the wall (`wiki/NODES.md`'s own `note.*`
section and `wiki/MILESTONES.md`'s Note Stream batch entry have the full reasoning).

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
  — nothing may rely on option index. (This rule had exactly one real violation:
  `instance.allocator.configuration`'s index genuinely was its stored value — the
  parameter this caveat used to cite. 09-28-InstanceAllocator.3 removed that parameter
  outright rather than fixing the violation, so as of that milestone the rule has no
  known exceptions again.)

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
| `Audio` (mono) | `Control` (`Unipolar`/`Bipolar`/`Dimensionless`) | NeedsAdapters | `adapt.audioToControl` — reads the waveform's instantaneous value, scaled by `depth` | ✅ (`wiki/plans/AudioControlBridge.md`) |
| `Audio` (mono) | `Control` (real quantity, e.g. `Frequency`) | NeedsAdapters | `adapt.audioToControl` → `adapt.map` (two-adapter chain, `adapt.map` seeded from the destination's range) | ✅ |
| `Audio` (stereo) | `Control` | Reject | "Stereo source into a Control-typed port needs mix.downmix first" — a 3-adapter chain (downmix + bridge + map) would exceed the two-adapter ceiling, so this stays manual | ✅ (deliberate v1 scope limit, not a gap) |
| `Audio` | `Control` (amplitude-tracking, not raw waveform) | — | `env.follower` (rectify + independent attack/release smoothing) is a DIFFERENT job from the row above — "how loud is this, smoothed" vs. "use the instantaneous waveform as a modulator" — and stays hand-placed only, on purpose: auto-inserting it on a bare wire-drag would silently defeat audio-rate FM/ring-mod, which needs the raw value `adapt.audioToControl` preserves. `archive_docs/decisions/0019-adapter-table.md`'s Amendment (0.x arc) has the full reasoning for why this revises the ADR's original M20 plan rather than fulfilling it. | ❌ not auto-inserted, deliberately — `env.follower` itself is real (✅), just never auto-spliced |
| `Note` | `Event` | — | catalog names a `note.gate`-style "Note gate" adapter | ❌ `note.gate` itself isn't built yet (M25) |
| `Note` | `Control` | — | catalog names via `instance.allocate.voice`'s own outputs, or `note.value` | ❌ `note.value` isn't built yet (M25); the instance.allocate.voice path is real but isn't a `canConnect` adapter — it's just wiring its own output ports |
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

### `instance.allocate.voice` — the region-opening node (renamed from `instance.allocator`, 09-28-InstanceAllocator.3; renamed again from `instance.voice`, 09-29-AddMenu.1)

Originally designed as "one node, several configurations" (Voice allocation and
"swarm" spawning treated as the same runtime machinery with a different event
source, switched by a `configuration` enum) — that design shipped with a real,
found-live cost: three of the four configurations (Swarm-population/Swarm-transient/
Trigger) never did anything behaviorally, so the dropdown was pure UI clutter, and
the four configurations don't even share a port shape (Voice needs a `Note` `spawn`
input; Swarm-population needs none at all) — a real structural mismatch for one node
with a mode switch, unlike e.g. `mix.downmix`'s legitimate same-shape mode enum.
`wiki/reports/InstanceAllocator_2026-09-28.md` has the full critique.

**Current design, as of 09-28-InstanceAllocator.3:** `instance.allocate.voice` does exactly the
one thing it ever actually did — Voice allocation, spawned from a `Note` stream. The
`configuration` parameter is gone outright, not defaulted. Swarm-population/
Swarm-transient/Trigger are still real, wanted future capability, but as **separate
node types** sharing the same underlying runtime machinery (instance context,
lifetime, events-across-boundary — everything below this point still applies to all
of them equally), each built as its own milestone once its real spawn/lifecycle
behavior actually exists — not as empty shells on `instance.allocate.voice` now, which would
just recreate the same dead-surface problem this rename fixed.

**Namespace note (09-29-AddMenu.1):** the type id itself carries an `allocate` segment
(`instance.allocate.voice`, not `instance.voice`) specifically so the Add menu's
category tree — derived from `getCategory()`'s own `/`-separated path, §4-equivalent
mechanism, see `wiki/MILESTONES.md`'s `09-29-AddMenu.1` entry — can nest all the
spawn-mechanism siblings (Voice, and eventually Swarm-population/Swarm-transient/
Trigger below) under one "Domain > Allocate" flyout instead of leaving them
indistinguishable from `instance.mix` under a flat "Domain" list. `instance.mix`
deliberately keeps its plain two-segment id and flat "Domain" category — it isn't one
of the spawn-mechanism siblings, it's the region-closing node, so it doesn't belong in
the same subcategory.

| Future node type | Spawn source | Instance context | Typical use | Built? |
|---|---|---|---|---|
| `instance.allocate.voice` | a `Note` stream | the note | playing an instrument | ✅ |
| `instance.allocate.swarmPopulation` | fixed count, always live | index + seeded randoms | cicadas, a drone of many bodies | 📋 M28 |
| `instance.allocate.swarmTransient` | an `Event` stream | seeded randoms per spawn | bubbles, crackles, sparks, raindrops | 📋 M28 |
| `instance.allocate.trigger` | an `Event` stream, one instance at a time | payload | percussive one-shots | 📋 M28 |

### Instance context (instance.allocate.voice's output ports)

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

## 9. Stereo audio: one real cable

**Status: done, built.** A stereo signal is one real Audio cable now — `space.pan`
outputs it, `io.output` takes it, and every stereo-shaped node in the catalog
declares exactly one `Channels::Stereo` port per side instead of a `left`/`right`
port pair. This replaces two earlier, narrower passes on the same problem: an initial
scoping-only pass (never built) and a point-fix (Master Out gained a second channel,
but every other node still used separate `left`/`right` ports) — both superseded, not
layered underneath this.

### 9.1 The mechanism: flat channel slots, not a new buffer type

`PortDescriptor::channels` (`Mono | Stereo | Inherited`, M16) already existed and
`canConnect` (`CanConnect.cpp`) already had the right rule — mono→mono free,
mono→stereo free (broadcast), stereo→stereo free, stereo→mono needs `mix.downmix`.
What was missing was the compiler actually backing that rule with real per-channel
buffers instead of always allocating exactly one per declared port. `GraphCompiler.cpp`
now does:

- Every node's input/output descriptor list gets a **flat channel layout**: each
  descriptor occupies 1 buffer slot (Mono, or any non-Audio type) or 2 consecutive
  slots (a `Channels::Stereo` Audio port) — computed once per compile
  (`channelCountOf`/`computeFlatStarts`), never stored on the node itself.
- Buffer/scalar allocation (both the ordinary block-rate path and the per-sample
  feedback-region path) iterates by **flat slot**, not by descriptor index — a stereo
  port gets two real buffers, not one.
- Connection resolution binds flat slots: stereo→stereo pairs them 0↔0, 1↔1; mono→stereo
  broadcasts the same single source slot into both destination slots (this is what makes
  the free-broadcast rule real, not just permitted); stereo→mono never reaches the
  compiler (rejected by `canConnect` first).
- `ExecutionPlan::finalOutputBufferIndexRight` (already existed from the earlier
  point-fix) is now set **unconditionally** whenever the graph's designated output port
  has 2 flat channels — no "is the second channel actually wired" guard needed, because
  by construction every flat slot is always allocated and, for an unwired stereo
  destination fed by a mono source, the broadcast rule already guarantees a real
  (duplicated) value lives there. The old guard (`rightInputIsWired`) existed only
  because the point-fix's `io.output` still had two *separate* ports; it's gone now.

**`Node::processSample`/`processBlock`'s signatures did not change.** A node's flat
`inputs[]`/`outputs[]` arrays are addressed by flat position exactly as before — a
node with one stereo output just reads/writes two adjacent array slots for it, same as
`space.pan` already did internally since M22 (its `left`/`right` gain math always
produced `outputs[0]`/`outputs[1]`; only how many *port descriptors* wrapped that array
position changed). Every one of the ~50 other node files needed zero changes.

**One real gap this surfaced and had to be fixed**: `Node.h`'s default `processBlock()`
(the one every node not overriding it inherits) sized its per-sample scratch loop from
`getNumInputPorts()`/`getNumOutputPorts()` — descriptor counts, not flat channel
counts. A node with one Stereo output but no override would only ever have its first
(left) channel written; the second output buffer would sit at whatever
uninitialized/stale value it started at. Fixed by adding
`Node::getNumInputChannels()`/`getNumOutputChannels()` (defaulting to the port-count
methods — a no-op for every node that never overrides them) and switching the default
`processBlock()` to use those instead. Every node that declares a `Channels::Stereo`
port must override these two with the real flat count (a plain hardcoded constant,
same idiom as this codebase's existing `numInputs`/`numOutputs` pattern) — caught by a
real, reproduced bug during implementation (a synthetic test's right channel read back
`-431602080.0f`, the classic uninitialized-debug-memory pattern), not discovered by
inspection.

### 9.2 The six real nodes

| Node | Shape now |
|---|---|
| `space.pan` (`PanNode.h`) | 1 output `out` (Stereo, primary) |
| `space.width` (`WidthNode.h`) | 1 input `in` (Stereo); 1 output `out` (Stereo) |
| `io.output` (`OutputNode.h`) | 1 input `in` (Stereo); 1 output `out` (Stereo) |
| `mix.downmix` (`DownmixNode.h`) | 1 input `in` (Stereo); 1 mono output `out` |
| `stereo.split` | 1 input `in` (Stereo) → 2 separate mono outputs `left`/`right` |
| `stereo.combine` | 2 separate mono inputs `left`/`right` → 1 output `out` (Stereo) |

`mix.downmix` becoming a genuine 1-in-1-out node closes a real, previously-logged gap:
it now fits `connectWithAutoAdapt`'s single-`AdapterStep` splice mechanism the same way
`adapt.map`/`adapt.normalise`/`adapt.threshold` already did, so wiring a stereo source
into a mono-only port auto-inserts a real `mix.downmix` node instead of being rejected
outright (`GraphEditController::connectWithAutoAdapt`, the hardcoded `mix.downmix`
refusal removed).

`stereo.split`/`stereo.combine` are the bridge nodes independent per-channel wiring
still needs — you can feed a stereo signal's two channels to genuinely different
downstream processing (`stereo.split`), or bundle two unrelated mono sources into one
stereo cable for a destination that expects one (`stereo.combine`). Both are trivial,
byte-for-byte passthroughs (`tests/SpaceNodesTests.cpp`).

### 9.3 No patch migration — CLAUDE.md rule 3 is suspended

Every one of the six nodes above had a shipped port id renamed or removed
(`left`/`right` → `out`, `in.left`/`in.right` → `in`). Under CLAUDE.md rule 3 ("port
ids never renamed once shipped") this would normally demand a real v4→v5 migration —
inserting `stereo.combine`/`stereo.split` bridge nodes for any old patch's asymmetric
`left`/`right` wiring, exactly like the v3→v4 migration already did for `mix.sum`'s
removed `level.N` ports.

**That migration was not written.** Rule 3 is suspended for now, on the user's own
explicit instruction (CLAUDE.md rule 3's own note has the full reasoning and the
re-enable trigger to watch for): nothing real depends on the pre-redesign port shape
yet. `PatchDocument::currentSchemaVersion` still bumped to 5 for hygiene, with a
trivial version-only `migrateV4ToV5` (no data rewriting) so an old v1–v4 patch still
*parses* successfully through the existing migration chain — it just fails later, at
`GraphCompiler::compile()` time, with a clear "no such port" error, if it actually
referenced one of the six nodes' old ids. `PatchSerializer.cpp::migrateV3ToV4` remains
the template to copy if a real migration is ever needed once rule 3's suspension ends.

### 9.4 Deliberately out of scope

- **Preview/tap lookups read channel 0 only.** `outputBufferIndexByNodeAndPort`/
  `inputSourceBufferIndexByNodeAndPort` (tap/telemetry infrastructure) record a stereo
  port's *first* flat channel only — a real stereo-aware scope/meter is genuine future
  work, not silently promised. Bypass (`BlockStep::bypassed`) behaves the same way: a
  bypassed stereo node's primary output only copies its left channel through.
- **No UI changes were needed.** `channels` was already read nowhere in `ui/src` except
  `canConnect.ts` (already correct); cable rendering draws by port screen position
  regardless of channel count. A visually distinct stereo cable (thicker line, doubled
  glyph, a small indicator) is optional polish, not built.
- **The mono-only render path is untouched.** A graph with no `instance.allocate.voice`
  (`DomainSplitter::monoOnly`, `PluginProcessor::renderMonoRange`) still only ever
  tracks one mono buffer.
- **Per-sample feedback regions** got the same flat-slot generalization for
  structural correctness, but no per-sample-region-capable node (delay/onepole/mix)
  is ever Stereo today, so this path is unexercised by any real node — proven only
  against synthetic types (`tests/GraphCompilerTests.cpp`).

### 9.5 Verification

New synthetic-node tests in `tests/GraphCompilerTests.cpp` prove the flat-slot
mechanism directly (mono→mono, mono→stereo broadcast, stereo→stereo pairing, a
maxPortsPerNode rejection that counts flat channels not descriptors) before any real
node was touched, per this project's standard practice for cross-cutting compiler
changes. Real-node coverage: the Init Patch (`ProofGraphs.h::buildInitPatchGraph()`)
now wires `pan.out` → `masterOut.in` as one cable;
`tests-plugin/HostInputTests.cpp` has both a hard-panned "left and right genuinely
differ" test and a "mono source still duplicates to both channels" test;
`tests/SpaceNodesTests.cpp` covers the two bridge nodes' transparency;
`tests-plugin/ConnectWithAutoAdaptTests.cpp` covers `mix.downmix` auto-insertion
end to end. A real mutation-testing pass on the mono→stereo broadcast logic (force it
to bind pairwise instead of broadcasting) crashed on a debug assertion rather than
silently passing, confirming it's load-bearing. 365/365 engine+plugin tests green,
`pluginval --strictness-level 10` SUCCESS, UI `npm run build`/`npm run lint` clean
with zero `ui/src` changes.
