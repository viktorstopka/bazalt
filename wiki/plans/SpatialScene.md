# Spatial Scene — placing sound in a 3D space

**Status:** Proposed, 2026-10-04. Vision and staging only — not scheduled, no code.
Depends on `Factories.md` (the scene is a factory) and shares its 3D view with
`BakedPhysics.md` Stage 2.

---

## 0. Origin

Direct direction, 2026-10-04: Bazalt is "very physically and spatially oriented",
and "in future there should be possibilities such as working visually in a 3D space
to create different effects."

## 1. The idea

Instead of a pan knob and a reverb send, a patch can describe **a place**: sources
at positions, a listener, walls and objects with materials — and Bazalt renders what
that place does to sound: distance, direction, occlusion, early reflections, a room
tail that follows the room's real size and surfaces, Doppler when things move.
Positions are ordinary modulatable values, so an LFO can orbit a source, a swarm's
instances can be scattered through a room, and a baked physics take
(`BakedPhysics.md`) can sound *where* each impact happened.

The digital-native angle: a mixing desk imitates a physical signal path; a scene
lets you describe the *physical situation* directly and derives the signal path from
it.

## 2. What must exist first

| Prerequisite | State |
|---|---|
| A reverb at all | **Missing.** `space.reverb` (FDN) and `space.diffuser` are catalog-only. Build them first — and design `space.reverb`'s parameters around room size/absorption so the scene can drive it later. |
| Stereo cables | Built (`NODES.System.md` §9). |
| Per-instance context (positions per voice/swarm member) | Built (`instance.allocate.*` context ports). |
| A factory editor surface | `Factories.md` step 1. |
| Live, click-free parameter changes | Built (`LiveParameterEdits.h`, value-only edits keep the node). Moving sources need *per-sample* smoothing of delays — designed into the renderer, below. |

## 3. Rendering, staged

### Stage 1: positional nodes (no scene editor)
- **`space.position`** — mono in, stereo out; ports `x`, `y`, `z` relative to the
  listener. Distance → gain (inverse-distance with a near-field clamp) and air
  absorption (a gentle distance-dependent low-pass); direction → panning; motion →
  Doppler via a fractional delay line whose length follows distance smoothly
  (per-sample interpolation, so moving sources never zipper).
- **`space.room`** — the rewritten `space.reverb` with physical parameters (room
  dimensions, surface absorption) mapped onto its FDN: delay lengths from room
  dimensions, decay from Sabine's RT60 estimate, damping from absorption.
- Musically useful immediately, and every later stage reuses these two.

### Stage 2: the scene factory
- **`factory.scene`** — a 3D editor (three.js in the WebView: visual only, it
  computes nothing audible — CLAUDE.md rule 1) for placing sources, the listener,
  a room box and its materials. Each source in the scene corresponds to an input
  port of the factory; the factory renders all of them into one stereo output.
- **Unwrap** → one `space.position` per source + a `space.room`, wired to a sum —
  the factory is never more capable than the primitives (`NODES.System.md` §8).
- Positions stay ports, so the scene is an editor of *defaults*; modulation moves
  things live, and the editor shows them moving (telemetry, like every preview).

### Stage 3: geometry-aware acoustics
- **Early reflections from geometry** — the image-source method for box rooms first
  (exact, cheap), arbitrary meshes later (ray/beam tracing on a worker thread,
  producing a reflection set as `Data`, crossfaded on change).
- **Occlusion** — objects between source and listener filter the direct path.
- **The room as a resonator** — the same `.obj` modal analysis planned in
  `BakedPhysics.md` §6, applied to an enclosure: a mesh becomes a set of room modes
  or an impulse response (`DataTag::ImpulseResponse` already exists), so a tube, a
  vase or a stairwell can be "played into".

### Stage 4: output formats
- **Binaural** (HRTF) for headphones, as a `space.room`/`factory.scene` output mode.
- Ambisonics / multichannel only if real demand appears; the plugin's buses are
  stereo today.

## 4. Order

1. `space.diffuser` → `space.reverb` (as `space.room`, physically parameterised).
2. `space.position` (distance, direction, Doppler, air absorption).
3. `factory.scene` after `Factories.md` proves the docked editor; share the 3D view
   with `BakedPhysics.md` Stage 2.
4. Geometry-aware reflections and occlusion; mesh rooms; binaural.

## 5. Risks and open questions

- **3D UI cost and performance** in the WebView — keep the scene view simple
  (primitives, a box room, gizmos), render it only while open, and keep all audio
  math in C++.
- **CPU** — per-source Doppler/filters are cheap; geometric acoustics is not.
  Reflection sets are computed off the audio thread and published as `Data`; the
  audio thread only convolves/sums.
- **Interaction with the voice system** — a scene with a polyphonic source places
  every voice at the same point unless the instance context supplies positions;
  decide whether a scene source can be a `Poly` input (*leaning:* yes, positions
  per instance, summed inside the factory).
- Open: listener in the scene vs. a fixed listener at the origin. *Leaning:* a
  movable listener — it is just another position, and moving it is a strong effect.
