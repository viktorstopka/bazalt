# Reverb — `space.diffuser` and `space.reverb`, built to a measured standard

**Status:** Built, 2026-10-04 — `engine/include/bazalt/engine/ReverbDsp.h`,
`nodes/DiffuserNode.h`, `nodes/ReverbNode.h`; measurements in §10. Deviations from
the design below, each found by measuring: lines span **1–2×** (not 0.5–1×) the
room's crossing time, because the sparser network's low modes stood out (§10);
lines are read with **allpass** interpolation, because cubic interpolation damped
the high band ~0.5 dB per pass (12 kHz decay 15 % short, freeze drained 2 dB in 24 s);
second-order shelves at fixed 250 Hz / 3 kHz crossovers; `mix` defaults to 0.3
(inserting in a patch is the common case); early reflections are the diffuser's
own output rather than a separate tap pattern. Character modes and shimmer remain
later work. Depends on `StereoChannels.md` (the
reverb is a fixed-stereo node; everything feeding it should stay stereo). Feeds
`SpatialScene.md` (which drives this node's physical parameters from a scene) and the
later convolution work (§8).

---

## 0. Origin

The user postponed reverb on purpose, "afraid of us doing a sloppy job on an important
effect", and asked how the best ones (Valhalla VintageVerb) get so good. The answer,
and this plan's premise: a *working* reverb is an afternoon; a *great* one is a known
recipe executed carefully and **checked by measurement and by ear**, not a secret
algorithm. This plan fixes the recipe, the quality bar, and how we prove we met it.

## 1. What makes the good ones good

From the literature (§9) and Sean Costello's (Valhalla) own published notes:

1. **Modulated delay lines.** Slowly moving delay lengths inside the feedback loop
   smear the fixed modes that make cheap reverbs ring "metallic". Single biggest
   difference between Freeverb and a pro tail.
2. **Fast, dense diffusion.** Allpass/mixing stages before (and inside) the loop turn
   a click into dense noise quickly, so the tail never flutters.
3. **Frequency-dependent decay** set per band (highs die sooner, lows can be longer or
   shorter), designed so the measured decay time matches the knob.
4. **Mutually incommensurate delay lengths** so echoes don't pile up on common
   multiples.
5. **Decorrelated stereo outputs** — wide but mono-compatible.
6. **Few, meaningful controls** and careful tuning by ear. Valhalla's "modes"
   (1970s/1980s/Now) partly *recreate old hardware's limits* (bandwidth, sample rate,
   grainy modulation) — character, not physics.

## 2. Design

One stereo-in/stereo-out node, built from shared DSP pieces in
`engine/include/bazalt/engine/dsp/` (the same way `BandLimited.h` is shared):

```
in (stereo) → predelay → input bandwidth (LP/HP) → diffuser (N stages)
            → FDN: N delay lines ── per-line absorption filters ── mixing matrix ─┐
                      ↑   modulated fractional reads                            │
                      └─────────────────────────────────────────────────────────┘
            → output taps (decorrelated L/R) → output EQ → width → out (stereo)
```

- **Diffuser** (Geraint Luff's multichannel form): the stereo input is spread over
  *N* internal channels (8; 16 at high quality); each stage = per-channel delays
  (random within a range, scaled by size) → Hadamard mix → per-channel polarity flips.
  4 stages ≈ near-instant density. Exposed separately as **`space.diffuser`**.
- **Feedback delay network (FDN)** (Jot & Chaigne): *N* delay lines, lengths spread
  geometrically between ~`size`-derived min/max and nudged to be mutually
  incommensurate, mixed by a lossless **Householder** matrix (cheap, O(N)) — Hadamard
  as an option to compare by ear.
- **Decay from physics, not "feedback"**: each line's gain per band comes from the
  target decay time: `g = 10^(−3·L / (RT60 · fs))` for a line of *L* samples, applied
  through a low/high shelf pair per line (Jot's absorption-filter design) so RT60 at
  low, mid and high bands are each exact. A final output tone-correction filter keeps
  the tail's spectrum flat when only decay times change.
- **Modulation**: each line's read position wanders slowly (independent smoothed
  random per line, a few samples deep, cubic/allpass-interpolated fractional read).
  Depth/rate exposed; defaults chosen by ear against the ringing measurement (§5).
- **Early reflections**: a short multi-tap stage before the FDN, its pattern scaled by
  `size` (and, later, *derived from geometry* — `SpatialScene.md` Stage 3).
- **Stereo**: L and R are read from the lines with orthogonal sign patterns
  (decorrelated); `width` blends toward mid.
- **Freeze**: matrix fully lossless, input muted → infinite sustain, a performance
  control (also a gate input).

### 2.1 Parameters — physical, few

| Param | Meaning |
|---|---|
| `size` | Room size in metres (scales delay lengths, early-reflection pattern, diffusion). |
| `decay` | RT60 in seconds (0.1 – 60, and ∞ via freeze). |
| `decayLow` / `decayHigh` | Multipliers on `decay` below/above crossover frequencies (e.g. 0.5×–2×). |
| `predelay` | ms. |
| `diffusion` | 0–1: how quickly echoes become dense. |
| `modulation` | Depth (and rate as a secondary param). |
| `early` | Early-reflection level vs. tail. |
| `width` | 0 (mono) – 1 (fully decorrelated). |
| `mix` | Dry/wet, defaulting to 100 % wet (send use); insert use turns it down. |
| `quality` | 8 or 16 lines (structural). 8 is cheap enough for per-voice placement. |
| `freeze` | Bool/gate input. |

`size` and `decay` stay independent (a big dead room and a tiny ringing tank are both
valid) — `SpatialScene.md` derives `decay` from size + surface absorption via Sabine
when the scene drives it.

Later, not v1: **character** modes (a "vintage" mode emulating reduced bandwidth and
grainier modulation, if we want it); shimmer (pitch shift in the loop).

### 2.2 Realtime rules

- Delay lines preallocated in `prepare()` for max size at the current sample rate.
- `size` changes glide the read positions (like tape — a deliberate, musical pitch
  smear) rather than jumping; live slider drags go through `LiveParameterEdits`.
- Denormals: flush-to-zero in the tail (a decaying FDN produces them by design).
- Block-size invariant; sample-rate-derived constants computed in `prepare()`.

## 3. Nodes

- **`space.diffuser`** — stereo in/out; `size` (ms), `stages` (1–6), `diffusion`,
  `modulation`. Useful alone (smear transients, thicken delays) and inside
  `space.reverb`. Reuses the same `dsp/` code, no duplicate.
- **`space.reverb`** — §2. Fixed `Channels::Stereo` in and out (mono input
  broadcasts). `SpatialScene.md`'s "physically parameterised room" *is* this node —
  no second `space.room`.

## 4. Order of work

1. Shared DSP: modulated fractional delay line, Householder/Hadamard mixers, the
   absorption-filter design function.
2. **Measurement harness** (§5) — before tuning, so tuning is against numbers.
3. `space.diffuser` (smaller, proves the diffusion core and the harness).
4. `space.reverb`.
5. **Tuning pass with the user**: rendered examples (render-cli WAVs: click, snare-like
   burst, sustained chord, vocal-like formant source) at a handful of presets; adjust
   defaults by ear; record the final measurements here.
6. Catalog/Status docs; Init Patch gets its reverb tail.

## 5. Quality bar — measured, in `tests/`

A reusable `ReverbMeasurements` helper renders an impulse response and checks:

| Measure | How | Bar |
|---|---|---|
| Decay time accuracy | Schroeder backward integration → T30 per octave band | within ±10 % of the target for `decay` 0.3–20 s, at low/mid/high bands honouring `decayLow/High` |
| Echo density | Abel & Huang normalized echo density | reaches ~1 (Gaussian-like) within a size-dependent time (≈ 50–150 ms) at `diffusion` 1 |
| No metallic ringing | spectrum of the late tail: max peak vs. median | below a fixed threshold (set during step 5, then enforced) |
| Stereo decorrelation | L/R correlation of the tail | \|ρ\| < 0.3 at `width` 1; ≈ 1 at `width` 0 |
| Mono compatibility | energy of (L+R)/2 vs. L, R | no deep loss (> −3 dB) |
| Freeze stability | energy over 30 s frozen | constant within ±0.5 dB, no blow-up |
| Stability | every param at its extremes, noise input, 60 s | finite, bounded output |
| Block-size invariance | standard test | bit-exact |
| CPU | benchmark, 8 and 16 lines | recorded; budget per instance noted in Status |

These numbers make "not sloppy" checkable; the listening pass covers what numbers
can't.

## 6. What makes ours *different* (the digital-native part)

- Controls are physical (metres, seconds per band), so a scene, a material or a
  modulator can drive them meaningfully.
- It's a node, so it can be **per voice** (each note its own space), **modulated**
  (size and decay are ordinary ports), and **frozen** on a gate.
- The diffuser is a first-class primitive, not buried inside.
- Geometry later replaces the generic early-reflection pattern (`SpatialScene.md`).

## 7. Risks

- Tuning is open-ended → the measurement bar + a time-boxed listening pass, not
  endless tweaking.
- Per-voice cost at 16 lines × 8 voices × 2 → `quality` param, 8 lines per-voice.
- Modulation that's too deep audibly detunes sustained notes → defaults checked on a
  sustained chord.

## 8. Convolution (separate, later)

The DSP is well known (uniformly partitioned FFT convolution with a zero-latency
direct head; `juce::dsp::Convolution` is available to `engine/` via `juce_dsp` and
loads/swaps IRs off the audio thread). The real dependency is **where IRs come from**:

1. `fx.convolve` consuming `Data(impulse-response)` (`DataTag::ImpulseResponse`).
2. `data.irSynth` — synthetic IRs from noise shaped by exact per-band decay (a
   perfectly smooth "ideal room", measurable with the same harness).
3. Capture-from-graph (convolve with a recorded snippet: cross-synthesis).
4. IR files — after the asset store (`Factories.md` decision 1).
5. Geometry IRs — `SpatialScene.md` Stage 3.

Gets its own plan file when it's next.

## 9. Reading list

- Schroeder, *Natural sounding artificial reverberation* (1962) — the origin.
- Moorer, *About this reverberation business* (1979).
- Jot & Chaigne, *Digital delay networks for designing artificial reverberators*
  (1991) — FDN and absorption filters.
- Dattorro, *Effect Design Part 1: Reverberator and Other Filters* (1997).
- Välimäki, Parker, Savioja, Smith, Abel, *Fifty Years of Artificial Reverberation*
  (2012) — survey.
- Abel & Huang, *A simple, robust measure of reverberation echo density* (2006).
- Schlecht & Habets — time-varying and scattering FDNs (2015–2020).
- Geraint Luff, *Let's Write a Reverb* (ADC 2021) — the practical modern recipe this
  design follows.
- Sean Costello's Valhalla DSP blog — design notes from the VintageVerb author.

## 10. Measured (2026-10-04, 48 kHz, `tests/ReverbTests.cpp`)

| Measure | Result | Bar |
|---|---|---|
| Decay at 1 kHz, 0.3–20 s, 8 and 16 lines | within 1–5 % | ±10 % |
| Per band, decay 2 s, ×2 low / ×0.4 high | 63 Hz 3.96 s (4.0), 1 kHz 1.97 s (2.0), 12 kHz 0.80 s (0.8) | ±10 % |
| Echo density reaches 0.9 | 25 ms (5 m), 50 ms (12 m), 105 ms (30 m) | < 150 ms |
| Tail peak-to-local-median | 8 lines 15–17 dB, 16 lines 13–14 dB (decaying white noise: 11 dB; before the 1–2× lines: 24–25 dB) | < 18 dB |
| L/R correlation of the tail, width 1 | ≈ 0.01 | \|ρ\| < 0.3 |
| Mono sum | −3.0 dB | > −4 dB |
| Freeze, 4–5 s vs 28–29 s | −0.1 dB | ±0.5 dB |
| 60 s of noise at every extreme | finite, peak < 4 | bounded |
| Block size 1 / 64 / 509 | bit-exact | bit-exact |
| CPU, 10 s stereo, Debug build | 8 lines ≈ 2.5 s, 16 lines ≈ 5 s | recorded |

Listening renders: `BAZALT_RENDER_DIR=<dir> EngineTests "[.render]"` writes a dry
source and room / hall / plate / huge presets.
