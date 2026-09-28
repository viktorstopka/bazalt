# Bazalt — Node catalog

Replaces `archive_docs/NODE_CATALOG.md`. Shared rules (signal types, the value
contract, the connection/adapter matrix, domains, naming philosophy, factories) live
in `wiki/NODES.System.md` — read that first if something here doesn't make sense on
its own; this file is node specs, not architecture.

Folds in both correction docs (`archive_docs/NODE_CATALOG_Correction1.md`, physical
modelling; `archive_docs/NODE_CATALOG_Correction2_Factories.md`, factories) — where
either amends an existing entry, that's noted inline; nothing from either correction
is treated as ground truth without having been checked against what's actually
implemented.

## Status legend

**✅ Implemented** — real, registered, in `engine/include/bazalt/engine/nodes/`.
**🚧 Partial** — implemented but narrower than this spec (the gap is named).
**📋 Catalog only** — specified here, not built yet. Most of the catalog is this today
— **57 of the ~100+ node types below are catalog-only.** Don't assume a node works in
the running app because it's in this file; check the status marker.

## Notation

`portName : kind·quantity·range·curve·default` for `Control` ports. `Audio`, `Event`,
`Note`, `Data(tag)` for the others. `[audio]` marks a port that accepts audio-rate
modulation; unmarked control ports are block-rate (though see `NODES.System.md` §4 —
the real engine doesn't actually enforce this distinction yet, every Control port
already accepts either). Domain is `any` unless stated (`NODES.System.md` §5). **Taps**
are telemetry outputs for live visualization, not ports.

---

# Part A — native nodes (`core.*`, written without the prefix)

## Status index

| Family | Nodes | Status |
|---|---|---|
| `io.*` | audioIn, output, noteIn, control, transport | ✅ all 5 |
| `osc.*` | analog, sine, wavetable, **glottal** (Correction 1) | ✅ analog, sine — 📋 wavetable, glottal |
| `sampler.*` | player, granular | 📋 both |
| `noise.*` | colored, dust | 📋 both |
| `excite.*` | impulse, burst, pluck, mallet, stickSlip, breath, contact, **vocalFolds** (Correction 1) | ✅ burst — 📋 the other 7 |
| `resonator.*` | modal, string, tube, plate, comb, **junction**, **tract** (Correction 1) | 📋 all 7 |
| `filter.*` | svf, ladder, onepole, allpass, shelf, peak, formant, dcBlock | ✅ svf, ladder, onepole, allpass, shelf, peak, dcBlock — 📋 formant |
| `shape.*` | waveshaper, clip, fold, rectify, crush | 📋 all 5 |
| `delay.*` | line | ✅ |
| `space.*` | reverb, diffuser, pan, width | ✅ pan, width — 📋 reverb, diffuser |
| `mix.*` | sum, crossfade, gain, downmix | ✅ all 4 |
| `env.*` | adsr, curve, follower | ✅ adsr, follower — 📋 curve |
| `lfo.*` | shape | 📋 |
| `random.*` | stepped, drift | ✅ both |
| `clock.*` | pulse, divide, counter | 📋 all 3 |
| `seq.*` | steps, euclid | 📋 both |
| `note.*` | gate, value, quantize, transpose, chord, hold, select, humanize, filter, assemble | 📋 all 10 |
| `math.*` | add, subtract, multiply, divide, abs, clamp, minmax, power, round, modulo, slew | ✅ all 11 |
| `logic.*` | boolean, not, compare, toggle, select | ✅ all 5 |
| `adapt.*` | map, remap, normalise, threshold, sampleHold | ✅ all 5 |
| `data.*` | load, table, scale, material, analyseModes, lookup, **record**, **eqToCurve** (Correction 2) | 📋 all 8 |
| `analysis.*` | onset, pitch, level, centroid | 📋 all 4 |
| `instance.*` | allocator (Voice only), mix | 🚧 allocator (Voice ✅, Swarm/Trigger 📋 — M28) — ✅ mix |
| `util.*` | constant, macro, reroute | ✅ constant, reroute — 📋 macro (ADR-0015, deliberately deferred) |
| `view.*` | listen, scope, spectrum, meter, **glance** (new, 0.6) | ✅ all 5 |
| `factory.*` | eq, curve, wave, sample, notes, material (Correction 2) | 📋 all 6 |

---

## io — the plugin boundary

#### `io.audioIn` — Audio In ✅
Reads one of the plugin's real input buses. **Out:** `channel.0…channel.N` — `Audio`, a port group sized by the selected bus (stereo main gives 2, a six-channel bus gives 6). **Structural:** `bus` (enum: Main, Aux 1–4). **Behavior:** passthrough; silence when the host has not activated the bus. **Taps:** per-channel level. **Native:** the plugin I/O boundary.

#### `io.output` — Master Out ✅
**In:** `in` — `Audio` (`Channels::Stereo` — one real stereo cable). **Out:** `out` — `Audio` (`Channels::Stereo`, primary, `hidden` in the editor). **Behavior:** unity passthrough per channel. A mono source wired into `in` broadcasts to both physical output channels automatically (`canConnect`'s free mono→stereo rule, `NODES.System.md` §9) — every pre-stereo patch behaves unchanged. **The output port exists for the compiler only** (`NodeGraph::setOutput()` needs a real output port on the designated node to point at) and is never rendered as a wireable glyph in the editor (`PortDescriptor::hidden`) — a terminal "Master Out" node showing a further output you could drag a cable from was a real, caught-live UX bug, not a quirk to leave alone. The engine itself stays fully permissive; only the editor stops offering it. The `missing-ui-command` (`graphSetOutput`) bug `NODES_Gaps.md` flagged is unrelated to this node's own port shape and still needs its own fix. **Taps:** output scope, spectrum, level.

#### `io.noteIn` — Note In ✅
**Out:** `notes` — `Note`. **Structural:** `channel` (enum: Omni, 1–16, schema-only — every channel is accepted regardless), `mpeMode` (enum: off, MPE, schema-only). **Behavior:** translates host MIDI into `Note` events; pitch bend folds into the continuous pitch field rather than a separate port. **Native:** plugin I/O boundary.

#### `io.control` — MIDI Control ✅
**Out:** `value` — `float·Unipolar·0–1·linear·0`. **Structural:** `source` (enum: CC number, mod wheel, channel pressure, pitch bend, sustain pedal), `channel`. **Params:** `smoothing : float·Time·0–500ms·log·20ms`.

#### `io.transport` — Transport ✅
**Out:** `beat` — `Event`; `tempo` — `float·Frequency`; `playing` — `bool`; `position` — `float·Time`. Standalone runs an internal transport.

## osc — oscillators

#### `osc.analog` — Analog Oscillator 🚧
Band-limited virtual-analog oscillator. **In (spec):** `frequency [audio]`; `fine`; `pulseWidth [audio]`; `phaseMod [audio]`; `sync : Event`. **In (real today):** only `frequency`-equivalent (MIDI-note pitch) and a plain Hz frequency input — `fine`/`pulseWidth`/`phaseMod`/`sync` don't exist yet on the shipped node (tracked as a known gap, not silently assumed fixed). **Out:** `out` — `Audio`. **Structural:** `shape` (enum: sine, triangle, saw, square, pulse). **Behavior:** phase accumulator with PolyBLEP correction. **Native:** numerically delicate band-limiting.

#### `osc.sine` — Sine ✅
Cheap, alias-free sine. **In:** `frequency [audio]`; `phaseMod [audio]`; `sync : Event`. **Out:** `out` — `Audio`. **Native:** inner-loop primitive — FM stacks and modal excitation use many of these.

#### `osc.wavetable` — Wavetable Oscillator 📋
**In:** `table` — `Data(wavetable)`, required; `frequency [audio]`; `position [audio]` (frame scan); `phaseMod [audio]`; `sync : Event`. **Out:** `out` — `Audio`. **Structural:** `interpolation` (enum: none, linear, cubic), `frameBlending` (enum: blend, jump). **Native:** inner loop scales with table size.

#### `osc.glottal` — Glottal Pulse 📋 *(Correction 1)*
A parametric model of the airflow pulse from a vibrating valve (Liljencrants–Fant, or a simpler Rosenberg mode) — voice research's own measured parameters go in directly. **In:** `f0 : float·Frequency·10–2000Hz·log·110 [audio]`; `openQuotient : float·Unipolar·0.01–0.9·linear·0.5 [audio]` (very low = a purr); `asymmetry : float·Unipolar·0.1–0.9·linear·0.6`; `closureSharpness : float·Unipolar·0–1·linear·0.5 [audio]`; `jitter : float·Unipolar·0–1·linear·0`; `shimmer : float·Unipolar·0–1·linear·0`; `sync : Event`. **Out:** `out` — `Audio` (the flow derivative — the acoustic source); `flow` — `float·Unipolar [audio]` (for gating aspiration noise); `open` — `bool`. **Structural:** `model` (enum: LF, Rosenberg), `seed`. **Native:** delicate band-limited pulse generation; would take dozens of nodes to approximate.

## sampler — samples and grains

#### `sampler.player` — Sampler 📋
**In:** `sample` — `Data(sample)`, required; `trigger : Event`; `pitch [audio]`; `start`; `level [audio]`. **Out:** `out` — `Audio`; `ended` — `Event`. **Structural:** `loopMode`, `interpolation`, `direction`. **Native:** delicate interpolation, data-bound.

#### `sampler.granular` — Granular Cloud 📋
**In:** `sample` — `Data(sample)`, required; `position [audio]`; `density [audio]`; `grainSize`; `pitch [audio]`; `spray`; `pitchSpray`; `pan`; `panSpray`; `trigger : Event`. **Out:** `left`, `right` — `Audio`. **Structural:** `maxGrains` (default 64), `window`, `spawnMode`. **Native:** inner loop scales with grain count; preallocates the pool.

## noise — stochastic sources

#### `noise.colored` — Noise 📋
**In:** `tilt : float·Bipolar·−1–1·linear·0`. **Out:** `out` — `Audio`. **Structural:** `color` (enum: white, pink, brown, blue, violet).

#### `noise.dust` — Dust 📋
Sparse random impulses — the primitive behind crackle, rain, footsteps, a population of tiny events. **In:** `density [audio]`; `jitter`. **Out:** `pulse` — `Event`; `out` — `Audio`. **Structural:** `seed`. **Native:** sample-accurate event timing an LFO-and-threshold group can't reproduce without aliasing.

## excite — physical excitation

`excite.*` nodes produce a signal that drives a resonator; `resonator.*` nodes are the
resonating bodies. Friction/collision models that need to feel the resonator expose a
`feedback` input for that purpose (`NODES.System.md` §1's `Audio`-typed excitation
path).

#### `excite.impulse` — Impulse 📋
**In:** `trigger : Event`; `amplitude`; `width` (0 = a true single-sample delta). **Out:** `out` — `Audio`.

#### `excite.burst` — Noise Burst 🚧 *(partially fixed — wiki/NODES_Gaps.md's `hardcoded-trigger`)*
**In (spec):** `trigger : Event`; `duration`; `tone`; `shape`. **In (real today):** `trigger : Event` and `duration [audio]·0.1–2000ms·log·30ms` — a clock, a threshold detector, or anything else that produces Events can now start it; the direct C++ `trigger(int durationSamples)` poke still exists too (tests/tools), calling the same internal logic. `tone`/`shape` aren't built yet — still 🚧, not full catalog compliance. **Out:** `out` — `Audio` (white-noise burst, linear decay envelope over the triggered duration).

#### `excite.pluck` — Pluck 📋
**In:** `trigger : Event`; `position`; `hardness`; `amplitude`. **Out:** `out` — `Audio`. **Native:** comb-notch shaping, awkward to hand-wire per patch.

#### `excite.mallet` — Mallet / Collision 📋
**In:** `trigger : Event`; `velocity`; `mass`; `stiffness`; `feedback` — `Audio` (optional). **Out:** `out` — `Audio`; `contact` — `bool`. **Native:** single-sample feedback, numerically delicate.

#### `excite.stickSlip` — Stick-Slip / Bow 📋
**In:** `pressure [audio]`; `speed [audio]`; `roughness`; `feedback` — `Audio` (optional); `trigger : Event`. **Out:** `out` — `Audio`; `slipping` — `bool`. **Native:** single-sample feedback; delicate near zero speed.

#### `excite.breath` — Breath 📋
**In:** `pressure [audio]`; `turbulence`; `noiseColor`; `feedback` — `Audio` (optional). **Out:** `out` — `Audio`. **Structural:** `mode` (enum: breath, reed, lip). **Native:** single-sample feedback, delicate nonlinearity.

#### `excite.contact` — Contact / Scrape 📋
**In:** `speed [audio]`; `pressure [audio]`; `surface` — `Data(curve)` (optional); `grainSize`. **Out:** `out` — `Audio`; `contactRate` — `float·Frequency`.

#### `excite.vocalFolds` — Vocal Folds 📋 *(Correction 1)*
A self-oscillating two-mass valve — frequency **emerges** from pressure/mass/stiffness/geometry, not commanded, exactly like a real larynx. Produces behavior a parametric model can only imitate: an oscillation threshold, subharmonics and deterministic chaos from left–right asymmetry. **In:** `pressure : float·Pressure·bipolar·linear·0 [audio]` (below threshold, nothing oscillates; negative = inhalation, which behaves differently); `mass : float·Mass [audio]`; `stiffness : float·Stiffness [audio]` (near the model's own frequency, this is how entrainment happens); `adduction : float·Unipolar·0–1·linear·0.5 [audio]`; `asymmetry : float·Bipolar·−1–1·linear·0` (the route to subharmonics/roughness); `damping : float·Unipolar·0–1·linear·0.3`; `feedback` — `Audio` (pressure from the tract above; optional). **Out:** `out` — `Audio`; `flow` — `float·Unipolar [audio]`; `contact` — `bool`; `contactQuotient` — `float·Unipolar`; `oscillating` — `bool`; `f0` — `float·Frequency` (measured). **Structural:** `masses` (enum: one-mass, two-mass), `seed`. **Native:** single-sample feedback, numerically delicate, runtime state no subgraph can express; clamps rather than diverging, reports `oscillating = false` below threshold instead of producing noise.

## resonator — resonating bodies

#### `resonator.modal` — Modal Bank 📋
The centre of the physical-modelling set. **In:** `excite` — `Audio`; `modes` — `Data(modal-set)`, required; `pitch [audio]`; `decay`; `brightness`; `inharmonicity`; `position` (pickup point); `spread`. **Out:** `left`, `right` — `Audio`. **Structural:** `maxModes` (default 64). **Native:** inner loop scales with mode count; delicate resonant filters.

#### `resonator.string` — String 📋
A waveguide string, the playable version of Karplus-Strong. **In:** `excite` — `Audio`; `pitch [audio]`; `decay`; `damping`; `stiffness`; `position`; `release`. **Out:** `out` — `Audio`; `motion` — `Audio` (feeds back into `excite.stickSlip`/`excite.mallet`). **Native:** single-sample feedback; tuning/interpolation delicate.

#### `resonator.tube` — Tube 📋
**In:** `excite` — `Audio`; `length`; `damping`; `reflection`; `flare`. **Out:** `out` — `Audio`; `motion` — `Audio`. **Structural:** `endCondition` (enum: open, closed). **Native:** single-sample feedback in both directions.

#### `resonator.plate` — Plate / Membrane 📋
**In:** `excite` — `Audio`; `size`; `tension`; `decay`; `damping`; `positionX`, `positionY`. **Out:** `left`, `right` — `Audio`. **Structural:** `quality` (enum: low, medium, high). **Native:** inner loop scales with mesh size.

#### `resonator.comb` — Comb 📋
The cheap resonator, and the building block for hand-built feedback experiments. **In:** `in` — `Audio`; `frequency [audio]`; `feedback` (hard-limited below 1); `damping`. **Out:** `out` — `Audio`. **Structural:** `type` (enum: feedforward, feedback).

#### `resonator.junction` — Scattering Junction 📋 *(Correction 1)*
A multi-way junction where waveguides meet — a branch closed at its far end acts as a side cavity that removes energy at its own resonances, producing the spectral notches a plain band-pass filter cannot create. **In:** `in` — `Audio`; port group `branch.0…branch.N` — `Audio` (growable, min 2, max 8, bidirectional); `impedance.0…N [audio]` (modulating one is opening/closing a valve, e.g. a soft palate); `loss`. **Out:** `out` — `Audio`; the branch group returns reflected waves. **Structural:** `branches` (2–8). **Native:** single-sample feedback across several paths (Kelly–Lochbaum scattering).

#### `resonator.tract` — Tract 📋 *(Correction 1)*
A multi-section waveguide whose cross-section profile is read from a curve, so formants arise from tube shape instead of being dialled in on filters — a vocal tract, a bore, a duct or a horn, all the same node. **In:** `in` — `Audio`; `shape` — `Data(curve)` (cross-sectional area along the tube; neutral default when unconnected); `length [audio]` (body size); `damping`; `radiation` (how open the far end is; near zero = a closed side cavity). **Out:** `out` — `Audio` (radiated at the open end); `motion` — `Audio` (pressure at the input end, for feeding back into an excitation node). **Structural:** `sections` (8–64), `maxLength`. **Native:** single-sample feedback along the whole chain; inner loop scales with section count.

## filter

#### `filter.svf` — State-Variable Filter 🚧
**In:** `in` — `Audio`; `cutoff [audio]`; `resonance [audio]`; `drive`; `keyTrack`; `keyPitch`. **Out (spec):** simultaneous `lowpass`/`bandpass`/`highpass`/`notch`/`peak` port group. **Out (real today):** one mode-switched `out` (host-only `setType()`, no port) — the 5-simultaneous-output redesign is a known, documented gap, not fixed silently. **Structural:** `slope` (enum: 12, 24 dB/oct). **Native:** TPT zero-delay-feedback, numerically delicate.

#### `filter.ladder` — Ladder Filter ✅
**In:** `in`; `cutoff [audio]`; `resonance [audio]` (self-oscillates at 1); `drive`; `keyTrack`, `keyPitch`. **Out:** `out` — `Audio`. **Structural:** `poles` (1–4), `mode` (enum: lowpass, highpass, bandpass). **Behavior:** four-stage nonlinear ladder, closed-form ZDF solve (non-iterative, bounded per-sample cost). **Native:** numerically delicate.

#### `filter.onepole` — One-Pole ✅
**In:** `in`; `cutoff [audio]`. **Out:** `lowpass`, `highpass` — `Audio`. **Behavior:** the cheap damper used inside feedback loops; exact and allocation-free.

#### `filter.allpass` — Allpass / Dispersion ✅
**In:** `in`; `frequency [audio]`; `amount`. **Out:** `out` — `Audio`. **Structural:** `stages` (1–16, fixed-size array, no allocation). **Behavior:** phase rotation without amplitude change — what gives strings stiffness and reverbs diffusion.

#### `filter.shelf` — Shelf ✅
**In:** `in`; `frequency [audio]`; `gain` (shown in dB); `slope`. **Out:** `out`. **Structural:** `type` (enum: low, high).

#### `filter.peak` — Peak / Bell ✅
**In:** `in`; `frequency [audio]`; `gain`; `q`. **Out:** `out`.

#### `filter.formant` — Formant 📋 *(Correction 1 replaces this entry entirely)*
**In:** `in`; `vowel [audio]`; `formants` — `Data(modal-set)` (optional custom table); `antiformants` — `Data(modal-set)` (optional — the spectral notches a closed side cavity produces; without them, nasal sounds can't be imitated by filters); `antiformantDepth`; `shift`; `intensity`. **Out:** `out`. **Native:** inner loop over bands.

#### `filter.dcBlock` — DC Blocker ✅
**In:** `in`; `cutoff`. **Out:** `out`. **Behavior:** the classic tunable one-pole (`y[n] = x[n] − x[n−1] + R·y[n−1]`), trivial but essential wherever nonlinearities and feedback meet.

## shape — nonlinearities

#### `shape.waveshaper` — Waveshaper 📋
**In:** `in` — `Audio`; `drive [audio]`; `bias`; `mix`; `curve` — `Data(curve)` (optional). **Out:** `out` — `Audio`. **Structural:** `shape` (enum: tanh, arctan, sine fold, asymmetric, hard, custom), `oversampling`. **Native:** oversampling, delicate.

#### `shape.clip` — Clip / Safety 📋
**In:** `in`; `ceiling`; `knee`. **Out:** `out`; `clipping` — `bool`. **Structural:** `mode` (enum: hard, soft, limiter). **Behavior:** the node you put in a feedback loop so a slider can't destroy a speaker.

#### `shape.fold` — Wavefolder 📋
**In:** `in`; `drive [audio]`; `offset`; `folds`. **Out:** `out`. **Structural:** `oversampling`.

#### `shape.rectify` — Rectify 📋
**In:** `in`; `amount`. **Out:** `out`. **Structural:** `mode` (enum: half, full).

#### `shape.crush` — Bitcrush / Downsample 📋
**In:** `in`; `bits [audio]`; `rate [audio]`; `mix`. **Out:** `out`.

## delay

#### `delay.line` — Delay ✅
**In:** `in` — `Audio`; `time [audio]`; `feedback` (hard-limited); `damping`; `mix`. **Out:** `out` — `Audio`. **Structural:** `maxTime`, `interpolation` (enum: linear, allpass, cubic), `timeMode` (enum: free, tempo-synced, samples). **Native:** the delay at the heart of every feedback structure.

## space

#### `space.reverb` — Reverb 📋
**In:** `left`, `right` — `Audio`; `size`; `decay`; `damping`; `predelay`; `diffusion`; `modulation`; `lowCut`, `highCut`; `mix`. **Out:** `left`, `right` — `Audio`. **Structural:** `quality` (enum: low, medium, high — FDN size). **Native:** inner loop scales with network size; cheap enough at low quality to use per voice. **M28.**

#### `space.diffuser` — Diffuser 📋
**In:** `in`; `size`; `amount`. **Out:** `out`. **Structural:** `stages` (2–8). **Behavior:** an allpass chain — early reflections, transient smearing. **M28.**

#### `space.pan` — Pan ✅
**In:** `in` — `Audio`; `pan [audio]`; `width`. **Out:** `out` — `Audio` (`Channels::Stereo`, primary — one real stereo cable). **Structural:** `law` (enum: linear, −3dB, −4.5dB, constant power — default constant power). **Behavior:** `width` is an equal-power mid/side cross-mix of the already-panned pair; `width=1` is a true no-op. Feeds `io.output`'s `in` directly in the Init Patch — a fresh plugin instance opens playing genuinely panned stereo.

#### `space.width` — Width ✅
**In:** `in` — `Audio` (`Channels::Stereo`); `width`; `bassMonoBelow`. **Out:** `out` — `Audio` (`Channels::Stereo`, primary). **Behavior:** the same mid/side cross-mix `space.pan` uses, plus a one-pole crossover so `width` only touches the band above `bassMonoBelow` (keeps bass phase-coherent, an ordinary mastering-chain technique).

#### `stereo.split` — Stereo Split ✅
**In:** `in` — `Audio` (`Channels::Stereo`). **Out:** `left`, `right` — `Audio` (ordinary, independently-wireable mono outputs). **Behavior:** exact passthrough — re-exposes each side of a stereo cable as a separate mono signal, e.g. to send only one channel into a different filter. Adapters category, Pattern B inline DSP.

#### `stereo.combine` — Stereo Combine ✅
**In:** `left`, `right` — `Audio` (two ordinary, independently-wireable mono inputs). **Out:** `out` — `Audio` (`Channels::Stereo`, primary). **Behavior:** exact passthrough — the inverse of `stereo.split`; lets two unrelated mono sources feed one stereo-shaped destination (e.g. `io.output`'s `in`) as one cable. Adapters category, Pattern B inline DSP.

## mix

#### `mix.sum` — Mix ✅ *(fixed — wiki/NODES_Gaps.md's `redundant-composable-param`)*
**In:** port group `in.0…in.N` — `Audio` (growable, min 2, max 16). **Out:** `out` — `Audio`. **Behavior:** a plain `out = sum(in.i)` — the per-input `level.N` this used to bake in is gone; place a real `mix.gain` node in front of an input for that instead. Patch schema v4 + `PatchSerializer`'s v3→v4 migration preserve an old patch's non-default/connected `level.N` values as a real, spliced-in `mix.gain` node — never silently dropped.

#### `mix.crossfade` — Crossfade ✅
**In:** `a`, `b` — `Audio`; `position [audio]`. **Out:** `out`. **Structural:** `law` (enum: linear, equal power).

#### `mix.gain` — Gain ✅ *(fixed — wiki/NODES_Gaps.md's `jargon-naming` + `modulation-only-port`)*
**In:** `audio` — `Audio`; `gain [audio]·0–4×·log·1` (displayed in dB). **Out:** `out` — `Audio`. **Behavior:** audio-rate `gain` makes it a ring modulator too. Now titled "Gain" in the running engine (was "VCA"), and `gain` has a real unconnected default (unity, 1.0) — leaving it unpatched is genuinely "just as loud as before."

#### `mix.downmix` — Downmix ✅
**In:** `in` — `Audio` (`Channels::Stereo`). **Out:** `out` — `Audio` (mono). **Structural:** `mode` (enum: sum, left, right, mid, side). Genuine 1-in-1-out shape now (`NODES.System.md` §9.2) — `connectWithAutoAdapt` auto-inserts it for a stereo source into a mono-only port, the same way `adapt.map`/`adapt.normalise`/`adapt.threshold` already do.

## env — envelopes

#### `env.adsr` — Envelope ✅
**In:** `gate : bool`; `trigger : Event`; `delay`; `attack [audio]`; `hold`; `decay`; `sustain`; `release`; `velocity`. **Out:** `out` — `float·Unipolar·0–1 [audio]`; `finished` — `Event`. **Structural:** `attackCurve`/`decayCurve`/`releaseCurve` (enum: linear, exponential, logarithmic, s-curve), `mode` (enum: normal, loop, one-shot).

#### `env.curve` — Curve Envelope 📋
**In:** `curve` — `Data(curve)`, required; `trigger : Event`; `gate : bool`; `time`; `sustainPoint`. **Out:** `out` — `float·Unipolar [audio]`; `finished` — `Event`. **Behavior:** plays a drawn multi-segment shape.

#### `env.follower` — Envelope Follower ✅
**In:** `in` — `Audio`; `attack`; `release`. **Out:** `out` — `float·Unipolar·0–1 [audio]`. **Structural:** `detection` (enum: peak, RMS). **Behavior:** independent attack/release one-pole ballistics — the Audio → Control adapter, by hand (not auto-inserted; see `NODES.System.md` §4's matrix).

## lfo

#### `lfo.shape` — LFO 📋
**In:** `rate [audio]`; `shape`/`shapeB` — `Data(curve)` (optional); `shapeMorph [audio]`; `phase`; `sync : Event`; `depth`; `smooth`; `fade`. **Out:** `out` — `float·Bipolar [audio]`; `phaseOut`; `cycle` — `Event`. **Structural:** `waveform` (enum: sine, triangle, saw, ramp, square, random step, random smooth, custom), `rateMode`, `retrigger`. **M24.**

## random — controlled unpredictability

#### `random.stepped` — Random ✅
**In:** `trigger : Event` (free-runs at `rate` when unconnected); `rate [audio]`; `amount`; `smooth` (0 = hard steps, 1 = fully glided); `bias`; `spread`; `steps` (0 = continuous); `chance`. **Out:** `out` — `float·Bipolar [audio]`; `changed` — `Event`. **Structural:** `distribution` (enum: uniform, gaussian, exponential, bimodal), `seed`, `polarity`.

#### `random.drift` — Drift ✅
Slow, correlated, natural wander. **In:** `rate`; `amount`; `centering` (0 = free Brownian walk, 1 = strongly pulled to centre — literally the leak coefficient of a one-pole leaky-integrated walk). **Out:** `out` — `float·Bipolar [audio]`. **Structural:** `spectrum` (enum: brown, pink, white-filtered), `seed`. **Behavior:** clamped to stay in range.

## clock and seq

#### `clock.pulse` — Clock 📋
**In:** `rate [audio]`; `swing`; `jitter`; `run : bool·true`; `reset : Event`. **Out:** `tick` — `Event`; `phase`. **Structural:** `rateMode`, `division`. **M24.**

#### `clock.divide` — Divide 📋
**In:** `tick : Event`; `divide`; `reset : Event`. **Out:** `tick` — `Event`. **M24.**

#### `clock.counter` — Counter 📋
**In:** `tick : Event`; `reset : Event`; `length`; `step`. **Out:** `index`; `normalised`; `wrapped` — `Event`. **Structural:** `mode` (enum: up, down, ping-pong, random). **Behavior:** the generic sequencer engine — with `note.select`, an arpeggiator; with `data.lookup`, a step sequencer. **M24.**

#### `seq.steps` — Step Sequencer 📋
**In:** `tick : Event`; `reset : Event`; `steps` — `Data(curve)` (optional, otherwise the node's own editable step data — see `NODES.System.md` §3's content-category note on this). **Out:** `value` — `float·Bipolar [audio]`; `gate`; `trigger` — `Event`; `index`. **Structural:** `length` (1–64), `range`. **M24.**

#### `seq.euclid` — Euclidean 📋
**In:** `tick : Event`; `steps`; `pulses`; `rotate`; `reset : Event`. **Out:** `trigger` — `Event`; `gate`. **M24.**

## note — the note stream

This family is what makes arpeggios, chords, scales, and audio-driven instruments
ordinary patching rather than built-in features. **Entirely catalog-only today — the
whole `note.*` family is M25 scope.**

#### `note.gate` — Note Gate 📋
**In:** `notes` — `Note`. **Out:** `noteOn`/`noteOff` — `Event`; `gate` — `bool`; `count`.

#### `note.value` — Note Value 📋
**In:** `notes` — `Note`. **Out:** `pitch`; `velocity`/`pressure`/`slide`. **Structural:** `select` (enum: last, lowest, highest, first). **Behavior:** the mono-domain way to read a note stream. Inside an instanced region, the allocator's own outputs are used instead.

#### `note.quantize` — Scale Quantize 📋
**In:** `notes`; `scale` — `Data(scale)`, required; `root`; `strength`. **Out:** `notes`. **Structural:** `direction`, `applyTo`.

#### `note.transpose` — Transpose 📋
**In:** `notes`; `semitones`; `octaves`. **Out:** `notes`.

#### `note.chord` — Chord 📋
**In:** `notes`; `spread`; `velocityFalloff`; port group `interval.0…interval.N`. **Out:** `notes`. **Structural:** `mode` (enum: fixed intervals, scale degrees).

#### `note.hold` — Hold Memory 📋
**In:** `notes`; `hold : bool` (latch); `clear : Event`. **Out:** `held` — `Note`; `count`. **Structural:** `order`, `maxHeld`.

#### `note.select` — Select Note 📋
**In:** `held` — `Note`; `index`; `trigger : Event`. **Out:** `notes`; `pitch`. **Structural:** `wrap`, `gateLength`.

#### `note.humanize` — Humanize 📋
**In:** `notes`; `timing`; `velocity`; `pitch`. **Out:** `notes`. **Structural:** `seed`.

#### `note.filter` — Note Filter 📋
**In:** `notes`; `lowPitch`/`highPitch`; `lowVelocity`/`highVelocity`. **Out:** `pass`/`reject` — `Note`.

#### `note.assemble` — Assemble Note 📋
**In:** `trigger : Event`; `release : Event` (optional); `pitch [audio]`; `velocity`; `confidence`; `confidenceGate`. **Out:** `notes` — `Note`. **Behavior:** turns detected events + a tracked pitch into a real note stream — what makes an audio input playable as an instrument.

## math — all ✅

All take and return `Control`; quantity is inherited from the first connected input;
mismatched quantities are rejected by `canConnect`.

| Node | Ports | Notes |
|---|---|---|
| `math.add` | `in.0…in.N` (growable, min 2) | |
| `math.subtract` | `a`, `b` | |
| `math.multiply` | `in.0…in.N` (growable) | |
| `math.divide` | `a`, `b`, `safeZero : bool·true` | division by zero returns 0, not NaN |
| `math.abs` | `in` | |
| `math.clamp` | `in`, `low`, `high` | |
| `math.minmax` | `a`, `b`; structural `mode` (min/max) | |
| `math.power` | `in`, `exponent` | curve shaping for modulation |
| `math.round` | `in`, `step`; structural `mode` (floor, ceil, nearest) | |
| `math.modulo` | `in`, `divisor` | wrapping phase, cycling indices |
| `math.slew` | `in`, `rise`, `fall` | portamento, smoothing |

## logic — all ✅

| Node | Ports | Notes |
|---|---|---|
| `logic.boolean` | `in.0…in.N` (growable); structural `op` (AND/OR/XOR/NAND/NOR) | one node, not five |
| `logic.not` | `in` → `out` (bool) | |
| `logic.compare` | `a`, `b`, `tolerance`; structural `op` | `=` uses `tolerance`, not exact float equality |
| `logic.toggle` | `trigger`, `reset : Event` → `out` (bool) | |
| `logic.select` | `condition` (bool), `whenTrue`, `whenFalse` → `out` | audio crossfades over a few samples to avoid clicks |

## adapt — the conversion family — all ✅

These are the nodes `canConnect` inserts automatically where it can (see
`NODES.System.md` §4's matrix for exactly which pairs really auto-insert today vs.
still need placing by hand). Ordinary nodes the user can also place directly.

#### `adapt.map` — Map
**In:** `in` — `Control [audio]`; `inLow`/`inHigh`/`outLow`/`outHigh`; `curve`; `shape` — `Data(curve)` (optional). **Out:** `out`. **Structural:** `clip` (enum: clip, wrap, fold, none).

#### `adapt.remap` — Remap
The drawn-curve shaper with the editor in its own body — internally `data.table` +
`data.lookup`, exists for immediacy. **In:** `in [audio]`; `curve`/`curveB` — `Data(curve)` (optional); `morph [audio]`; `amount`. **Out:** `out`. **Structural:** its own drawn curve, `polarity`, `edgeMode`.

#### `adapt.normalise` — Normalise
**In:** `in` (real quantity), `low`, `high`. **Out:** `out` — `float·Unipolar`. The inverse of Map.

#### `adapt.threshold` — Threshold
**In:** `in`; `threshold` (default: mid-range of the source); `hysteresis`. **Out:** `rise`/`fall` — `Event`; `above` — `bool`. The Control → Event adapter.

#### `adapt.sampleHold` — Sample & Hold
**In:** `in`; `trigger : Event`; `glide`. **Out:** `out`.

## data — producing and reading buffers

Nothing else in the catalog can create a `Data` buffer — this family is a
prerequisite for the sampler, wavetable oscillator, every resonator, and scale
quantisation. **Entirely catalog-only today.**

#### `data.load` — Load File 📋
**Out:** `data` — `Data`, tagged by content. **Structural:** `file`, `interpretAs` (enum: sample, wavetable, impulse response), `frameSize`, `normalise`, `rootNote`. **M27.**

#### `data.table` — Table / Curve 📋
**Out:** `data` — `Data(curve)`. **Structural:** the curve itself, `resolution`, `loop`. **Behavior:** the shared, hand-drawn source of envelopes, surface profiles, sequencer lanes, LFO shapes, waveshaper transfer functions, remapping curves — editing it updates every place it's used. **Note:** its "structural" curve content is really the `NodeContent` third category (`NODES.System.md` §3) once that lands — a forward-looking clarification, not a live-code conflict, since this node isn't built yet.

#### `data.scale` — Scale 📋
**In:** `root`. **Out:** `data` — `Data(scale)`. **Structural:** `scale` (enum: major, the church modes, pentatonics, blues, whole tone, chromatic, harmonic series, custom), `customDegrees`, `octaveSize`. **M24.**

#### `data.material` — Material 📋
The physical-modelling counterpart of `data.scale`. **In:** `stiffness`; `density`; `damping`; `size`; `inharmonicity`; `irregularity`. **Out:** `data` — `Data(modal-set)`. **Structural:** `geometry` (enum: string, bar, tube, membrane, plate, irregular solid), `modeCount` (default 32), `preset` (enum: wood, glass, metal, stone, ceramic, bone, ice, custom), `seed`. **M23.**

#### `data.analyseModes` — Analyse Modes 📋
**In:** `data` — `Data(sample)`. **Out:** `data` — `Data(modal-set)`. **Structural:** `modeCount`, `windowStart`, `windowLength`, `decayEstimation`. **Behavior:** hit a rock, drop in the file, play the rock. **M23.**

#### `data.lookup` — Lookup 📋
**In:** `in [audio]`; `data` — `Data`, required; `dataB` — `Data` (optional morph target, same tag required); `morph [audio]`. **Out:** `out`. **Structural:** `mode` (enum: nearest, interpolate, index, wrap-index), `polarity`, `edgeMode`. **M24.**

#### `data.record` — Record 📋 *(Correction 2)*
**In:** `in` — `Audio`; `trigger : Event`; `stop : Event`; `threshold` (auto-start on signal); `maxLength`. **Out:** `data` — `Data(sample)` (published on stop); `recording` — `bool`; `level [audio]`. **Structural:** `preRoll`, `channels`. **Behavior:** writes into a buffer preallocated from `maxLength`; on stop, hands off to a worker thread that writes an immutable asset. **Native:** real-time capture with preallocation and thread handoff — nothing allocates on the audio thread, recording never blocks it.

#### `data.eqToCurve` — EQ to Curve 📋 *(Correction 2)*
**In:** `eq` — `Data(eq-curve)`; `lowFrequency`/`highFrequency` (span to render); `normalise : bool·true`. **Out:** `data` — `Data(curve)`. **Behavior:** renders an EQ's magnitude response as a plain curve so a shape designed in the EQ editor can drive a waveshaper/remap/tract profile — one-way, explicit, never automatic (see `NODES.System.md` §4's matrix).

## analysis 📋 (all — M25)

#### `analysis.onset` — Onset Detector
**In:** `in` — `Audio`; `sensitivity`; `holdOff`. **Out:** `onset` — `Event`; `strength`. **Structural:** `method` (enum: energy, spectral flux).

#### `analysis.pitch` — Pitch Tracker
**In:** `in`; `lowestPitch`; `smoothing`. **Out:** `pitch [audio]`; `confidence`; `voiced` — `bool`. **Structural:** `method` (enum: autocorrelation, YIN), `windowSize`.

#### `analysis.level` — Level
**In:** `in`; `attack`, `release`. **Out:** `level [audio]`; `peak`; `clipped` — `bool`. **Structural:** `detection` (enum: peak, RMS, true peak).

#### `analysis.centroid` — Brightness
**In:** `in`; `smoothing`. **Out:** `centroid`; `normalised`. **Behavior:** spectral centroid — how bright a signal is, for driving models from incoming sound.

## instance — domains

Full domain model (configurations, instance context, lifetime, events-across-boundary)
lives in `wiki/NODES.System.md` §5. Node specs only, here:

#### `instance.allocate.voice` — Voice ✅ *(renamed from `instance.allocator`, 09-28-InstanceAllocator.3; renamed again from `instance.voice`, 09-29-AddMenu.1, to make room for `instance.allocate.swarmPopulation`/`swarmTransient`/`trigger` as siblings under one Add-menu category)*
Opens an instanced region. Was "one node, several configurations" (a `configuration`
enum for Voice/Swarm-population/Swarm-transient/Trigger); three of those four options
never did anything, and the four don't even share a port shape (Voice needs a `Note`
`spawn` input, Swarm-population needs none at all) — real UI surface for a choice that
wasn't one. The dropdown is gone outright, not just defaulted: this node now does
exactly the one thing it ever actually did (Voice allocation), and Swarm-population/
Swarm-transient/Trigger will get their own real node types later, once actual
Swarm/Trigger runtime machinery exists — not empty shells bolted onto this one.
`wiki/reports/InstanceAllocator_2026-09-28.md` has the full reasoning. **In:** `spawn`
— `Note`. **Out** (all `polyOnly`): `gate`, `pitch`, `velocity`, `instanceIndex`,
`instanceAge`, `random1`, `random2`, `start`, `stop` — 9 ports. **Structural:**
`maxInstances`.

#### `instance.mix` — Voice Mix ✅
Closes an instanced region. **In:** `in` — `Audio`, `polyOnly`. **Out:** `out` —
`Audio` (mono domain). **Structural:** `mode` (enum: sum, average). Placeable
anywhere, more than once — though `DomainSplitter` allows only one allocator/one mix
region per graph today.

## util

#### `util.constant` — Constant ✅
**Out:** `out` — `Control`, contract configurable on the node.

#### `util.macro` — Macro 📋 *(deliberately deferred, ADR-0015)*
**Out:** `out` — `Control`. **Structural:** `slot` (one of 32 fixed host-automation slots), plus the exposed contract. Identical to Constant, plus host-bound and smoothed.

#### `util.reroute` — Reroute ✅
**In:** `in`. **Out:** `out` (adopts the source's signal type *and* quantity — a real, polymorphic port, not hardcoded Audio). Layout waypoint. "Not connectable" reports against this node are tracked as a UI-layer bug in `NODES_Gaps.md`, not a missing feature — the engine-side implementation reads correctly.

## view — listening and looking — all ✅

#### `view.listen` — Listen
**In:** `in` — `Audio`. **Behavior:** auditions this point in the graph, replacing normal output while active. **Taps:** `in`.

#### `view.scope` — Scope
**In:** `in` — `Audio` or `Control`. **Structural:** `timeWindow`, `triggerMode`.

#### `view.spectrum` — Spectrum
**In:** `in` — `Audio`. **Structural:** `fftSize`, `tilt`, `averaging`.

#### `view.meter` — Meter
**In:** `in` — `Audio` or `Control`. **Structural:** `mode` (enum: peak, RMS, true peak, histogram).

#### `view.glance` — Glance ✅ *(new, Milestone 0.6 — wiki/NODES_Gaps.md's `single-type-preview-coverage`)*
**In:** `in` — `Audio`, `Control`, `Boolean` or `Event` (polymorphic — adopts whatever's wired, same mechanism `util.reroute`/`view.scope`/`view.meter` use). **Out:** `out` — same type/quantity as `in`, unchanged value. **Behavior:** splices into any existing cable like `util.reroute` does, and shows a live trace of whatever passes through — unlike the three viewers above, it has a real output and doesn't need a separate branch off the wire. `NodeLayoutVariant::Glance`: no title, no parameter list — just an input glyph, a compact live preview, an output glyph. Doesn't support `Note` or `Data`, same scope `view.scope`/`view.meter` already have.

## factory — content-owning nodes 📋 (all — Correction 2, none built)

Shape, unwrap contract, and the shared editor infrastructure are in
`NODES.System.md` §8 — specs only, here.

#### `factory.eq` — Spectral Factory (EQ)
**Content:** ordered band list `{id, type, frequency, gain, q, enabled, externalized}`. **In:** `in` — `Audio`; `tilt [audio]`; `mix`; `outputGain [audio]`. **Out:** `out` — `Audio`; `curve` — `Data(eq-curve)`. **Structural:** `maxBands` (default 16), `oversampling`. **Unwrap:** one band → matching `filter.peak`/`shelf`/`svf`/`allpass`; whole factory → the series chain + `mix.gain`. **Deferred within this factory:** dynamic bands, mid/side, linear phase (none unwrap into today's primitives).

#### `factory.curve` — Curve Factory
**Content:** points + per-segment tension, loop/polarity, optional morph-target shape. **In:** `morph [audio]`. **Out:** `data` — `Data(curve)`. **Structural:** `resolution`. **Unwrap:** → `data.table` + the implied consumer (`lfo.shape`/`env.curve`/`adapt.remap`/`shape.waveshaper`).

#### `factory.wave` — Wave Factory
**Content:** one or more single-cycle frames (waveform or harmonic amplitude/phase). **In:** `position [audio]` (frame scan). **Out:** `data` — `Data(wavetable)`. **Structural:** `frameSize`, `frameCount`, `normalise`. **Unwrap:** → `data.table`/`data.load` + `osc.wavetable`.

#### `factory.sample` — Sample Factory
**Content:** an asset reference, slice markers, loop points, root note, gain, trim. **In:** `trigger : Event`; `slice`; `pitch [audio]`; `start [audio]`; `level [audio]`. **Out:** `out` — `Audio`; `data` — `Data(sample)`; `ended` — `Event`. **Structural:** `interpolation`, `loopMode`. **Unwrap:** one slice → the asset + a configured `sampler.player`; whole factory → the asset + one `sampler.player` per slice, selected by `logic.select`. Recording is the separate `data.record` node, not part of this editor.

#### `factory.notes` — Notes Factory
**Content:** chord definitions, arpeggio patterns, step patterns, scale definitions. **In:** `notes` — `Note`; `tick` — `Event` (external clock; internal when unconnected); `rate [audio]`; `gateLength`; `swing`; `humanize`. **Out:** `notes` — `Note` (always — a second representation of a note is exactly the parallel-truth problem `RECONCILIATION.md` warned about; plain pitch/gate values come from `note.value` or the allocator, both already in the catalog); `scale` — `Data(scale)`. **Unwrap:** arpeggio → `note.hold`+`clock.pulse`+`clock.counter`+`note.select`; chord → `note.chord`; scale → `data.scale`+`note.quantize`; humanise → `note.humanize`. **Deferred within this factory:** MIDI clips (needs a timeline/piano-roll/host transport sync — a project of its own).

#### `factory.material` — Material Factory
**Content:** geometry, material preset, hand-made per-mode overrides. **In:** `stiffness`, `density`, `damping`, `size`, `inharmonicity`, `irregularity` (as `data.material`, all modulatable). **Out:** `data` — `Data(modal-set)`. **Structural:** `geometry`, `modeCount`, `seed`. **Unwrap:** → `data.material` (or `data.analyseModes` if from a recording) + `resonator.modal`.

---

# Part B — stock groups (`stock.*`, was "factory groups" — renamed per Correction 2 to avoid colliding with the `factory.*` family above)

Built from Part A, shipped as data, openable and editable by the user. **None of
these are built as real, loadable `stock.*` assets yet** — `Init Patch` is the one
exception: it exists as a real hand-built starting graph
(`ProofGraphs.h::buildInitPatchGraph()`), just not yet as a `stock.*`-loadable group
(groups themselves are M29 scope, per `NODES.System.md`'s note that a factory ≠ a
group).

| Group | What it is | Built from | Status |
|---|---|---|---|
| **Karplus-Strong** | The textbook plucked loop, as a teaching patch | `excite.burst` → `delay.line` → `filter.onepole` → `mix.gain` → back into the delay, `shape.clip` for safety | 📋 |
| **Scale Quantize** | Pitch snapped to a scale | `data.scale` → `note.quantize` | 📋 |
| **Arpeggiator** | Cycles held notes | `note.hold` → `clock.pulse` → `clock.counter` → `note.select` | 📋 |
| **Chord** | One note becomes several | `note.chord` with `data.scale` | 📋 |
| **Bubble** | A single water bubble | `osc.sine` with pitch from `env.curve` (rising chirp) × `env.adsr` (short decay) | 📋 |
| **Water** | Rain, a stream, a boil | `noise.dust` → a future `instance.allocate.swarmTransient` (09-28-InstanceAllocator.3: Swarm modes are no longer a config on `instance.allocate.voice` — they'll be their own node type once built) → Bubble per instance, radius from `random` → `instance.mix` | 📋 |
| **Crackle** | Fire, static, ice | `noise.dust` → `excite.burst` → `resonator.modal` with a small stone/ceramic set | 📋 |
| **Scrape** | Stone dragged across asphalt | `excite.contact` → `resonator.modal` with `data.material` (stone, irregular) → `space.reverb` | 📋 |
| **Cicada** | One insect | `clock.pulse` with jitter → `excite.burst` → `filter.formant` → body from `resonator.modal` | 📋 |
| **Cicada Field** | A population of them | a future `instance.allocate.swarmPopulation` (see the Water row above) → Cicada per instance, rate/pitch from `random.drift`, placement from `panPosition` | 📋 |
| **Breath / Wind** | Wind, breathing, flutes | `excite.breath` → `resonator.tube`, contour from `env.curve` | 📋 |
| **Bowed String** | Violin-like | `excite.stickSlip` ↔ `resonator.string`, coupling loop closed through `motion` | 📋 |
| **Struck Body** | Drum, bell, plate | `excite.mallet` ↔ `resonator.plate` or `resonator.modal` | 📋 |
| **Hex Guitar Front End** | Six strings to six note streams | six `io.audioIn` channels → `analysis.onset` + `analysis.pitch` → `note.assemble` per string | 📋 |
| **Init Patch** | Ordinary subtractive synth | `io.noteIn` → `instance.allocate.voice` → `osc.analog` ×2 → `filter.ladder` → `env.adsr` (×2: amp + filter cutoff) → `instance.mix` → `space.pan` → `io.output` (one real stereo cable, `pan.out` → `masterOut.in`) | ✅ real hand-built graph, genuinely stereo; 📋 not yet a loadable `stock.*` asset — no `space.reverb` tail yet (M28) |
| **Voiced self-oscillation (cat purr)** *(Correction 1's new coverage item)* | The hardest test in the set | `env.curve` (breath pressure) → `random.drift` (stiffness jitter) + `lfo.shape` (~26Hz stiffness modulation, for entrainment) → `excite.vocalFolds` → `flow` gates `noise.colored` through `mix.gain` (aspiration) → `resonator.junction` splits `resonator.tract` (nasal route) vs. a closed branch (antiresonances) vs. `resonator.modal` (body conduction) → `mix.crossfade` (microphone position) | 📋 — exercises audio-rate physical-parameter modulation, emergent oscillation thresholds, source–resonator coupling, branched waveguides, `Data` as a geometric profile, flow-gated noise, two sources sharing one tract, sub-30Hz fundamentals. **Testing note:** self-oscillating/chaotic models are deterministic but rounding-sensitive — two compilers or an enabled FMA path diverge within seconds, so bit-exact golden renders don't work here; verify statistically (measured f₀/spectral envelope/jitter/shimmer within tolerance, oscillation threshold within a pressure window) or CI failures become indistinguishable from physics. |

# Reference patches: coverage

Every reference patch from the original planning prompt builds from Part A, with no
missing primitives (once M23–M29 land):

1. **Karplus-Strong with single-sample feedback** — `delay.line` + `filter.onepole` + `mix.gain`, closed as a per-sample region; `resonator.string` is the playable native version.
2. **MIDI remapped to a scale** — `io.noteIn` → `note.quantize` ← `data.scale`.
3. **Arpeggiator and chords** — `note.hold`, `clock.counter`, `note.select`, `note.chord`.
4. **Struck body with material data** — `excite.mallet` → `resonator.modal` ← `data.material`.
5. **Stone on asphalt** — `excite.contact` → `resonator.modal`, `util.macro` driving speed/pressure.
6. **Transient and persistent swarms** — future `instance.allocate.swarmTransient`/`instance.allocate.swarmPopulation` node types (09-28-InstanceAllocator.3: no longer configurations of `instance.allocate.voice` — see the Water/Cicada Field rows above).
7. **Ordinary subtractive patch** — `osc.analog`, `filter.ladder`, `env.adsr` — **built, playable today** as Init Patch.
8. **Per-voice effects** — `shape.waveshaper`, `delay.line`, `space.reverb` placed before `instance.mix`.
9. **Hexaphonic guitar** — `io.audioIn` per channel → `analysis.onset` + `analysis.pitch` → `note.assemble` → `instance.allocate.voice` (each string gets its own voice/mix pair, not a shared one — avoids needing a Note-stream-merge node).
10. **Voiced self-oscillation (cat purr)** — see Part B, above.

# Deliberately deferred

- **Spectral family** (FFT-domain nodes): the `Spectral` signal type stays reserved and unused until it has a concrete design.
- **The group system itself**: `stock.*` groups ship once the group-inlining mechanism exists (M29) — this catalog doesn't depend on them.
- **Nested allocators** beyond one level: supported by the model, opt-in later.
- **MIDI and note output** to the host: the `Note` type is ready for it; the I/O node isn't specified yet.
- **Additive and FM-operator oscillator families**: `osc.sine` + phase-modulation inputs covers the common cases until a dedicated family earns its place.
