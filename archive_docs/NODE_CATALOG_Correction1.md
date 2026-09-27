# Node catalogue — correction 1: voiced and self-oscillating models

Amends `NODE_CATALOG.md`. Derived from the cat-purr reference model, which is the first case that exercised the physical-modelling set hard enough to expose what it was missing. Nothing in the architecture had to change; everything below is a missing primitive or a rule that was implicit and should be written down.

Each section below names the part of `NODE_CATALOG.md` it amends.

---

## 1. Amends "Decisions this catalogue settles" — four additions

**Pressure is bipolar.** Any port carrying breath or air pressure uses quantity `Pressure` with a bipolar range. Inhalation is not exhalation with a minus sign — the geometry of a valve is not symmetric front to back, so a physical model must be drivable in both directions and must behave differently in each. This applies to `excite.breath`, `excite.vocalFolds`, and anything added later that is driven by flow.

**Excitation nodes expose their internal state.** Every excitation node outputs not only audio but the physical state that drives it: flow, contact, whether it is oscillating. Without flow as a signal, aspiration noise cannot be gated by the actual airflow, which is the difference between a convincing voice and noise laid under a tone. Without an `oscillating` flag, a self-oscillating model sitting below its threshold looks broken rather than silent.

**Physical parameters accept audio-rate modulation.** On self-oscillating models, stiffness, mass, adduction and pressure are all `[audio]`. This is not a luxury: driving stiffness with a periodic signal near the model's own frequency is how entrainment, beating and mode-locking arise, and those behaviours are what make a model feel alive. A model whose physical parameters are block-rate cannot produce them.

**Physical quantities are first class.** `Pressure`, `Mass`, `Length`, `Stiffness` join the quantity list, each with one canonical unit. The reason is practical: published measurements (a 4 mm connective-tissue pad, f₀ of 26.3 Hz, 80–95 % contact per cycle) can be entered as themselves instead of being guessed as normalised values. Adding a quantity later is safe; changing an existing port's quantity is not, so physical nodes are specified with them from the start.

---

## 2. Amends the index table — three rows replaced

| Family | Nodes |
|---|---|
| `osc.*` | analog, sine, wavetable, glottal |
| `excite.*` | impulse, burst, pluck, mallet, stickSlip, breath, contact, vocalFolds |
| `resonator.*` | modal, string, tube, plate, comb, junction, tract |

---

## 3. New node in the `osc` family

#### `osc.glottal` — Glottal Pulse
A parametric model of the airflow pulse produced by a vibrating valve (Liljencrants–Fant, with a simpler Rosenberg mode), instead of a generic impulse. Its parameters are the ones voice research actually measures, so a published figure can be dialled in directly.
**In:** `f0 : float·Frequency·10–2000Hz·log·110 [audio]`; `openQuotient : float·Unipolar·0.01–0.9·linear·0.5 [audio]` (fraction of the cycle the valve is open — very low values are what make a purr a purr); `asymmetry : float·Unipolar·0.1–0.9·linear·0.6` (rise versus fall of the flow pulse); `closureSharpness : float·Unipolar·0–1·linear·0.5 [audio]` (how abruptly the valve shuts, which sets how much energy reaches the high harmonics); `jitter : float·Unipolar·0–1·linear·0` (period-to-period deviation); `shimmer : float·Unipolar·0–1·linear·0` (amplitude deviation); `sync : Event`.
**Out:** `out` — `Audio` (the flow derivative, i.e. the acoustic source); `flow` — `float·Unipolar [audio]` (instantaneous flow, for gating aspiration noise); `open` — `bool`.
**Structural:** `model` (enum: LF, Rosenberg), `seed`.
**Behavior:** synthesises one pulse per period in closed form, band-limited, with jitter and shimmer applied per period rather than as post-hoc modulation. The pulse has finite width and an asymmetric shape, so the highest harmonics are damped the way a real flow pulse damps them. **Taps:** the pulse shape over one period, plus a live f₀ readout. **Native:** delicate band-limited pulse generation; the parameter-to-waveform mapping is closed form and would take dozens of nodes to approximate.

---

## 4. New node in the `excite` family

#### `excite.vocalFolds` — Vocal Folds
A self-oscillating two-mass valve: pressure pushes the masses apart, flow and elasticity pull them back, and the phase lag between the lower and upper mass lets the system extract energy from a steady airflow. Frequency is not an input — it emerges from pressure, mass, stiffness and geometry, exactly as it does in an excised larynx. This is the node that produces behaviour the parametric models can only imitate: an oscillation threshold, subharmonics and deterministic chaos from left–right asymmetry, and a different regime when the airflow reverses.
**In:** `pressure : float·Pressure·bipolar·linear·0 [audio]` (below the threshold nothing oscillates; negative is inhalation, which behaves differently); `mass : float·Mass·[audio]`; `stiffness : float·Stiffness·[audio]` (muscle tension — modulating this near the model's own frequency produces entrainment); `adduction : float·Unipolar·0–1·linear·0.5 [audio]` (resting separation of the two sides); `asymmetry : float·Bipolar·−1–1·linear·0` (left–right mismatch — the route to subharmonics and roughness); `damping : float·Unipolar·0–1·linear·0.3`; `feedback` — `Audio` (pressure from the tract above; optional).
**Out:** `out` — `Audio`; `flow` — `float·Unipolar [audio]`; `contact` — `bool` (true while the valve is shut); `contactQuotient` — `float·Unipolar` (fraction of the cycle in contact, measured per period); `oscillating` — `bool`; `f0` — `float·Frequency` (measured, not commanded).
**Structural:** `masses` (enum: one-mass, two-mass), `seed`.
**Behavior:** two coupled mass-spring-damper systems with a nonlinear aerodynamic driving force and a collision condition when the sides meet. With `feedback` connected, source and filter stop being independent and the tract's pressure influences the oscillation. Integration runs at audio rate with an internal step small enough to stay stable under audio-rate parameter changes; it clamps rather than diverging, and reports `oscillating = false` instead of producing noise when it falls below threshold. **Taps:** `out`, flow and contact over time, measured f₀, a state indicator for "below threshold". **Native:** single-sample feedback, numerically delicate, runtime state that cannot be expressed as a subgraph.

---

## 5. Two new nodes in the `resonator` family

#### `resonator.junction` — Scattering Junction
A multi-way junction where waveguides meet: incoming waves are partly transmitted into each branch and partly reflected, according to the branches' impedances. It is what makes a branched acoustic system possible, and a branch closed at its far end acts as a side cavity that removes energy at its own resonances, producing the spectral notches (antiresonances) that give nasal and muffled sounds their character. A band-pass filter cannot create those.
**In:** `in` — `Audio`; port group `branch.0…branch.N` — `Audio` (growable, min 2, max 8, bidirectional: each branch pairs with a resonator's `in`/`motion`); `impedance.0…N : float·Unipolar·0–1·linear·0.5 [audio]` (relative cross-section of each branch — modulating one is opening or closing a valve, such as a soft palate); `loss : float·Unipolar·0–1·linear·0.05`.
**Out:** `out` — `Audio`; the branch group returns the reflected waves.
**Structural:** `branches` (2–8).
**Behavior:** Kelly–Lochbaum scattering at a shared node, energy-preserving up to `loss`, stable under modulation of the impedances. **Taps:** per-branch energy. **Native:** single-sample feedback across several paths.

#### `resonator.tract` — Tract
A multi-section waveguide whose cross-section profile is read from a curve, so formants arise from the shape of the tube instead of being dialled in on filters. The same node is a vocal tract, a bore, a duct or a horn.
**In:** `in` — `Audio`; `shape` — `Data(curve)` (cross-sectional area along the tube; a neutral default profile is used when unconnected); `length : float·Length·0.005–2m·log·0.08m [audio]` (scaling the whole tube — this is body size); `damping : float·Unipolar·0–1·linear·0.2`; `radiation : float·Unipolar·0–1·linear·0.5` (how open the far end is; near zero it is a closed side cavity).
**Out:** `out` — `Audio` (radiated at the open end); `motion` — `Audio` (pressure at the input end, to be fed back into an excitation node's `feedback`).
**Structural:** `sections` (8–64, the resolution of the profile), `maxLength`.
**Behavior:** bidirectional wave propagation with scattering at each section boundary derived from the area profile, plus a radiation filter at the open end. Because the profile is `Data`, it can be drawn, morphed between two shapes, or produced by another node. **Taps:** `out`, the area profile, the resulting frequency response. **Native:** single-sample feedback along the whole chain; inner loop scales with section count.

---

## 6. Amends `filter.formant` — its port list is replaced by this one

**In:** `in`; `vowel : float·Unipolar·0–1·linear·0 [audio]`; `formants` — `Data(modal-set)` (optional custom formant table); `antiformants` — `Data(modal-set)` (optional — spectral notches, which is what a side cavity such as a closed mouth produces; without them nasal sounds cannot be imitated by filters); `antiformantDepth : float·Unipolar·0–1·linear·0.7`; `shift : float·Pitch·−24–24 st·linear·0`; `intensity : float·Unipolar`.

---

## 7. Amends "Reference patches: coverage" — new item 10

10. **Voiced self-oscillation (cat purr)** — the hardest test in the set, because it exercises nearly every claim the architecture makes at once: `env.curve` supplies a slow bipolar breath pressure that crosses zero between phases; `random.drift` jitters stiffness; an `lfo.shape` at roughly 26 Hz modulates stiffness at audio rate so the neural-rhythm and tissue-oscillation hypotheses can be blended and made to entrain or drift apart; `excite.vocalFolds` oscillates from that pressure with f₀ emerging rather than commanded; its `flow` gates `noise.colored` through `mix.gain`, so aspiration exists only while air is moving; `resonator.junction` splits the path into `resonator.tract` for the nasal route and a closed branch for the shut mouth, producing antiresonances; `resonator.modal` fed from the same source with a heavily damped irregular material set carries the body-conduction path; `mix.crossfade` between the two paths is the microphone position, from the nose to the animal's back.

**What it proves:** audio-rate modulation of physical parameters, emergent oscillation thresholds, source–resonator coupling through `feedback`/`motion`, branched waveguides, `Data` as a geometric profile, flow-gated noise, two sources sharing one tract, and a fundamental below 30 Hz.

**Testing note:** self-oscillating and chaotic models are deterministic but extremely sensitive to rounding, so two compilers or an enabled FMA path will diverge into a different realisation within seconds. Bit-exact golden renders do not work for them. These models are verified statistically instead: measured f₀ within tolerance, spectral envelope within tolerance, jitter and shimmer within a band, and the oscillation threshold occurring within a pressure window. This must be stated in the test plan, or CI failures will be indistinguishable from physics.
