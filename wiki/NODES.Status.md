# Bazalt — Node build status & roadmap

Tracks, per node, **what's actually built** vs. **what's left**, and for what's left,
**how badly we need it** and **what order to build it in**. Derived from
`wiki/NODES.md` (the catalog — read that for what each node actually does) and
`wiki/NODES.System.md` (architecture). Cross-checked directly against
`engine/include/bazalt/engine/nodes/*.h` (**90 registered node types across 88 files**
as of the PM Core batch's fourth and final step (`excite.mallet`/`resonator.plate`,
2026-10-02) — eight new files total this arc (`excite.impulse`/`resonator.comb`/
`data.material`/`resonator.modal`/`excite.pluck`/`resonator.string`/
`excite.mallet`/`resonator.plate`), +8 from the 82 registered at the Control -> Audio
Bridge — the authoritative running count
`tests/NodeDescriptorTests.cpp`'s own `NodeDescriptorTests` case tracks and
narrates per batch; was 77/74 after `wiki/plans/DomainRedesign.md` Batch 3 folded
`mix.sum` (`MixNode.h`, its own dedicated file/type) straight into `math.add` with
no replacement type added, then 78/77 once `util.macro` (`MacroNode.h`, new file)
landed, then 79/78, 80/79, 81/80 once `InstanceSwarmPopulationNode.h`/
`InstanceSwarmTransientNode.h`/`InstanceTriggerNode.h` (3 new files) landed in
turn. `GrowableGroupNode`/`InheritingPortsNode` excluded as shared base classes;
`ViewNodes.h` holds 3 node types and `PitchFrequencyNodes.h` holds 2, which is why the
type count exceeds the file count. This file's own header count has drifted stale
twice now from same-day/same-milestone batches that landed after it was last
written — both corrected here against the authoritative source (a post-`util.macro`-
ship sweep caught the second drift, 2026-10-01) rather than incrementally patched)
— every ✅/🚧 claim below is a real, compiled, registered node, not aspirational.

This file doesn't replace `wiki/NODES_Gaps.md` (found-defect tracking for nodes that
exist) or `wiki/MILESTONES.md` (what actually shipped, in what order). It's the
forward-looking build plan `wiki/NODES.md` itself doesn't try to be.

## Methodology

**Status** is a direct relabelling of `wiki/NODES.md`'s own ✅/🚧/📋 markers, onto the
three buckets you asked for:

| Your bucket | Maps from | Meaning |
|---|---|---|
| **Implemented** | ✅ | Built, registered, matches the catalog spec. Simple enough in concept that there's nothing meaningful left to do. |
| **MVP** | 🚧 | Built and registered, but narrower than the catalog spec — a real, working barebone that needs real further work to be "done." |
| **To be implemented** | 📋 | Not built at all. |

This mapping is exact, not approximate: a node is only "Implemented" once it has zero
open gap against its own catalog spec — that's the same bar `wiki/NODES.md` already
holds itself to, and it happens to line up with your own "NOT node, not much to do
here" example (every ✅ node is conceptually closed, whether the *DSP inside it* was
easy or hard to write; "MVP" is reserved for nodes that are open, not for nodes that
were merely difficult to build).

**Necessity + difficulty** (`A1`–`D3`) and **Batch** are only assigned to "To be
implemented" rows — an Implemented/MVP node needs no further prioritization. Necessity
letters follow your definitions exactly; the reasoning per family is in the "Necessity
buckets, explained" section below, since eyeballing 67 individual node-by-node
justifications would be noise. Difficulty (1–3) is this session's judgment call on
build effort, independent of necessity — an A-tier node can still be a 3.

### A cross-cutting prerequisite worth flagging up front

**Resolved by the Data Foundations batch — kept here as historical context.**
`Data` was a declared, implemented signal type (`canConnect` handled it fully — tag
matching, rejection rules) but the "build on a worker thread, publish, atomic pointer
swap" pipeline `wiki/NODES.System.md` §1 describes had zero real callers until
`data.scale`/`data.table`/`data.lookup` shipped: `Node::getDataPublisher()`/
`setDataInput()` (new `Node.h` virtuals) and `GraphCompiler.cpp`'s new `Data`-connection
branch now exist for real. Every later `data.*`/`osc.wavetable`/`sampler.*` node rides
on top of this for free. Similarly, **`NodeContent` (the third value category, §3) still
doesn't exist as real code** — `data.table`'s own curve content and `seq.steps`' own
step bank both shipped as a fixed `ParameterDescriptor` bank ahead of it, deliberately,
rather than waiting; every `factory.*` node still does want the real thing.

---

## Part A — native nodes

### `io.*` — the plugin boundary — 5/5 Implemented

| Node | Status |
|---|---|
| `io.audioIn` | Implemented |
| `io.output` | Implemented |
| `io.noteIn` | Implemented |
| `io.control` | Implemented |
| `io.transport` | Implemented |

### `osc.*` — oscillators — 1 Implemented, 1 MVP, 2 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `osc.sine` | Implemented | | | |
| `osc.analog` | MVP | | Oscillator Expansion | Missing `fine`/`pulseWidth`/`phaseMod`/`sync` inputs — real work, catalog-specified, not started. |
| `osc.wavetable` | To be implemented | **B2** | Oscillator Expansion | Needs `Data(wavetable)` — rides the Data pipeline once it exists. |
| `osc.glottal` | To be implemented | **B3** | PM Voice | Correction 1; LF/Rosenberg parametric pulse model — delicate band-limiting. |

### `sampler.*` — samples and grains — 2 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `sampler.player` | To be implemented | **B2** | Sampler | Needs `Data(sample)` (→ `data.load`). |
| `sampler.granular` | To be implemented | **B3** | Noise & Grain | Preallocated grain pool, scales with grain count. |

### `noise.*` — stochastic sources — 2 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `noise.colored` | To be implemented | **B1** | Noise & Grain | Filtered-white-noise family; straightforward. |
| `noise.dust` | To be implemented | **B2** | Noise & Grain | Sample-accurate sparse-impulse timing. |

### `excite.*` — physical excitation — 3 Implemented, 1 MVP, 4 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `excite.impulse` | Implemented | | PM Core | Done, batch 1 (2026-10-02). |
| `excite.pluck` | Implemented | | PM Core | Done, batch 3 (2026-10-02). Fixed 5ms noise burst; `position` is a fixed-window FIR comb (no pitch concept of its own, unlike `resonator.string`'s own `position`). |
| `excite.mallet` | Implemented | | PM Core | Done, batch 4 (2026-10-02), closes the PM Core batch. A half-sine contact pulse; `feedback` genuinely couples to a resonator's own `motion`, the first real cross-node per-sample feedback cycle in production node code. |
| `excite.burst` | MVP | | PM Core | Missing `tone`/`shape`; trigger/duration already real. |
| `excite.stickSlip` | To be implemented | **B3** | PM Friction/Breath | Delicate near-zero-speed friction. |
| `excite.breath` | To be implemented | **B2** | PM Friction/Breath | Nonlinear noise-modulation (breath/reed/lip modes). |
| `excite.contact` | To be implemented | **B2** | PM Friction/Breath | Scrape/grain-rate model. |
| `excite.vocalFolds` | To be implemented | **B3** | PM Voice | Hardest excite node — self-oscillating two-mass valve. |

### `resonator.*` — resonating bodies — 4 Implemented, 3 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `resonator.comb` | Implemented | | PM Core | Done, batch 1 (2026-10-02). |
| `resonator.modal` | Implemented | | PM Core | Done, batch 2 (2026-10-02). The centre of the PM set — a real bank of up to 64 two-pole resonators driven by `data.material`'s mode set, one real stereo output. |
| `resonator.string` | Implemented | | PM Core | Done, batch 3 (2026-10-02). The playable Karplus-Strong flagship — a real circular delay line through a damping one-pole + single-stage stiffness allpass; `release` is a real `bool` gate (held/muted), not a knob. |
| `resonator.plate` | Implemented | | PM Core | Done, batch 4 (2026-10-02), closes the PM Core batch. A real, documented simplification — reuses `resonator.modal`'s two-pole bank over a fixed, internally-generated mode set (the same membrane-Bessel-zero table `data.material` uses, squared), not a literal 2D mesh solve. |
| `resonator.tube` | To be implemented | **B2** | PM Friction/Breath | Single-tube waveguide, feedback both directions. |
| `resonator.junction` | To be implemented | **B3** | PM Voice | Correction 1; multi-way scattering junction. |
| `resonator.tract` | To be implemented | **B3** | PM Voice | Correction 1; multi-section vocal-tract-shaped waveguide. |

### `filter.*` — 6 Implemented, 1 MVP, 1 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `filter.ladder` | Implemented | | | |
| `filter.onepole` | Implemented | | | |
| `filter.allpass` | Implemented | | | |
| `filter.shelf` | Implemented | | | |
| `filter.peak` | Implemented | | | |
| `filter.dcBlock` | Implemented | | | |
| `filter.svf` | MVP | | (standalone) | Needs the 5-simultaneous-output redesign (lowpass/bandpass/highpass/notch/peak port group) — currently one mode-switched `out`. Structural, not tied to a batch. |
| `filter.formant` | To be implemented | **B2** | PM Voice | Bank of bandpass filters over `Data(modal-set)`/vowel table. |

### `shape.*` — nonlinearities — 1 Implemented, 4 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `shape.clip` | Implemented | | | Done, direct feedback (2026-10-03) — pulled forward out of the Shaping batch, same "build early, useful everywhere" reasoning this row already gave it. Real hard/soft/limiter modes, a real quadratic soft-knee. |
| `shape.rectify` | To be implemented | **B1** | Shaping | Trivial half/full rectify. |
| `shape.crush` | To be implemented | **B1** | Shaping | Bit/sample-rate reduction. |
| `shape.waveshaper` | To be implemented | **B2** | Shaping | Multiple curve types + oversampling. |
| `shape.fold` | To be implemented | **B2** | Shaping | Wavefolder, oversampled. |

### `delay.*` — 1/1 Implemented

`delay.line` — Implemented.

### `space.*` / `stereo.*` — 4 Implemented, 2 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `space.pan` | Implemented | | | |
| `space.width` | Implemented | | | |
| `stereo.split` | Implemented | | | |
| `stereo.combine` | Implemented | | | |
| `space.diffuser` | To be implemented | **B2** | Space | Allpass chain — reuses `filter.allpass`. |
| `space.reverb` | To be implemented | **B3** | Space | FDN reverb — the hardest effect node; cheap enough at low quality to run per-voice. |

### `mix.*` — 3/3 Implemented

`mix.crossfade`, `mix.gain`, `mix.downmix` — all Implemented. `mix.sum` is gone as of
`wiki/plans/DomainRedesign.md` Batch 3 — folded into `math.add` (see the `math.*`
family below), not a fourth entry here anymore.

### `env.*` — 2 Implemented, 1 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `env.adsr` | Implemented | | | |
| `env.follower` | Implemented | | | |
| `env.curve` | To be implemented | **B2** | Env/LFO Shapes | Drawn multi-segment envelope — needs `Data(curve)`. |

### `lfo.*` — 1 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `lfo.shape` | To be implemented | **B2** | Env/LFO Shapes | Multi-waveform + custom-shape morphing — needs `Data(curve)`. |

### `random.*` — 2/2 Implemented

`random.stepped`, `random.drift` — both Implemented.

### `clock.*` — 3/3 Implemented

| Node | Status | Notes |
|---|---|---|
| `clock.pulse` | Implemented | Phase-accumulator clock; swing/jitter/division mode all real. Gained a `seed` param beyond the catalog spec (jitter determinism), same convention `random.stepped`/`random.drift` already established. |
| `clock.divide` | Implemented | Output port id is `tickOut` (not catalog's `tick`) — a real engine invariant (no node may reuse a port id across its own inputs/outputs) forced this; display label is still "Tick". |
| `clock.counter` | Implemented | All 4 modes (up/down/pingPong/random) real; `wrapped` deliberately never fires in `random` mode (documented, not an oversight). Gained a `seed` param, same reasoning as `clock.pulse`. |

### `seq.*` — 1 Implemented, 1 MVP

| Node | Status | Notes |
|---|---|---|
| `seq.euclid` | Implemented | Standard Bjorklund-equivalent onset formula; `rotate` real. |
| `seq.steps` | MVP | Two real, documented deviations from spec: no `Data(curve)` input (nothing in the engine produces `Data` yet — see the cross-cutting note above), and `length` capped at 16 instead of the catalog's 64 (trivial to raise later; these are plain numbered parameters, not a wire-format array size). Real per-step content editing belongs on `NodeContent` (§3) once that exists. |

### `note.*` — the note stream — 3 to build (all blocked on the same engine limit)

Whole family is essential per the catalog's own framing: *"what makes arpeggios,
chords, scales, and audio-driven instruments ordinary patching rather than built-in
features."*

**Note Stream batch — done, with a real finding.** 6 of 9 built:
`note.gate`/`note.value`/`note.transpose`/`note.filter`/`note.humanize`/`note.quantize`.
The other 3 hit a real, previously-unexercised engine limit — `ExecutionPlan::
BlockStep` supports only one `Note` input and one `Note` output per node, and
`note.chord`/`note.hold`/`note.select` all assume a multi-note signal one `Note` cable
can't carry (the catalog's own `note.filter` hit the same wall — a real 1-output
redesign closed that one, see its own row below). Deferred rather than forced through
with a compromised, misleading shape — a real design for multi-note `Note` signals is
separate, larger engine work. Full reasoning in `wiki/MILESTONES.md`'s own entry.

**`note.assemble` follow-up — done, out of its originally planned order.** Direct
feedback on this family's own remaining gaps (not this file's own roadmap) named it as
the single highest-leverage node missing — the one thing that can PRODUCE a `Note`
stream from scratch, where every other node here only reshapes one that already
exists. Built ahead of the **Analysis+Assemble** batch below (its planned batch-mate,
`analysis.pitch`, is still 📋) — deliberately: this node accepts any `pitch [audio]`
source, not only a pitch tracker's, so a `clock.*`/`random.*`/`data.lookup` chain can
already drive it today with no analysis nodes involved at all. It'll gain its other
intended use (an audio input playable as an instrument) once `analysis.pitch`/
`analysis.onset` land — see that batch's own updated row.

| Node | Status | Notes |
|---|---|---|
| `note.gate` | Implemented | `count` (not elaborated by the catalog) is this node's own design: a running tally of note-ons since reset. |
| `note.value` | Implemented | `pressure`/`slide` not built (`NoteEvent` doesn't carry them). `select` genuinely works via a small internal 8-note memory, not just "whatever's live this sample." |
| `note.transpose` | Implemented | Output port id is `notesOut`, not the catalog's `notes` — same engine invariant `clock.divide`'s `tickOut` already hit. |
| `note.filter` | Implemented | **Redesigned** from the catalog's literal `pass`/`reject` (two `Note` outputs) to one `Note` output + a plain `inRange` Boolean — see the batch note above. |
| `note.humanize` | Implemented | Timing jitter via a small scheduled countdown (sufficient — the stream is monophonic), capped at 50ms; only note-on is jittered, not note-off. |
| `note.quantize` | Implemented | The flagship Data Foundations consumer — real, tested `data.scale → note.quantize` round trip. `root` is a second, independent knob from `data.scale`'s own root (post-quantization offset, not a duplicate). |
| `note.assemble` | Implemented | The one node that PRODUCES a `Note` stream. `trigger` retriggers legato (no forced stop first); `pitch` tracked continuously while held; `velocity` captured once at trigger; a note ends on explicit `release` OR `confidence` dropping below `confidenceGate` — neither wired (plain generative use) means it holds until an explicit `release`, ordinary MIDI semantics. |

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `note.hold` | To be implemented | **A2** | *(blocked)* | Real engine limit, not just unbuilt — needs a multi-note `Note` signal the engine can't represent yet (see the batch note above). |
| `note.select` | To be implemented | **A2** | *(blocked)* | Same real engine limit as `note.hold` — pairs with `clock.counter` once buildable. |
| `note.chord` | To be implemented | **A2** | *(blocked)* | Same real engine limit — needs to emit several simultaneous notes from one input note. |

### `math.*` / `logic.*` / `adapt.*` — 26/26 Implemented

All done — no rows needed (count unaffected by `wiki/plans/DomainRedesign.md` Batch 3:
`math.add`/`math.multiply` gained real Audio/Poly polymorphism and absorbed
`mix.sum`'s job, but neither is a new or removed row here). `adapt.audioToControl` ("To Modulation") was added whole by
the Audio → Control Bridge (`wiki/plans/AudioControlBridge.md`); `adapt.boolToControl`
("From Bool"), `adapt.pitchToFrequency`/`adapt.frequencyToPitch`, and
`adapt.gateLength` were all added whole in a direct-feedback sweep the same session
(a real, previously-wrong linear-remap-for-Pitch↔Frequency correctness fix among
them) — none of the five were ever moved out of the catalog-only backlog; they never
were catalog-only.

### `data.*` — producing and reading buffers — 4 Implemented, 4 to build

Foundational, but not "essential-for-modularity" the way `note.*`/`clock.*` are —
these exist to feed other nodes, so bucketed **C**: needed for a first content wave,
not the graph-flow infra `A` is reserved for.

**Data Foundations batch — done.** `data.scale`/`data.table`/`data.lookup` are the
pathfinder for the whole `Data`-publishing pipeline: before these three, no node in the
engine had ever produced a real `Data` value — only `canConnect`'s tag-matching rules
were real. Building them required real, new engine infrastructure, not just three node
files: `Node::getDataPublisher()`/`setDataInput()` (new virtuals) and a new
`Data`-typed-connection branch in `GraphCompiler.cpp`, wiring a producer's
`DataPublisher*` straight into a consumer once, at compile time — much lighter than
`Note`'s own per-block mechanism, since a `Data` buffer only ever changes on a discrete
edit. Every later `data.*`/`osc.wavetable`/`sampler.*` node rides on this for free.

| Node | Status | Notes |
|---|---|---|
| `data.scale` | Implemented | 12 named scales (everything the catalog asks for except "harmonic series" and "custom", both deliberately deferred — see below); `octaveSize` proportionally rescales the 12-tone patterns. Real, documented RT-safety limit: `root`'s *live* cable value is never read on the audio thread (rebuilding means a heap allocation) — only the value applied via `setParameter()` republishes. |
| `data.table` | Implemented | Curve content is a fixed 32-point parameter bank (`seq.steps`' own pattern), not real `NodeContent` — that category doesn't exist as code yet. `resolution` (2–32) picks how many points publish. |
| `data.lookup` | Implemented | 4 modes (nearest/interpolate/index/wrapIndex) with a concrete, tested contract this session had to design (the catalog names the modes, not their exact semantics); `dataB`/`morph` blending with graceful tag-mismatch fallback. |
| `data.material` | Implemented | Done, PM Core batch 2 (2026-10-02). Publishes `Data(modal-set)`, stride 3 (`ratio`/`amplitudeWeight`/`decayWeight`) per mode; `string`/`tube`/`bar`/`membrane` geometries use real or closely-approximated closed forms, `plate` reuses `membrane`'s table squared (documented simplification), `irregularSolid` is deterministic seeded noise. `size` is a declared port, not yet consumed — a real, deliberate gap. Feeds `resonator.modal` (built same batch) and, later, `filter.formant`. |

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `data.record` | To be implemented | **C2** | EQ/Curve Data | Real-time capture + worker-thread handoff, preallocated buffer. |
| `data.load` | To be implemented | **C3** | Sampler | File I/O + multiple interpretations (sample/wavetable/IR) — real format work, not just DSP. |
| `data.analyseModes` | To be implemented | **C3** | Analysis+Assemble | FFT/peak-picking modal analysis of a recording. |
| `data.eqToCurve` | To be implemented | **C1** | EQ/Curve Data | Simple render, but blocked on a real `Data(eq-curve)` producer (`factory.eq`, D-tier) to be meaningful. |

### `analysis.*` — 4 to build (none started)

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `analysis.level` | To be implemented | **C1** | Analysis+Assemble | Mirrors `env.follower`'s existing ballistics — easy. |
| `analysis.onset` | To be implemented | **C2** | Analysis+Assemble | Energy/spectral-flux onset detection. |
| `analysis.centroid` | To be implemented | **C2** | Analysis+Assemble | Spectral centroid — needs an FFT. |
| `analysis.pitch` | To be implemented | **C3** | Analysis+Assemble | Hardest analysis node — autocorrelation/YIN pitch tracking. |

### `instance.*` — domains — 2 Implemented, 3 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `instance.allocate.voice` | Implemented | | | |
| `instance.sum` | Implemented | | | renamed from `instance.mix`, `wiki/plans/DomainRedesign.md` Batch 1b |
| `instance.allocate.swarmPopulation` | Implemented | **A1** | Domain Extensions | Fixed count, always live — simplest of the three (no spawn logic). Done 2026-10-01. |
| `instance.allocate.swarmTransient` | Implemented | **A2** | Domain Extensions | Event-triggered spawn, closest to Voice's own shape. Done 2026-10-01. |
| `instance.allocate.trigger` | Implemented | **A2** | Domain Extensions | Event-triggered, one instance at a time. Done 2026-10-01. |

### `util.*` — 5/5 Implemented

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `util.constant` | Implemented | | | |
| `util.reroute` | Implemented | | | |
| `util.macro` | Implemented | | | `wiki/plans/UtilMacro.md` — ADR-0030 amends ADR-0015 (the 32-fixed-slot pool stays; the macro node becomes its own mapping target with a real wireable output, instead of a side-channel poke onto a foreign node). |
| `util.unipolarToBipolar` | Implemented | | | `wiki/plans/PropsAndMacroRedesign.md` Batch D — new, replacing `random.stepped`/`seq.steps`/`data.lookup`'s old per-node Unipolar/Bipolar selectors. Manual placement only, never auto-inserted. |
| `util.bipolarToUnipolar` | Implemented | | | Same batch, the inverse direction. |

### `view.*` — 9/9 Implemented

`view.listen`, `view.spectrum`, `view.meter`, `view.ripple`, `view.count`,
`view.scope.control`, `view.scope.modulation`, `view.gate`, `view.cycle` — all
Implemented. `view.scope` and `view.glance` were removed 2026-10-04, replaced
by the phase-locked `view.cycle` (old patches migrate on load).

### `factory.*` — content-owning nodes — 6 to build (none started)

Explicitly **D**: `wiki/NODES.System.md` §8 already frames these as "ship one at a
time whenever their milestone comes up... none of this blocks anything currently
built." They also need the `NodeContent`/custom-editor infrastructure to be worth
building for real, not simplified.

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `factory.material` | To be implemented | **D2** | Factories | Editor over `data.material`'s params — the least new mechanism among the six. |
| `factory.curve` | To be implemented | **D2** | Factories | Editor over `data.table`. |
| `factory.eq` | To be implemented | **D3** | Factories | Band-list editor + realtime filter bank. |
| `factory.wave` | To be implemented | **D3** | Factories | Wave editor + wavetable data. |
| `factory.sample` | To be implemented | **D3** | Factories | Sample editor + slicing. |
| `factory.notes` | To be implemented | **D3** | Factories | Pattern editor — also depends on the whole `note.*`/`clock.*`/`seq.*` family existing first. |

---

## Totals

| Status | Count |
|---|---|
| Implemented | 84 |
| MVP | 4 (`osc.analog`, `filter.svf`, `excite.burst`, `seq.steps`) |
| To be implemented | 40 |
| **Total native node types** | **128** |

By necessity, among the 40 still to build: **A** 4 · **B** 23 · **C** 7 · **D** 6.

**`shape.clip` — done, 2026-10-03, direct feedback.** Pulled forward out of the
Shaping batch: a real, wireable, mid-chain safety limiter (hard/soft/limiter modes,
a real quadratic soft-knee), built in response to "it gets very tedious trying to
test this and getting my ears blown off." Complements, not replaces, the plugin's
own new always-on master-output `OutputLimiter` (`PluginProcessor.cpp`), which
protects the final mix unconditionally but can't be inserted mid-chain (e.g. to
tame a resonant feedback loop before it hits another node). See
`wiki/MILESTONES.md`'s own entry.

**The whole PM Core batch is now closed** (2026-10-02): `excite.impulse`/`pluck`/
`mallet`, `resonator.comb`/`modal`/`string`/`plate`, and `data.material` are all
real, wireable, and tested end to end — Bazalt's first physical-modelling
instruments, the Struck Body and (bar `excite.stickSlip`) Bowed String reference
patches now fully or almost-fully buildable. `excite.burst`'s own `tone`/`shape`
MVP gap and `resonator.tube`/`junction`/`tract` are explicitly NOT part of this —
left for their own later batches (PM Friction/Breath, PM Voice) exactly as
originally planned.

**PM Core batch 1 — done, 2026-10-02.** `excite.impulse`/`resonator.comb` built and
tested (`tests/PMCoreNodesTests.cpp`) — the simplest real excite→resonate pair,
proving the pattern with no `Data` pipeline dependency and no cross-node feedback
requirement (both nodes' own feedback, where they have any, is self-contained
internal state). See `wiki/MILESTONES.md`'s own entry.

**PM Core batch 2 — done, 2026-10-02.** `data.material`/`resonator.modal` built and
tested (`tests/PMCoreNodesTests.cpp`, 18 more cases) — the first real `Data(modal-set)`
producer/consumer pair, and the centre of the whole physical-modelling set. See
`wiki/MILESTONES.md`'s own entry.

**PM Core batch 3 — done, 2026-10-02.** `excite.pluck`/`resonator.string` built and
tested (`tests/PMCoreNodesTests.cpp`, 10 more cases) — the flagship playable
Karplus-Strong pair the whole batch was named after. See `wiki/MILESTONES.md`'s own
entry.

**Clock+Seq batch — done.** `clock.pulse`/`clock.divide`/`clock.counter`/`seq.euclid`/
`seq.steps` all built and tested — see their family sections above for per-node notes;
`wiki/MILESTONES.md`'s own entry has the full build record.

**Data Foundations batch — done.** `data.scale`/`data.table`/`data.lookup` all built
and tested, plus the new cross-cutting `Data`-publishing infrastructure they needed
(`Node::getDataPublisher()`/`setDataInput()`, `GraphCompiler.cpp`'s Data-connection
wiring) — see the `data.*` family section above and `wiki/MILESTONES.md`'s own entry.

**Note Stream batch — done, 6 of 9 (3 blocked on a real engine limit).**
`note.gate`/`note.value`/`note.transpose`/`note.filter`/`note.humanize`/`note.quantize`
all built and tested; `note.hold`/`note.select`/`note.chord` deferred — a real,
previously-unexercised limit (one `Note` input/output per node, max), not just "not
built yet" — see the `note.*` family section above and `wiki/MILESTONES.md`'s own
entry.

---

## Necessity buckets, explained

- **A — essential, ASAP.** Everything here is graph-flow/timing/note-stream/domain
  infrastructure that unlocks broad modularity, mirroring why `logic.*`/`adapt.*`
  already shipped early: `clock.*`/`seq.*` (**done**) and 6 of 9 `note.*` (**done** —
  see the Clock+Seq and Note Stream batch notes below; `note.hold`/`select`/`chord`
  remain, blocked on a real engine limit, not just unbuilt), the three
  `instance.allocate.*` spawn types, `util.macro` (**done**). None of these are hard
  *because* they're essential — several are difficulty 1 — they're essential because
  a huge amount of "ordinary patching" (arpeggios, chords, scale-snapping, swarms,
  percussive one-shots, host-automatable knobs) is blocked on them existing at all.
- **B — important, complex, deserves individual focus.** The "personality" DSP
  families: `osc.*`, `sampler.*`, `noise.*`, `excite.*`, `resonator.*`,
  `filter.formant`, `shape.*`, `space.reverb`/`diffuser`, `env.curve`, `lfo.shape`.
  Difficulty varies 1–3 within the bucket (a trivial `excite.impulse` is still "B"
  because it's part of the physical-modelling family, not because it's hard).
- **C — other nodes that should ship first.** Support/utility nodes that other things
  depend on but aren't themselves core graph-infra (`A`) or characterful DSP (`B`):
  the `data.*` producers/consumers and the `analysis.*` family. Several of these
  should be pulled *forward* in actual build order despite being "C" — see below.
- **D — nice-to-have, later extension.** The `factory.*` family exclusively — already
  explicitly deferred in `wiki/NODES.System.md` §8, needs its own editor
  infrastructure, and nothing currently planned depends on it existing.

**Necessity tier is not the same as build order.** `data.scale`/`data.table` are "C"
but sit at the top of the recommended order below because `note.quantize` (A),
`env.curve`/`lfo.shape` (B), and `seq.steps` (A) all consume them.

---

## Batches

| Batch | Members | Why batched |
|---|---|---|
| **Clock+Seq** — done | `clock.pulse`, `clock.divide`, `clock.counter`, `seq.steps`, `seq.euclid` | Chain into each other directly (pulse → divide → counter → steps/euclid); tested as one rhythmic pipeline (`tests/ClockSeqNodesTests.cpp`). |
| **Data Foundations** — done | `data.scale`, `data.table`, `data.lookup` | `data.lookup` literally reads what `data.table`/`data.scale` produce — the pathfinder for the whole Data-publishing pipeline (`tests/DataFoundationsNodesTests.cpp`, 18 cases including two real compiled-graph round trips). |
| **Note Stream** — done (6/9; `hold`/`select`/`chord` blocked on a real engine limit) | `note.gate`, `note.value`, `note.transpose`, `note.filter`, `note.humanize`, `note.quantize` | All consume/produce `Note`, tested together (`tests/NoteStreamNodesTests.cpp`, including a real compiled-graph round trip through `data.scale → note.quantize → note.value`). |
| **Domain Extensions** — done | `instance.allocate.swarmPopulation`, `instance.allocate.swarmTransient`, `instance.allocate.trigger` | Share the same instance-context/lifetime runtime machinery `instance.allocate.voice` already proved out. |
| **PM Core** — done | `excite.impulse`, `excite.pluck`, `excite.mallet`, `resonator.comb`, `resonator.modal`, `resonator.string`, `resonator.plate`, `data.material` | The basic excite→resonate pairs (Struck Body, Karplus-Strong) — designed to plug straight into each other; `data.material` feeds `resonator.modal` directly, moved here from the now-closed Data Foundations batch (a stray tag in an earlier pass — it was never one of that batch's 3 actual members). `excite.burst`'s own MVP top-off (`tone`/`shape`) deliberately NOT included — a separate, smaller follow-up, not scoped into this batch. |
| **PM Friction/Breath** | `excite.stickSlip`, `excite.breath`, `excite.contact`, `resonator.tube` | Friction/breath-driven excitation, tested against tube/string for Bowed String / Breath-Wind. |
| **PM Voice** | `osc.glottal`, `excite.vocalFolds`, `resonator.junction`, `resonator.tract`, `filter.formant` | Correction 1's vocal-modelling cluster — the hardest batch, targets the "cat purr" reference patch. Build last within Physical Modelling. |
| **Noise & Grain** | `noise.colored`, `noise.dust`, `sampler.granular` | Stochastic/granular sources sharing test approach. |
| **Sampler** | `sampler.player`, `data.load` | `sampler.player` needs what `data.load` produces. |
| **Shaping** | `shape.rectify`, `shape.crush`, `shape.waveshaper`, `shape.fold` | All single-in/single-out nonlinear audio shapers — one shared distortion-test harness. `shape.clip` (its own former member) already shipped separately, direct feedback. |
| **Space** | `space.diffuser`, `space.reverb` | Spatial effects; diffuser reuses `filter.allpass`. |
| **Env/LFO Shapes** | `env.curve`, `lfo.shape` | Both are "`Data(curve)`-driven modulation source" nodes — same dependency, same shape. |
| **Analysis** | `analysis.level`, `analysis.onset`, `analysis.centroid`, `analysis.pitch`, `data.analyseModes` | Audio-input analysis nodes tested together; `note.assemble` (its originally-planned capstone) is already built — this batch's job now is just feeding it a real tracked pitch instead of an algorithmic one. |
| **EQ/Curve Data** | `data.record`, `data.eqToCurve` | Small utility `Data` producers, low mutual dependency but similar scope/size. |
| **Factories** | `factory.material`, `factory.curve`, `factory.eq`, `factory.wave`, `factory.sample`, `factory.notes` | Share the Content/custom-editor infrastructure — build as one wave once that infra exists. |

Nodes without a batch (`filter.svf`'s redesign) are self-contained enough that
batching them buys nothing (`util.macro`, the other former member of this list,
is done — `wiki/plans/UtilMacro.md`).

---

## Nodes to build next

Ordered recommendation — not a strict necessity-letter sort, because dependency order
matters more than the letter for a couple of "C" items:

1. ~~**Clock+Seq**~~ — **done.** `clock.pulse`/`clock.divide`/`clock.counter`/
   `seq.euclid`/`seq.steps` all built and tested (`tests/ClockSeqNodesTests.cpp`, 21
   cases). `seq.steps` shipped as MVP (two documented, deliberate spec deviations —
   see its own row above); the other four fully match the catalog.
2. ~~**Data Foundations**~~ — **done.** `data.scale`/`data.table`/`data.lookup` all
   built and tested (`tests/DataFoundationsNodesTests.cpp`, 18 cases, including two
   real compiled-graph round trips proving the new `GraphCompiler.cpp` wiring, not just
   direct node-to-node C++ calls). Pulled forward ahead of its nominal "C" letter, as
   planned — it was the right call: `note.quantize`, `env.curve`, `lfo.shape`, and
   `shape.waveshaper`'s custom-curve mode are all unblocked by it now. `data.scale`
   really was the pathfinder — the whole `Data`-publishing pipeline
   (`Node::getDataPublisher()`/`setDataInput()`, `GraphCompiler.cpp`'s Data-connection
   branch) exists because of this step, not the other way around.
3. ~~**Note Stream**~~ — **done, 6 of 9, plus its `note.assemble` follow-up.**
   `note.gate`/`note.value`/`note.transpose`/`note.filter`/`note.humanize`/
   `note.quantize` all built and tested (`tests/NoteStreamNodesTests.cpp`, 17 cases).
   Unblocked by step 2's `data.scale` as planned — `note.quantize` is real, tested, and
   genuinely reads a live `data.scale` buffer. **A real finding, not in the original
   plan**: `note.hold`/`note.select`/`note.chord` hit a previously-unexercised engine
   limit (one `Note` input/output per node, max — `ExecutionPlan::BlockStep`'s own
   fixed shape) that the catalog's literal `note.filter` also hit (two `Note` outputs)
   — `note.filter` got a clean one-output redesign; the other three were deferred
   outright rather than forced through with a misleading shape. A real design for
   multi-note `Note` signals is separate, larger engine work, not scoped here.
   **`note.assemble`** — originally step 10's own capstone, moved up and built here
   instead on direct feedback identifying it as the single highest-leverage gap in the
   whole family (the only node that can PRODUCE a `Note` stream from scratch); it
   doesn't actually need `analysis.pitch` to be useful (accepts any `pitch [audio]`),
   so building it out of order cost nothing.
4. ~~**Domain Extensions**~~ — **done.** `instance.allocate.swarmPopulation`/
   `swarmTransient`/`trigger` all built and tested (`tests/InstanceSwarmPopulationNodeTests.cpp`/
   `InstanceSwarmTransientNodeTests.cpp`/`InstanceTriggerNodeTests.cpp` + matching
   `tests-plugin/` integration suites). Reused proven `instance.allocate.voice`
   machinery as planned — `VoiceManager`, the generalized `InstanceOriginNode`
   interface (new, Batch 1), and `09-28-InstanceAllocator.2`'s own
   `(seed, ordinal)` determinism all carried over with zero Voice regressions.
   Unlocks the Water/Cicada Field/percussive-one-shot stock groups.
5. ~~**`util.macro`**~~ — **done.** `wiki/plans/UtilMacro.md`/ADR-0030: a real,
   wireable node (not the hand-curated side-table ADR-0015 originally rejected),
   plus the drag-a-port-out-to-create-a-macro UI gesture and the top-bar knob
   panel.
6. ~~**PM Core**~~ — **done, 2026-10-02 (all 4 batches).** `excite.impulse`,
   `resonator.comb`, `data.material`, `resonator.modal`, `excite.pluck`,
   `resonator.string`, `excite.mallet`, `resonator.plate` all built and tested
   (`tests/PMCoreNodesTests.cpp`, 48 cases total across the 4 batches) — the first
   real "personality" sounds beyond subtractive synthesis, the flagship playable
   Karplus-Strong string, and a real, production-proven cross-node feedback cycle
   (`excite.mallet`↔`resonator.string`, the first one this engine has ever compiled
   outside a synthetic test). `excite.burst`'s own `tone`/`shape` MVP gap was
   explicitly left open, as planned — a separate, smaller follow-up, not part of
   this batch's own scope.
7. **Shaping** + **Noise & Grain** — cheap wins (mostly difficulty 1–2), round out the
   effects/sources palette while PM Core's more delicate nodes are being tuned.
8. **PM Friction/Breath** (`excite.stickSlip`/`breath`/`contact` + `resonator.tube`) —
   builds on PM Core's resonators, unlocks Bowed String / Breath-Wind.
9. **Sampler** (`data.load` + `sampler.player`) — real file-format work, best done
   once the Data pipeline (step 2) is already proven.
10. **Analysis** — `analysis.level`/`onset`/`centroid` first (cheaper), `analysis.pitch`
    next; `note.assemble`, its originally-planned capstone, is already built (step 3) —
    this batch's remaining job is giving it a real tracked pitch to consume instead of
    an algorithmic one. `data.analyseModes` fits here too (shares the FFT work).
11. **Space** (`space.diffuser` → `space.reverb`) and **Env/LFO Shapes** (`env.curve`,
    `lfo.shape`) — round out modulation/effects once their `Data(curve)` dependency
    (step 2) is real.
12. **`osc.wavetable`**, topping off `osc.analog`'s MVP gaps alongside it (Oscillator
    Expansion) — both consume the Data pipeline.
13. **`filter.svf`'s redesign** — standalone structural work, schedule whenever a gap
    in the calendar allows; not gated by anything above.
14. **PM Voice** (`osc.glottal`, `excite.vocalFolds`, `resonator.junction`,
    `resonator.tract`, `filter.formant`) — deliberately last within physical
    modelling: hardest batch, most rounding/determinism-sensitive to test (see
    `wiki/NODES.md`'s own testing note on the cat-purr reference patch), and benefits
    from every earlier PM batch's test infrastructure already existing.
15. **EQ/Curve Data** (`data.record`, `data.eqToCurve`) — low urgency, small scope,
    fits in a gap anywhere after step 2.
16. **Factories** (all six `factory.*`) — last, deliberately: `wiki/NODES.System.md`
    §8 already defers this whole family, `factory.notes` additionally needs the
    entire `note.*`/`clock.*`/`seq.*` arc (steps 1+3) done first, and none of it
    blocks anything else in this list.

---

## Appendix — Part B stock groups (`stock.*`)

Not primitive nodes — shipped, editable compositions built from Part A. Tracked here
only as a rough "what becomes buildable once its ingredients exist" cross-reference,
not with the A/B/C/D/Batch treatment above.

| Group | Status | Blocked on |
|---|---|---|
| **Init Patch** | ✅ real hand-built graph (not yet a loadable `stock.*` asset) | Nothing for the graph itself; needs `stock.*` loading (M29) and `space.reverb` for its tail. |
| **Karplus-Strong** | ✅ buildable now | `shape.clip` Implemented (2026-10-03) — every ingredient this patch uses is now real. |
| **Scale Quantize** | ✅ buildable now | `data.scale` and `note.quantize` both Implemented. |
| **Arpeggiator** | 📋 — blocked | `clock.pulse`/`clock.counter` Implemented, but `note.hold`/`note.select` hit the real one-Note-port-per-node engine limit (deferred, not just unbuilt). |
| **Chord** | 📋 — blocked | `data.scale` Implemented, but `note.chord` hits the same real engine limit as `note.hold`/`note.select`. |
| **Bubble** | 📋 | `env.curve` (Env/LFO Shapes). |
| **Water** | 📋 | `noise.dust`, `instance.allocate.swarmTransient` (step 4), Bubble. |
| **Crackle** | 📋 — only `noise.dust` missing | `resonator.modal`/`data.material`/`excite.burst` all Implemented now (PM Core done); just `noise.dust` (Noise & Grain) left. |
| **Scrape** | 📋 | `excite.contact` (PM Friction/Breath), `space.reverb` — `resonator.modal`/`data.material` now Implemented (PM Core done). |
| **Cicada** | 📋 | `clock.pulse`/`resonator.modal` now Implemented — `excite.burst` already MVP-usable; still needs `filter.formant` (PM Voice). |
| **Cicada Field** | 📋 | `instance.allocate.swarmPopulation` (step 4), `random.drift` (already Implemented), Cicada. |
| **Breath / Wind** | 📋 | `excite.breath`, `resonator.tube` (PM Friction/Breath), `env.curve`. |
| **Bowed String** | 📋 — only `excite.stickSlip` missing | `resonator.string` now Implemented (PM Core done, with a real `motion` output ready for the coupling); just `excite.stickSlip` (PM Friction/Breath) left. |
| **Struck Body** | ✅ buildable now | `excite.mallet` and `resonator.plate`/`modal` all Implemented (PM Core done, 2026-10-02) — the real coupled `excite.mallet`↔`resonator.string` cycle is proven end to end by a real compiled-graph test, same mechanism this patch's own `resonator.plate`/`modal` pairing would use. |
| **Hex Guitar Front End** | 📋 | `analysis.onset`, `analysis.pitch` (Analysis batch) — `note.assemble` itself is now Implemented. |
| **Voiced self-oscillation (cat purr)** | 📋 | The entire PM Voice batch, plus `env.curve`/`random.drift`/`lfo.shape`/`mix.crossfade` (all either Implemented or earlier batches). Hardest reference patch in the catalog. |
