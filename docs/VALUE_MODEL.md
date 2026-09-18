# Bazalt — Value model

Status: **design draft.** This document states the intended model, not what the code does today. Claude Code reconciles it with the codebase, lists conflicts, and proposes migrations before anything is implemented.

This is the foundation everything else rests on: DSP, UI rendering, host automation, patch serialisation, and auto-conversion all read the same value description. If these systems ever describe a value in two places, they will drift.

---

## 1. One canonical description

Every number, switch, and choice anywhere in Bazalt is described by a single structure, the **value contract**. The engine owns it, it is serialised into node descriptors, and the UI consumes it. There is no second, UI-only notion of what a value is.

```
ValueContract {
  kind          float | int | bool | enum
  quantity      see §3
  min, max      optional; absent = unbounded
  default       required
  curve         linear | exponential | logarithmic | custom-ref
  polarity      unipolar | bipolar        // meaningful for normalised quantities
  enumOptions   [{ id, label }]           // required iff kind = enum
  step          optional quantisation step in canonical units
}
```

Rules:

- **Bounds are optional and their absence is meaningful.** A bounded value gets a slider with a fill showing its ratio; an unbounded one gets a drag field with no fill. The UI never guesses.
- **`default` is always present.** It is what an unconnected port feeds (§5), what double-click resets to, and what a fresh node starts with.
- **`curve` describes perception, not storage.** Values are always stored and transmitted in canonical units; the curve only maps the 0–1 gesture of a control onto the range.
- **`step`** is how integers, note divisions, and quantised parameters express themselves; `kind = int` implies `step = 1`.

## 2. Kinds

| Kind | Meaning | Notes |
|---|---|---|
| `float` | continuous value | the default case |
| `int` | whole number | dragging and typing snap to integers |
| `bool` | true/false | transmitted as a control signal, not a separate signal type |
| `enum` | one of a fixed set | options carry **stable IDs**; labels are display-only and may be renamed freely |

Enum option IDs are part of the patch format. Renaming an option's label is safe; changing its ID breaks saved patches. Reordering options must not change behaviour, so nothing may rely on option index.

## 3. Quantity: the missing first-class concept

`quantity` is the physical dimension of a value. It is what makes conversion, colouring, and formatting deterministic instead of heuristic.

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
| `Dimensionless` | raw number | plain number |

Rules:

- **One canonical unit per quantity, always.** Everything inside the engine is in canonical units. Display units (ms, dB, note names, tempo divisions) are a formatting layer in the UI and never change what flows on a cable.
- `Unipolar` and `Bipolar` are the "modulation" quantities. Everything else is a real-world value. This distinction, not the signal type, is what the UI colours (orange for normalised, white for real units).
- Quantity drives the **default seeding of conversions**: dropping a `Unipolar` signal into a `Frequency` port creates a Map node already scaled to that port's min and max.
- New quantities can be added later. Removing or redefining one cannot.

## 4. Every value lives on a port

There is no separate "parameter" concept for ordinary values. A node declares **ports**; a port carries a value contract; a port that has no cable attached uses its **inline default**, editable directly on the node.

This is decided deliberately against the alternative (parameters and ports as separate lists), because the fork forces every "connectable and automatable" value to be special-cased in node DSP code.

**Implementation requirement:** the inline default must be resolved by the **graph compiler**, not by each node. When a port is unconnected, the compiler supplies a constant buffer (or a scalar slot) holding the inline value. Node DSP code therefore sees exactly one case: a connected input. No sentinels, no per-node branching, no "is this connected" check inside `process()`.

## 5. Structural parameters: the one exception

A small, clearly delimited class of settings is **not** a port:

A setting is structural if, and only if, changing it requires reallocation, a change of graph topology, or a new `prepare()` — for example table size, oversampling factor, maximum mode count, algorithm variant that swaps the processing kernel.

Structural parameters:

- cannot be modulated and have no port,
- may cause an audible discontinuity when changed, and the UI says so,
- are still described by a value contract (usually `enum` or `Count`), so the UI renders them with the same controls,
- are **not** bindable to the host macro pool by default (see §6).

If a setting does not meet the definition above, it is a port. This test is deliberately strict: "it feels like a parameter" is not a reason.

## 6. Host automation

The DAW needs a fixed parameter list; a modular graph does not have one. The bridge is the **Macro node**: a node whose job is to hold a value, bind into the fixed pool of host-automation slots, and expose that value as an output port.

- Want a node input automatable from the DAW? Wire a Macro into it. The target node's code is untouched.
- A Macro is a Constant that also binds to a host slot; the two node types share their implementation.
- A Macro's own value contract is user-editable (kind, quantity, range, curve, enum options). Changing it after the host has recorded automation changes the meaning of that automation; the UI must warn.
- Macro slots are identified by **slot index in the patch**, and a slot's identity must survive node renaming and repositioning.
- Structural parameters are not bindable, because host automation implies continuous, glitch-free change.

Open question for reconciliation: today's pool binds directly to per-node parameter descriptors. Moving to Macro-only binding is cleaner but is a breaking change for existing patches; Claude Code should propose whether to support both during a transition and how to migrate.

## 7. Formatting and editing

Derived entirely from the value contract, never hardcoded per node:

- number of decimals and unit suffix from quantity and range,
- slider versus drag field from the presence of bounds,
- dropdown from `kind = enum`, toggle from `kind = bool`,
- integer snapping from `kind = int` or `step`,
- gesture-to-value mapping from `curve`,
- reset target from `default`.

Display-unit preferences (ms versus samples, Hz versus note names) are a per-port UI preference stored in the patch, never a change to the transmitted value.

## 8. Stability rules

Because IDs are foreign keys in saved patches:

| Change | Allowed after shipping? |
|---|---|
| Rename a label, title, or enum option label | yes |
| Add a new quantity, kind, or enum option | yes |
| Widen a range | yes, with care (old values stay valid) |
| Narrow a range, change a default, change a curve | only with a migration |
| Rename a port ID, node type ID, or enum option ID | no |
| Change a port's quantity or kind | no; add a new port instead |
| Move a value from port to structural parameter or back | only with a migration |

Everything in `core.*` follows this table. A `lab.*` namespace exists for experimental node types where no stability is promised and patches may break; nothing in `lab.*` ships in a factory preset.

## 9. Open questions

1. Is `Pitch` in semitones the right canonical unit versus Hz, given that both pitch-based and frequency-based nodes exist and conversion between them is not linear?
2. Should `curve` support custom curves as data (a `Data`-typed reference) from day one, or only named curves initially?
3. Do bounded ports need a soft range (UI drag range) distinct from a hard range (engine clamp), as many synths have? Recommendation: yes, add `softMin`/`softMax` now, because adding them later changes how every existing slider behaves.
4. How is a value contract that is itself dynamic handled — e.g. a Map node whose output quantity depends on what it is connected to? Proposal: ports may declare a quantity as `inherited`, resolved at compile time from the connection, and the UI shows the resolved value.
