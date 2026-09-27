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
| `Audio` (stereo) | `Audio` (mono) | NeedsAdapters | `mix.downmix` (currently **manual only** — the UI predicts the need but doesn't auto-insert it yet) | ⚠️ partial |
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
| *(open)* | `Note`, `Data` — no colour assigned yet; the "same colour won't connect" bug report against Note ports (`NODES_Gaps.md`) is not actually a colour problem, since Note has no distinct colour to begin with — the real cause is under investigation there |
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
