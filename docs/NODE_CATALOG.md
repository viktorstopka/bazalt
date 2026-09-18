# Bazalt — Node catalogue (rewrite)

Written against the reconciled model: unified value contract, everything-is-a-port, real `canConnect` with adapters, `Data` as a first-class type, Instance Allocator / Voice Mix split.

This is not an MVP list. It is the shape the instrument should have when it is genuinely usable: enough primitives that the reference patches build without gaps, and enough parameters per node that each one is playable rather than a stub. Nodes will keep being added, but nothing here is placeholder.

## Notation

`portName : kind·quantity·range·curve·default` — for `Control` ports. `Audio`, `Event`, `Note`, `Data(tag)` for the others.

- **All value ports are modulatable and have an inline default.** The compiler supplies a constant when unconnected; node DSP only ever sees a connected input.
- **Structural** lists the settings that are not ports, because they reallocate, change topology, or swap the kernel. Nothing else is a parameter.
- `[audio]` marks a port that accepts audio-rate modulation; unmarked control ports are block-rate.
- Domain is `any` unless stated.
- **Taps** are telemetry outputs for live visualisation, not ports.

## Decisions this catalogue settles

**Audio is one channel.** An `Audio` port carries a mono signal. Stereo is made explicitly (two cables, or a `space.pan` producing an L/R pair), because implicit channel counts are how modular systems become unpredictable. Ports may declare `channels: 1 | 2 | inherited`; where a node is naturally stereo (reverb, width), it declares stereo ports. Mono → stereo is free (duplicated); stereo → mono requires `mix.downmix`. This is also what makes a six-channel guitar pickup ordinary rather than special: `io.audioIn` exposes one output per physical channel.

**Density and rate are `Frequency`.** Events per second is a frequency; there is no `Dimensionless` escape hatch for it.

**Display units are never a second value.** Gain is linear internally, shown in dB. There is no parallel `gainDb` parameter anywhere.

**Curves are shared data, not a property of one node.** A drawn shape is a `Data(curve)` buffer produced by `data.table` and read by anything that takes one: remapping, LFO shapes, envelopes, waveshaper transfer functions, sequencer lanes, surface profiles. The scalar `curve` field in a value contract is a separate, smaller thing — it only describes how a slider's gesture maps onto its range, never what happens to a signal. Anywhere a drawn curve can be read, two of them plus a `morph` port can be interpolated.

**Two namespaces of excitation.** `excite.*` nodes produce a signal that drives a resonator. `resonator.*` nodes are the resonating bodies. Friction and collision models that genuinely need to feel the resonator expose a `feedback` input for that purpose.

---

# Part A — native nodes

## Index

| Family | Nodes |
|---|---|
| `io.*` | audioIn, output, noteIn, control, transport |
| `osc.*` | analog, sine, wavetable |
| `sampler.*` | player, granular |
| `noise.*` | colored, dust |
| `excite.*` | impulse, burst, pluck, mallet, stickSlip, breath, contact |
| `resonator.*` | modal, string, tube, plate, comb |
| `filter.*` | svf, ladder, onepole, allpass, shelf, peak, formant, dcBlock |
| `shape.*` | waveshaper, clip, fold, rectify, crush |
| `delay.*` | line |
| `space.*` | reverb, diffuser, pan, width |
| `mix.*` | sum, crossfade, gain, downmix |
| `env.*` | adsr, curve, follower |
| `lfo.*` | shape |
| `random.*` | stepped, drift |
| `clock.*` | pulse, divide, counter |
| `seq.*` | steps, euclid |
| `note.*` | gate, value, quantize, transpose, chord, hold, select, humanize, filter, assemble |
| `math.*` | add, subtract, multiply, divide, abs, clamp, minmax, power, round, modulo, slew |
| `logic.*` | boolean, not, compare, toggle, select |
| `adapt.*` | map, remap, normalise, threshold, sampleHold |
| `data.*` | load, table, scale, material, analyseModes, lookup |
| `analysis.*` | onset, pitch, level, centroid |
| `instance.*` | allocator, mix |
| `util.*` | constant, macro, reroute |
| `view.*` | listen, scope, spectrum, meter |

---

## io — the plugin boundary

#### `io.audioIn` — Audio In
Reads one of the plugin's real input buses. **Out:** `channel.0…channel.N` — `Audio`, a port group sized by the selected bus (stereo main gives 2, a six-channel bus gives 6). **Structural:** `bus` (enum: Main, Aux 1–4). **Behavior:** passthrough; silence when the host has not activated the bus. **Taps:** per-channel level. **Native:** the plugin I/O boundary.

#### `io.output` — Output
**In:** `left`, `right` — `Audio`, domain `monoOnly`; `right` unconnected mirrors `left`. **Behavior:** unity passthrough to the main output bus, with a final NaN/Inf guard. **Taps:** output scope, spectrum, level.

#### `io.noteIn` — Note In
**Out:** `notes` — `Note`. **Structural:** `channel` (enum: Omni, 1–16), `mpeMode` (enum: off, MPE). **Behavior:** translates host MIDI into `Note` events (id, continuous pitch including bend, velocity, pressure, slide, release velocity). No separate bend path. **Taps:** active note count. **Native:** plugin I/O boundary.

#### `io.control` — MIDI Control
**Out:** `value` — `float·Unipolar·0–1·linear·0`. **Structural:** `source` (enum: CC number, mod wheel, channel pressure, pitch bend, sustain pedal), `channel`. **Behavior:** the mono-domain half of MIDI, kept out of `Note` deliberately. **Params:** `smoothing : float·Time·0–500ms·log·20ms`.

#### `io.transport` — Transport
**Out:** `beat` — `Event` (bar-synced pulse); `tempo` — `float·Frequency` (BPM-derived); `playing` — `bool`; `position` — `float·Time`. **Behavior:** exposes the host timeline so clocks and LFOs can sync. In Standalone it runs an internal transport.

## osc — oscillators

#### `osc.analog` — Analog Oscillator
Band-limited virtual-analog oscillator. **In:** `frequency : float·Frequency·20–20000Hz·log·440 [audio]`; `fine : float·Pitch·−1–1 st·linear·0`; `pulseWidth : float·Unipolar·0.01–0.99·linear·0.5 [audio]`; `phaseMod : float·Bipolar·−1–1·linear·0 [audio]` (through-zero phase modulation); `sync : Event` (hard sync reset). **Out:** `out` — `Audio`. **Structural:** `shape` (enum: sine, triangle, saw, square, pulse). **Behavior:** phase accumulator with PolyBLEP correction on discontinuities; pulse width applies to pulse and triangle shapes. **prepare():** nothing sample-rate-dependent beyond the increment. **Taps:** `out`, waveform preview. **Native:** numerically delicate band-limiting.

#### `osc.sine` — Sine
Cheap, alias-free sine for FM, test tones, and modal excitation. **In:** `frequency : float·Frequency·0.01–20000Hz·log·440 [audio]`; `phaseMod : float·Bipolar·−1–1·linear·0 [audio]`; `sync : Event`. **Out:** `out` — `Audio`. **Behavior:** exact sine from a phase accumulator. **Native:** inner-loop primitive, must be as cheap as possible because FM stacks and modal excitation use many.

#### `osc.wavetable` — Wavetable Oscillator
**In:** `table` — `Data(wavetable)`, required; `frequency : float·Frequency·20–20000Hz·log·440 [audio]`; `position : float·Unipolar·0–1·linear·0 [audio]` (frame scan); `phaseMod : float·Bipolar [audio]`; `sync : Event`. **Out:** `out` — `Audio`. **Structural:** `interpolation` (enum: none, linear, cubic — "none" is a deliberate lo-fi option and the stepped-frame rattle the design brief asked for), `frameBlending` (enum: blend, jump). **Behavior:** mipmapped band-limited playback; `position` scans frames with the selected blending; `jump` blending intentionally produces frame-edge discontinuities. **Taps:** `out`, current frame index, table preview. **Native:** inner loop scales with table size; delicate interpolation.

## sampler — samples and grains

#### `sampler.player` — Sampler
**In:** `sample` — `Data(sample)`, required; `trigger : Event`; `pitch : float·Pitch·−48–48 st·linear·0 [audio]`; `start : float·Unipolar·0–1·linear·0`; `level : float·Gain·0–2·log·1 [audio]`. **Out:** `out` — `Audio`; `ended` — `Event`. **Structural:** `loopMode` (enum: one-shot, forward, ping-pong, sustain loop), `interpolation` (enum: linear, cubic), `direction` (enum: forward, reverse). **Params (ports):** `loopStart`, `loopEnd` — `float·Unipolar`; `loopCrossfade : float·Time·0–500ms·log·10ms`. **Behavior:** trigger resets the head to `start`; playback rate derives from `pitch` against the sample's root note in the `Data` header. **Taps:** `out`, playhead position (for a waveform preview with a moving head). **Native:** delicate interpolation, data-bound.

#### `sampler.granular` — Granular Cloud
**In:** `sample` — `Data(sample)`, required; `position : float·Unipolar·0–1·linear·0 [audio]`; `density : float·Frequency·0.1–500Hz·log·20 [audio]`; `grainSize : float·Time·1–2000ms·log·80ms`; `pitch : float·Pitch·−48–48 st·linear·0 [audio]`; `spray : float·Unipolar·0–1·linear·0.1` (position jitter); `pitchSpray : float·Unipolar·0–1·linear·0`; `pan : float·Bipolar·−1–1·linear·0`; `panSpray : float·Unipolar·0–1·linear·0`; `trigger : Event` (manual grain spawn). **Out:** `left`, `right` — `Audio`. **Structural:** `maxGrains` (default 64), `window` (enum: Hann, Tukey, expo, reverse-expo), `spawnMode` (enum: stochastic, periodic, triggered). **Behavior:** spawns grains at `density` (Poisson when stochastic, seeded and deterministic), each reading the sample around `position ± spray` with the chosen window. **Taps:** `out`, active grain count, grain positions over the waveform. **Native:** inner loop scales with grain count; preallocates the pool.

## noise — stochastic sources

#### `noise.colored` — Noise
**In:** `tilt : float·Bipolar·−1–1·linear·0` (continuous spectral tilt, −1 dark to +1 bright, on top of the chosen colour). **Out:** `out` — `Audio`. **Structural:** `color` (enum: white, pink, brown, blue, violet). **Behavior:** seeded uniform noise through the colour's shaping filter, then the tilt filter. **Taps:** `out`, spectrum.

#### `noise.dust` — Dust
Sparse random impulses — the primitive behind crackle, rain, footsteps, friction, and anything that is a population of tiny events. **In:** `density : float·Frequency·0.1–5000Hz·log·20 [audio]`; `jitter : float·Unipolar·0–1·linear·1` (0 = evenly spaced, 1 = pure Poisson). **Out:** `pulse` — `Event`; `out` — `Audio` (one-sample impulses, amplitude-randomised), so it can be used directly as an exciter or as a trigger source. **Params (ports):** `amplitudeSpread : float·Unipolar·0–1·linear·0.5`. **Structural:** `seed`. **Behavior:** per-sample Bernoulli trial at `density/sampleRate`, interpolated toward a jittered periodic grid as `jitter` falls. **Taps:** event-rate meter, impulse scope. **Native:** sample-accurate event timing that an LFO-and-threshold group cannot reproduce without aliasing.

## excite — physical excitation

#### `excite.impulse` — Impulse
**In:** `trigger : Event`; `amplitude : float·Gain·0–1·linear·1`; `width : float·Time·0–20ms·log·0ms` (0 = a true single-sample delta). **Out:** `out` — `Audio`. **Behavior:** the cleanest possible input to a resonator.

#### `excite.burst` — Noise Burst
**In:** `trigger : Event`; `duration : float·Time·0.1–2000ms·log·30ms`; `tone : float·Bipolar·−1–1·linear·0` (dark to bright); `shape : float·Unipolar·0–1·linear·0.5` (percussive to flat envelope). **Out:** `out` — `Audio`.

#### `excite.pluck` — Pluck
**In:** `trigger : Event`; `position : float·Unipolar·0.01–0.99·linear·0.2` (plucking point along the body); `hardness : float·Unipolar·0–1·linear·0.5`; `amplitude : float·Gain`. **Out:** `out` — `Audio`. **Behavior:** a shaped pulse whose spectrum has notches at harmonics cancelled by the pluck position — the reason a string plucked near the bridge sounds thin. **Native:** the comb-notch shaping must be exact and is awkward to hand-wire per patch.

#### `excite.mallet` — Mallet / Collision
**In:** `trigger : Event`; `velocity : float·Unipolar·0–1·linear·0.7`; `mass : float·Unipolar·0–1·linear·0.5`; `stiffness : float·Unipolar·0–1·linear·0.5`; `feedback` — `Audio` (the struck body's motion; optional). **Out:** `out` — `Audio`; `contact` — `bool` (true while in contact). **Behavior:** Hertzian contact model — force rises nonlinearly with penetration, contact ends when the body pushes back. With `feedback` connected it is a real collision (double strikes, rattles, a mallet bouncing on a drum); unconnected it degrades gracefully to a shaped pulse. **Native:** single-sample feedback, numerically delicate.

#### `excite.stickSlip` — Stick-Slip / Bow
**In:** `pressure : float·Unipolar·0–1·linear·0.5 [audio]`; `speed : float·Unipolar·0–1·linear·0.3 [audio]`; `roughness : float·Unipolar·0–1·linear·0.3` (surface irregularity); `feedback` — `Audio` (the resonator's velocity; optional); `trigger : Event` (re-seed). **Out:** `out` — `Audio`; `slipping` — `bool`. **Behavior:** friction with a velocity-dependent characteristic — tension builds while stuck, releases on slip. With `feedback` it is a true bowed/scraped interaction that locks to the resonator's motion; without it, a relaxation oscillator driven by `speed` and `roughness`. **Taps:** `out`, stick/slip state. **Native:** single-sample feedback; delicate near zero speed.

#### `excite.breath` — Breath
**In:** `pressure : float·Unipolar·0–1·linear·0.5 [audio]`; `turbulence : float·Unipolar·0–1·linear·0.3`; `noiseColor : float·Bipolar·−1–1·linear·0`; `feedback` — `Audio` (optional, for reed/lip behaviour). **Out:** `out` — `Audio`. **Structural:** `mode` (enum: breath, reed, lip). **Behavior:** pressure-driven turbulent noise; in reed and lip modes the connected `feedback` drives a nonlinear valve, which is what makes a clarinet or a brass instrument sound alive rather than filtered. **Native:** single-sample feedback, delicate nonlinearity.

#### `excite.contact` — Contact / Scrape
**In:** `speed : float·Unipolar·0–1·linear·0.3 [audio]`; `pressure : float·Unipolar·0–1·linear·0.5 [audio]`; `surface` — `Data(curve)` (a surface roughness profile; optional — a default profile is used when unconnected); `grainSize : float·Time·0.1–50ms·log·2ms`. **Out:** `out` — `Audio`; `contactRate` — `float·Frequency` (impacts per second, useful for driving other things). **Behavior:** reads the surface profile at a rate set by `speed` and converts height changes into impulses whose amplitude scales with `pressure`. This is the "object dragged across a textured surface" primitive. **Taps:** `out`, contact-rate meter.

## resonator — resonating bodies

#### `resonator.modal` — Modal Bank
The centre of the physical-modelling set. **In:** `excite` — `Audio`; `modes` — `Data(modal-set)`, required; `pitch : float·Pitch·−48–48 st·linear·0 [audio]` (transposes the whole set); `decay : float·Ratio·0.05–20×·log·1` (scales every mode's decay); `brightness : float·Bipolar·−1–1·linear·0` (tilts high modes' gain and decay — the "material" control); `inharmonicity : float·Bipolar·−1–1·linear·0` (stretches or compresses mode spacing); `position : float·Unipolar·0.01–0.99·linear·0.3` (pickup point — attenuates modes with a node there); `spread : float·Unipolar·0–1·linear·0` (stereo placement of modes). **Out:** `left`, `right` — `Audio`. **Structural:** `maxModes` (default 64). **Behavior:** a bank of resonant two-pole sections driven in parallel and summed; the `Data` buffer supplies frequency, gain, and decay per mode, and the ports above transform that set continuously without rebuilding it. **prepare():** per-mode coefficients from the current buffer and sample rate. **Taps:** `out`, per-mode energy (a modal spectrum preview, the signature visual for this family). **Native:** inner loop scales with mode count; delicate resonant filters.

#### `resonator.string` — String
A waveguide string, the playable version of Karplus-Strong. **In:** `excite` — `Audio`; `pitch : float·Pitch·semitones·linear·60 [audio]`; `decay : float·Time·0.05–60s·log·3s`; `damping : float·Unipolar·0–1·linear·0.3` (high-frequency loss per pass); `stiffness : float·Unipolar·0–1·linear·0` (dispersion — piano-like stretched partials); `position : float·Unipolar·0.01–0.99·linear·0.2` (pickup); `release : float·Unipolar·0–1·linear·0` (damping applied on note release). **Out:** `out` — `Audio`; `motion` — `Audio` (the string's velocity at the excitation point, for feeding back into `excite.stickSlip` or `excite.mallet`). **Structural:** `maxLength` (buffer size). **Behavior:** fractional-delay loop with a damping filter and an allpass dispersion cascade; loop length compensates for filter and unit delays so tuning stays accurate at high pitches. **Taps:** `out`, loop energy. **Native:** single-sample feedback; tuning and interpolation are delicate.

#### `resonator.tube` — Tube
**In:** `excite` — `Audio`; `length : float·Time·0.1–200ms·log·5ms` (or driven from pitch through `adapt.map`); `damping : float·Unipolar`; `reflection : float·Unipolar·0–1·linear·0.8`; `flare : float·Bipolar·−1–1·linear·0` (cylindrical to conical). **Out:** `out` — `Audio`; `motion` — `Audio` (pressure at the mouth end, for reed and lip feedback). **Structural:** `endCondition` (enum: open, closed). **Behavior:** bidirectional waveguide with a reflection filter at each end. **Native:** single-sample feedback in both directions.

#### `resonator.plate` — Plate / Membrane
**In:** `excite` — `Audio`; `size : float·Unipolar·0–1·linear·0.5`; `tension : float·Unipolar·0–1·linear·0.5`; `decay : float·Time·0.05–30s·log·2s`; `damping : float·Unipolar`; `position` (two ports: `positionX`, `positionY` — `float·Unipolar`). **Out:** `left`, `right` — `Audio`. **Structural:** `quality` (enum: low, medium, high — the number of coupled sections, and therefore the CPU cost). **Behavior:** a 2D mesh or coupled-waveguide approximation of a membrane or plate; inharmonic, dense, the drum-and-cymbal counterpart to `resonator.modal`. **Native:** inner loop scales with mesh size.

#### `resonator.comb` — Comb
The cheap resonator, and the building block for hand-built feedback experiments. **In:** `in` — `Audio`; `frequency : float·Frequency·20–20000Hz·log·220 [audio]`; `feedback : float·Bipolar·−0.999–0.999·linear·0.9`; `damping : float·Unipolar·0–1·linear·0.2`. **Out:** `out` — `Audio`. **Structural:** `type` (enum: feedforward, feedback). **Behavior:** fractional delay tuned to `frequency`, with a one-pole damper in the loop; feedback is hard-limited below 1 so it cannot explode.

## filter

#### `filter.svf` — State-Variable Filter
**In:** `in` — `Audio`; `cutoff : float·Frequency·20–20000Hz·log·1000 [audio]`; `resonance : float·Unipolar·0–1·linear·0.1 [audio]`; `drive : float·Gain·1–20×·log·1`; `keyTrack : float·Ratio·−1–2×·linear·0`; `keyPitch : float·Pitch` (the note the tracking follows). **Out:** port group `lowpass`, `bandpass`, `highpass`, `notch`, `peak` — `Audio` (all available simultaneously; unconnected outputs cost nothing). **Structural:** `slope` (enum: 12, 24 dB/oct). **Behavior:** TPT zero-delay-feedback SVF with saturation inside the resonance loop, stable under audio-rate modulation. **Taps:** `out`, live frequency-response curve (the node's inline preview). **Native:** numerically delicate.

#### `filter.ladder` — Ladder Filter
**In:** `in`; `cutoff [audio]`; `resonance : float·Unipolar·0–1·linear·0.2 [audio]` (self-oscillates at 1); `drive : float·Gain·1–30×·log·1`; `keyTrack`, `keyPitch`. **Out:** `out` — `Audio`. **Structural:** `poles` (enum: 1–4), `mode` (enum: lowpass, highpass, bandpass). **Behavior:** four-stage nonlinear ladder with ZDF solving. **Native:** numerically delicate.

#### `filter.onepole` — One-Pole
**In:** `in`; `cutoff : float·Frequency·1–20000Hz·log·1000 [audio]`. **Out:** `lowpass`, `highpass` — `Audio`. **Behavior:** the cheap damper used inside feedback loops; exact and allocation-free.

#### `filter.allpass` — Allpass / Dispersion
**In:** `in`; `frequency : float·Frequency·20–20000Hz·log·1000 [audio]`; `amount : float·Unipolar·0–1·linear·0.5`. **Out:** `out` — `Audio`. **Structural:** `stages` (1–16). **Behavior:** phase rotation without amplitude change; cascaded, it is what gives strings stiffness and reverbs diffusion.

#### `filter.shelf` — Shelf
**In:** `in`; `frequency [audio]`; `gain : float·Gain·0.06–16×·log·1` (shown in dB); `slope : float·Unipolar`. **Out:** `out`. **Structural:** `type` (enum: low, high).

#### `filter.peak` — Peak / Bell
**In:** `in`; `frequency [audio]`; `gain`; `q : float·Ratio·0.1–30·log·1`. **Out:** `out`.

#### `filter.formant` — Formant
**In:** `in`; `vowel : float·Unipolar·0–1·linear·0 [audio]` (morphs through the vowel set); `formants` — `Data(modal-set)` (optional, a custom formant table); `shift : float·Pitch·−24–24 st·linear·0` (vocal-tract size); `intensity : float·Unipolar`. **Out:** `out`. **Behavior:** a bank of three to five resonant bands interpolated between vowels — for voice, animal calls, and anything with a throat. **Native:** inner loop over bands.

#### `filter.dcBlock` — DC Blocker
**In:** `in`; `cutoff : float·Frequency·1–100Hz·log·20`. **Out:** `out`. **Behavior:** trivial but essential wherever nonlinearities and feedback meet.

## shape — nonlinearities

#### `shape.waveshaper` — Waveshaper
**In:** `in` — `Audio`; `drive : float·Gain·0.1–50×·log·1 [audio]`; `bias : float·Bipolar·−1–1·linear·0`; `mix : float·Unipolar·0–1·linear·1`; `curve` — `Data(curve)` (optional custom transfer function). **Out:** `out` — `Audio`. **Structural:** `shape` (enum: tanh, arctan, sine fold, asymmetric, hard, custom), `oversampling` (enum: off, 2×, 4×, 8×). **Behavior:** memoryless transfer function with oversampling and correct latency reporting. **Taps:** `out`, the transfer curve with a live input dot on it. **Native:** oversampling, delicate.

#### `shape.clip` — Clip / Safety
**In:** `in`; `ceiling : float·Gain·0.01–4×·log·1`; `knee : float·Unipolar·0–1·linear·0.2`. **Out:** `out`; `clipping` — `bool`. **Structural:** `mode` (enum: hard, soft, limiter). **Behavior:** the node you put in a feedback loop so a slider cannot destroy a speaker. In limiter mode it has lookahead and reports latency. **Taps:** gain reduction.

#### `shape.fold` — Wavefolder
**In:** `in`; `drive [audio]`; `offset : float·Bipolar`; `folds : float·Ratio·1–8·linear·2`. **Out:** `out`. **Structural:** `oversampling`.

#### `shape.rectify` — Rectify
**In:** `in`; `amount : float·Unipolar·0–1·linear·1`. **Out:** `out`. **Structural:** `mode` (enum: half, full).

#### `shape.crush` — Bitcrush / Downsample
**In:** `in`; `bits : float·Count·1–24·linear·16 [audio]`; `rate : float·Frequency·100–48000Hz·log·48000 [audio]`; `mix : float·Unipolar`. **Out:** `out`.

## delay

#### `delay.line` — Delay
**In:** `in` — `Audio`; `time : float·Time·0–10s·log·250ms [audio]`; `feedback : float·Bipolar·−0.99–0.99·linear·0` (internal, hard-limited); `damping : float·Unipolar·0–1·linear·0.2` (in the internal feedback path); `mix : float·Unipolar·0–1·linear·1`. **Out:** `out` — `Audio`. **Structural:** `maxTime`, `interpolation` (enum: linear, allpass, cubic — allpass is the default for pitched feedback loops), `timeMode` (enum: free, tempo-synced, samples). **Behavior:** fractional-delay line with interpolation; time changes are smoothed (tape-style pitch shift) or crossfaded, selected per patch. **Taps:** `out`, current time. **Native:** the delay at the heart of every feedback structure; interpolation must not colour the loop.

## space

#### `space.reverb` — Reverb
**In:** `left`, `right` — `Audio`; `size : float·Unipolar·0–1·linear·0.5`; `decay : float·Time·0.1–60s·log·2s`; `damping : float·Unipolar·0–1·linear·0.4`; `predelay : float·Time·0–500ms·log·10ms`; `diffusion : float·Unipolar·0–1·linear·0.7`; `modulation : float·Unipolar·0–1·linear·0.2`; `lowCut`, `highCut` — `float·Frequency`; `mix : float·Unipolar·0–1·linear·0.3`. **Out:** `left`, `right` — `Audio`. **Structural:** `quality` (enum: low, medium, high — FDN size). **Behavior:** feedback delay network with modulated delays. Cheap enough at low quality to be used per voice. **Native:** inner loop scales with network size.

#### `space.diffuser` — Diffuser
**In:** `in`; `size : float·Time·1–200ms·log·30ms`; `amount : float·Unipolar`. **Out:** `out`. **Structural:** `stages` (2–8). **Behavior:** an allpass chain — early reflections, smearing a transient, or the diffusion half of a hand-built reverb.

#### `space.pan` — Pan
**In:** `in` — `Audio`; `pan : float·Bipolar·−1–1·linear·0 [audio]`; `width : float·Unipolar·0–2·linear·1`. **Out:** `left`, `right` — `Audio`. **Structural:** `law` (enum: linear, −3 dB, −4.5 dB, constant power).

#### `space.width` — Width
**In:** `left`, `right`; `width : float·Unipolar·0–2·linear·1`; `bassMonoBelow : float·Frequency·20–500Hz·log·120`. **Out:** `left`, `right`.

## mix

#### `mix.sum` — Mix
**In:** port group `in.0…in.N` — `Audio` (growable, min 2, max 16), each with a `level : float·Gain·0–2·log·1` companion in the same group. **Out:** `out` — `Audio`.

#### `mix.crossfade` — Crossfade
**In:** `a`, `b` — `Audio`; `position : float·Unipolar·0–1·linear·0.5 [audio]`. **Out:** `out`. **Structural:** `law` (enum: linear, equal power).

#### `mix.gain` — Gain
**In:** `in` — `Audio`; `gain : float·Gain·0–4×·log·1 [audio]` (displayed in dB). **Out:** `out` — `Audio`. **Behavior:** the VCA; audio-rate `gain` makes it a ring modulator too.

#### `mix.downmix` — Downmix
**In:** `left`, `right`. **Out:** `out` — `Audio`. **Structural:** `mode` (enum: sum, left, right, mid, side).

## env — envelopes

#### `env.adsr` — Envelope
**In:** `gate : bool` (sustains while true); `trigger : Event` (retrigger); `delay : float·Time·0–5s·log·0`; `attack : float·Time·0–20s·log·5ms [audio]`; `hold : float·Time·0–5s·log·0`; `decay : float·Time·0–20s·log·200ms`; `sustain : float·Unipolar·0–1·linear·0.7`; `release : float·Time·0–30s·log·300ms`; `velocity : float·Unipolar·0–1·linear·1` (scales the peak). **Out:** `out` — `float·Unipolar·0–1 [audio]`; `finished` — `Event`. **Structural:** `attackCurve`, `decayCurve`, `releaseCurve` (enum: linear, exponential, logarithmic, s-curve), `mode` (enum: normal, loop, one-shot). **Behavior:** DAHDSR with per-segment curves; loop mode turns it into a shape-accurate LFO. **Taps:** `out`, the envelope shape with a moving playhead.

#### `env.curve` — Curve Envelope
**In:** `curve` — `Data(curve)`, required; `trigger : Event`; `gate : bool`; `time : float·Time·1ms–60s·log·1s` (total duration); `sustainPoint : float·Unipolar` (where the curve holds while gated). **Out:** `out` — `float·Unipolar [audio]`; `finished` — `Event`. **Behavior:** plays a drawn multi-segment shape — gestures, bowing motions, breath contours, anything an ADSR cannot express. **Taps:** curve with playhead.

#### `env.follower` — Envelope Follower
**In:** `in` — `Audio`; `attack : float·Time·0–500ms·log·5ms`; `release : float·Time·0–5s·log·100ms`. **Out:** `out` — `float·Unipolar·0–1 [audio]`. **Structural:** `detection` (enum: peak, RMS). **Behavior:** the Audio → Control adapter, and the way an external signal drives anything.

## lfo

#### `lfo.shape` — LFO
**In:** `rate : float·Frequency·0.001–200Hz·log·1 [audio]`; `shape` — `Data(curve)` (optional custom shape); `shapeB` — `Data(curve)` (optional morph target); `shapeMorph : float·Unipolar·0–1·linear·0 [audio]`; `phase : float·Phase·0–1·linear·0`; `sync : Event` (phase reset); `depth : float·Unipolar·0–1·linear·1`; `smooth : float·Unipolar·0–1·linear·0` (slew applied to the output — a stepped shape becomes fluid); `fade : float·Time·0–10s·log·0` (fade-in from note start). **Out:** `out` — `float·Bipolar·−1–1 [audio]`; `phaseOut` — `float·Phase`; `cycle` — `Event` (fires once per cycle). **Structural:** `waveform` (enum: sine, triangle, saw, ramp, square, random step, random smooth, custom), `rateMode` (enum: free, tempo-synced, note-synced), `retrigger` (enum: free-running, per instance). **Taps:** shape with a moving phase marker.

## random — controlled unpredictability

#### `random.stepped` — Random
**In:** `trigger : Event` (unconnected, it free-runs at `rate`); `rate : float·Frequency·0.01–1000Hz·log·5 [audio]`; `amount : float·Unipolar·0–1·linear·1`; `smooth : float·Unipolar·0–1·linear·0` (0 = hard steps, 1 = fully glided between values — this is the difference between rattle and drift); `bias : float·Bipolar·−1–1·linear·0` (skews the distribution); `spread : float·Unipolar·0–1·linear·1` (how far values may stray from `bias`); `steps : float·Count·0–64·linear·0` (0 = continuous, otherwise quantised to N discrete levels); `chance : float·Unipolar·0–1·linear·1` (probability that a trigger actually produces a new value — sparse, sticky randomness). **Out:** `out` — `float·Bipolar·−1–1 [audio]`; `changed` — `Event`. **Structural:** `distribution` (enum: uniform, gaussian, exponential, bimodal), `seed`, `polarity` (enum: bipolar, unipolar). **Behavior:** on each trigger, draws from the distribution, then glides toward it over a time derived from `smooth` and the current rate. **Taps:** `out` with a rolling history (the horizontal-node stepped preview from the design reference).

#### `random.drift` — Drift
Slow, correlated, natural wander — the thing that makes a patch sound alive rather than static. **In:** `rate : float·Frequency·0.001–20Hz·log·0.2`; `amount : float·Unipolar·0–1·linear·0.3`; `centering : float·Unipolar·0–1·linear·0.5` (0 = free Brownian walk, 1 = strongly pulled back to centre). **Out:** `out` — `float·Bipolar [audio]`. **Structural:** `spectrum` (enum: brown, pink, white-filtered), `seed`. **Behavior:** bounded 1/f-style noise, guaranteed to stay in range.

## clock and seq

#### `clock.pulse` — Clock
**In:** `rate : float·Frequency·0.01–200Hz·log·2 [audio]`; `swing : float·Bipolar·−1–1·linear·0`; `jitter : float·Unipolar·0–1·linear·0` (humanised timing); `run : bool·true`; `reset : Event`. **Out:** `tick` — `Event`; `phase` — `float·Phase`. **Structural:** `rateMode` (enum: free, tempo-synced), `division` (enum of note values, when synced).

#### `clock.divide` — Divide
**In:** `tick : Event`; `divide : float·Count·1–64·linear·2`; `reset : Event`. **Out:** `tick` — `Event`.

#### `clock.counter` — Counter
**In:** `tick : Event`; `reset : Event`; `length : float·Count·1–256·linear·8`; `step : float·Count·−16–16·linear·1` (increment, negative counts down). **Out:** `index` — `int·Count`; `normalised` — `float·Unipolar`; `wrapped` — `Event`. **Structural:** `mode` (enum: up, down, ping-pong, random). **Behavior:** the generic sequencer engine — combined with `note.select` it is an arpeggiator, with `data.lookup` it is a step sequencer.

#### `seq.steps` — Step Sequencer
**In:** `tick : Event`; `reset : Event`; `steps` — `Data(curve)` (optional — otherwise the node's own editable step data). **Out:** `value` — `float·Bipolar [audio]`; `gate` — `bool`; `trigger` — `Event`; `index` — `int·Count`. **Structural:** `length` (1–64), `range` (value range for the lane). **Behavior:** the node with the step-grid editor from the controls spec; per-step value plus per-step gate, playhead from telemetry. **Taps:** step grid with playhead.

#### `seq.euclid` — Euclidean
**In:** `tick : Event`; `steps : float·Count·1–64·linear·16`; `pulses : float·Count·0–64·linear·4`; `rotate : float·Count·0–63·linear·0`; `reset : Event`. **Out:** `trigger` — `Event`; `gate` — `bool`. **Behavior:** evenly distributed pulses — cheap, musical, and the easiest way to get non-square natural rhythms.

## note — the note stream

This family is what makes arpeggios, chords, scales, and audio-driven instruments ordinary patching rather than built-in features.

#### `note.gate` — Note Gate
**In:** `notes` — `Note`. **Out:** `noteOn` — `Event`; `noteOff` — `Event`; `gate` — `bool`; `count` — `int·Count` (held notes).

#### `note.value` — Note Value
**In:** `notes` — `Note`. **Out:** `pitch` — `float·Pitch`; `velocity`, `pressure`, `slide` — `float·Unipolar`. **Structural:** `select` (enum: last, lowest, highest, first). **Behavior:** the mono-domain way to read a note stream, for monophonic patches and for driving global modulation. Inside an instanced region the allocator's own outputs are used instead.

#### `note.quantize` — Scale Quantize
**In:** `notes` — `Note`; `scale` — `Data(scale)`, required; `root : float·Pitch·0–11 st·linear·0`; `strength : float·Unipolar·0–1·linear·1` (partial quantisation glides toward the scale note). **Out:** `notes` — `Note`. **Structural:** `direction` (enum: nearest, up, down), `applyTo` (enum: note-on only, continuously — continuous also snaps bends and MPE slides).

#### `note.transpose` — Transpose
**In:** `notes`; `semitones : float·Pitch·−48–48·linear·0`; `octaves : int·Count·−4–4·linear·0`. **Out:** `notes`.

#### `note.chord` — Chord
**In:** `notes` — `Note`; `spread : float·Unipolar·0–1·linear·0` (strum timing); `velocityFalloff : float·Unipolar·0–1·linear·0`; port group `interval.0…interval.N` — `float·Pitch` (growable, the offsets to add). **Out:** `notes` — `Note`. **Structural:** `mode` (enum: fixed intervals, scale degrees), `scale` input when in scale mode. **Behavior:** each incoming note emits several notes with tied lifetimes, so note-off releases the whole chord.

#### `note.hold` — Hold Memory
**In:** `notes` — `Note`; `hold : bool` (latch — notes stay held after release); `clear : Event`. **Out:** `held` — `Note` (the live set); `count` — `int·Count`. **Structural:** `order` (enum: as played, pitch ascending, pitch descending, random), `maxHeld`. **Behavior:** keeps the set of currently held notes in a defined order. This is the memory an arpeggiator needs.

#### `note.select` — Select Note
**In:** `held` — `Note` (a set); `index : int·Count·0–63·linear·0`; `trigger : Event` (emit now). **Out:** `notes` — `Note`; `pitch` — `float·Pitch`. **Structural:** `wrap` (enum: wrap, clamp, silent), `gateLength` relative to the trigger interval. **Behavior:** with `clock.counter` driving `index`, this plus `note.hold` is a complete arpeggiator, built from three visible nodes.

#### `note.humanize` — Humanize
**In:** `notes`; `timing : float·Time·0–200ms·log·0`; `velocity : float·Unipolar·0–1·linear·0`; `pitch : float·Pitch·0–1 st·linear·0` (detune per note). **Out:** `notes`. **Structural:** `seed`.

#### `note.filter` — Note Filter
**In:** `notes`; `lowPitch`, `highPitch` — `float·Pitch`; `lowVelocity`, `highVelocity` — `float·Unipolar`. **Out:** `pass` — `Note`; `reject` — `Note`. **Behavior:** key splits, velocity layers, and routing one physical controller to several instrument regions.

#### `note.assemble` — Assemble Note
**In:** `trigger : Event` (note-on); `release : Event` (note-off, optional); `pitch : float·Pitch [audio]`; `velocity : float·Unipolar·0–1·linear·0.8`; `confidence : float·Unipolar·0–1·linear·1`; `confidenceGate : float·Unipolar·0–1·linear·0.3`. **Out:** `notes` — `Note`. **Behavior:** turns detected events and a tracked pitch into a real note stream, with continuous pitch updates feeding the note's expression while it is held. This is what makes an audio input — a guitar string, a voice, a contact mic — playable as an instrument.

## math

All take and return `Control`; quantity is inherited from the first connected input; mismatched quantities are rejected by `canConnect`.

| Node | Ports | Notes |
|---|---|---|
| `math.add` | `in.0…in.N` (growable, min 2) | |
| `math.subtract` | `a`, `b` | |
| `math.multiply` | `in.0…in.N` (growable) | |
| `math.divide` | `a`, `b`, `safeZero : bool·true` | division by zero returns 0 rather than NaN |
| `math.abs` | `in` | |
| `math.clamp` | `in`, `low`, `high` | |
| `math.minmax` | `a`, `b`; structural `mode` (min/max) | |
| `math.power` | `in`, `exponent : float·Ratio·0.01–10·log·1` | curve shaping for modulation |
| `math.round` | `in`, `step : float·linear·1`; structural `mode` (floor, ceil, nearest) | quantising any value, not just pitch |
| `math.modulo` | `in`, `divisor` | wrapping phase, cycling indices |
| `math.slew` | `in`, `rise : float·Time·0–10s·log·10ms`, `fall : float·Time·0–10s·log·10ms` | portamento, smoothing, envelope-like shaping of any control |

## logic

| Node | Ports | Notes |
|---|---|---|
| `logic.boolean` | `in.0…in.N` (growable, bool); structural `op` (AND, OR, XOR, NAND, NOR) | one node, not five |
| `logic.not` | `in` → `out` (bool) | |
| `logic.compare` | `a`, `b` (same quantity required), `tolerance`; structural `op` (>, ≥, =, ≠, ≤, <) | `=` uses `tolerance`, not exact float equality |
| `logic.toggle` | `trigger : Event`, `reset : Event` → `out` (bool) | |
| `logic.select` | `condition` (bool), `whenTrue`, `whenFalse` → `out` | one node for every signal type: input types must match, output quantity inherited; audio crossfades over a few samples to avoid clicks |

## adapt — the conversion family

These are the nodes `canConnect` inserts automatically. They are ordinary nodes the user can also place by hand.

#### `adapt.map` — Map
**In:** `in` — `Control [audio]`; `inLow`, `inHigh`, `outLow`, `outHigh` — `Control` (inherited quantities); `curve : float·Bipolar·−1–1·linear·0` (exponential through linear to logarithmic); `shape` — `Data(curve)` (optional — when connected, the drawn curve replaces the scalar `curve`). **Out:** `out` — `Control`, quantity inherited from `outLow`/`outHigh` or from the destination. **Structural:** `clip` (enum: clip, wrap, fold, none). **Behavior:** the universal range and shape converter; auto-seeded from the destination port's range when inserted automatically. Because the scalar and the drawn curve live on the same node, an auto-inserted Map can be turned into an arbitrary transfer shape without replacing it.

#### `adapt.remap` — Remap
The drawn-curve shaper as its own node, with the editor in its body — one click when the goal is "bend this LFO", rather than wiring a table and a lookup by hand. Internally it is `data.table` plus `data.lookup`, so it breaks no layering rule; it exists for immediacy.
**In:** `in` — `Control [audio]`; `curve` — `Data(curve)` (optional — otherwise the node's own drawn curve is used); `curveB` — `Data(curve)` (optional, the morph target); `morph : float·Unipolar·0–1·linear·0 [audio]`; `amount : float·Unipolar·0–1·linear·1` (dry/wet between the input and the shaped result). **Out:** `out` — `Control`, quantity inherited from the destination. **Structural:** the node's own curve (drawn in its custom editor), `polarity` (enum: unipolar, bipolar — see `data.lookup`), `edgeMode` (enum: clamp, wrap, mirror). **Behavior:** reads the curve at the position given by `in`, interpolating between `curve` and `curveB` when morphing. Morphing interpolates point-by-point along the normalised x-axis, so two curves of different resolutions still morph cleanly. **Taps:** the curve with a live dot at the current input position, and the morph state.

#### `adapt.normalise` — Normalise
**In:** `in` — `Control` (real quantity), `low`, `high`. **Out:** `out` — `float·Unipolar`. **Behavior:** the inverse of Map; auto-seeded from the source's range.

#### `adapt.threshold` — Threshold
**In:** `in` — `Control`; `threshold` — `Control` (default: mid-range of the source); `hysteresis : float·Unipolar·0–1·linear·0.05`. **Out:** `rise` — `Event`; `fall` — `Event`; `above` — `bool`. **Behavior:** the Control → Event adapter; the one that turns a modulation signal into a trigger.

#### `adapt.sampleHold` — Sample & Hold
**In:** `in` — `Control`; `trigger : Event`; `glide : float·Time·0–5s·log·0`. **Out:** `out` — `Control`, quantity inherited.

## data — producing and reading buffers

Nothing else in the catalogue can create a `Data` buffer, so this family is a prerequisite for the sampler, the wavetable oscillator, every resonator, and scale quantisation.

#### `data.load` — Load File
**Out:** `data` — `Data`, tagged by content. **Structural:** `file` (user-chosen audio or table file), `interpretAs` (enum: sample, wavetable, impulse response), `frameSize` (when interpreting as a wavetable), `normalise` (bool), `rootNote`. **Behavior:** loads and prepares the buffer on a worker thread, publishing an immutable reference; the audio thread only ever swaps a pointer. Files are embedded in the patch or referenced, per the patch format's rules. **Taps:** waveform overview.

#### `data.table` — Table / Curve
**Out:** `data` — `Data(curve)`. **Structural:** the curve itself (a drawn multi-segment shape with per-segment tension, edited in this node's custom editor), `resolution`, `loop` (bool). **Behavior:** the hand-drawn source of envelopes, surface profiles, sequencer lanes, LFO shapes, waveshaper transfer functions, and remapping curves. One table can feed all of them at once: because the curve is shared data rather than a property of a single node, editing it changes every place it is used, which is something a per-node curve editor cannot do. Editing while audio runs republishes the buffer and the consumer crossfades over a few milliseconds, so drawing never clicks. Two tables plus a morph port (`adapt.remap`, `data.lookup`) give continuous interpolation between shapes. **Taps:** the curve, with live input positions from every consumer that reads it.

#### `data.scale` — Scale
**In:** `root : float·Pitch·0–11·linear·0`. **Out:** `data` — `Data(scale)`. **Structural:** `scale` (enum: major, natural/harmonic/melodic minor, the seven modes including Lydian, pentatonics, blues, whole tone, chromatic, harmonic series, custom), `customDegrees` (when custom), `octaveSize` (12 by default, other values for non-Western tunings).

#### `data.material` — Material
The physical-modelling counterpart of `data.scale`: physical parameters in, a modal set out. **In:** `stiffness : float·Unipolar·0–1·linear·0.5`; `density : float·Unipolar·0–1·linear·0.5`; `damping : float·Unipolar·0–1·linear·0.3`; `size : float·Unipolar·0–1·linear·0.5`; `inharmonicity : float·Bipolar·−1–1·linear·0`; `irregularity : float·Unipolar·0–1·linear·0` (how far modes stray from the ideal set — the difference between a perfect bell and a real stone). **Out:** `data` — `Data(modal-set)`. **Structural:** `geometry` (enum: string, bar, tube, membrane, plate, irregular solid), `modeCount` (default 32), `preset` (enum: wood, glass, metal, stone, ceramic, bone, ice, custom), `seed`. **Behavior:** computes mode frequencies, gains, and decay times from the geometry's modal equations on a worker thread; port changes republish the buffer. Because `resonator.modal` can transpose, damp, and tilt the set continuously, this node only has to be re-evaluated when the material itself changes, not per note. **Taps:** the resulting mode spectrum.

#### `data.analyseModes` — Analyse Modes
**In:** `data` — `Data(sample)` (a recording). **Out:** `data` — `Data(modal-set)`. **Structural:** `modeCount`, `windowStart`, `windowLength`, `decayEstimation` (enum: fit, fixed). **Behavior:** extracts prominent resonances and their decay rates from a recording of a real object — hit a rock, drop in the file, play the rock. The most direct route from "nature" to a playable model. **Taps:** detected modes overlaid on the spectrum.

#### `data.lookup` — Lookup
**In:** `in` — `Control [audio]`; `data` — `Data`, required; `dataB` — `Data` (optional morph target, same tag required); `morph : float·Unipolar·0–1·linear·0 [audio]`. **Out:** `out` — `Control`, quantity inherited from the destination. **Structural:** `mode` (enum: nearest, interpolate, index, wrap-index), `polarity` (enum: unipolar, bipolar), `edgeMode` (enum: clamp, wrap, mirror). **Behavior:** reads a value out of any buffer — nearest scale degree for a pitch, a curve value at a position, a step value at an index, a surface height at a scrape position. `polarity` defines how the input maps onto the table: unipolar treats 0…1 as the whole table, bipolar maps −1…1 across it, so a bipolar LFO sweeps the full shape rather than only its upper half. With `dataB` connected, the two buffers are interpolated point-by-point along the normalised x-axis, so tables of different resolutions still morph cleanly. Runs at audio rate, since reshaping audio-rate modulation is table reading with interpolation and nothing more. One node covers scale snapping, curve reading, sequencer lanes, and morphing between shapes.

## analysis

#### `analysis.onset` — Onset Detector
**In:** `in` — `Audio`; `sensitivity : float·Unipolar·0–1·linear·0.5`; `holdOff : float·Time·1–500ms·log·30ms`. **Out:** `onset` — `Event`; `strength` — `float·Unipolar` (per detected onset). **Structural:** `method` (enum: energy, spectral flux). **Taps:** input level with onset markers.

#### `analysis.pitch` — Pitch Tracker
**In:** `in` — `Audio`; `lowestPitch : float·Pitch·12–72·linear·36`; `smoothing : float·Time·0–200ms·log·20ms`. **Out:** `pitch` — `float·Pitch [audio]`; `confidence` — `float·Unipolar`; `voiced` — `bool`. **Structural:** `method` (enum: autocorrelation, YIN), `windowSize`. **Behavior:** reports latency to the host correctly, since tracking inherently costs a window. **Taps:** pitch trace with confidence shading.

#### `analysis.level` — Level
**In:** `in` — `Audio`; `attack`, `release` — `float·Time`. **Out:** `level` — `float·Gain [audio]`; `peak` — `float·Gain`; `clipped` — `bool`. **Structural:** `detection` (enum: peak, RMS, true peak).

#### `analysis.centroid` — Brightness
**In:** `in` — `Audio`; `smoothing : float·Time`. **Out:** `centroid` — `float·Frequency`; `normalised` — `float·Unipolar`. **Behavior:** spectral centroid — how bright the signal is, for driving models from an incoming sound.

## instance — domains

#### `instance.allocator` — Instance Allocator
Opens an instanced region. One node, several configurations; voices and swarms are the same machinery.

**In:** `spawn` — `Note` (Voice configuration) or `Event` (Swarm-transient, Trigger); absent in Swarm-population. Plus `maxInstancesLive : int·Count` (soft limit, modulatable — density can be performed).

**Out**, all `polyOnly`:
- always: `gate` — `bool`; `start`, `stop` — `Event`; `index` — `int·Count`; `age` — `float·Time`; `random.0…random.3` — `float·Bipolar` (independent, stable for the instance's life); `panPosition` — `float·Bipolar`; `distance` — `float·Unipolar`
- Voice configuration adds: `pitch` — `float·Pitch`; `velocity`, `pressure`, `slide`, `releaseVelocity` — `float·Unipolar`; `unisonIndex` — `int·Count`; `unisonDetune` — `float·Bipolar`

**Structural:** `configuration` (enum: Voice, Swarm-population, Swarm-transient, Trigger), `maxInstances` (preallocates per-instance state), `seed`.

**Ports (non-structural):** Voice: `glide : float·Time·0–5s·log·0`, `bendRange : float·Pitch·0–48·linear·2`, `unisonCount : int·Count·1–16·linear·1`, `unisonDetune : float·Pitch·0–1 st·linear·0.1`, `unisonSpread : float·Unipolar·0–1·linear·0.5`. Swarm: `population : int·Count·1–256` (population mode), `lifetime : float·Time` (transient mode), `spreadAmount : float·Unipolar`, distribution shaping per random output.

**Structural, behavioural:** `voiceMode` (enum: poly, mono, legato), `stealing` (enum: oldest, quietest, same note, none), `distribution` (enum: uniform, gaussian, exponential, bimodal).

**Behavior:** allocates an instance per spawn, seeding its randoms deterministically from `(patch seed, spawn ordinal)`; drives the instance context for its lifetime; frees an instance only when the signal reaching Voice Mix has been below the silence threshold for the hold time; fades stolen instances over a ramp rather than cutting them. Per-instance DSP state is keyed by `(instanceIndex, nodeId)` and survives a recompile that keeps node IDs. **Taps:** live instance count, instance age histogram, stealing indicator. **Native:** runtime machinery.

#### `instance.mix` — Voice Mix
Closes an instanced region. **In:** `in` — `Audio`, `polyOnly`; `silenceThreshold : float·Gain·−120–−40 dB·log·−80 dB`; `silenceHold : float·Time·1–2000ms·log·200ms`. **Out:** `out` — `Audio` (mono domain). **Structural:** `mode` (enum: sum, average, normalised by live count). **Behavior:** sums live instances and reports per-instance silence back to the allocator, which is what actually frees a slot and lets tails ring. Placeable anywhere and more than once. **Taps:** contributing instance count. **Native:** domain boundary.

## util

#### `util.constant` — Constant
**Out:** `out` — `Control`, contract configurable on the node (kind, quantity, range). **Behavior:** holds a value. Created automatically when a slider is unwrapped.

#### `util.macro` — Macro
**Out:** `out` — `Control`. **Structural:** `slot` (which of the fixed 32 host-automation slots this node claims; changing it re-targets recorded automation and warns), plus the exposed contract (kind, quantity, range, curve, enum options). **Behavior:** identical to Constant, plus its value is driven by the bound host parameter, smoothed. Because the host-facing pool stays fixed, there is no dynamic parameter list and no `restartComponent` problem; because the node has a real output port, a macro can be wired through Map, Add, or anything else, and can share a destination with an LFO.

#### `util.reroute` — Reroute
**In:** `in`. **Out:** `out` (same type). Layout waypoint; takes its source's colour.

## view — listening and looking

#### `view.listen` — Listen
**In:** `in` — `Audio`. **Behavior:** auditions this point in the graph, replacing the normal output while active. Created temporarily by the solo-listen gesture, or placed by hand. **Taps:** `in`.

#### `view.scope` — Scope
**In:** `in` — `Audio` or `Control`. **Structural:** `timeWindow`, `triggerMode` (enum: free, rising edge, per note). **Behavior:** a pure preview node; passthrough is not needed since it taps its input.

#### `view.spectrum` — Spectrum
**In:** `in` — `Audio`. **Structural:** `fftSize`, `tilt` (dB/octave display slope), `averaging`.

#### `view.meter` — Meter
**In:** `in` — `Audio` or `Control`. **Structural:** `mode` (enum: peak, RMS, true peak, histogram).

---

# Part B — factory groups

Built from Part A, shipped as data, openable and editable by the user.

| Group | What it is | Built from |
|---|---|---|
| **Karplus-Strong** | The textbook plucked loop, as a teaching patch | `excite.burst` → `delay.line` → `filter.onepole` → `mix.gain` → back into the delay, with `shape.clip` for safety |
| **Scale Quantize** | Pitch snapped to a scale | `data.scale` → `note.quantize` |
| **Arpeggiator** | Cycles held notes | `note.hold` → `clock.pulse` → `clock.counter` → `note.select` |
| **Chord** | One note becomes several | `note.chord` with `data.scale` for scale-aware voicings |
| **Bubble** | A single water bubble | `osc.sine` with pitch from `env.curve` (rising chirp) × `env.adsr` (short decay) |
| **Water** | Rain, a stream, a boil | `noise.dust` → `instance.allocator` (Swarm-transient) → Bubble per instance, radius from `random` → `instance.mix` |
| **Crackle** | Fire, static, ice | `noise.dust` → `excite.burst` → `resonator.modal` with a small stone/ceramic set |
| **Scrape** | Stone dragged across asphalt | `excite.contact` (speed and pressure from macros) → `resonator.modal` with `data.material` (stone, irregular) → `space.reverb` |
| **Cicada** | One insect | `clock.pulse` with jitter → `excite.burst` → `filter.formant` → body from `resonator.modal` |
| **Cicada Field** | A population of them | `instance.allocator` (Swarm-population) → Cicada per instance, rate and pitch from `random.drift`, placement from `panPosition` |
| **Breath / Wind** | Wind, breathing, flutes | `excite.breath` → `resonator.tube`, contour from `env.curve` |
| **Bowed String** | Violin-like | `excite.stickSlip` ↔ `resonator.string` with the coupling loop closed through `motion` |
| **Struck Body** | Drum, bell, plate | `excite.mallet` ↔ `resonator.plate` or `resonator.modal` |
| **Hex Guitar Front End** | Six strings to six note streams | six `io.audioIn` channels → `analysis.onset` + `analysis.pitch` → `note.assemble` per string |
| **Init Patch** | Ordinary subtractive synth | `io.noteIn` → `instance.allocator` → `osc.analog` ×2 → `filter.ladder` → `env.adsr` → `instance.mix` → `space.reverb` |

# Reference patches: coverage

Every reference patch from the planning prompt builds from Part A, with no missing primitives:

1. **Karplus-Strong with single-sample feedback** — `delay.line` + `filter.onepole` + `mix.gain`, closed as a per-sample region; `resonator.string` is the playable native version.
2. **MIDI remapped to a scale** — `io.noteIn` → `note.quantize` ← `data.scale`.
3. **Arpeggiator and chords** — `note.hold`, `clock.counter`, `note.select`, `note.chord`.
4. **Struck body with material data** — `excite.mallet` → `resonator.modal` ← `data.material`.
5. **Stone on asphalt** — `excite.contact` → `resonator.modal`, with `util.macro` driving speed and pressure.
6. **Transient and persistent swarms** — `instance.allocator` in both swarm configurations.
7. **Ordinary subtractive patch** — `osc.analog`, `filter.ladder`, `env.adsr`.
8. **Per-voice effects** — `shape.waveshaper`, `delay.line`, `space.reverb` placed before `instance.mix`.
9. **Hexaphonic guitar** — `io.audioIn` per channel → `analysis.onset` + `analysis.pitch` → `note.assemble` → `instance.allocator`.

# Deliberately deferred

- **Spectral family** (FFT-domain nodes): the `Spectral` signal type stays reserved and unused until it has a concrete design.
- **The group system itself**: Part B ships once groups exist; the catalogue above does not depend on them.
- **Nested allocators** beyond one level: supported by the model, opt-in later.
- **MIDI and note output** to the host: the `Note` type is ready for it; the I/O node is not specified yet.
- **Additive and FM-operator oscillator families**: `osc.sine` plus phase-modulation inputs covers the common cases until a dedicated family earns its place.
