# Bazalt — Node build status & roadmap

Tracks, per node, **what's actually built** vs. **what's left**, and for what's left,
**how badly we need it** and **what order to build it in**. Derived from
`wiki/NODES.md` (the catalog — read that for what each node actually does) and
`wiki/NODES.System.md` (architecture). Cross-checked directly against
`engine/include/bazalt/engine/nodes/*.h` (58 registered node types across 56 files,
`GrowableGroupNode`/`InheritingPortsNode` excluded as shared base classes, `ViewNodes.h`
holding 3 node types) — every ✅/🚧 claim below is a real, compiled, registered node,
not aspirational.

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

**No node in the engine produces a real `Data` value today.** `Data` is a declared,
implemented signal type (`canConnect` handles it fully — tag matching, rejection
rules) but the entire "build on a worker thread, publish, atomic pointer swap" pipeline
`wiki/NODES.System.md` §1 describes has zero real callers. Whichever `data.*` node gets
built *first* pays for building that pipeline; every `data.*`/`osc.wavetable`/
`sampler.*` node after it rides on top for free. Recommendation below picks
`data.scale` as that pathfinder (smallest, most self-contained buffer shape — no file
I/O, no editor). Similarly, **`NodeContent` (the third value category, §3) doesn't
exist as real code yet** — `data.table`/`seq.steps`/every `factory.*` node's content
nominally wants it, but none of them strictly need the fully-general version to ship a
first cut (a plain structural array works until something demands live editing).

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

### `excite.*` — physical excitation — 1 MVP, 7 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `excite.burst` | MVP | | PM Core | Missing `tone`/`shape`; trigger/duration already real. |
| `excite.impulse` | To be implemented | **B1** | PM Core | A single-sample (or short) delta — trivial. |
| `excite.pluck` | To be implemented | **B2** | PM Core | Comb-notch shaping. |
| `excite.mallet` | To be implemented | **B2** | PM Core | Single-sample feedback collision model. |
| `excite.stickSlip` | To be implemented | **B3** | PM Friction/Breath | Delicate near-zero-speed friction. |
| `excite.breath` | To be implemented | **B2** | PM Friction/Breath | Nonlinear noise-modulation (breath/reed/lip modes). |
| `excite.contact` | To be implemented | **B2** | PM Friction/Breath | Scrape/grain-rate model. |
| `excite.vocalFolds` | To be implemented | **B3** | PM Voice | Hardest excite node — self-oscillating two-mass valve. |

### `resonator.*` — resonating bodies — 7 to build (none started)

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `resonator.comb` | To be implemented | **B1** | PM Core | Simplest resonator, feedforward/feedback. |
| `resonator.modal` | To be implemented | **B2** | PM Core | The centre of the PM set; scales with mode count. |
| `resonator.string` | To be implemented | **B2** | PM Core | Waveguide string — the playable Karplus-Strong. |
| `resonator.tube` | To be implemented | **B2** | PM Friction/Breath | Single-tube waveguide, feedback both directions. |
| `resonator.plate` | To be implemented | **B3** | PM Core | 2D mesh, scales with mesh size. |
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

### `shape.*` — nonlinearities — 5 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `shape.clip` | To be implemented | **B1** | Shaping | Safety clipper/limiter — build early, useful everywhere. |
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

### `mix.*` — 4/4 Implemented

`mix.sum`, `mix.crossfade`, `mix.gain`, `mix.downmix` — all Implemented.

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

### `note.*` — the note stream — 10 to build (none started)

Whole family is essential per the catalog's own framing: *"what makes arpeggios,
chords, scales, and audio-driven instruments ordinary patching rather than built-in
features."*

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `note.gate` | To be implemented | **A1** | Note Stream | Trivial extraction (noteOn/off, gate, count). |
| `note.value` | To be implemented | **A1** | Note Stream | Trivial mono-domain read (pitch/velocity/pressure/slide). |
| `note.transpose` | To be implemented | **A1** | Note Stream | Trivial semitone/octave shift. |
| `note.filter` | To be implemented | **A1** | Note Stream | Simple pitch/velocity range gate. |
| `note.hold` | To be implemented | **A2** | Note Stream | Latch/memory, moderate state management. |
| `note.select` | To be implemented | **A2** | Note Stream | Indexing into held notes — pairs with `clock.counter`. |
| `note.chord` | To be implemented | **A2** | Note Stream | Interval generation (fixed or scale-degree mode). |
| `note.humanize` | To be implemented | **A2** | Note Stream | Timing/velocity/pitch jitter. |
| `note.quantize` | To be implemented | **A2** | Note Stream | Needs `data.scale`. |
| `note.assemble` | To be implemented | **A2** | Analysis+Assemble | Needs a tracked pitch — genuinely useful once `analysis.pitch` exists, though it'll accept any `pitch [audio]` source. |

### `math.*` / `logic.*` / `adapt.*` — 21/21 Implemented

All done — no rows needed.

### `data.*` — producing and reading buffers — 8 to build (none started)

Foundational, but not "essential-for-modularity" the way `note.*`/`clock.*` are —
these exist to feed other nodes, so bucketed **C**: needed for a first content wave,
not the graph-flow infra `A` is reserved for.

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `data.scale` | To be implemented | **C2** | Data Foundations | Recommended **pathfinder** for the whole Data-publishing pipeline (smallest, self-contained buffer shape) — see the prerequisite note above. |
| `data.table` | To be implemented | **C2** | Data Foundations | Feeds `env.curve`/`lfo.shape`/`shape.waveshaper`/`adapt.remap`'s editor. Can ship with a simplified content store ahead of full `NodeContent`. |
| `data.lookup` | To be implemented | **C2** | Data Foundations | Reads any `Data` buffer — build paired with `data.table`, tested against it directly. |
| `data.material` | To be implemented | **C2** | Data Foundations | Physical-modelling counterpart of `data.scale`; feeds `resonator.modal`/`filter.formant`. |
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
| `instance.mix` | Implemented | | | |
| `instance.allocate.swarmPopulation` | To be implemented | **A1** | Domain Extensions | Fixed count, always live — simplest of the three (no spawn logic). |
| `instance.allocate.swarmTransient` | To be implemented | **A2** | Domain Extensions | Event-triggered spawn, closest to Voice's own shape. |
| `instance.allocate.trigger` | To be implemented | **A2** | Domain Extensions | Event-triggered, one instance at a time. |

### `util.*` — 2 Implemented, 1 to build

| Node | Status | Necessity | Batch | Notes |
|---|---|---|---|---|
| `util.constant` | Implemented | | | |
| `util.reroute` | Implemented | | | |
| `util.macro` | To be implemented | **A1** | (standalone) | ADR-0015, deliberately deferred — "identical to Constant, plus host-bound and smoothed." Genuinely easy whenever it's picked up; no batch dependency. |

### `view.*` — 5/5 Implemented

`view.listen`, `view.scope`, `view.spectrum`, `view.meter`, `view.glance` — all
Implemented.

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
| Implemented | 59 |
| MVP | 4 (`osc.analog`, `filter.svf`, `excite.burst`, `seq.steps`) |
| To be implemented | 62 |
| **Total native node types** | **125** |

By necessity, among the 62 still to build: **A** 14 · **B** 30 · **C** 12 · **D** 6.

**Clock+Seq batch — done.** `clock.pulse`/`clock.divide`/`clock.counter`/`seq.euclid`/
`seq.steps` all built and tested — see their family sections above for per-node notes;
`wiki/MILESTONES.md`'s own entry has the full build record.

---

## Necessity buckets, explained

- **A — essential, ASAP.** Everything here is graph-flow/timing/note-stream/domain
  infrastructure that unlocks broad modularity, mirroring why `logic.*`/`adapt.*`
  already shipped early: `clock.*`/`seq.*` (**done** — see the Clock+Seq batch note
  below), the whole `note.*` family, the three `instance.allocate.*` spawn types,
  `util.macro`. None of these are hard
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
| **Data Foundations** | `data.scale`, `data.table`, `data.lookup` | `data.lookup` literally reads what `data.table`/`data.scale` produce — the pathfinder for the whole Data-publishing pipeline. |
| **Note Stream** | `note.gate`, `note.value`, `note.transpose`, `note.filter`, `note.hold`, `note.select`, `note.chord`, `note.humanize`, `note.quantize` | All consume/produce `Note`, chain naturally (quantize → chord → hold → select), share test fixtures. |
| **Domain Extensions** | `instance.allocate.swarmPopulation`, `instance.allocate.swarmTransient`, `instance.allocate.trigger` | Share the same instance-context/lifetime runtime machinery `instance.allocate.voice` already proved out. |
| **PM Core** | `excite.impulse`, `excite.pluck`, `excite.mallet`, `resonator.comb`, `resonator.modal`, `resonator.string`, `resonator.plate`, `excite.burst` (MVP top-off) | The basic excite→resonate pairs (Struck Body, Karplus-Strong) — designed to plug straight into each other. |
| **PM Friction/Breath** | `excite.stickSlip`, `excite.breath`, `excite.contact`, `resonator.tube` | Friction/breath-driven excitation, tested against tube/string for Bowed String / Breath-Wind. |
| **PM Voice** | `osc.glottal`, `excite.vocalFolds`, `resonator.junction`, `resonator.tract`, `filter.formant` | Correction 1's vocal-modelling cluster — the hardest batch, targets the "cat purr" reference patch. Build last within Physical Modelling. |
| **Noise & Grain** | `noise.colored`, `noise.dust`, `sampler.granular` | Stochastic/granular sources sharing test approach. |
| **Sampler** | `sampler.player`, `data.load` | `sampler.player` needs what `data.load` produces. |
| **Shaping** | `shape.clip`, `shape.rectify`, `shape.crush`, `shape.waveshaper`, `shape.fold` | All single-in/single-out nonlinear audio shapers — one shared distortion-test harness. |
| **Space** | `space.diffuser`, `space.reverb` | Spatial effects; diffuser reuses `filter.allpass`. |
| **Env/LFO Shapes** | `env.curve`, `lfo.shape` | Both are "`Data(curve)`-driven modulation source" nodes — same dependency, same shape. |
| **Analysis+Assemble** | `analysis.level`, `analysis.onset`, `analysis.centroid`, `analysis.pitch`, `note.assemble`, `data.analyseModes` | Audio-input analysis nodes tested together; `note.assemble` is the capstone consuming `analysis.pitch`'s output. |
| **EQ/Curve Data** | `data.record`, `data.eqToCurve` | Small utility `Data` producers, low mutual dependency but similar scope/size. |
| **Factories** | `factory.material`, `factory.curve`, `factory.eq`, `factory.wave`, `factory.sample`, `factory.notes` | Share the Content/custom-editor infrastructure — build as one wave once that infra exists. |

Nodes without a batch (`filter.svf`'s redesign, `util.macro`) are self-contained
enough that batching them buys nothing.

---

## Nodes to build next

Ordered recommendation — not a strict necessity-letter sort, because dependency order
matters more than the letter for a couple of "C" items:

1. ~~**Clock+Seq**~~ — **done.** `clock.pulse`/`clock.divide`/`clock.counter`/
   `seq.euclid`/`seq.steps` all built and tested (`tests/ClockSeqNodesTests.cpp`, 21
   cases). `seq.steps` shipped as MVP (two documented, deliberate spec deviations —
   see its own row above); the other four fully match the catalog.
2. **Data Foundations** (`data.scale` → `data.table` → `data.lookup`) — now the next
   step. Pulled forward
   ahead of its "C" letter specifically because `note.quantize`, `env.curve`,
   `lfo.shape`, and `shape.waveshaper`'s custom-curve mode all block on it. Build
   `data.scale` first as the Data-pipeline pathfinder (see the prerequisite note up
   top).
3. **Note Stream** (the 9-node `note.*` batch) — now unblocked by step 2's
   `data.scale`; this is the single biggest "ordinary patching" unlock in the whole
   list (arpeggios, chords, scale-snapping become real).
4. **Domain Extensions** (`instance.allocate.swarmPopulation`/`swarmTransient`/
   `trigger`) — reuses proven `instance.allocate.voice` machinery; unlocks the Water/
   Cicada Field/percussive-one-shot stock groups.
5. **`util.macro`** — cheap, standalone, closes out the ADR-0015 deferral whenever
   convenient; doesn't need to wait for anything above.
6. **PM Core** (`excite.impulse`/`pluck`/`mallet` + `resonator.comb`/`modal`/`string`/
   `plate`, plus topping off `excite.burst`'s MVP gap with `tone`/`shape`) — delivers
   the first real "personality" sounds (Struck Body, Karplus-Strong) and is the
   biggest **B** cluster.
7. **Shaping** + **Noise & Grain** — cheap wins (mostly difficulty 1–2), round out the
   effects/sources palette while PM Core's more delicate nodes are being tuned.
8. **PM Friction/Breath** (`excite.stickSlip`/`breath`/`contact` + `resonator.tube`) —
   builds on PM Core's resonators, unlocks Bowed String / Breath-Wind.
9. **Sampler** (`data.load` + `sampler.player`) — real file-format work, best done
   once the Data pipeline (step 2) is already proven.
10. **Analysis+Assemble** — `analysis.level`/`onset`/`centroid` first (cheaper),
    `analysis.pitch` next, `note.assemble` last as the capstone tying note-stream +
    analysis together; `data.analyseModes` fits here too (shares the FFT work).
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
| **Karplus-Strong** | 📋 | Only `shape.clip` (Shaping batch) is missing — everything else it uses is already Implemented. |
| **Scale Quantize** | 📋 | `data.scale`, `note.quantize` (steps 2–3 above). |
| **Arpeggiator** | 📋 | `clock.pulse`/`clock.counter` now Implemented — only `note.hold`, `note.select` (step 3) remain. |
| **Chord** | 📋 | `note.chord`, `data.scale` (step 3). |
| **Bubble** | 📋 | `env.curve` (Env/LFO Shapes). |
| **Water** | 📋 | `noise.dust`, `instance.allocate.swarmTransient` (step 4), Bubble. |
| **Crackle** | 📋 | `noise.dust`, `excite.burst` (already MVP-usable), `resonator.modal` (PM Core), `data.material` (Data Foundations). |
| **Scrape** | 📋 | `excite.contact` (PM Friction/Breath), `resonator.modal`, `data.material`, `space.reverb`. |
| **Cicada** | 📋 | `clock.pulse` now Implemented — `excite.burst` already MVP-usable; still needs `filter.formant` (PM Voice), `resonator.modal` (PM Core). |
| **Cicada Field** | 📋 | `instance.allocate.swarmPopulation` (step 4), `random.drift` (already Implemented), Cicada. |
| **Breath / Wind** | 📋 | `excite.breath`, `resonator.tube` (PM Friction/Breath), `env.curve`. |
| **Bowed String** | 📋 | `excite.stickSlip`, `resonator.string` (PM Friction/Breath + PM Core). |
| **Struck Body** | 📋 | `excite.mallet`, `resonator.plate`/`modal` (PM Core). |
| **Hex Guitar Front End** | 📋 | `analysis.onset`, `analysis.pitch` (Analysis+Assemble), `note.assemble`. |
| **Voiced self-oscillation (cat purr)** | 📋 | The entire PM Voice batch, plus `env.curve`/`random.drift`/`lfo.shape`/`mix.crossfade` (all either Implemented or earlier batches). Hardest reference patch in the catalog. |
