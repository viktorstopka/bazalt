# Classes and Entities

**Status:** Agreed with the user 2026-10-17, being built. This file is the R1 design that
`wiki/plans/Release.md` asks for before code; it replaces LifeCycle.md's Population
(2b) as the user-facing model.

---

## 1. Why

A Population is one node doing two jobs: it decides **when** something is born (Born,
Lives, Size, When full) and it **is** each member (gate, pitch, velocity, index, order,
age, alive, random 1 / 2, position). The member half only fits notes — a grain wants a
position in a sample and a pan, a particle a position in space, a cricket a chirp rate —
so everything else rode on Random 1 / Random 2 and a Map. And because a Population is fed
by plain value cables, two Populations meeting through a value cable nested by accident.

Classes split the two jobs and name the members' properties:

- a **Class** says what a member *is*: its props, each with a type and how a new member
  gets its value;
- an **Entity** is one member;
- a **Spawn** makes entities (the *when*);
- a **Foreach** runs the patch after it once per entity (the *is*).

## 2. The user's model

```
 Note In ──entities of Note──▶ Foreach ─ pitch ─▶ Pitch→Freq ─▶ Saw ─▶ … ─▶ Merge
                                      ─ velocity ▶ …
 Events ─▶ Spawn (Grain) ──entities of Grain──▶ Foreach ─ position ▶ Sampler
```

- **The cable between them is "Entities of <Class>"** (today's green Note cable, now
  labelled with the class). It is the only way entities travel: a Foreach can only be
  fed by entities, and **nesting is a Spawn inside a Foreach** (its inputs come from the
  outer entities), never two value cables meeting.
- **A MIDI note is an entity of the built-in Note class.** Note In outputs entities of
  Note; Note In → Foreach is a keyboard voice, and nothing else is needed. Note In owns
  the polyphony (Voices, When full) since it is where notes come from.
- **Foreach** outputs the class's props by name, plus the life signals every entity has:
  Gate, Age, Index, Order (the how-manyeth birth), Alive (how many now), Start, Stop.
  Random 1, Random 2 and Position are gone: a random prop does that, named.
- **Spawn** makes entities: Born on an event (a rate is an Events node), on a note (Notes
  in — each note-on births one), or Always; Lives for its Duration / until Release /
  forever; Size and When full. Its class lives in it.
- **Split** (by a prop or any key) and **Merge** stay as they are.

## 3. The class

A class is a JSON document — node `content`, edited in the Class factory, saved with the
patch (`{"schema": "class", "version": 1, "name": ..., "props": [...]}`). Up to 16 props.
Each prop:

| field | meaning |
|---|---|
| `id` | stable, ASCII (`position`, `chirpRate`); the Foreach output's port id |
| `name` | shown on the port |
| `kind` | `value` (with `quantity`, `min`, `max`, `unit`), `pitch`, `bool`, `choice` (`options`), `vector3` |
| `source` | `fixed` (`value`), `random` (`min`, `max`, `distribution`: uniform / normal; a bool's `probability`), `input` (read from a Spawn input at the birth), `note` (Note class only: pitch / velocity from the note) |
| `variation` | 0..1 (random only): 0 every entity the same draw, 1 each its own |

- **Every prop draws from its own stream**, seeded by (Seed, prop id, birth order): adding
  a prop never changes the others, and the same patch replays the same entities.
- **A `vector3` prop is three outputs** (`id.x`, `id.y`, `id.z`) for now. Release.md said
  "on the channel axis (three channels)"; the channel axis only knows mono and stereo
  today, and growing it is R2's job when the particle view needs it (§8).
- **An `input` prop** gives the Spawn an input port named after it, read at the sample
  of each birth (a Spawn inside a voice reads the voice's own signal).
- **Built-in classes** (the factory's starting points): **Note** (Pitch, Velocity — from
  the note), **Grain** (Position random 0..1, Pan random −1..1, Length fixed 0.08 s),
  **Particle** (Position and Velocity vector3 random −1..1, Size random 0.5..1),
  **Entity** (no props).

The Note class is extendable: Note In's class can gain props (a random Detune per voice)
next to Pitch and Velocity, which stay.

## 4. Engine: lowering onto the instance axis

The instance axis (InstanceAxis.md, LifeCycle.md 2b) already does all the hard parts —
member slots, births at the exact sample, steal fades, nested slices, Split's skipping,
reductions. Classes change what the user wires, not that machinery, so the editable graph
is **lowered** before it is resolved and expanded (`EntityLowering.h`):

- Each **Foreach** becomes the group's origin, a `life.foreach` node carrying everything
  the old Population carried, filled in from its source:
  - fed by a **Spawn**: the Spawn's settings (Size, Born, Lives, When full, Seed) and
    class are copied onto the Foreach, and the Spawn's input cables are re-pointed at
    the Foreach's hidden birth inputs (`born`, `release`, `notes`, `life.foreach.duration`,
    `in.<prop>`). The Spawn itself disappears from the compiled graph.
  - fed by **Note In** (directly, or through note filters like Transpose): Voices and When
    full come from Note In, Lives is While held, the class is Note In's; the notes cable
    stays — the existing MIDI path (each member hears its own note-in copy).
  - fed by nothing: Size 1, no births — silent, never an error (so a Foreach can be placed
    before it is wired).
- From there the resolver, the expansion (the birth copy for internal births, the slices
  for nesting) and the plugin's driver run as they did for Population, with `life.foreach`
  in its place. The id of the origin is the Foreach's id, so previews, badges and
  multiplicity keep working.
- **Prop values** are drawn by the member at its start (`EntityClass::draw`), from the
  class, Seed and order; `input` props and the note's pitch / velocity arrive with the
  birth (`BirthValues`, through the birth sink — also kept across a steal's fade).
- **Ports from content.** A Foreach's outputs follow its class and a Spawn's inputs follow
  its own; after each compile the controller reports those nodes' real ports
  (`graphGetNodeMultiplicity`'s `resolvedPorts`) and the editor draws them instead of the
  type's default ones. A changed class is structural (a fresh node).
- `SignalType::Note` is renamed **Entities** (the payload stays `NoteEvent` for the Note
  class; the other classes never put entities on a per-sample buffer — births travel
  through the sink).

## 5. UI

- **Class factory** (Factory window, opened from Spawn or Note In): the props as rows —
  name, kind, range, source, variation — add / remove / reorder; a starting-point menu
  (Note, Grain, Particle, Entity); a preview of 20 entities drawn by the engine
  (`classPreview`), so what you see is what plays.
- The entities cable is labelled with its class name; Foreach shows the class name and
  its live count (the old badge).
- Add-menu presets: **Voice** (Note In → Foreach), **Grains** (Events → Spawn (Grain) →
  Foreach), **Swarm** (Spawn Always, Size 16 → Foreach).

## 6. Done means (Release.md R1.5)

- A keyboard voice is Note In → Foreach.
- Granular keys (10 grains per key, each its own position): Note In → Foreach → Spawn
  (Grain, born by Events inside the voice) → Foreach → Sampler.
- 50 crickets from a Cricket class with their own chirp rates and pitches, born once.
- All of it saves and loads; the Population tests are rewritten on the new model; seam
  scenarios place, wire and play a Voice and Grains.

## 7. Removed

`life.population` (no migration — compatibility is off until R0.6), its Random 1 /
Random 2 / Position outputs, the Population presets, the accidental "a value cable from
one Population nests the other" rule.

## 8. Later

- **Markov** prop source (a value that follows the previous entity's by a transition
  table — birdsong syntax) and a standalone Markov node for sequences (R1.3's last part).
- **Drawn-curve and weighted-list distributions** (the same editor as Events' intervals).
- **Vector 3D on the channel axis** (three-channel signals) — with R2's particle view.
- Pressure and channel on the Note class, once MIDI MPE is read.
- Saving a class to the library (with R7's browser).
