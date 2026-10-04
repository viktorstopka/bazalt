# Sound Palette — the everyday nodes still missing

**Status:** Built, 2026-10-04 — Batches 1-3 complete, Batch 4's `fx.freqShift` built;
chorus/flanger/phaser stay planned as stock groups until `stock.*` loading exists.
Tests: `tests/SoundPaletteTests.cpp`. Deviations: shapers use first-order ADAA
(antiderivative antialiasing) instead of oversampling — per sample, no latency,
block-size safe, so they also work inside feedback loops and channel lanes;
`analysis.level` takes stereo (both channels measured together); `filter.svf` keeps
`out` as its lowpass id so no patch migration was needed. Step 3 of the agreed sequence:
1. `StereoChannels.md` → 2. `Reverb.md` → **3. this** → then `Factories.md`.

Brief by design: each node's spec already lives in `wiki/NODES.md`; this file only
picks *what* belongs to the palette, *in which batches*, and the rules they share.

---

## 0. Origin

"We need the rest" (2026-10-04) — after stereo and reverb, fill in the nodes a
sound designer reaches for every day, so ordinary patches stop running into gaps.

## 1. Shared rules for every node here

- **Channel-aware from birth**: per-channel processors declare `Channels::Inherited`
  (`StereoChannels.md` §2.2); only nodes whose channels interact declare fixed Stereo.
- **Live-editable**: every non-structural parameter goes through the value-only edit
  path / `LiveParameterEdits` (no clicks while dragging).
- **One test harness per family**, not per node:
  - *Shaping harness*: static transfer curve, DC behaviour, and **aliasing** measured
    as energy outside the expected harmonics for a high sine (sets the oversampling bar).
  - *Noise harness*: spectral slope per colour (±1 dB/oct), level, seed determinism.
  - Block-size invariance for all of them.
- **Previews** reuse what exists (phase-locked for sources, history/modulation scopes).

## 2. Batches

### Batch 1 — simple, high value
| Node | Notes |
|---|---|
| `noise.colored` | white/pink/brown/blue/violet; `stereo` option = decorrelated channels; seeded. |
| `shape.rectify` | half/full, bipolar-aware. |
| `shape.crush` | bit depth + sample-rate reduction (fractional, modulatable, block-size-safe). |
| `lfo.shape` *(v1)* | The palette has **no LFO node** today. Built-in shapes (sine/tri/saw/square/S&H/smooth random), rate free or tempo-synced, phase, unipolar/bipolar, retrigger; a phase source so the existing phase-locked preview works. The `Data(curve)` input joins later with `factory.curve`. |
| `analysis.level` | RMS/peak level as Control — mirrors `env.follower`'s ballistics. |

### Batch 2 — shaping and finishing existing MVPs
| Node | Notes |
|---|---|
| `shape.waveshaper` | tanh/asym/tube-ish/sine/polynomial curves, drive, bias, oversampling (shared `OversamplingStage`). Custom-curve input later via `factory.curve`. |
| `shape.fold` | wavefolder, oversampled. |
| `filter.svf` → full | the 5 simultaneous outputs redesign (LP/BP/HP/notch/peak) — closes its MVP. |
| `osc.analog` → full | `fine`, `pulseWidth`, `phaseMod`, `sync` — closes its MVP. |
| `excite.burst` → full | `tone`/`shape` — closes its MVP. |
| `noise.dust` | sample-accurate sparse impulses (density, random amplitude/polarity). |

### Batch 3 — dynamics (new family, not yet in the catalog)
The catalog has no compressor/gate at all; add them to `NODES.md` first.
| Node | Notes |
|---|---|
| `dyn.compress` | threshold/ratio/knee/attack/release/makeup, **sidechain input**, stereo-linked detection (fixed Stereo — channels interact), gain-reduction output as Control (so ducking is wiring, not a special mode). |
| `dyn.gate` | gate/expander, sidechain, gain-reduction output. |

Digital-native angle: a compressor is "an envelope follower driving a gain" — expose
the gain-reduction signal so it can modulate *anything*, not only the level.

### Batch 4 — modulation effects
- **Chorus / flanger / phaser as stock groups** (from `delay.line`, `filter.allpass`,
  `lfo.shape`), shipped once `stock.*` loading exists — the primitives-first rule.
  A native node only if the group's cost or sound measurably falls short.
- **`fx.freqShift`** — Hilbert-transform frequency shifter (and ring-mod side output):
  inharmonic shifting that's natural digitally and awkward in analog.

### Later, tied to other plans (listed so nothing is forgotten)
- `osc.wavetable`, `env.curve`, `lfo.shape`'s curve input → `Factories.md`.
- `sampler.player`, `sampler.granular`, IR/convolution → asset store / `Reverb.md` §8.
- `filter.formant`, `excite.breath/contact/stickSlip`, `resonator.tube` → the PM
  Friction/Breath and PM Voice batches (own track).
- `analysis.onset/centroid/pitch` → Analysis batch.

## 3. Done means

Each batch: nodes registered, documented in `NODES.md`/`NODES.Status.md`, family
harness green, stereo-preserving, and at least one factory patch that uses the batch
(e.g. an LFO-swept, waveshaped, compressed bass; a noise-and-dust texture).
