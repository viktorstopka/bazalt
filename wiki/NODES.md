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
**📋 Catalog only** — specified here, not built yet. Most of the catalog is this today.
`wiki/NODES.Status.md` is the authoritative running count of how many node types are
implemented vs. catalog-only and tracks the build order for what's left — this file
used to maintain its own separate count here too (it drifted stale at least twice,
caught during a post-`util.macro`-ship sweep, 2026-10-01), so it no longer tries;
check that file for the current number rather than trusting a hardcoded one here.
Don't assume a node works in the running app because it's in this file; check the
status marker.

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
| `osc.*` | analog, sine, saw, square, triangle, wavetable, **glottal** (Correction 1) | ✅ analog, sine, saw, square, triangle — 📋 wavetable, glottal |
| `sampler.*` | player, granular | 📋 both |
| `noise.*` | colored, dust | 📋 both |
| `excite.*` | impulse, burst, pluck, mallet, stickSlip, breath, contact, **vocalFolds** (Correction 1) | ✅ impulse, burst, pluck, mallet — 📋 stickSlip, breath, contact, vocalFolds |
| `resonator.*` | modal, string, tube, plate, comb, **junction**, **tract** (Correction 1) | ✅ comb, modal, string, plate — 📋 tube, junction, tract |
| `filter.*` | svf, ladder, onepole, allpass, shelf, peak, formant, dcBlock | ✅ svf, ladder, onepole, allpass, shelf, peak, dcBlock — 📋 formant |
| `shape.*` | waveshaper, clip, fold, rectify, crush | 📋 all 5 |
| `delay.*` | line | ✅ |
| `space.*` | reverb, diffuser, pan, width | ✅ pan, width — 📋 reverb, diffuser |
| `mix.*` | crossfade, gain, downmix | ✅ all 3 (`sum` removed — folded into `math.add`, `wiki/plans/DomainRedesign.md` Batch 3) |
| `env.*` | adsr, curve, follower | ✅ adsr, follower — 📋 curve |
| `lfo.*` | shape | 📋 |
| `random.*` | stepped, drift | ✅ both |
| `clock.*` | pulse, divide, counter | ✅ all 3 |
| `seq.*` | steps, euclid | ✅ euclid — 🚧 steps |
| `note.*` | gate, value, quantize, transpose, chord, hold, select, humanize, filter, assemble | ✅ gate, value, quantize, transpose, humanize, filter, assemble — 📋 chord, hold, select (real engine limit — see the `note.filter`/`note.hold` entries below) |
| `math.*` | add, subtract, multiply, divide, abs, clamp, minmax, power, round, modulo, slew | ✅ all 11 |
| `logic.*` | and, or, xor, not, compare, toggle, latch, select, edge, eventGroup | ✅ all 10 |
| `adapt.*` | map (absorbed remap, 2026-10-04), normalise, threshold, sampleHold, **audioToControl** (AudioControlBridge), **controlToAudio** (ControlToAudioBridge), **boolToControl**, **pitchToFrequency**, **frequencyToPitch**, **gateLength** (all new, direct-feedback sweep) | ✅ all 10 |
| `data.*` | load, table, scale, material, analyseModes, lookup, **record**, **eqToCurve** (Correction 2) | ✅ table, scale, material, lookup — 📋 load, analyseModes, record, eqToCurve |
| `analysis.*` | onset, pitch, level, centroid | 📋 all 4 |
| `instance.*` | allocate.voice, allocate.swarmPopulation, allocate.swarmTransient, allocate.trigger, sum | ✅ all five — Domain Extensions batch done 2026-10-01 |
| `util.*` | constant, macro, reroute | ✅ all 3 (`macro` real as of `wiki/plans/UtilMacro.md` — ADR-0030 amends ADR-0015, doesn't reverse it) |
| `view.*` | listen, spectrum, meter, ripple, count, scope.control, scope.modulation, gate, cycle | ✅ all 9 (scope, glance removed) |
| `factory.*` | eq, curve, wave, sample, notes, material (Correction 2) | 📋 all 6 |

---

## io — the plugin boundary

#### `io.audioIn` — Audio In ✅
Reads one of the plugin's real input buses. **Out:** `channel.0…channel.N` — `Audio`, a port group sized by the selected bus (stereo main gives 2, a six-channel bus gives 6). **Structural:** `bus` (enum: Main, Aux 1–4). **Behavior:** passthrough; silence when the host has not activated the bus. **Taps:** per-channel level. **Native:** the plugin I/O boundary.

#### `io.output` — Master Out ✅
**In:** `in` — `Audio` (`Channels::Stereo` — one real stereo cable). **Out:** `out` — `Audio` (`Channels::Stereo`, primary, `hidden` in the editor). **Behavior:** unity passthrough per channel. A mono source wired into `in` broadcasts to both physical output channels automatically (`canConnect`'s free mono→stereo rule, `NODES.System.md` §9) — every pre-stereo patch behaves unchanged. **The output port exists for the compiler only** (`NodeGraph::setOutput()` needs a real output port on the designated node to point at) and is never rendered as a wireable glyph in the editor (`PortDescriptor::hidden`) — a terminal "Master Out" node showing a further output you could drag a cable from was a real, caught-live UX bug, not a quirk to leave alone. The engine itself stays fully permissive; only the editor stops offering it. The `missing-ui-command` (`graphSetOutput`) bug `NODES_Gaps.md` flagged is unrelated to this node's own port shape and still needs its own fix. **Taps:** output scope, spectrum, level.

#### `io.noteIn` — Note In ✅
The plugin's MIDI input boundary — turns the host's incoming note-on/off, velocity,
and pitch bend into the engine's `Note` signal, the single starting point every
note-driven patch wires from. **Out:** `notes` — `Note`. **Structural:** `channel` (enum: Omni, 1–16, schema-only — every channel is accepted regardless), `mpeMode` (enum: off, MPE, schema-only). **Behavior:** translates host MIDI into `Note` events; pitch bend folds into the continuous pitch field rather than a separate port. **Native:** plugin I/O boundary.

#### `io.control` — MIDI Control ✅
Exposes one host performance-control source — a CC number, mod wheel, channel
pressure, pitch bend, or sustain pedal — as an ordinary smoothed Control signal,
ready to patch anywhere a knob could go. **Out:** `value` — `float·Unipolar·0–1·linear·0`. **Structural:** `source` (enum: CC number, mod wheel, channel pressure, pitch bend, sustain pedal), `channel`. **Params:** `smoothing : float·Time·0–500ms·log·20ms`.

#### `io.transport` — Transport ✅
Exposes the host's transport — tempo, play state, song position, and a beat pulse
— so a patch can sync to song time instead of free-running. **Out:** `beat` — `Event`; `tempo` — `float·Frequency`; `playing` — `bool`; `position` — `float·Time`. Standalone runs an internal transport.

## osc — oscillators

#### `osc.analog` — Analog Oscillator 🚧
Band-limited virtual-analog oscillator. **In (spec):** `frequency [audio]`; `fine`; `pulseWidth [audio]`; `phaseMod [audio]`; `sync : Event`. **In (real today):** only `frequency`-equivalent (MIDI-note pitch) and a plain Hz frequency input — `fine`/`pulseWidth`/`phaseMod`/`sync` don't exist yet on the shipped node (tracked as a known gap, not silently assumed fixed). **Out:** `out` — `Audio`. **Structural:** `shape` (enum: sine, triangle, saw, square, pulse). **Behavior:** phase accumulator with PolyBLEP correction. **Native:** numerically delicate band-limiting.

#### `osc.sine`, `osc.saw`, `osc.square`, `osc.triangle` — Sine / Saw / Square / Triangle ✅ *(restructured / new 2026-10-04)*
Four minimal per-shape oscillators sharing one class (`SineOscillatorNode.h`'s
`BasicOscillatorNode`), one port set and one layout; they differ only in the
waveform. **In:** `frequency` — value row, Hz, 0.01–20000 (sub-audio on
purpose), default 440; `amplitude` — value row, 0–1, default 1.00; `phase` —
Modulation (Bipolar) row, in cycles, default 0.00 — an ordinary modulatable
port with its own inline value, offsetting the read point (through-zero,
never detunes the accumulator); it replaced osc.sine's bare `phaseMod` input;
`pulseWidth` — **Square only**, Unipolar, default 0.5 (clamped 0.01–0.99);
`sync : Event` — resets phase. **Out:** `out` — `Audio`. **Band-limited:**
Sine is exact; Saw and Square use PolyBLEP; Triangle uses PolyBLAMP (stateless,
so a modulated Phase can't make it drift — unlike `osc.analog`'s leaky-
integrated triangle).
**Preview:** phase-locked (`PreviewKind::PhaseLocked`) — 4 cycles aligned to
phase zero, the node's own band-limited function evaluated at its current,
fully modulated parameters every frame, fixed ±1 scale with headroom (clip
marked), playhead Auto (shown below 30 Hz) / On / Off. Shared by every phase
source (`PhaseLockedPreview.tsx`), `osc.analog` included.
**Native:** inner-loop primitives — FM stacks and modal excitation use many.

#### `osc.wavetable` — Wavetable Oscillator 📋
Scans across a table of single-cycle waveform frames as it plays, using `position`
to morph the timbre over the frame set instead of holding one fixed shape. **In:** `table` — `Data(wavetable)`, required; `frequency [audio]`; `position [audio]` (frame scan); `phaseMod [audio]`; `sync : Event`. **Out:** `out` — `Audio`. **Structural:** `interpolation` (enum: none, linear, cubic), `frameBlending` (enum: blend, jump). **Native:** inner loop scales with table size.

#### `osc.glottal` — Glottal Pulse 📋 *(Correction 1)*
A parametric model of the airflow pulse from a vibrating valve (Liljencrants–Fant, or a simpler Rosenberg mode) — voice research's own measured parameters go in directly. **In:** `f0 : float·Frequency·10–2000Hz·log·110 [audio]`; `openQuotient : float·Unipolar·0.01–0.9·linear·0.5 [audio]` (very low = a purr); `asymmetry : float·Unipolar·0.1–0.9·linear·0.6`; `closureSharpness : float·Unipolar·0–1·linear·0.5 [audio]`; `jitter : float·Unipolar·0–1·linear·0`; `shimmer : float·Unipolar·0–1·linear·0`; `sync : Event`. **Out:** `out` — `Audio` (the flow derivative — the acoustic source); `flow` — `float·Unipolar [audio]` (for gating aspiration noise); `open` — `bool`. **Structural:** `model` (enum: LF, Rosenberg), `seed`. **Native:** delicate band-limited pulse generation; would take dozens of nodes to approximate.

## sampler — samples and grains

#### `sampler.player` — Sampler 📋
Plays back a loaded sample at a controllable pitch and start offset — the basic
one-shot/loop/drum playback primitive everything sample-based builds on. **In:** `sample` — `Data(sample)`, required; `trigger : Event`; `pitch [audio]`; `start`; `level [audio]`. **Out:** `out` — `Audio`; `ended` — `Event`. **Structural:** `loopMode`, `interpolation`, `direction`. **Native:** delicate interpolation, data-bound.

#### `sampler.granular` — Granular Cloud 📋
Slices a sample into a stream of overlapping grains and randomizes their position,
size, pitch, and pan — the engine behind time-stretching, textures, and clouds a
plain playhead can't produce. **In:** `sample` — `Data(sample)`, required; `position [audio]`; `density [audio]`; `grainSize`; `pitch [audio]`; `spray`; `pitchSpray`; `pan`; `panSpray`; `trigger : Event`. **Out:** `left`, `right` — `Audio`. **Structural:** `maxGrains` (default 64), `window`, `spawnMode`. **Native:** inner loop scales with grain count; preallocates the pool.

## noise — stochastic sources

#### `noise.colored` — Noise 📋
A continuous noise source with a selectable spectral colour (white through violet)
plus a fine tilt — the raw material behind hiss, air, and shaped textures. **In:** `tilt : float·Bipolar·−1–1·linear·0`. **Out:** `out` — `Audio`. **Structural:** `color` (enum: white, pink, brown, blue, violet).

#### `noise.dust` — Dust 📋
Sparse random impulses — the primitive behind crackle, rain, footsteps, a population of tiny events. **In:** `density [audio]`; `jitter`. **Out:** `pulse` — `Event`; `out` — `Audio`. **Structural:** `seed`. **Native:** sample-accurate event timing an LFO-and-threshold group can't reproduce without aliasing.

## excite — physical excitation

`excite.*` nodes produce a signal that drives a resonator; `resonator.*` nodes are the
resonating bodies. Friction/collision models that need to feel the resonator expose a
`feedback` input for that purpose (`NODES.System.md` §1's `Audio`-typed excitation
path).

#### `excite.impulse` — Impulse ✅ *(PM Core batch 1)*
The simplest possible excitation — a single sharp transient (or a short pulse once
widened) to knock a resonator into motion. **In:** `trigger : Event`; `amplitude`; `width` (0 = a true single-sample delta). **Out:** `out` — `Audio`. **Shape, a real design call the catalog named but didn't define:** `width == 0` fires an exact one-sample delta at `amplitude`; `width > 0` widens into a raised-cosine (Hann) bump over that span instead of a hard rectangular pulse, so widening stays click-free. Capped at 50ms (`maxWidthMs`) — longer than that is `excite.burst`/`excite.pluck`'s job.

#### `excite.burst` — Noise Burst 🚧 *(partially fixed — wiki/NODES_Gaps.md's `hardcoded-trigger`)*
A timed burst of white noise with a linear decay — the standard broadband
excitation for plucks, hits, and anything else that needs a transient kick rather
than a tonal source. **In (spec):** `trigger : Event`; `duration`; `tone`; `shape`. **In (real today):** `trigger : Event` and `duration [audio]·0.1–2000ms·log·30ms` — a clock, a threshold detector, or anything else that produces Events can now start it; the direct C++ `trigger(int durationSamples)` poke still exists too (tests/tools), calling the same internal logic. `tone`/`shape` aren't built yet — still 🚧, not full catalog compliance. **Out:** `out` — `Audio` (white-noise burst, linear decay envelope over the triggered duration).

#### `excite.pluck` — Pluck ✅ *(PM Core batch 3)*
A pre-shaped plucked-string excitation, pickup position and hardness baked in —
the one-node shortcut into `resonator.string` without hand-building the comb-notch
spectrum yourself. **In:** `trigger : Event`; `position`; `hardness`; `amplitude`. **Out:** `out` — `Audio`. **Native:** comb-notch shaping, awkward to hand-wire per patch. **A concrete, tested contract this session had to design:** a fixed 5ms linearly-decaying noise burst, `hardness` an in-line one-pole (`filter.onepole`'s own "Damping" convention — `0` darkest, `1` brightest), `position` a plain FIR difference-tap against a FIXED 200-sample reference window (this node has no pitch concept, unlike `resonator.string`'s own `position`, which taps its real, live delay line). `amplitude`/`hardness`/`position` are all sampled once, at the moment `trigger` fires — not continuously tracked during the decay, matching how a real pluck's attack character never changes mid-ring.

#### `excite.mallet` — Mallet / Collision ✅ *(PM Core batch 4, closes the batch)*
Models a mallet or hammer striking something, with a `feedback` path from the
resonator so the collision itself reacts to what it hits — real contact dynamics,
not a fixed envelope. **In:** `trigger : Event`; `velocity`; `mass`; `stiffness`; `feedback` — `Audio` (optional). **Out:** `out` — `Audio`; `contact` — `bool`. **Native:** single-sample feedback, numerically delicate. **Behavior:** a half-sine contact pulse (a real, standard simplified Hertzian-contact-force approximation), `stiffness`/`mass` setting its frequency (hence duration/brightness); `feedback` subtracts from the mallet's own effective driving velocity every sample it's in contact — real Newton's-third-law coupling, not a fixed envelope. Wiring `resonator.string`'s own `motion` output back into `feedback` closes a real, literal 2-node graph cycle — the first production node pair to exercise `GraphCompiler.cpp`'s existing per-sample-region/SCC mechanism (confirmed architecturally before this batch's first line of code, proven end to end by a real compiled-graph test).

#### `excite.stickSlip` — Stick-Slip / Bow 📋
A friction/bow model — continuous pressure and speed, not a trigger, drive the
stick-slip cycle against a resonator that's the mechanism behind bowed strings and
squeaks. **In:** `pressure [audio]`; `speed [audio]`; `roughness`; `feedback` — `Audio` (optional); `trigger : Event`. **Out:** `out` — `Audio`; `slipping` — `bool`. **Native:** single-sample feedback; delicate near zero speed.

#### `excite.breath` — Breath 📋
A pressure-driven turbulence/reed source — continuous breath pressure into a
noise/nonlinearity model, the excitation behind winds, reeds, and lips. **In:** `pressure [audio]`; `turbulence`; `noiseColor`; `feedback` — `Audio` (optional). **Out:** `out` — `Audio`. **Structural:** `mode` (enum: breath, reed, lip). **Native:** single-sample feedback, delicate nonlinearity.

#### `excite.contact` — Contact / Scrape 📋
Continuous scraping or rolling contact between two surfaces, driven by speed and
pressure rather than a trigger — the excitation behind scrapes, rolls, and
grinds. **In:** `speed [audio]`; `pressure [audio]`; `surface` — `Data(curve)` (optional); `grainSize`. **Out:** `out` — `Audio`; `contactRate` — `float·Frequency`.

#### `excite.vocalFolds` — Vocal Folds 📋 *(Correction 1)*
A self-oscillating two-mass valve — frequency **emerges** from pressure/mass/stiffness/geometry, not commanded, exactly like a real larynx. Produces behavior a parametric model can only imitate: an oscillation threshold, subharmonics and deterministic chaos from left–right asymmetry. **In:** `pressure : float·Pressure·bipolar·linear·0 [audio]` (below threshold, nothing oscillates; negative = inhalation, which behaves differently); `mass : float·Mass [audio]`; `stiffness : float·Stiffness [audio]` (near the model's own frequency, this is how entrainment happens); `adduction : float·Unipolar·0–1·linear·0.5 [audio]`; `asymmetry : float·Bipolar·−1–1·linear·0` (the route to subharmonics/roughness); `damping : float·Unipolar·0–1·linear·0.3`; `feedback` — `Audio` (pressure from the tract above; optional). **Out:** `out` — `Audio`; `flow` — `float·Unipolar [audio]`; `contact` — `bool`; `contactQuotient` — `float·Unipolar`; `oscillating` — `bool`; `f0` — `float·Frequency` (measured). **Structural:** `masses` (enum: one-mass, two-mass), `seed`. **Native:** single-sample feedback, numerically delicate, runtime state no subgraph can express; clamps rather than diverging, reports `oscillating = false` below threshold instead of producing noise.

## resonator — resonating bodies

#### `resonator.modal` — Modal Bank ✅ *(PM Core batch 2)*
The centre of the physical-modelling set. **In:** `excite` — `Audio`; `modes` — `Data(modal-set)`, required; `pitch [audio]` (absolute semitones, same `440·2^((pitch-69)/12)` convention `osc.analog` uses); `decay`; `brightness`; `inharmonicity`; `position` (pickup point); `spread`. **Out:** `out` — `Audio` (`Channels::Stereo`, primary — one real stereo cable; the catalog's own `left`/`right` pair predates the stereo redesign, `NODES.System.md` §9, which made one `Channels::Stereo` port the catalog-wide rule for every node, including ones not yet built when it landed). **Structural:** `maxModes` (default 64). **Native:** inner loop scales with mode count; delicate resonant filters — a bank of real two-pole resonators (`y[n] = 2r·cos(w)y[n-1] - r²y[n-2] + excite[n]·gain`), coefficients recomputed per mode per sample, the single most expensive node in this catalog by construction. `position` uses the textbook string mode-shape weighting (`|sin((k+1)πposition)|`) as a documented generalization across every geometry; `position == 0` is a real physical null (total silence), not a bug. `spread` pans each mode deterministically via a golden-angle constant — no `seed` needed, the same mode always lands at the same stereo position on every run.

#### `resonator.string` — String ✅ *(PM Core batch 3)*
A waveguide string, the playable version of Karplus-Strong. **In:** `excite` — `Audio`; `pitch [audio]`; `decay`; `damping`; `stiffness`; `position`; `release`. **Out:** `out` — `Audio`; `motion` — `Audio` (feeds back into `excite.stickSlip`/`excite.mallet`). **Native:** single-sample feedback; tuning/interpolation delicate. **Behavior:** a real circular delay line (`sampleRate/pitch` samples) closed through a damping one-pole (`filter.onepole`'s own convention) plus a single-stage stiffness allpass (a documented simplification of the full Jaffe-Smith dispersion cascade), scaled by a `decay`-derived per-round-trip gain (the same time-based formula `resonator.comb`'s feedback mode uses, parametrized by seconds instead of a raw gain). `position` is a real difference-tap against the string's own live delay line (unlike `excite.pluck`'s fixed-window version). **`release` is a real, documented design call: a `bool` gate, not a knob** — held (`true`, the default) rings normally; released (`false`) ramps an extra damping multiplier down to a floor over ~15ms, modelling a palm-mute/finger-lift. `motion` is the loop's own freshly-computed value at the injection point, for a real coupled exciter (`excite.mallet`'s `feedback`) to read — a literal graph cycle once wired that way, compiled by `GraphCompiler.cpp`'s existing per-sample-region mechanism.

#### `resonator.tube` — Tube 📋
An open- or closed-end waveguide tube — the resonating body behind wind
instruments, ducts, and bores, with reflection and flare shaping its resonant
character. **In:** `excite` — `Audio`; `length`; `damping`; `reflection`; `flare`. **Out:** `out` — `Audio`; `motion` — `Audio`. **Structural:** `endCondition` (enum: open, closed). **Native:** single-sample feedback in both directions.

#### `resonator.plate` — Plate / Membrane ✅ *(PM Core batch 4, closes the batch — real, documented simplification)*
A 2D waveguide mesh — the resonating body behind drum heads, plates, and gongs,
with a pickup point placed anywhere on the surface. **In:** `excite` — `Audio`; `size`; `tension`; `decay`; `damping`; `positionX`, `positionY`. **Out:** `out` — `Audio` (`Channels::Stereo`, primary — one real stereo cable, the modern catalog-wide shape, not the stale `left`/`right` pair this entry predates). **Structural:** `quality` (enum: low, medium, high → 8/16/32 modes). **Native:** inner loop scales with mode count. **Not a literal 2D mesh solve** — a real, documented simplification: self-contained (no `modes` input, unlike `resonator.modal`), reusing `resonator.modal`'s own two-pole-resonator-bank technique over the SAME membrane-Bessel-zero table `data.material`'s own `plate` geometry already uses, squared the same way. No `pitch` input — `size`/`tension` together set its own absolute fundamental (smaller/tenser rings higher). `positionX`/`positionY` generalize `resonator.modal`'s own mode-shape weighting into two axes; stereo spread is unconditional (no `spread` knob exists for this node) via the same golden-angle constant `resonator.modal` uses.

#### `resonator.comb` — Comb ✅ *(PM Core batch 1)*
The cheap resonator, and the building block for hand-built feedback experiments. **In:** `in` — `Audio`; `frequency [audio]`; `feedback` (hard-limited to ±0.999); `damping`. **Out:** `out` — `Audio`. **Structural:** `type` (enum: feedforward, feedback; default feedback). **Behavior:** `feedback` mode is the real IIR loop (`y[n] = x[n] + g·damped(y[n-M])`, the textbook Karplus-Strong-style absorption comb — damping sits INSIDE the loop); `feedforward` mode taps the input only (`y[n] = x[n] + g·damped(x[n-M])`), unconditionally stable, pure notch/peak comb-filtering with no possible ring-up. `damping` reuses `filter.onepole`'s own "Damping" port convention exactly: `0` = darkest/most damped, `1` = brightest/no damping. No fractional-delay interpolation, same simplification `delay.line` itself already makes.

#### `resonator.junction` — Scattering Junction 📋 *(Correction 1)*
A multi-way junction where waveguides meet — a branch closed at its far end acts as a side cavity that removes energy at its own resonances, producing the spectral notches a plain band-pass filter cannot create. **In:** `in` — `Audio`; port group `branch.0…branch.N` — `Audio` (growable, min 2, max 8, bidirectional); `impedance.0…N [audio]` (modulating one is opening/closing a valve, e.g. a soft palate); `loss`. **Out:** `out` — `Audio`; the branch group returns reflected waves. **Structural:** `branches` (2–8). **Native:** single-sample feedback across several paths (Kelly–Lochbaum scattering).

#### `resonator.tract` — Tract 📋 *(Correction 1)*
A multi-section waveguide whose cross-section profile is read from a curve, so formants arise from tube shape instead of being dialled in on filters — a vocal tract, a bore, a duct or a horn, all the same node. **In:** `in` — `Audio`; `shape` — `Data(curve)` (cross-sectional area along the tube; neutral default when unconnected); `length [audio]` (body size); `damping`; `radiation` (how open the far end is; near zero = a closed side cavity). **Out:** `out` — `Audio` (radiated at the open end); `motion` — `Audio` (pressure at the input end, for feeding back into an excitation node). **Structural:** `sections` (8–64), `maxLength`. **Native:** single-sample feedback along the whole chain; inner loop scales with section count.

## filter

#### `filter.svf` — State-Variable Filter 🚧
A state-variable filter offering lowpass/bandpass/highpass/notch/peak modes from
one topology, self-resonant near the top of its range — the general-purpose swept
filter. **In:** `in` — `Audio`; `cutoff [audio]`; `resonance [audio]`; `drive`; `keyTrack`; `keyPitch`. **Out (spec):** simultaneous `lowpass`/`bandpass`/`highpass`/`notch`/`peak` port group. **Out (real today):** one mode-switched `out` (host-only `setType()`, no port) — the 5-simultaneous-output redesign is a known, documented gap, not fixed silently. **Structural:** `slope` (enum: 12, 24 dB/oct). **Native:** TPT zero-delay-feedback, numerically delicate.

#### `filter.ladder` — Ladder Filter ✅
The classic four-stage transistor-ladder lowpass — warm, self-oscillating at high
resonance, switchable between lowpass/highpass/bandpass modes and pole counts. **In:** `in`; `cutoff [audio]`; `resonance [audio]` (self-oscillates at 1); `drive`; `keyTrack`, `keyPitch`. **Out:** `out` — `Audio`. **Structural:** `poles` (1–4), `mode` (enum: lowpass, highpass, bandpass). **Behavior:** four-stage nonlinear ladder, closed-form ZDF solve (non-iterative, bounded per-sample cost). **Native:** numerically delicate.

#### `filter.onepole` — One-Pole ✅
**In:** `in`; `cutoff [audio]`. **Out:** `lowpass`, `highpass` — `Audio`. **Behavior:** the cheap damper used inside feedback loops; exact and allocation-free.

#### `filter.allpass` — Allpass / Dispersion ✅
**In:** `in`; `frequency [audio]`; `amount`. **Out:** `out` — `Audio`. **Structural:** `stages` (1–16, fixed-size array, no allocation). **Behavior:** phase rotation without amplitude change — what gives strings stiffness and reverbs diffusion.

#### `filter.shelf` — Shelf ✅
Boosts or cuts everything above or below a corner frequency — the basic tone-shaping
shelf EQ. **In:** `in`; `frequency [audio]`; `gain` (shown in dB); `slope`. **Out:** `out`. **Structural:** `type` (enum: low, high).

#### `filter.peak` — Peak / Bell ✅
Boosts or cuts a band around a centre frequency — a parametric/bell EQ band. **In:** `in`; `frequency [audio]`; `gain`; `q`. **Out:** `out`.

#### `filter.formant` — Formant 📋 *(Correction 1 replaces this entry entirely)*
Shapes a signal's spectrum through a bank of resonant peaks (formants) and optional
notches (antiformants) — the filter-only route to vowel-like or nasal colouration
without a physical tract model. **In:** `in`; `vowel [audio]`; `formants` — `Data(modal-set)` (optional custom table); `antiformants` — `Data(modal-set)` (optional — the spectral notches a closed side cavity produces; without them, nasal sounds can't be imitated by filters); `antiformantDepth`; `shift`; `intensity`. **Out:** `out`. **Native:** inner loop over bands.

#### `filter.dcBlock` — DC Blocker ✅
**In:** `in`; `cutoff`. **Out:** `out`. **Behavior:** the classic tunable one-pole (`y[n] = x[n] − x[n−1] + R·y[n−1]`), trivial but essential wherever nonlinearities and feedback meet.

## shape — nonlinearities

#### `shape.waveshaper` — Waveshaper 📋
Passes a signal through a fixed or user-drawn transfer curve for distortion and
saturation — the general-purpose nonlinearity node. **In:** `in` — `Audio`; `drive [audio]`; `bias`; `mix`; `curve` — `Data(curve)` (optional). **Out:** `out` — `Audio`. **Structural:** `shape` (enum: tanh, arctan, sine fold, asymmetric, hard, custom), `oversampling`. **Native:** oversampling, delicate.

#### `shape.clip` — Clip / Safety ✅ *(direct feedback: real, wireable, mid-chain safety)*
**In:** `in`; `ceiling`; `knee`. **Out:** `out`; `clipping` — `bool`. **Structural:** `mode` (enum: hard, soft, limiter). **Behavior:** the node you put in a feedback loop so a slider can't destroy a speaker. `hard` is an exact clamp to `±ceiling` with a real quadratic soft-knee region (`knee`, 0–1, scaled by `ceiling`) smoothing the approach; `soft` is a `tanh` saturation that asymptotically approaches `ceiling` and never hard-clips at all; `limiter` reuses the same attack(~1ms)/release(~100ms) smoothed gain-reduction envelope the plugin's own always-on master-output `OutputLimiter` uses, plus the same hard-clamp backstop underneath it for a single isolated spike the envelope can't react to in time. `clipping` is live, not static — true only while this sample is actually being altered. A real, wireable complement to the plugin-level safety net (`PluginProcessor.cpp`'s `OutputLimiter`), which protects the final mix unconditionally but can't be inserted mid-chain.

#### `shape.fold` — Wavefolder 📋
Folds a signal back on itself past a threshold instead of clipping it, producing
the harmonically rich, reflective character of a wavefolder. **In:** `in`; `drive [audio]`; `offset`; `folds`. **Out:** `out`. **Structural:** `oversampling`.

#### `shape.rectify` — Rectify 📋
Removes or flips the negative half of a signal — half- or full-wave rectification,
useful for octave-up effects and envelope-like shaping. **In:** `in`; `amount`. **Out:** `out`. **Structural:** `mode` (enum: half, full).

#### `shape.crush` — Bitcrush / Downsample 📋
Reduces bit depth and/or effective sample rate on purpose — the lo-fi/bitcrush
degradation node. **In:** `in`; `bits [audio]`; `rate [audio]`; `mix`. **Out:** `out`.

## delay

#### `delay.line` — Delay ✅
A single tap of delayed signal with feedback and damping — the basic echo/comb
building block behind delays, chorus, and feedback networks. **In:** `in` — `Audio`; `time [audio]`; `feedback` (hard-limited); `damping`; `mix`. **Out:** `out` — `Audio`. **Structural:** `maxTime`, `interpolation` (enum: linear, allpass, cubic), `timeMode` (enum: free, tempo-synced, samples). **Native:** the delay at the heart of every feedback structure.

## space

#### `space.reverb` — Reverb ✅
A feedback-delay-network reverb with physical controls — the general-purpose
space/ambience node (`wiki/plans/Reverb.md`). **In:** `in` — `Audio` (`Channels::Stereo`); `size` (m, 1–50); `decay` (RT60, s, 0.1–60); `decayLow`, `decayHigh` (×decay below 250 Hz / above 3 kHz); `predelay` (ms); `diffusion`; `modulation`, `modRate`; `early`; `lowCut`, `highCut`; `width`; `mix` (default 0.3 — inserting in a patch is the common case); `freeze` (`Boolean`). **Out:** `out` — `Audio` (`Channels::Stereo`). **Structural:** `quality` (8 or 16 lines). **Behavior:** input tone → predelay → multichannel diffuser (its early sound) → 8/16-line FDN (Householder matrix, per-line shelves derived from the per-band RT60, slowly wandering line lengths read through lossless allpass interpolation) → decorrelated stereo → width → mix. The measured decay per band is what the controls say (±10 %, tested); loudness of a sustained signal is independent of `decay`; `freeze` = lossless loop, muted input. Tested against `Reverb.md` §5's bar in `tests/ReverbTests.cpp`.

#### `space.diffuser` — Diffuser ✅
Smears a stereo signal into dense, uncoloured texture without adding a tail —
the first half of a reverb on its own. **In:** `in` — `Audio` (`Channels::Stereo`); `size` (ms); `diffusion`; `modulation`. **Out:** `out` — `Audio` (`Channels::Stereo`). **Structural:** `stages` (1–6). **Behavior:** eight internal channels through Hadamard diffusion stages with seeded per-channel delays and polarity flips (Geraint Luff's design, the same code `space.reverb` runs); lossless.

#### `space.pan` — Pan ✅
Positions a mono (or already-stereo) source in the stereo field and outputs one
real stereo cable — the standard mono/stereo-to-stereo panner. **In:** `in` — `Audio`; `pan [audio]`; `width`. **Out:** `out` — `Audio` (`Channels::Stereo`, primary — one real stereo cable). **Structural:** `law` (enum: linear, −3dB, −4.5dB, constant power — default constant power). **Behavior:** `width` is an equal-power mid/side cross-mix of the already-panned pair; `width=1` is a true no-op. Feeds `io.output`'s `in` directly in the Init Patch — a fresh plugin instance opens playing genuinely panned stereo.

#### `space.width` — Width ✅
Widens or narrows an existing stereo signal's image — mid/side cross-mixing with
a crossover so bass stays mono and phase-coherent. **In:** `in` — `Audio` (`Channels::Stereo`); `width`; `bassMonoBelow`. **Out:** `out` — `Audio` (`Channels::Stereo`, primary). **Behavior:** the same mid/side cross-mix `space.pan` uses, plus a one-pole crossover so `width` only touches the band above `bassMonoBelow` (keeps bass phase-coherent, an ordinary mastering-chain technique).

#### `stereo.split` — Stereo Split ✅
**In:** `in` — `Audio` (`Channels::Stereo`). **Out:** `left`, `right` — `Audio` (ordinary, independently-wireable mono outputs). **Behavior:** exact passthrough — re-exposes each side of a stereo cable as a separate mono signal, e.g. to send only one channel into a different filter. Adapters category, Pattern B inline DSP.

#### `stereo.combine` — Stereo Combine ✅
**In:** `left`, `right` — `Audio` (two ordinary, independently-wireable mono inputs). **Out:** `out` — `Audio` (`Channels::Stereo`, primary). **Behavior:** exact passthrough — the inverse of `stereo.split`; lets two unrelated mono sources feed one stereo-shaped destination (e.g. `io.output`'s `in`) as one cable. Adapters category, Pattern B inline DSP.

## mix

`mix.sum` (fixed at Milestone 0.5 — `wiki/NODES_Gaps.md`'s `redundant-composable-param`)
is gone as of `wiki/plans/DomainRedesign.md` Batch 3 — folded straight into `math.add`
(see the `math` section below): the two were almost line-for-line the same node once
`level.N` was removed, the only real differences being Audio-vs-Control ports and a
stored-fallback convenience an unwired Audio input never needed (silence is already
the sum identity). `math.add` is now genuinely polymorphic and does everything
`mix.sum` did, plus Control. Patch schema migration for a pre-Batch-3 patch's literal
`mix.sum` nodes is intentionally NOT provided — CLAUDE.md's Rule 3 (never-rename-ids)
is suspended for this codebase; a saved graph naming `mix.sum` fails to load with a
plain "Unknown node type" today, not a silent reinterpretation.

#### `mix.crossfade` — Crossfade ✅
Blends continuously between two signals by position — from a discrete A/B switch
at one extreme to an even blend at the mid-point. **In:** `a`, `b` — `Audio`; `position [audio]`. **Out:** `out`. **Structural:** `law` (enum: linear, equal power).

#### `mix.gain` — Gain ✅ *(fixed — wiki/NODES_Gaps.md's `jargon-naming` + `modulation-only-port`)*
**In:** `audio` — `Audio`; `gain [audio]·0–4×·log·1` (displayed in dB). **Out:** `out` — `Audio`. **Behavior:** audio-rate `gain` makes it a ring modulator too. Now titled "Gain" in the running engine (was "VCA"), and `gain` has a real unconnected default (unity, 1.0) — leaving it unpatched is genuinely "just as loud as before."

#### `mix.downmix` — Downmix ✅
Collapses a stereo signal down to mono by a chosen rule (sum, one side, mid, or
side) — the explicit stereo-to-mono adapter `canConnect` reaches for automatically. **In:** `in` — `Audio` (`Channels::Stereo`). **Out:** `out` — `Audio` (mono). **Structural:** `mode` (enum: sum, left, right, mid, side). Genuine 1-in-1-out shape now (`NODES.System.md` §9.2) — `connectWithAutoAdapt` auto-inserts it for a stereo source into a mono-only port, the same way `adapt.map`/`adapt.normalise`/`adapt.threshold` already do.

## env — envelopes

#### `env.adsr` — Envelope ✅
The standard attack/decay/sustain/release envelope generator, driven by a gate or
trigger — shapes amplitude, filter cutoff, or any other modulation target over the
life of a note. **In:** `gate : bool`; `trigger : Event`; `delay`; `attack [audio]`; `hold`; `decay`; `sustain`; `release`; `velocity`. **Out:** `out` — `float·Unipolar·0–1 [audio]`; `finished` — `Event`. **Structural:** `attackCurve`/`decayCurve`/`releaseCurve` (enum: linear, exponential, logarithmic, s-curve), `mode` (enum: normal, loop, one-shot).

#### `env.curve` — Curve Envelope 📋
**In:** `curve` — `Data(curve)`, required; `trigger : Event`; `gate : bool`; `time`; `sustainPoint`. **Out:** `out` — `float·Unipolar [audio]`; `finished` — `Event`. **Behavior:** plays a drawn multi-segment shape.

#### `env.follower` — Envelope Follower ✅
Tracks how loud a signal is, smoothed — rectifies and applies independent attack/
release ballistics, throwing the waveform itself away on purpose to leave only a
slow-moving loudness contour. **In:** `in` — `Audio`; `attack`; `release`. **Out:** `out` — `float·Unipolar·0–1 [audio]`. **Structural:** `detection` (enum: peak, RMS). **Behavior:** independent attack/release one-pole ballistics — the Audio → Control node for amplitude/sidechain-style tracking, by hand, never auto-inserted (`NODES.System.md` §4's matrix): auto-inserting it on a bare wire-drag would silently defeat audio-rate FM/ring-mod, where the raw waveform is the modulator — that job goes through `adapt.audioToControl` instead (wiki/plans/AudioControlBridge.md), which `canConnect` DOES auto-insert for a plain `Audio → Control` crossing. The two nodes answer genuinely different questions and neither replaces the other.

## lfo

#### `lfo.shape` — LFO 📋
A low-frequency modulation source with a selectable or user-drawn waveform,
sync, and morph between two shapes — the general-purpose wobble/vibrato/tremolo
driver. **In:** `rate [audio]`; `shape`/`shapeB` — `Data(curve)` (optional); `shapeMorph [audio]`; `phase`; `sync : Event`; `depth`; `smooth`; `fade`. **Out:** `out` — `float·Bipolar [audio]`; `phaseOut`; `cycle` — `Event`. **Structural:** `waveform` (enum: sine, triangle, saw, ramp, square, random step, random smooth, custom), `rateMode`, `retrigger`. **M24.**

## random — controlled unpredictability

#### `random.stepped` — Random ✅
Generates a new random value on each trigger (or free-running at `rate` when
unconnected), with control over smoothing, distribution, spread, and how many
discrete steps it lands on — the general sample-and-hold-style randomness
source. **In:** `trigger : Event` (free-runs at `rate` when unconnected); `rate [audio]`; `amount`; `smooth` (0 = hard steps, 1 = fully glided); `bias`; `spread`; `steps` (0 = continuous); `chance`. **Out:** `out` — `float·Bipolar [audio]`; `changed` — `Event`. **Structural:** `distribution` (enum: uniform, gaussian, exponential, bimodal), `seed`. Output is always bipolar as of `wiki/plans/PropsAndMacroRedesign.md` Batch D (the old `polarity` selector is gone — `util.bipolarToUnipolar` covers the unipolar case explicitly if ever wanted downstream).

#### `random.drift` — Drift ✅
Slow, correlated, natural wander. **In:** `rate`; `amount`; `centering` (0 = free Brownian walk, 1 = strongly pulled to centre — literally the leak coefficient of a one-pole leaky-integrated walk). **Out:** `out` — `float·Bipolar [audio]`. **Structural:** `spectrum` (enum: brown, pink, white-filtered), `seed`. **Behavior:** clamped to stay in range.

## clock and seq

#### `clock.pulse` — Clock ✅
The master clock — a free-running or tempo-synced pulse generator with swing
and jitter, the timing source everything else in the `clock.*`/`seq.*` family
ticks from. **In:** `rate [audio]`; `swing`; `jitter`; `run : bool·true`; `reset : Event`. **Out:** `tick` — `Event`; `phase`. **Structural:** `rateMode` (enum: free, division), `division` (enum: 1/1…1/32), `seed` (not in the original catalog spec — added for the same reason `random.stepped`/`random.drift` have one: `jitter`'s randomness needs to be reproducible for a deterministic render, matching this codebase's universal convention for any node with internal randomness). **Behavior:** a phase accumulator that fires ticks at successive integer thresholds `k = 0, 1, 2, ...`; `swing` delays every odd-numbered tick's threshold by up to half a period (`k + swing*0.5`), which — because the next even threshold is always the next plain integer — preserves the pair's average period automatically, no separate compensation needed. `jitter` perturbs each threshold by a random offset redrawn once per tick (stable for the whole upcoming interval, not resampled every sample). In `division` mode, `rate` is read as beats/sec (wire `io.transport.tempo` straight in) scaled by the selected division — this node's own concrete design choice for a tempo-sync contract the catalog named but didn't pin down.

#### `clock.divide` — Divide ✅
Divides an incoming clock down by an integer factor — the standard
clock-divider for deriving slower rates from one master clock. **In:** `tick : Event`; `divide`; `reset : Event`. **Out:** `tick` — `Event` (engine port id `tickOut` — the catalog names both ports `tick`, but a same-id input/output pair on one node is a real, enforced engine invariant; the display label stays "Tick"). **Behavior:** fires every Nth incoming tick; `divide = 1` is a plain passthrough.

#### `clock.counter` — Counter ✅
Counts incoming ticks into an index (up, down, ping-pong, or random) — the
generic sequencing engine behind step sequencers and arpeggiators. **In:** `tick : Event`; `reset : Event`; `length`; `step`. **Out:** `index`; `normalised`; `wrapped` — `Event`. **Structural:** `mode` (enum: up, down, ping-pong, random), `seed` (same reasoning as `clock.pulse`'s — needed for `random` mode's determinism, not in the original catalog spec). **Behavior:** the generic sequencer engine — with `note.select`, an arpeggiator; with `data.lookup`, a step sequencer. `wrapped` fires once per lap in `up`/`down`/`pingPong` (crossing the boundary / bouncing off either end); it never fires in `random` mode — there's no meaningful "wrapped" for an independent uniform draw each tick, so this node doesn't invent one.

#### `seq.steps` — Step Sequencer 🚧
A classic step sequencer — a bank of per-step values stepped through by an
incoming clock, with gate derived from whether each step is non-zero. **In:** `tick : Event`; `reset : Event`. **Out:** `value` — `float·Bipolar [audio]`; `gate`; `trigger` — `Event`; `index`. **Structural:** `length` (1–**16**, not the catalog's 1–64 — see below), plus `step.0`…`step.15` (the node's own editable step bank, always bipolar as of `wiki/plans/PropsAndMacroRedesign.md` Batch D — the old `range` selector is gone). **Two deliberate, documented deviations from the catalog spec** (not silently narrower): the catalog's `steps` input (`Data(curve)`, optional) isn't built — no node in the engine produces a real `Data` value yet (`wiki/NODES.Status.md`'s cross-cutting prerequisite note) — so this node only has the fallback the catalog itself names, "the node's own editable step data," as fixed `ParameterDescriptor`s rather than a real `NodeContent`-backed bank; and `length` is capped at 16 instead of 64 (trivial to raise later — these are plain, individually-numbered parameters, not a wire-format array size). **Behavior:** `gate` is `abs(currentStepValue) > epsilon` — a step storing exactly 0.0 is a rest, since this data model has no separate per-step enable flag. Holds at step 0 until the first tick (which advances to step 1) — classic hardware step-sequencer "power-on shows step 1" behaviour, not an off-by-one bug.

#### `seq.euclid` — Euclidean ✅
Generates an evenly-spread (Euclidean) rhythm of pulses across a step count —
the standard algorithmic-rhythm generator, from four-on-the-floor to complex
polyrhythms depending on `rotate`. **In:** `tick : Event`; `steps`; `pulses`; `rotate`; `reset : Event`. **Out:** `trigger` — `Event`; `gate` (Boolean — the catalog leaves this port's type unmarked; every other "gate" port in the catalog is Boolean, so this follows that convention). **Behavior:** the standard `floor(i·pulses/steps) != floor((i-1)·pulses/steps)` construction (Bjorklund-equivalent onset pattern, no recursion needed); `rotate` shifts which step of the fixed pattern is read without moving the sequencer's own advancing index.

## note — the note stream

This family is what makes arpeggios, chords, scales, and audio-driven instruments
ordinary patching rather than built-in features. **7 of 10 built (Note Stream batch +
the `note.assemble` follow-up); `note.chord`/`note.hold`/`note.select` deliberately
deferred** — see the shared engine-limit note below. `note.assemble` is the ONE node
in the family that PRODUCES a `Note` stream from scratch rather than reshaping one
that already exists — before it, `io.noteIn` (real host MIDI) was the only way a
`Note` signal could ever originate; direct feedback (a design session on this
family's own gaps) named it as the single highest-leverage thing missing, since
nothing else lets a `clock.*`/`random.*`/`data.lookup` chain — or a future
`analysis.pitch`/`analysis.onset` pair — ever become a playable voice.

**A real, previously-unexercised engine limit this batch was the first to hit**:
`ExecutionPlan::BlockStep` has exactly one `noteInputBufferIndex`/`noteOutputBufferIndex`
field each — a node can carry at most one `Note` input and one `Note` output, total,
today. `note.chord` (needs to emit several simultaneous notes), `note.hold`/
`note.select` (need a real multi-note list handed over one cable), and the catalog's
literal `note.filter` (two `Note` outputs, `pass`/`reject`) all assume a multi-note
`Note` signal the engine can't represent yet. `note.filter` got a clean one-`Note`-
output redesign (see its own entry below); `note.chord`/`note.hold`/`note.select` were
deferred outright rather than forced through the same wall with a compromised, misleading
shape — a real design for multi-note `Note` signals (a growable `Note` port group? an
id-tagged polyphonic `NoteEvent`?) is separate, larger engine work, not something to
improvise as a side effect of three individual nodes. Full reasoning in `wiki/
MILESTONES.md`'s own Note Stream batch entry.

#### `note.gate` — Note Gate ✅
Extracts a plain gate/trigger signal from a note stream — the note-stream
equivalent of a keyboard gate, for driving envelopes and anything else that
just needs on/off and edges. **In:** `notes` — `Note`. **Out:** `noteOn`/`noteOff` — `Event`; `gate` — `bool`; `count`. **Behavior:** `count` (not elaborated by the catalog) is this node's own concrete design — a running tally of note-on events seen since reset.

#### `note.value` — Note Value ✅
Reads pitch and velocity out of a note stream as plain Control signals — the
bridge from `Note`-typed wiring to ordinary knobs and modulation. **In:** `notes` — `Note`. **Out:** `pitch`; `velocity` (`pressure`/`slide` not built — `NoteEvent` doesn't carry them, same "deliberately narrower, add a field once something drives it" reasoning `instance.allocate.voice` already established). **Structural:** `select` (enum: last, lowest, highest, first). **Behavior:** the mono-domain way to read a note stream — genuinely meaningful even against today's strictly-monophonic `io.noteIn`, since this node maintains its own small internal memory (up to 8 concurrently-held notes) by watching the incoming stream's start/stop edges over time, not just "whatever's live this sample." Inside an instanced region, the allocator's own outputs are used instead.

#### `note.quantize` — Scale Quantize ✅
Snaps incoming note pitches to a given scale in real time — the actual
"auto-scale" effect, whether on live MIDI or an algorithmically generated
stream. **In:** `notes`; `scale` — `Data(scale)`, required; `root`; `strength`. **Out:** `notes` (engine port id `notesOut` — see `note.transpose`'s own note on this). **Structural:** `direction` (enum: nearest, up, down), `applyTo` (enum: continuous, onNoteOnOnly). **Behavior:** the flagship consumer the Data Foundations batch was built for — `io.noteIn → note.quantize ← data.scale → instance.allocate.voice` is reference patch #2 ("MIDI remapped to a scale"), genuinely buildable now. `root` here is a *second*, independent knob from `data.scale`'s own `root` — a deliberate design, not a duplicate: `data.scale.root` rotates which pitch-classes are IN the published scale; this node's `root` is a plain post-quantization semitone offset, the same "movable key centre without touching the scale table" knob real quantizer modules commonly have. Assumes a standard 12-semitone octave for pitch reconstruction — a `data.scale` wired in with `octaveSize != 12` isn't meaningfully quantizable against absolute pitch by this node.

#### `note.transpose` — Transpose ✅
Shifts a note stream's pitch by a fixed number of semitones and/or octaves —
the note-stream equivalent of a transpose knob. **In:** `notes`; `semitones`; `octaves`. **Out:** `notes` (engine port id `notesOut`, not the catalog's literal `notes` — the catalog names both the input and output port `notes`, but a same-id input/output pair on one node is a real, enforced engine invariant, same as `clock.divide`'s own `tickOut`; the display label stays "Notes"). This port-id note applies identically to `note.quantize`, `note.filter`'s `notes` output, and `note.humanize` below — not repeated per entry.

#### `note.chord` — Chord 📋 *(deferred — see the shared engine-limit note above)*
Turns one incoming note into several simultaneous notes at fixed intervals or
scale degrees — the chord-generator primitive. **In:** `notes`; `spread`; `velocityFalloff`; port group `interval.0…interval.N`. **Out:** `notes`. **Structural:** `mode` (enum: fixed intervals, scale degrees).

#### `note.hold` — Hold Memory 📋 *(deferred — see the shared engine-limit note above)*
Latches whichever notes are currently held so they keep sounding after the
keys are released — the memory an arpeggiator reads from. **In:** `notes`; `hold : bool` (latch); `clear : Event`. **Out:** `held` — `Note`; `count`. **Structural:** `order`, `maxHeld`.

#### `note.select` — Select Note 📋 *(deferred — see the shared engine-limit note above)*
Cycles through a held set of notes by index or trigger — the part of an
arpeggiator that actually picks which held note plays next. **In:** `held` — `Note`; `index`; `trigger : Event`. **Out:** `notes`; `pitch`. **Structural:** `wrap`, `gateLength`.

#### `note.humanize` — Humanize ✅
Adds small, musically natural randomness to a note stream's timing, velocity,
and pitch, so a mechanically perfect sequence doesn't sound like one. **In:** `notes`; `timing`; `velocity`; `pitch`. **Out:** `notes` (engine port id `notesOut`). **Structural:** `seed`. **Behavior:** `timing` delays a note-on by up to 50ms (this node's own concrete bound), implemented as a small scheduled countdown rather than a full ring buffer — sufficient since the stream is monophonic, at most one note-on is ever pending. Only note-on timing is jittered, deliberately (note-off humanization is far less musically useful for the added complexity). `pitch` draws one detune per note (up to ±0.5 semitones), held for that note's whole duration, matching how real-world humanization plugins typically treat pitch.

#### `note.filter` — Note Filter ✅ *(redesigned — see the shared engine-limit note above)*
Gates a note stream by pitch and/or velocity range — a keyboard split or
velocity gate. **In:** `notes`; `lowPitch`/`highPitch`; `lowVelocity`/`highVelocity`. **Out (catalog):** `pass`/`reject` — `Note`. **Out (real today):** `notes` — `Note` (engine port id `notesOut`; the note verbatim when in range, fully suppressed — gate false, no start/stop — when out of range) plus `inRange` — `Boolean`, carrying the pass/reject decision as an ordinary signal. Captures the real, useful behaviour (a keyboard split, a velocity gate) without pretending the engine can carry two simultaneous `Note` streams off one node today.

#### `note.assemble` — Assemble Note ✅ *(new — the Note Stream follow-up, direct feedback's own top pick; revised same session — see below)*
Turns a plain gate + a tracked pitch into a real `Note` stream from scratch —
what makes an audio input playable as an instrument, and equally what lets a
`clock.*`/`random.*`/`data.lookup` chain drive a synth voice with no MIDI involved
at all. **Revised from its first cut, same session**: the catalog's own
`trigger : Event`/`release : Event` pair fought the grain — `gate` (Boolean) is the
convention every other note-adjacent thing here uses (`env.adsr`, `io.noteIn`'s own
translated MIDI, `note.gate`'s own "gate" output) — so this node now takes one `gate`
input instead of two Events; `adapt.gateLength` (new, below) turns a bare trigger into
a timed gate for whoever needs one. **In:** `gate : bool`; `pitch [audio]`; `velocity`; `confidence`; `confidenceGate`. **Out:** `notes` — `Note`. **Behavior — this node's own concrete design** (the catalog names the ports, not their exact contract): a note starts on `gate`'s rising edge, re-checked every sample the gate stays high (not just once, at the edge) — so a note whose confidence hasn't stabilized yet still starts the moment it becomes confident, without needing the gate to re-open — suppressed entirely while `confidence < confidenceGate`, so a low-confidence pitch-tracker reading can't spawn a bogus note. `pitch` is tracked continuously while held (vibrato/bend, or an algorithmically modulated pitch), `velocity` is captured once at the start instant. A note ends on `gate`'s falling edge, or — since a monophonic pitch tracker has no discrete note-off of its own — automatically once `confidence` drops back below `confidenceGate` while `gate` is still high; with neither wired (the plain generative case), `confidence`'s own unconnected fallback (1.0) never drops, so `gate` alone drives everything — ordinary MIDI semantics, no invented auto-timeout.

## math — all ✅

**Add-menu category: Math** (top level). Until 2026-10-04 every `math.*` node
reported `getCategory() == "Utility"`, so they were buried under Utility
despite their own ids; the same sweep also moved `excite.burst` (was
Generators) to Excite, `adapt.map` (was Utility) to Adapters, `view.listen`
(was Utility) to View and `io.output` (was Utility) to IO. Rule: a node's
Add-menu category follows its id family unless there's a stated reason not to.

Every node here is Control-typed and quantity-inherits from its first connected
input, EXCEPT `math.add`/`math.multiply` — see their own rows below,
`wiki/plans/DomainRedesign.md` Batch 3. For the rest: mismatched quantities are
rejected by `canConnect`.

| Node | Ports | Notes |
|---|---|---|
| `math.add` | `in.0…in.N` (growable, min 2) | sums every connected input — genuinely polymorphic (`PortPolymorphism::SignalAndQuantity`, same mechanism as `util.reroute`/`logic.select`): Audio or Control, whichever's wired (SignalType follows the lowest-numbered still-wired input's priority); Quantity is LENIENT, not the same priority rule — only resolves to a specific quantity when every quantity-declaring input unanimously agrees, any disagreement (or nothing declared) falls back to Dimensionless, so summing genuinely different real quantities (a pitch offset + a raw modulation amount, say — `buildInitPatchGraph()`'s own detuneSum/cutoffSum do exactly this) still works. `mix.sum` (see the `mix` section above) folded into this node outright at the same time. |
| `math.subtract` | `a`, `b` | `a − b` |
| `math.multiply` | `in.0…in.N` (growable) | multiplies every connected input together — same polymorphism as `math.add` above (this doc previously claimed audio-rate ring-mod already worked here; verified false against the real source and fixed at the same time, Batch 3 — its ports were fixed Control until then). Two Poly Audio signals from the SAME voice-allocator origin into this node's growable ports is exactly detuned self-ring-mod per note, no special case needed. |
| `math.divide` | `a`, `b`, `safeZero : bool·true` | division by zero returns 0, not NaN |
| `math.abs` | `in` | absolute value — rectifies a bipolar signal into unipolar |
| `math.clamp` | `in`, `low`, `high` | hard-limits a signal to a `[low, high]` range |
| `math.minmax` | `a`, `b`; structural `mode` (min/max) | the running minimum or maximum of two signals |
| `math.power` | `in`, `exponent` | curve shaping for modulation |
| `math.round` | `in`, `step`; structural `mode` (floor, ceil, nearest) | quantizes a continuous value to a step size |
| `math.modulo` | `in`, `divisor` | wrapping phase, cycling indices |
| `math.slew` | `in`, `rise`, `fall` | portamento, smoothing |

## logic — all ✅

| Node | Ports | Notes |
|---|---|---|
| `logic.and`, `logic.or`, `logic.xor` | `in.0…in.N` (growable bool, like `math.add`) → `out` (bool); `invert` (bool) | separate nodes since 2026-10-04 (replacing `logic.boolean`'s Op menu; patches migrate, `PatchSerializer` v9 → v10). Only wired inputs count; Xor over >2 is parity; `invert` gives Nand / Nor / Xnor |
| `logic.eventGroup` — Event Group | `in.0…in.N` (growable Event) → `out` (Event) | fires when any input fires; same-sample events merge into the strongest |
| `logic.edge` — Edge | `in` (bool) → `out` (Event); `mode` Rising / Falling / Both | a state's change as a moment |
| `logic.latch` — Latch | `set`, `reset` (Event) → `out` (bool) | set/reset flip-flop: repeated sets are harmless (unlike Toggle); reset wins a tie |
| `logic.not` | `in` → `out` (bool) | inverts a boolean signal |
| `logic.compare` | `a`, `b`, `tolerance`; structural `op` | `=` uses `tolerance`, not exact float equality |
| `logic.toggle` | `trigger`, `reset : Event` → `out` (bool); structural `initialState` (bool) | flips on each trigger and holds until the next one or a reset — a button-like latched state; reset and voice-restart both return to `initialState`, not unconditionally false |
| `logic.select` | `condition` (bool), `whenTrue`, `whenFalse` → `out` | a two-way switch/crossfade between two signals, driven by a boolean; audio crossfades over a few samples to avoid clicks |

## adapt — the conversion family — all ✅

These are the nodes `canConnect` inserts automatically where it can (see
`NODES.System.md` §4's matrix for exactly which pairs really auto-insert today vs.
still need placing by hand). Ordinary nodes the user can also place directly.

#### `adapt.map` — Map ✅ *(design/Map.png — redesigned 2026-10-04)*
The one rescaling adapter: `in` mapped linearly from **In Min…In Max** onto
**Out Min…Out Max**, clamped. Formerly `adapt.remap` ("Remap"); the old
two-parameter Map (a fixed 0–1 / −1–1 input onto `min`/`max`) was the special
case of this with the input range seeded from the source, so it no longer
exists. `canConnect` auto-inserts it for modulation → real quantity and for two
different real quantities (Pitch ↔ Frequency excepted), and
`connectWithAutoAdapt` seeds **both** ranges: the input range from whatever
feeds it (its declared bounds, else its polarity: Bipolar −1…1, Unipolar 0…1 —
for `adapt.audioToControl → adapt.map` that's the bridge's own Bipolar output),
the output range from the destination. Patches saved with either old node are
rewritten on load (`PatchSerializer` v7 → v8; an old Map fed by a Bipolar
source comes back with In Min 0 and needs it set to −1 by hand).
**In:** `in` — `Control` (the merged "In" pass-through row); `inMin`, `inMax`,
`outMin`, `outMax` — all real, wireable ports. **Out:** `out`.
**Body:** between In Max and Out Min, a fixed-size mapping diagram — the input
range as a vertical segment on the left, the output range on the right, both on
one common scale, joined end to end: a triangle when one range dwarfs the
other, a bowtie when the mapping inverts. A live readout of committed values,
in-progress slider drags, and (for wired ranges) the engine's own telemetry.
**Later:** the drawn-curve shaping NODE_CATALOG.md describes (`curve`, `morph`,
`clip` modes) grows this node rather than adding another one.

#### `adapt.normalise` — Normalise
Rescales a real-quantity value down into the 0–1 Unipolar range — the inverse of
Map, and what `canConnect` auto-inserts when a real-quantity source feeds a
modulation-range port. **In:** `in` (real quantity), `low`, `high`. **Out:** `out` — `float·Unipolar`. The inverse of Map.

#### `adapt.threshold` — Threshold
Watches a Control signal cross a threshold and fires an Event on the way up and
down — the Control → Event adapter `canConnect` auto-inserts. **In:** `in`; `threshold` (default: mid-range of the source); `hysteresis`. **Out:** `rise`/`fall` — `Event`; `above` — `bool`. The Control → Event adapter.

#### `adapt.sampleHold` — Sample & Hold
Freezes the input's value at each trigger and holds it steady until the next one
— turns a continuous signal into a stepped one, with optional glide between
steps. **In:** `in`; `trigger : Event`; `glide`. **Out:** `out`.

#### `adapt.audioToControl` — To Modulation ✅ *(new, `wiki/plans/AudioControlBridge.md`)*
Reads a raw waveform's instantaneous per-sample value and hands it out as an
ordinary Bipolar Control signal, scaled by `depth` — the mechanical Audio →
Control bridge `canConnect` auto-inserts for exactly that crossing (chained into
`adapt.map` when the destination is a real-quantity port). Deliberately NOT
`env.follower`: that node rectifies and smooths on purpose, throwing away the
very thing this node exists to keep — the waveform's instant-by-instant value,
which IS the modulator for FM, ring-mod, and audio-rate parameter modulation.
`env.follower` stays real, correct for amplitude/sidechain-style tracking, and
hand-placed only — the two nodes answer genuinely different questions ("how
loud is this, smoothed" vs. "use the actual waveform"), see `NODES.System.md`
§4 and its own entry above. **In:** `in` — `Audio`, mono only (a stereo source
needs `mix.downmix` first — a 3-adapter chain would exceed the two-adapter
ceiling); `depth : float·Unipolar·0–1·linear·1.0` (unpatched = full-strength
passthrough, the same "just as loud as before" contract `mix.gain.gain`
established). **Out:** `out` — `float·Bipolar` (clamped to −1…1).

#### `adapt.controlToAudio` — To Audio ✅ *(new, `wiki/plans/ControlToAudioBridge.md`)*
The reverse of the node above — reads an ordinary Control signal and hands it
out as an Audio signal, closing the "open symmetric question for later"
`AudioControlBridge.md` §6 explicitly deferred. Same mechanical/opinion-free
spirit as every other `adapt.*` node, never a creative DSP effect. **In:**
`in` — `Control`, polymorphic on quantity (`Unipolar` by default, `Bipolar`
once resolved — same mechanism `adapt.map`'s own `in` already uses). A real
quantity source (e.g. `Frequency`) is chained through `adapt.normalise`
first, seeded from the *source's* own range — the mirror image of
`adapt.audioToControl → adapt.map`'s own two-step shape, just with the
real-quantity step on the source side instead of the destination side.
**Out:** `out` — `Audio`, mono (a Control source has no stereo concept to
spread; the existing mono → stereo free broadcast handles a destination that
wants it). **Behavior:** `Bipolar` passes straight through, clamped to
−1…1; `Unipolar` (0..1) expands to the full audio swing via `in*2-1` first —
a 0..1 modulation source has no natural centre in audio terms, so using its
full excursion rather than only the positive half is the more useful
default against a destination range that's always exactly ±1.

#### `adapt.boolToControl` — From Bool ✅ *(new — direct feedback: "bool not being pluggable into control and ints... annoying")*
Maps a Boolean to either of two editable numbers — the mechanical Boolean → Control
bridge `canConnect` auto-inserts, replacing the `logic.select` + two `util.constant`
workaround outright rather than just easing it. **In:** `in` — `Boolean`. **Out:** `out` — `Control` (Dimensionless — a free pass into any destination, since the two edited values are what actually target it). **Structural:** `whenFalse` (default 0), `whenTrue` (default 1) — plain numbers, not wireable ports, matching `adapt.normalise`'s own `min`/`max` convention.

#### `adapt.pitchToFrequency` — Pitch to Frequency ✅ *(new — a real correctness fix, see below)*
#### `adapt.frequencyToPitch` — Frequency to Pitch ✅ *(new, the inverse)*
The exact exponential MIDI-pitch↔Hz conversion (A4 = pitch 69 = 440Hz) — `canConnect`
now prefers these over the generic `adapt.map` specifically for a `Pitch ↔
Frequency` connection. Direct feedback surfaced a real, previously undiscovered
correctness gap: `adapt.map` is a plain *linear* interpolation between two seeded
endpoints, but pitch-to-Hz is exponential (each semitone is ×2^(1/12)) — the old
auto-inserted remap was quietly wrong for every pitch value between its two seed
points. **In:** `pitch : float·Pitch·0–127·60` / **In:** `frequency : float·Frequency·0.01–20000Hz·440`.
**Out:** `frequency` / **Out:** `pitch` respectively. No seeding needed at all — the
formula is fixed, not range-dependent.

#### `adapt.gateLength` — Gate Length ✅ *(new — direct feedback: "duration for the note held... using 2 clocks... too complicated")*
A monostable trigger-to-gate — opens a Boolean gate for a set number of seconds from
an incoming trigger, the standard "Gate Length" utility every modular rack has one of.
**In:** `trigger : Event`; `length : float·Time·0–10s·0.2s`. **Out:** `gate` —
`Boolean`. **Behavior:** a new `trigger` arriving before the gate closes retriggers —
restarts the countdown from the full `length` rather than queuing or extending
additively. Deliberately not auto-inserted by `canConnect` for a plain `Event →
Boolean` wire-drag — `length` is a real timing choice, not a mechanical, opinion-free
crossing (the same bar `env.follower` failed for `Audio → Control` auto-insertion).

## data — producing and reading buffers

Prerequisite for the sampler, wavetable oscillator, every resonator, and scale
quantisation. **The Data Foundations batch (`data.scale`/`data.table`/`data.lookup`) is
the pathfinder for the whole `Data`-publishing pipeline** — before these three, no node
in the engine had ever produced a real `Data` value; `Node::getDataPublisher()`/
`setDataInput()` and `GraphCompiler.cpp`'s Data-connection wiring exist because of this
batch (`wiki/NODES.Status.md`'s own cross-cutting prerequisite note, now closed). The
remaining five (`load`/`material`/`analyseModes`/`record`/`eqToCurve`) stay catalog-only.

#### `data.load` — Load File 📋
Reads a file from disk into a `Data` buffer, tagged by what it's interpreted as
— the on-ramp for real samples, wavetables, and impulse responses. **Out:** `data` — `Data`, tagged by content. **Structural:** `file`, `interpretAs` (enum: sample, wavetable, impulse response), `frameSize`, `normalise`, `rootNote`. **M27.**

#### `data.table` — Table / Curve ✅
**Out:** `data` — `Data(curve)`. **Structural:** `resolution` (2–32, not the full generality the name might suggest — see below), `loop` (carried, not yet consumed by anything), plus a fixed bank `point.0`…`point.31`. **Behavior:** the shared curve source of envelopes, surface profiles, sequencer lanes, LFO shapes, waveshaper transfer functions, remapping curves — editing it updates every place it's used. **Deliberate interim shape:** "the curve itself" is a fixed 32-point parameter bank (same pattern `seq.steps`' own step bank uses), not real `NodeContent` — that third category (`NODES.System.md` §3) doesn't exist as code yet; this node ships ahead of it rather than waiting, same reasoning `seq.steps` already documents.

#### `data.scale` — Scale ✅
**In:** `root`. **Out:** `data` — `Data(scale)`. **Structural:** `scale` (enum: major, the church modes, pentatonics, blues, whole tone, chromatic — **12 named scales**; "harmonic series" and "custom" are the two catalog items deliberately deferred, both real gaps not silent ones — see below), `octaveSize` (generalizes the 12-tone patterns to other divisions by proportional scaling, not just padding). **A real, documented RT-safety limit:** `root` is a genuine wireable port, but its *live* cable value is never read on the audio thread — rebuilding a `Data` buffer means a heap allocation, forbidden there (CLAUDE.md rule 2); only the value applied via `setParameter()` (the node's own inline slider) actually republishes. A real worker-thread content-rebuild pipeline (`NODES.System.md` §8's own still-open item) is what closes this properly — not built as a side effect of this one node.

#### `data.material` — Material ✅ *(PM Core batch 2)*
The physical-modelling counterpart of `data.scale` — describes a resonating
object's mode set from a handful of physical parameters rather than a
frequency list. **In:** `stiffness`; `density`; `damping`; `size`; `inharmonicity`; `irregularity`. **Out:** `data` — `Data(modal-set)`, `stride = 3` per mode (`ratio`, `amplitudeWeight`, `decayWeight`). **Structural:** `geometry` (enum: string, bar, tube, membrane, plate, irregular solid), `modeCount` (default 32, max 64), `preset` (enum: wood, glass, metal, stone, ceramic, bone, ice, custom; each sets a fixed amplitude-rolloff exponent, this node's own documented material characterization), `seed`. **A concrete, tested contract this session had to design** (the catalog names six knobs, not what they do): `string`/`tube`/`bar`/`membrane` use real or closely-approximated closed-form mode ratios (plain harmonics, odd harmonics, the free-free-beam asymptotic formula, hardcoded Bessel-zero ratios); `plate` reuses the membrane's own table squared (a documented simplification — a `k²` bending dispersion over the membrane's own `k¹` nodal pattern, not a literal 2D mesh solve); `irregularSolid` has no closed form at all, built from deterministic seeded gaps. `stiffness`/`inharmonicity` jointly stretch higher modes sharp (the classic stiff-string formula); `irregularity` adds deterministic per-mode jitter; `density`×`damping` sets a per-mode relative decay falloff. Same RT-safety limit `data.scale`'s own `root` port has: every input is a real wireable port, but only a `setParameter()`-driven change actually rebuilds/republishes. **`size` is a declared port, not yet consumed** — a real, deliberate MVP gap, not a silent one.

#### `data.analyseModes` — Analyse Modes 📋
Extracts a mode set directly from a recorded sample — the real-world
counterpart of `data.material`'s hand-specified physics. **In:** `data` — `Data(sample)`. **Out:** `data` — `Data(modal-set)`. **Structural:** `modeCount`, `windowStart`, `windowLength`, `decayEstimation`. **Behavior:** hit a rock, drop in the file, play the rock. **M23.**

#### `data.lookup` — Lookup ✅
**In:** `in [audio]`; `data` — `Data`, required (accepts `Curve` or `Scale` — the two tags this batch's producers actually emit); `dataB` — `Data` (optional morph target; a tag mismatch silently falls back to `data` alone rather than rejecting at runtime, since nothing enforces "required" ports today); `morph [audio]`. **Out:** `out`. **Structural:** `mode` (enum: nearest, interpolate, index, wrap-index), `edgeMode` (clamp, wrap). **Behavior — this node's own concrete mode contract** (the catalog names the four modes, not their exact semantics): `nearest`/`interpolate` treat `in` as a normalised bipolar position (-1..1 remapped to 0..1, then scaled across the buffer — always bipolar as of `wiki/plans/PropsAndMacroRedesign.md` Batch D, the old `polarity` selector is gone); `index`/`wrapIndex` treat `in` as a literal element index, no position remap applied (an index has no natural normalised meaning); `wrapIndex` always wraps regardless of `edgeMode`, plain `index` respects it. Only `stride() == 1` buffers (both real producers) are meaningfully supported today.

#### `data.record` — Record 📋 *(Correction 2)*
Captures live audio into a `Data(sample)` buffer, real-time-safe — the node
that turns a recorded performance into something every other `Data`-consuming
node can play back or analyse. **In:** `in` — `Audio`; `trigger : Event`; `stop : Event`; `threshold` (auto-start on signal); `maxLength`. **Out:** `data` — `Data(sample)` (published on stop); `recording` — `bool`; `level [audio]`. **Structural:** `preRoll`, `channels`. **Behavior:** writes into a buffer preallocated from `maxLength`; on stop, hands off to a worker thread that writes an immutable asset. **Native:** real-time capture with preallocation and thread handoff — nothing allocates on the audio thread, recording never blocks it.

#### `data.eqToCurve` — EQ to Curve 📋 *(Correction 2)*
Renders an EQ's frequency response as a plain curve, one-way — lets a shape
designed in the EQ editor drive a waveshaper, remap, or tract profile. **In:** `eq` — `Data(eq-curve)`; `lowFrequency`/`highFrequency` (span to render); `normalise : bool·true`. **Out:** `data` — `Data(curve)`. **Behavior:** renders an EQ's magnitude response as a plain curve so a shape designed in the EQ editor can drive a waveshaper/remap/tract profile — one-way, explicit, never automatic (see `NODES.System.md` §4's matrix).

## analysis 📋 (all — M25)

#### `analysis.onset` — Onset Detector
Fires an event whenever a new sound event starts in an incoming signal — the
audio-to-trigger primitive behind hit detection and audio-driven sequencing. **In:** `in` — `Audio`; `sensitivity`; `holdOff`. **Out:** `onset` — `Event`; `strength`. **Structural:** `method` (enum: energy, spectral flux).

#### `analysis.pitch` — Pitch Tracker
Continuously estimates the fundamental pitch of an incoming signal — turns a
sung or played note into a Control-rate pitch value with a confidence score. **In:** `in`; `lowestPitch`; `smoothing`. **Out:** `pitch [audio]`; `confidence`; `voiced` — `bool`. **Structural:** `method` (enum: autocorrelation, YIN), `windowSize`.

#### `analysis.level` — Level
Tracks a signal's loudness with selectable detection (peak, RMS, true peak) —
the general-purpose metering/level-driven-modulation source. **In:** `in`; `attack`, `release`. **Out:** `level [audio]`; `peak`; `clipped` — `bool`. **Structural:** `detection` (enum: peak, RMS, true peak).

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
`maxInstances`, `seed` (real patch-level determinism for `random1`/`random2` as of
`09-28-InstanceAllocator.2` — each spawn draws from a fresh generator seeded by
`(seed, instanceIndex)`, not a wall-clock-seeded persistent one).

#### `instance.allocate.swarmPopulation` — Swarm (Population) ✅ *(new, Domain Extensions batch, 2026-10-01)*
Opens an instanced region with a fixed, always-live count — no spawn/release
mechanism at all (unlike Voice, nothing ever calls `noteOn`/`noteOff` for
this origin; `populationSize` of its physical slots simply report `gate =
true` from the very first block onward, the rest report `false`). **In:**
none. **Out** (all `polyOnly`): `gate`, `instanceIndex`, `instanceAge`,
`random1`, `random2`, `start`, `stop`, `position` — 8 ports (`position`:
`Control`, Bipolar — pan-like; `archive_docs/DOMAINS.md` §4's own "spatial
position model" scoped down to this one plain per-instance value for MVP,
not a full spatial subsystem). **Structural:** `populationSize` (1-64,
default 8 — deliberately not named `maxInstances`: there's no
demand-driven ceiling to distinguish from the live count, this many are
*always* live), `seed` (same `(seed, instanceIndex)` determinism
`09-28-InstanceAllocator.2` built for Voice, reused verbatim via the shared
`combineInstanceSeed` helper).

#### `instance.allocate.swarmTransient` — Swarm (Transient) ✅ *(new, Domain Extensions batch, 2026-10-01)*
Opens an instanced region whose spawn source is a plain `Event` (`spawn`), not
a `Note` — no pitch/velocity concept (the shared `InstanceOriginNode`
interface's `spawnInstance()` accepts but ignores them). Each firing restarts
this instance's own gate/age/random state and auto-releases itself after
`duration` seconds elapse — there's no separate release input; a transient is
inherently self-terminating, unlike Voice's explicit note-off. The first
non-Voice origin to exercise `PluginProcessor`'s internal-trigger-relay
dispatch, generalized from a concrete `InstanceVoiceNode*` to the shared
`InstanceOriginNode*` interface for exactly this (`findAllocatorNode`/
`renderOriginVoiceRange`). **In:** `spawn` — `Event`. **Out** (all
`polyOnly`): `gate`, `instanceIndex`, `instanceAge`, `random1`, `random2`,
`start`, `stop`, `position` — 8 ports (same shape as Swarm-population).
**Structural:** `maxInstances` (1-64, default 8 — a real, demand-driven
ceiling, unlike Swarm-population's fixed count), `seed`. **Non-structural:**
`duration` (seconds, default 0.1, live-editable). **Known scope limit** (see
the node's own doc comment, `InstanceSwarmTransientNode.h`): a single
internal generator feeding one `spawn` port can only drive one in-flight
transient's own release-tracking at a time (slot 0's own compiled copy can
only represent one lifecycle), so rapid overlapping spawns beyond that
degrade gracefully to ordinary voice-stealing rather than a true leak — not
a correctness issue for the realistic case (spawning slower than `duration`).

#### `instance.allocate.trigger` — Trigger ✅ *(new, Domain Extensions batch, 2026-10-01)*
Opens an instanced region with exactly one instance at a time — no
`maxInstances` parameter at all (implicitly 1, matching Voice's own
precedent of only exposing parameters that actually do something). Spawn
source is a plain `Event` (`trigger`), not `Note`. No `position` output —
not a swarm type. Deliberately no auto-release (unlike Swarm-transient's own
`duration`): the gate stays asserted until the next trigger fires. "A second
trigger re-triggers the same instance" falls out of `maxInstances == 1` for
free, via `instance.sum`'s shared `VoiceManager`'s own existing stealing
policy — no special-casing needed in the node itself. **In:** `trigger` —
`Event`. **Out** (all `polyOnly`): `gate`, `instanceIndex`, `instanceAge`,
`random1`, `random2`, `start`, `stop` — 7 ports. **Structural:** `seed` only.

#### `instance.sum` — Voice Sum ✅ *(renamed from `instance.mix`, wiki/plans/DomainRedesign.md Batch 1b — C++ class name (`InstanceMixNode`) unchanged)*
Closes an instanced region. **In:** `in` — `Audio`, `polyOnly`. **Out:** `out` —
`Audio` (Scalar). **Structural:** `mode` (enum: sum, average). `DomainSplitter` (one
allocator/one mix region per graph, full stop) is gone — `MultiplicityResolver`
(DomainRedesign.md) supports up to `maxOrigins` (4) simultaneous, independent
allocator/sum pairs per graph, each its own physical `ExecutionPlan`, each either
driven by real MIDI (`io.noteIn`) or an internal `clock`→`seq`→`note.assemble` chain
with no MIDI involved at all — never both at once for the same origin.

## util

#### `util.constant` — Constant ✅
A fixed value with no input at all — the basic "just a number" source for
biasing, offsetting, or feeding a structural default into a modulation chain. **Out:** `out` — `Control`, contract configurable on the node.

#### `util.macro` — Macro ✅ *(real as of `wiki/plans/UtilMacro.md` — ADR-0030 amends ADR-0015, doesn't reverse it)*
A host-automatable, smoothed version of Constant — claims one of 32 fixed
host-automation slots (`slot`; -1 means unclaimed) and exposes that slot's own
smoothed value through an ordinary, freely-wireable `Control` output, real DSP
all the way through rather than a side-channel poke onto some other node's
parameter. **Out:** `out` — `Control`, contract configurable on the node
(`min`/`max`/`isInteger`/`quantity`). **Structural:** `slot`, `min`, `max`,
`isInteger`, `quantity`. Two placed macros can never claim the same slot
(rejected at compile time, naming both node ids); deleting one frees its slot
on the very next recompile. Dragging an unconnected `Control`/`Event`/`Boolean`
input out and releasing on empty canvas auto-creates one, pre-configured from
that port's own contract (bounds/unit/quantity copied verbatim when the port
declares them), already wired in and slotted, in one undo step — and every
claimed macro shows up as a knob in the top-bar panel automatically, no
further step needed.

#### `util.reroute` — Reroute ✅
A pure passthrough with no fixed type of its own — a cable-routing waypoint for
untangling a busy layout, nothing else. **In:** `in`. **Out:** `out` (adopts the source's signal type *and* quantity — a real, polymorphic port, not hardcoded Audio). Layout waypoint. "Not connectable" reports against this node are tracked as a UI-layer bug in `NODES_Gaps.md`, not a missing feature — the engine-side implementation reads correctly.

#### `util.unipolarToBipolar` / `util.bipolarToUnipolar` — Unipolar to Bipolar / Bipolar to Unipolar ✅ *(`wiki/plans/PropsAndMacroRedesign.md` Batch D)*
Thin, explicit, self-labeled converters between the two normalised modulation
ranges — `in [0..1] -> out [-1..1]` and the inverse, clamped not extrapolated.
**In:** `in`. **Out:** `out`. No structural parameters. Added alongside
removing `random.stepped`/`seq.steps`/`data.lookup`'s old per-node Unipolar/
Bipolar selectors (modulation is always bipolar by default now) — a thin
wrapper over what `adapt.map` already does (same shape as `adapt.normalise`/
`adapt.map`/`adapt.pitchToFrequency`), for readability in the Add-menu rather
than filling a capability gap: a Unipolar<->Bipolar quantity mismatch already
auto-resolves via `adapt.map`'s own generic fallback. **Deliberately not
auto-inserted** by `connectWithAutoAdapt` — manual placement only.

## view — listening and looking — all ✅

#### `view.listen` — Listen
Routes whatever's plugged into it straight to the monitored output, so you can
audition one point in the graph in isolation without rewiring anything. **In:** `in` — `Audio`. **Behavior:** auditions this point in the graph, replacing normal output while active. **Taps:** `in`.

#### `view.spectrum` — Spectrum
A live FFT display of a signal's frequency content — the standard "watch the
spectrum" view, for seeing what a filter, distortion, or synthesis stage is
actually doing to the harmonic content. **In:** `in` — `Audio`. **Structural:** `fftSize`, `tilt`, `averaging`.

#### `view.meter` — Meter
A live level readout of whatever's wired into it — peak, RMS, true peak, or a
histogram, for watching loudness rather than shape. **In:** `in` — `Audio` or `Control`. **Structural:** `mode` (enum: peak, RMS, true peak, histogram).

#### `view.cycle` — Cycle ✅ *(2026-10-04 — replaces `view.scope` and `view.glance`, both removed)*
The placeable **phase-locked** viewer. A polymorphic pass-through (Audio or
Control — the same node watches an audio path or an LFO) whose horizontal axis
is the phase of the nearest phase source upstream, over a fixed 4 cycles: a saw
through a filter reads as the filtered saw, standing still, at any rate. No
trigger, no time window — the engine already knows the phase
(`ExecutionPlan::resolvePhaseSources`, "phase follows the cable"); the cable's
real samples are folded into phase bins (equivalent-time sampling for fast
signals, drawn in behind the playhead for slow ones). Fixed ±1 scale with
headroom; Auto/On/Off playhead. Empty when nothing upstream has a phase.
Patches with the old nodes load with scopes dropped and glances spliced out of
their cables (`PatchSerializer` v8 → v9).

### Per-type viewers — pass-through, one per signal type

Each splices into a cable (real `in` → `out`, value unchanged), has no title,
and is the **Ctrl/Cmd-click default viewer** for its port type — one table,
`graphStore.ts`'s `DEFAULT_VIEWER_BY_PORT_KIND`, keyed by the same
classification that colours the port.

#### `view.ripple` — Ripple ✅ *(design/Visualization/Ripple.png)*
**In/Out:** `Event`. Expanding rings, one per event.

#### `view.count` — Count ✅ *(design/Visualization/Count.png)*
**In/Out:** integer `Control`. The current value as a number, editable Min/Max footer.

#### The three scrolling-history viewers
One shared panel (`ScopeHistoryBody.tsx`) and one shared engine setting
(`ViewHistoryWindow.h`: a structural `timeWindow`, 0.01–30 s, auto-chosen on
connection from the signal's observed period, `PreviewKind::RollingHistory`
min/max-decimated columns that never drop a peak). They differ only in vertical
scale and trace style:

#### `view.scope.control` — Scope ✅ *(design/Visualization/Scope1.png)*
**In/Out:** plain (real-quantity) `Control`. White line; editable Min/Max
range seeded from the source's declared bounds, else observed then frozen.
**Structural:** `view.scope.control.timeWindow`.

#### `view.scope.modulation` — Scope (Modulation) ✅ *(design/Visualization/ScopeMod.png)*
**In/Out:** `Control`, adopting the source's *quantity* (polymorphic, so a
Unipolar source splices in without an adapter; Bipolar when unconnected).
Everything orange. The range autofills from the source's polarity (bipolar
−1…1, unipolar 0…1), still editable. A horizontal **centre line**
(`properties["viewer.center"]`, editable; defaults to the middle of the range —
0 for bipolar, 0.5 for unipolar) and the area between trace and centre filled
dim orange under a brighter line: a signed value, not a level.
**Structural:** `view.scope.modulation.timeWindow`.

#### `view.gate` — Gate ✅ *(design/Visualization/Gate.png)*
**In/Out:** `Boolean`. Everything blue, `?` glyphs. Fixed, non-editable
TRUE/FALSE scale; a square-edged region filled from FALSE up to TRUE wherever
the value was true. **A brief true state is never dropped:** the engine folds
every sample into its column's (lo, hi), and the UI draws on the real
screen-pixel grid — any pixel column holding a true sample is filled, at every
canvas zoom — so a single-sample pulse is always a visible sliver.
**Structural:** `view.gate.timeWindow`.

## factory — content-owning nodes 📋 (all — Correction 2, none built)

Shape, unwrap contract, and the shared editor infrastructure are in
`NODES.System.md` §8 — specs only, here.

#### `factory.eq` — Spectral Factory (EQ)
A multi-band parametric EQ with its own editor, that unwraps into ordinary
`filter.*` nodes once you need to touch what's inside. **Content:** ordered band list `{id, type, frequency, gain, q, enabled, externalized}`. **In:** `in` — `Audio`; `tilt [audio]`; `mix`; `outputGain [audio]`. **Out:** `out` — `Audio`; `curve` — `Data(eq-curve)`. **Structural:** `maxBands` (default 16), `oversampling`. **Unwrap:** one band → matching `filter.peak`/`shelf`/`svf`/`allpass`; whole factory → the series chain + `mix.gain`. **Deferred within this factory:** dynamic bands, mid/side, linear phase (none unwrap into today's primitives).

#### `factory.curve` — Curve Factory
A drawn-curve editor that publishes a `Data(curve)` and unwraps into
`data.table` plus whatever reads it. **Content:** points + per-segment tension, loop/polarity, optional morph-target shape. **In:** `morph [audio]`. **Out:** `data` — `Data(curve)`. **Structural:** `resolution`. **Unwrap:** → `data.table` + the implied consumer (`lfo.shape`/`env.curve`/`adapt.map`/`shape.waveshaper`).

#### `factory.wave` — Wave Factory
A single-cycle waveform/harmonic editor for building a wavetable, unwrapping
into `data.table`/`data.load` feeding `osc.wavetable`. **Content:** one or more single-cycle frames (waveform or harmonic amplitude/phase). **In:** `position [audio]` (frame scan). **Out:** `data` — `Data(wavetable)`. **Structural:** `frameSize`, `frameCount`, `normalise`. **Unwrap:** → `data.table`/`data.load` + `osc.wavetable`.

#### `factory.sample` — Sample Factory
A sample/slice editor with playback built in, unwrapping into one
`sampler.player` per slice. **Content:** an asset reference, slice markers, loop points, root note, gain, trim. **In:** `trigger : Event`; `slice`; `pitch [audio]`; `start [audio]`; `level [audio]`. **Out:** `out` — `Audio`; `data` — `Data(sample)`; `ended` — `Event`. **Structural:** `interpolation`, `loopMode`. **Unwrap:** one slice → the asset + a configured `sampler.player`; whole factory → the asset + one `sampler.player` per slice, selected by `logic.select`. Recording is the separate `data.record` node, not part of this editor.

#### `factory.notes` — Notes Factory
An editor for chords, arpeggios, step patterns, and scales in one place,
unwrapping into the ordinary `note.*`/`clock.*`/`data.scale` nodes that
actually play them. **Content:** chord definitions, arpeggio patterns, step patterns, scale definitions. **In:** `notes` — `Note`; `tick` — `Event` (external clock; internal when unconnected); `rate [audio]`; `gateLength`; `swing`; `humanize`. **Out:** `notes` — `Note` (always — a second representation of a note is exactly the parallel-truth problem `RECONCILIATION.md` warned about; plain pitch/gate values come from `note.value` or the allocator, both already in the catalog); `scale` — `Data(scale)`. **Unwrap:** arpeggio → `note.hold`+`clock.pulse`+`clock.counter`+`note.select`; chord → `note.chord`; scale → `data.scale`+`note.quantize`; humanise → `note.humanize`. **Deferred within this factory:** MIDI clips (needs a timeline/piano-roll/host transport sync — a project of its own).

#### `factory.material` — Material Factory
An editor for a resonating object's material, with hand-tunable per-mode
overrides on top of `data.material`'s own generated set, unwrapping into
`data.material` + `resonator.modal`. **Content:** geometry, material preset, hand-made per-mode overrides. **In:** `stiffness`, `density`, `damping`, `size`, `inharmonicity`, `irregularity` (as `data.material`, all modulatable). **Out:** `data` — `Data(modal-set)`. **Structural:** `geometry`, `modeCount`, `seed`. **Unwrap:** → `data.material` (or `data.analyseModes` if from a recording) + `resonator.modal`.

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
| **Water** | Rain, a stream, a boil | `noise.dust` → `instance.allocate.swarmTransient` (real as of the Domain Extensions batch, 2026-10-01) → Bubble per instance, radius from `random` → `instance.sum` | 📋 — the allocator is real now; Bubble itself still isn't built |
| **Crackle** | Fire, static, ice | `noise.dust` → `excite.burst` → `resonator.modal` with a small stone/ceramic set | 📋 |
| **Scrape** | Stone dragged across asphalt | `excite.contact` → `resonator.modal` with `data.material` (stone, irregular) → `space.reverb` | 📋 |
| **Cicada** | One insect | `clock.pulse` with jitter → `excite.burst` → `filter.formant` → body from `resonator.modal` | 📋 |
| **Cicada Field** | A population of them | `instance.allocate.swarmPopulation` (real now, see the Water row above) → Cicada per instance, rate/pitch from `random.drift`, placement from `panPosition` | 📋 — the allocator is real now; Cicada itself still isn't built |
| **Breath / Wind** | Wind, breathing, flutes | `excite.breath` → `resonator.tube`, contour from `env.curve` | 📋 |
| **Bowed String** | Violin-like | `excite.stickSlip` ↔ `resonator.string`, coupling loop closed through `motion` | 📋 |
| **Struck Body** | Drum, bell, plate | `excite.mallet` ↔ `resonator.plate` or `resonator.modal` | 📋 |
| **Hex Guitar Front End** | Six strings to six note streams | six `io.audioIn` channels → `analysis.onset` + `analysis.pitch` → `note.assemble` per string | 📋 |
| **Init Patch** | Ordinary subtractive synth | `io.noteIn` → `instance.allocate.voice` → `osc.analog` ×2 → `filter.ladder` → `env.adsr` (×2: amp + filter cutoff) → `instance.sum` → `space.pan` → `io.output` (one real stereo cable, `pan.out` → `masterOut.in`) | ✅ real hand-built graph, genuinely stereo; 📋 not yet a loadable `stock.*` asset — no `space.reverb` tail yet (M28) |
| **Voiced self-oscillation (cat purr)** *(Correction 1's new coverage item)* | The hardest test in the set | `env.curve` (breath pressure) → `random.drift` (stiffness jitter) + `lfo.shape` (~26Hz stiffness modulation, for entrainment) → `excite.vocalFolds` → `flow` gates `noise.colored` through `mix.gain` (aspiration) → `resonator.junction` splits `resonator.tract` (nasal route) vs. a closed branch (antiresonances) vs. `resonator.modal` (body conduction) → `mix.crossfade` (microphone position) | 📋 — exercises audio-rate physical-parameter modulation, emergent oscillation thresholds, source–resonator coupling, branched waveguides, `Data` as a geometric profile, flow-gated noise, two sources sharing one tract, sub-30Hz fundamentals. **Testing note:** self-oscillating/chaotic models are deterministic but rounding-sensitive — two compilers or an enabled FMA path diverge within seconds, so bit-exact golden renders don't work here; verify statistically (measured f₀/spectral envelope/jitter/shimmer within tolerance, oscillation threshold within a pressure window) or CI failures become indistinguishable from physics. |

# Reference patches: coverage

Every reference patch from the original planning prompt builds from Part A, with no
missing primitives (once M23–M29 land):

1. **Karplus-Strong with single-sample feedback** — `delay.line` + `filter.onepole` + `mix.gain`, closed as a per-sample region; `resonator.string` is the playable native version.
2. **MIDI remapped to a scale** — `io.noteIn` → `note.quantize` ← `data.scale`.
3. **Arpeggiator and chords** — `note.hold`, `clock.counter`, `note.select`, `note.chord`.
4. **Struck body with material data** — `excite.mallet` → `resonator.modal` ← `data.material`.
5. **Stone on asphalt** — `excite.contact` → `resonator.modal`, `util.macro` driving speed/pressure.
6. **Transient and persistent swarms** — `instance.allocate.swarmTransient`/`instance.allocate.swarmPopulation`, real node types as of the Domain Extensions batch (2026-10-01) — see the Water/Cicada Field rows above for what still blocks each full reference patch.
7. **Ordinary subtractive patch** — `osc.analog`, `filter.ladder`, `env.adsr` — **built, playable today** as Init Patch.
8. **Per-voice effects** — `shape.waveshaper`, `delay.line`, `space.reverb` placed before `instance.sum`.
9. **Hexaphonic guitar** — `io.audioIn` per channel → `analysis.onset` + `analysis.pitch` → `note.assemble` → `instance.allocate.voice` (each string gets its own voice/mix pair, not a shared one — avoids needing a Note-stream-merge node).
10. **Voiced self-oscillation (cat purr)** — see Part B, above.

# Deliberately deferred

- **Spectral family** (FFT-domain nodes): the `Spectral` signal type stays reserved and unused until it has a concrete design.
- **The group system itself**: `stock.*` groups ship once the group-inlining mechanism exists (M29) — this catalog doesn't depend on them.
- **Nested allocators** beyond one level: supported by the model, opt-in later.
- **MIDI and note output** to the host: the `Note` type is ready for it; the I/O node isn't specified yet.
- **Additive and FM-operator oscillator families**: `osc.sine` + phase-modulation inputs covers the common cases until a dedicated family earns its place.
