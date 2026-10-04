# Baked Physics — simulations recorded as instructions, not audio

**Status:** Proposed, 2026-10-04. Vision and staging only — not scheduled, no code.
Depends on `Factories.md` (a physics scene is a factory) and builds directly on the
PM Core nodes that already exist.

---

## 0. Origin

Direct idea, 2026-10-04: "bake a rigidbody simulation into samples, that instead of
audio contain information (some curves, ...) on how audio should be created out of
these." And, as a distant-future extension: resonance and excitation from actual
geometry — "place a .obj file to the resonator node".

## 1. The idea in one paragraph

A sample freezes a *sound*. A baked simulation freezes a *performance* — when
things collided, how hard, where, which body, how long they scraped or rolled — and
leaves the sound to be made live by Bazalt's excitation and resonator nodes. The
same recording can then be replayed as glass, as wood, as a larger object, in a
different room, at a different speed, or with a different seed for variation — none
of which a sampled clatter can do. It is "physically informed sound synthesis", a
research field (rigid-body sound for animation) that no instrument offers as a
musical tool. It fits `CLAUDE.md`'s own direction exactly: drop the analog/
recording habit of capturing results, and capture *causes* instead.

## 2. What already exists

| Need | Already there |
|---|---|
| Something to excite | `excite.impulse`, `excite.mallet`, `excite.pluck`, `excite.burst` |
| Something that rings | `resonator.modal` (a real mode bank, stereo, `position` pickup), `resonator.string`, `.plate`, `.comb` |
| A body's modes from physical parameters | `data.material` → `Data(modal-set)` (geometry, stiffness, density, damping, seed) |
| Many short-lived sounding instances | `instance.allocate.swarmTransient` / `trigger` — one instance per impact |
| Shipping immutable data to the audio thread | the `Data` pipeline (`DataBuffer`/`DataPublisher`) |
| Deterministic randomness | `(seed, ordinal)` determinism from `09-28-InstanceAllocator.2` |

So the playback half is mostly built. What's missing is the **event-track data
type**, a **player**, and the **simulation** itself.

## 3. The data: `Data(event-track)`

A new `DataTag::EventTrack` — a time-sorted list of contact records, plus optional
continuous curves:

```
impact   { time, bodyId, otherId, impulse (N·s), relativeSpeed, contactPoint (body-local xyz), normal }
contact  { bodyId, start, end, curves: normalForce(t), slipSpeed(t), contactPoint(t) }   // scraping, rolling, sliding
body     { id, materialRef, size, massShape }                                           // the cast list
```

- Times in seconds from the start of the take; playback can stretch them.
- `bodyId` lets each body keep its own material / resonator settings.
- `contactPoint` matters later (§6): with geometry-derived mode shapes it selects
  which modes an impact excites.

## 4. Playback

- **`data.eventTrack`** (producer) — holds a baked take (content, via the factory
  `NodeContent` path) and publishes `Data(event-track)`.
- **`seq.eventPlayer`** (consumer) — plays a track: `trigger`/`reset`, `rate`,
  `loop`, `start`, and outputs per-impact Events carrying strength, plus the Instance
  context (`bodyId`, contact position, speed) through a swarm/trigger allocator, so
  each impact becomes one voice-like instance exciting a resonator. Continuous
  contacts output curves (`normalForce`, `slipSpeed`) to drive `excite.stickSlip`/
  `excite.contact` (catalog nodes from the PM Friction/Breath batch, built first).
- A **stock group** "Rigid Body Player" wires the obvious chain:
  event player → swarm allocator → `excite.mallet` → `resonator.modal` ←
  `data.material` per body → `instance.sum`.

## 5. The simulation — staged

### Stage 1: analytic drops (no physics engine)
A `factory.physics` editor with a single object dropped onto a surface: height,
restitution, mass, spin, surface material. Bounce times and impulses follow closed-
form restitution (`v' = e·v`), with seeded jitter for irregular objects. Proves the
data type, the player and the "re-render as another material" payoff with almost no
risk.

### Stage 2: a real rigid-body engine
Several bodies (dice, marbles, debris, a drawer of cutlery) simulated by an embedded
rigid-body library. **Jolt Physics** (MIT licence, C++, deterministic, fast) is the
natural candidate; it runs in the engine library on a worker thread — never the
audio thread — and bakes a take into content. Determinism matters: the same scene +
seed must bake the same take on every machine, so patches stay reproducible.
Editor: a simple 3D scene (primitive shapes, drop/throw tools, "bake" button) — the
same 3D view `SpatialScene.md` needs, built once.

### Stage 3: live (not baked) physics
Run the simulation continuously in real time, driven by ports (gravity, tilt, shake)
— a shaker you can tilt with a macro. A later step; baking first keeps the audio
thread simple and the result reproducible.

## 6. Geometry-driven resonance and excitation (distant future)

The `.obj` idea: **a resonator whose modes come from a real mesh.**

- **`data.meshModes`** — load an `.obj` (or a shape from the scene editor), run a
  modal analysis (a small finite-element eigen-solve on a tetrahedralized mesh, done
  offline on a worker thread, cached by mesh hash), and publish `Data(modal-set)`
  with *mode shapes*: for each mode, its frequency, damping and its amplitude at
  every surface vertex.
- `resonator.modal` then honours **where** it's struck: an impact's `contactPoint`
  looks up each mode's amplitude at that point, so hitting a bowl's rim and its base
  sound different, as they do physically. The same table gives pickup position
  ("where you listen").
- Material still comes from `data.material` (stiffness, density, damping) — mesh
  for shape, material for substance.
- With Stage 2, a simulated object and its sound share one mesh: drop a teapot, hear
  *that* teapot.

The solver is the hard part (meshing robustness, eigen-solves of a few thousand
DOFs, cost). It should be prototyped offline, outside the audio engine, before any
node is designed around it.

## 7. Order and prerequisites

1. Factory infrastructure (`Factories.md` steps 1–2).
2. PM Friction/Breath batch (`excite.stickSlip`/`contact`) for continuous contacts.
3. Stage 1: `DataTag::EventTrack`, `data.eventTrack`, `seq.eventPlayer`, analytic
   `factory.physics`, the stock "Rigid Body Player" group.
4. Stage 2 alongside `SpatialScene.md`'s 3D view (shared editor).
5. `data.meshModes` research prototype → node.
6. Stage 3 live physics.

## 8. Risks and open questions

- **Scope** — each stage must be musically useful on its own; Stage 1 alone (one
  object, re-materialised at will) already is.
- **Determinism** across platforms for Stage 2 (Jolt's cross-platform determinism
  mode; fixed timestep; seeded jitter only).
- **Voice budget** — a debris take can produce hundreds of impacts per second; the
  player needs impact merging/thinning and a per-take voice cap.
- **Licensing** of any physics or meshing library (MIT/BSD only).
- Open: should a take store absolute physical units (N·s, m/s) or normalised
  strengths? *Leaning:* physical units in the data, normalised at the player, so a
  take can be re-scaled ("heavier") without re-baking.
