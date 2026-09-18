# Bazalt — Signal types, ports and connection rules

Status: **design draft.** Companion to `VALUE_MODEL.md`. States the intended model; Claude Code reconciles it with the code, lists conflicts, and proposes migrations.

---

## 1. Principle

Keep the number of **signal types** small and push the richness into **metadata**. A signal type answers "what shape of data flows here"; the value contract answers "what does this number mean". Colours, controls, and conversions are derived from the value contract, not from a proliferation of types.

## 2. The types

| Type | What flows | Rate | Notes |
|---|---|---|---|
| `Audio` | one sample per frame, audio range | audio rate | the only type that is always audio rate |
| `Control` | a numeric value (see value contract) | block or audio rate | carries floats, ints, bools and enums alike |
| `Event` | discrete occurrences with a sample offset | sample-accurate | what the UI calls **Trigger** |
| `Note` | note lifecycle events with a payload | sample-accurate | id, pitch, velocity, pressure, slide, release velocity |
| `Data` | an immutable block of values | updated between blocks | tables, modal sets, scales, curves, impulse responses |
| `Spectral` | reserved | — | no meaning assigned yet; do not use |

Deliberate consequences:

- **Boolean and Integer are not signal types.** They are `Control` with `kind = bool` / `int`. The UI still colours and glyphs them distinctly, because the UI reads the value contract.
- **Modulation is not a signal type.** It is `Control` with quantity `Unipolar` or `Bipolar`.
- **Trigger is Event.** One type, two names; the UI name is "Trigger". Pick one name for the codebase and use it everywhere.
- **Note is a real type**, not a convention. MIDI must enter the graph through a Note-typed port instead of being wired directly into voice allocation, otherwise arpeggiators, chord generators and scale quantisers cannot exist as ordinary nodes.
- **Data is first class from day one.** Physical modelling (modal sets, material tables), scale quantisation, wavetables, and curve editors all need arrays to flow between nodes. Retro-fitting this later would touch every node.

### Data type details

- A `Data` value is an **immutable, reference-counted buffer** with a small header (element type, length, semantic tag such as `modal-set`, `scale`, `wavetable`, `curve`, `ir`).
- Producers build new buffers on a worker thread and publish them; the audio thread only swaps a pointer. **Never allocated or mutated on the audio thread.**
- Consumers declare which semantic tags they accept. A Modal Bank accepts `modal-set`; wiring a `scale` into it is rejected at compile time.

### Event and Note details

- Events carry a sample offset within the block and an optional payload (`Control` value).
- Note events are `note-on`, `note-off`, and `note-update` (continuous per-note expression), each carrying a note ID that ties them together. Pitch is a continuous `Pitch` value, so bends and MPE need no special path.
- Event ordering within a block is deterministic and stable across runs.

## 3. What a port declares

```
Port {
  id             stable string
  direction      in | out
  signalType     Audio | Control | Event | Note | Data | Spectral
  value          ValueContract          // required for Control, optional payload for Event
  rate           block | audio | any    // Control only
  domain         any | polyOnly | monoOnly
  dataTags       [..]                   // Data only
  group          optional; see §6
  label          display only
}
```

`rate` matters because "modulate as much as possible" means many ports must accept audio-rate signals, but not all can. It must be declared, not discovered.

`domain` is defined in `DOMAINS.md`; most ports are `any`.

## 4. Who decides whether two ports can connect

**The engine is the authority, and there is exactly one implementation.**

```
canConnect(from: Port, to: Port) -> Ok | NeedsAdapters(chain) | Reject(reason)
```

- It is a pure function of two port descriptors, in C++, with no engine state.
- Its rules are **generated** into the TypeScript mirror by the same mechanism that already exports node metadata. The UI predicts with identical rules; it does not reimplement them.
- The UI's answer is a prediction for live feedback. The engine validates every connection at compile time, so a hand-edited or scripted patch cannot produce an invalid graph. A rejected command surfaces as an error banner.
- If UI and engine ever disagree, the engine wins and the discrepancy is a bug, not a fallback path.

## 5. Adapters: conversions are visible nodes

When `canConnect` returns `NeedsAdapters`, the UI inserts **real nodes** into the graph as a single undoable command. Conversion is never hidden coercion inside a cable: the user can see it, tune it, and delete it.

Intended adapter table (Claude Code should turn this into the full matrix and the test suite):

| From | To | Adapter | Seeding |
|---|---|---|---|
| Control `Unipolar`/`Bipolar` | Control with real quantity | `Map` | min/max from the target port's range and curve |
| Control real quantity | Control `Unipolar` | `Normalise` | from the source port's range |
| Control | Event | `Threshold` (trigger by threshold) | rising edge at 50 % of range |
| Event | Control | `Envelope` or `Latch` | short decay by default |
| Audio | Control | `Envelope follower` | musical default times |
| Control (block) | Control (audio) | none needed | broadcast |
| Control (audio) | Control (block) | `Sample & hold` at block rate | implicit, warn on aliasing |
| Note | Event | `Note gate` | note-on as trigger |
| Note | Control | via Voice Allocator or `Note value` | see `DOMAINS.md` |
| Audio | Audio across domains | `Voice mix` (poly→mono) | see `DOMAINS.md` |
| anything | `Data` | rejected | no implicit construction |

Rules:

- Adapter insertion is **deterministic**: the same pair always produces the same chain with the same seeded values.
- A chain is at most **two** adapters. If a conversion would need more, it is rejected; the user builds it explicitly.
- Auto-inserted nodes are marked as such in the UI (a badge) until the user edits them, so the graph stays honest about what was added for them.
- `Data` is never converted implicitly, and `Spectral` connects to nothing until it is defined.

## 6. Growable ports

A node that conceptually takes N inputs (Mix, Add, Sum of modal excitations) declares a **port group**:

```
PortGroup { idPrefix, min, max, growPolicy }
```

Port IDs within a group are `prefix.0`, `prefix.1`, … and are stable: removing a middle port does not renumber the others, it leaves a hole that serialisation preserves. The UI reveals a fresh empty port once the last one is wired, never falling below `min` or above `max`. This is core from day one because real node families need it immediately.

## 7. How the UI derives its visual language

Nothing in the UI hardcodes a node or a colour rule per type name:

| Visual | Derived from |
|---|---|
| pink / magenta cable | `signalType = Audio` |
| orange | `Control`, quantity `Unipolar` or `Bipolar` |
| white | `Control`, real-world quantity |
| yellow | `Control`, `kind = int` |
| blue, `?` glyph | `Control`, `kind = bool` |
| violet, `!` glyph | `Event` |
| (to be designed) | `Note`, `Data` |
| line style | domain (see `DOMAINS.md`) |

Two visual decisions are still open and should be resolved before implementation: the appearance of `Note` and `Data` cables, and how poly versus mono is encoded without colliding with the palette above.

## 8. Stability rules

- Signal types are the hardest thing to change: adding one is safe, removing or redefining one is not. The list in §2 should be treated as closed except for `Spectral`.
- Adapter behaviour is part of the contract: changing which adapter a pair produces changes how future patches are built, but must never rewrite existing patches.
- Port IDs, including group prefixes, are permanent.

## 9. Open questions

1. Should `Note` carry only per-note expression, or also channel-level data (mod wheel, pedals)? Proposal: channel-level data is `Control` in the mono domain, not part of `Note`.
2. Does `Event` need a typed payload (e.g. an event that carries a `Frequency`), or is payload always a plain number? Physical-modelling excitation suggests typed payloads are worth it.
3. Should `Control` audio-rate versus block-rate be a port declaration, or inferred by the compiler from what is connected? Inference is friendlier but makes CPU cost invisible to the user.
4. Is a `Data` port allowed to change its buffer while audio runs (a live curve edit), and if so, what is the crossfade or ramp policy?
5. Does the UI need an explicit "domain conversion" cable style, or is the Voice Mix node enough?
