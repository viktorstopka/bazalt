# Bazalt — Domains, instances and groups

Status: **design draft.** Third document of the set, after `VALUE_MODEL.md` and `SIGNAL_TYPES.md`. States the intended model; Claude Code reconciles it with the code, lists conflicts, and proposes migrations.

---

## 1. What a domain is

A **domain** is a structural property of a region of the graph: how many times per audio block that region is evaluated, and with what private state.

- **Mono (global) domain:** evaluated once. One instance, one set of state.
- **Poly (instanced) domain:** evaluated once per live instance, each with its own private state and its own instance context.

A domain is **not** a signal type and **not** a channel count on a port. It is inferred by the compiler from graph structure, so the user never declares it directly; they only decide where the boundary nodes sit.

## 2. Boundaries

Exactly two things move signals between domains:

- **Instance Allocator** — mono → poly. Opens an instanced region and provides the instance context (§4).
- **Voice Mix** — poly → mono. Sums all live instances into a single signal.

Everything else follows two rules:

- **mono → poly is free** (broadcast). A global LFO, a sidechain input, a macro, or a `Data` table is readable inside every instance without a node.
- **poly → mono requires Voice Mix.** There is no implicit summing anywhere, ever.

The user may place Voice Mix anywhere, including immediately before the output. Every node between the allocator and the mix runs per instance with its own state, which is how per-voice distortion, per-voice delay, and per-voice resonators work. Placing several mixes at different points is allowed; a region may be partly per-voice and partly global.

Ports may restrict themselves with `domain: polyOnly | monoOnly` (see `SIGNAL_TYPES.md` §3), but this is rare and needs justification: an output-only node like Master Out is `monoOnly`; note-lifecycle outputs of the allocator are `polyOnly`. Most ports are `any`.

## 3. Instance Allocator: one node, several configurations

Voice allocation and "swarm" spawning are the same runtime machinery with different event sources. Implement **one** node type with a configurable source, not two node types that grow apart.

| Configuration | Spawn source | Instance context | Typical use |
|---|---|---|---|
| Voice | a `Note` stream | the note | playing an instrument |
| Swarm (population) | fixed count, always live | index + seeded randoms | cicadas, a drone of many bodies |
| Swarm (transient) | an `Event` stream | seeded randoms per spawn | bubbles, crackles, sparks, raindrops |
| Trigger | an `Event` stream, one instance at a time | payload | percussive one-shots |

Shared parameters: maximum instances, stealing policy, instance lifetime policy, seed.

Note-specific parameters: mono/legato/glide, retrigger policy, unison count/detune/spread.

Swarm-specific parameters: population size or spawn density, parameter distributions (uniform, gaussian, exponential), spatial position model.

## 4. Instance context

An allocator's outputs are the per-instance signals available inside the region. Everything here is `polyOnly`.

Common to all configurations:

- `Instance Index` (`Count`)
- `Instance Age` (`Time`)
- `Random` — a stable random value for the instance's lifetime; several independent ones, addressable by index
- `Gate` (`Control`, bool)
- `Start` and `Stop` (`Event`)
- `Position` (spatial: pan, distance) for swarm configurations

Note configuration adds:

- `Pitch` (`Pitch`, continuous — bends and MPE need no separate path)
- `Velocity`, `Pressure`, `Slide`, `Release Velocity`
- `Unison Index` and `Unison Detune`

**Determinism:** randoms derive from a patch-level seed plus the instance's spawn ordinal. The same patch, the same MIDI, the same seed produce bit-identical output. This is required for the offline render CLI to be a useful regression tool.

## 5. Instance lifetime

This is the part that is hard to retrofit, so it is specified now.

- An instance is **created** on spawn and reaches the **releasing** state when its note ends or its lifetime expires.
- An instance is **freed only when its per-instance chain is silent**, not when an envelope finishes. Per-voice reverbs and delays must be allowed to ring out. Silence detection sits at the end of the instanced region, measuring the signal that reaches Voice Mix, with a threshold and a hold time.
- The number of **live** instances can therefore exceed the number of held notes. `maxInstances` bounds the live count, not the note count.
- **Stealing** applies to live instances. A stolen instance is faded out over a short ramp rather than cut; the ramp length is a parameter. Stealing a ringing tail is audible and must be reported in the UI (an instance-count readout plus a "stealing" indicator).
- **Buffers are preallocated for `maxInstances`**, so per-instance delay lines and reverbs multiply memory. The UI shows estimated memory for the instanced region, and `maxInstances` has a per-patch ceiling.
- Latency inside an instanced region must be **identical for every instance**, and is reported to the host once.

## 6. Events across the boundary

- A mono `Event` broadcast into an instanced region fires in **every live instance**. This is the correct default (a global clock ticking all swarm members).
- To fire in one instance only, the event must go through the allocator, which decides the target — that is exactly what a note-on does.
- Events generated **inside** an instance never leave it, except through Voice Mix (which is audio only) or through an explicit aggregation node (e.g. "any instance fired"), which is a later feature, not MVP.
- `Data` is read-only and shared: a modal set or a scale is computed once and every instance references it. Never copied per instance.

## 7. Domain inference and errors

The compiler:

1. flattens groups (§8),
2. marks the allocator's outputs as poly and propagates poly forward across connections,
3. stops propagation at Voice Mix,
4. rejects the graph with a precise error when a poly signal reaches a `monoOnly` port without a mix, when a `polyOnly` port is fed from mono, or when a cycle crosses a domain boundary,
5. allocates per-instance state for every node in the poly region and single state elsewhere,
6. lays out per-instance state so instances can be processed vectorised across instances.

Rejection keeps the previous execution plan live: an invalid edit never silences the instrument.

**Nested allocators** (a swarm inside a voice) are architecturally natural but multiply cost. Support them in the model, but require an explicit opt-in per allocator and show the multiplied worst-case instance count in the UI.

## 8. Groups

A **group** is a subgraph with a name and exposed ports that behaves, from every other perspective, exactly like a node. It is how most nature-oriented helpers ship (see §9).

- **Interface:** Group Input and Group Output nodes inside define the exposed ports. An exposed port has its own value contract and default, which may differ from the internal one (Hz inside, 0–1 outside with a Map).
- **Compilation:** groups are **inlined** and flattened, recursively, before domain inference. At run time a group does not exist and costs nothing. Per-sample feedback regions may therefore cross group boundaries.
- **Domain signature:** the compiler computes what a group does to domains — `mono→mono`, `any→any` (polymorphic, the common case), `mono→poly` (it contains an allocator), `poly→mono` (it contains a Voice Mix). Placing a group somewhere incompatible is rejected with the same precision as an ordinary node, before the user hears anything wrong.
- **Macros:** inside a group, "macro-like" controls are just exposed input ports. Only a top-level Macro node binds into the host's 32-slot pool. There is no third meaning of "macro".
- **Instancing:** the same group placed in an instanced region compiles per instance; in a mono region, once. Nothing about the group changes.
- **Previews:** a group declares which internal tap is its preview; otherwise its output is used. Groups must not be the only nodes without live visualisation.
- **Storage:** two modes — **embedded copy** (patch is self-contained; the default) and **library reference** with a pinned version. Updating a library never silently changes an existing patch; it is an explicit action. "Make unique" detaches an instance. Groups export as files with IDs in the `user.*` namespace.
- **Namespaces:** `core.*` native primitives with frozen IDs, `factory.*` shipped groups, `user.*` user groups, `lab.*` experiments with no stability promise (never used by a factory preset).

## 9. Native versus group

The implementation of a node is invisible to the patch, the UI, and the user: same descriptor, same ports, same serialisation, same telemetry. A node may therefore be reimplemented from a group into C++ later for performance **as long as its type ID and ports stay identical**.

**Write it natively when** it needs single-sample feedback; its inner loop scales with N (modal banks, granular clouds, FFT); it allocates or prepares data (samples, tables, plans); it is numerically delicate (ZDF filters, band-limited oscillators, interpolated delay lines); it is runtime machinery rather than DSP (the Instance Allocator itself); or it needs a custom editor UI. Practical rule of thumb: if the group version would exceed roughly twenty nodes or cost more than about three times the CPU, write it natively.

**Ship it as a group when** it is mostly composition and routing — and when the user benefits from opening it up and changing it.

**The completeness test:** for every native node where a subgraph version is conceivable, that subgraph must be expressible from the primitives. It does not have to be the implementation, but if it cannot be drawn at all, a primitive is missing and that is a design bug. Where practical, keep the subgraph version as a reference in the test suite and compare characteristics against the native one.

Planned factory groups for the first catalogue, chosen to cover the reference patches: **Karplus-Strong**, **Scale Quantize**, **Arpeggiator**, **Bubble**, **Crackle**, **Scrape**. Each needs a one-line description and a note on which primitives it is built from; the point of listing them is to prove the primitive set is sufficient, not to specify them in detail.

## 10. Stability rules

- The two boundary node types and the instance-context port IDs are permanent.
- Adding a new allocator configuration is safe; changing what an existing one does to lifetime or stealing is not.
- Domain inference rules are part of the contract: a graph that compiles today must compile tomorrow.

## 11. Open questions

1. How is poly versus mono shown in the UI? It must not collide with the type palette, so the proposal is line style or weight rather than colour. This is still the open item from the node editor design.
2. Should Voice Mix offer more than summing (average, normalise by instance count), given that swarms with 200 instances behave very differently from 8 voices?
3. What exactly does `Instance Age` do in a persistent swarm where instances never die — is it wall-clock time since patch start?
4. Should the silence threshold be per patch, per allocator, or per instanced region?
5. Does a group need to declare a maximum instance cost so the UI can warn before a swarm of groups is placed inside a voice?
6. For transient swarms, what happens when spawn density exceeds `maxInstances` — drop new spawns, steal the oldest, or dynamically reduce density? These sound very different, and it should be a parameter rather than an implementation detail.
