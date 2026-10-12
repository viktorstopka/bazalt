# Life and Biology — research for roadmap stage 2

**Date:** 2026-10-12. **Question:** how should Bazalt add a biological layer — sounds made
of instances that live, sense each other and the input, and move — grounded in real
models from biology and physics, while the life-cycle mechanism itself gets redesigned
into something robust enough for ordinary patches too? **Feeds:** `wiki/plans/LifeCycle.md`
(the plan) and `wiki/plans/InstanceAxis.md` (the engine half).

Agreed in conversation before this report (2026-10-12):

- The goal is sounds made by DSP from instances that interact with each other or with
  the audio input; the bottom layer is ordinary synthesis of real sounds (one cricket from
  scratch), the layer above is the population (a field of crickets that is aware).
  Examples: fire driven by a guitar; a 3D cricket field whose positions come from LFOs or
  from biological movement; a physically grounded bird song; a real purr.
- **Papers over datasets**, unless a dataset is clearly the right source.
- **Instances interact** — flocking, avoiding, syncing — built from the base nodes as far
  as possible; the life-cycle nodes leave room for it.
- **Traits, not evolution.** Evolution is scope creep; not in this stage.
- **Class + body**: a Class carries traits and the sound (a Group of nodes).
- **Determinism is a setting**, not a rule.
- One listener point by default (a Microphone node later). The audio input is global
  by default, but each instance can also hear it locally.
- **Split** for different chains per instance; Groups open in place.
- No Cricket / Purr / Bird nodes — sound models are example patches plus the few
  generic nodes they need.
- This is equally a **life-cycle redesign for ordinary patches**: different chains per
  voice by order, pitch or anything else.

---

## 1. The life cycle today, and why it is scattered

Five nodes, each its own C++ class: `life.voice` (Voice), `life.trigger` (Trigger),
`life.swarmPopulation` (Swarm (Population)), `life.swarmTransient` (Swarm (Transient)),
`life.merge` (Merge). What is wrong:

1. **Four allocators that are one idea.** They differ only in when an instance is born,
   how long it lives and what happens when the pool is full (`InstanceAxis.md` §4) — yet
   each re-declares the same identity outputs (`gate`, `instanceIndex`, `instanceAge`,
   `random1`, `random2`, `start`, `stop`; Swarm adds `position`).
2. **Eight copies of the patch.** Every poly part is compiled 8 times; a "shared" mono
   LFO feeding voices is nine drifting copies; limits are structural (8 voices, 4
   allocators, 4 sums each); a swarm inside a voice is impossible.
3. **A known bug by construction.** Swarm (Transient)'s relay reads one node copy's state,
   so a second transient spawned before the first ends loses its release (the class's
   own comment documents it).
4. **MIDI is a special path.** The plugin pokes `io.noteIn` directly; the other origins
   use an internal relay. Two ways to be born.
5. **Position is one number** (a pan), and nothing can sense another instance.
6. **No way to send different voices down different chains** except duplicating the
   allocator.

None of this is about biology. It is the debt the instance axis pays off.

## 2. Prior art: this is a particle system

**Reeves (1983)** defined particle systems for film effects with exactly the parts we
need: an **emitter** that creates particles at a rate, each particle born with
**attributes drawn from a mean and a variance** (initial position, velocity, size,
colour, lifetime), particles that **move** under simple rules, and **die** when their
lifetime ends. Map it onto Bazalt:

| Reeves | Bazalt |
|---|---|
| emitter, generation rate | Population, *born on* (note / event / rate / always) |
| attributes with mean ± variance | **Class** — traits with a distribution |
| lifetime | Population, *lives* (while held / fixed / forever) |
| per-particle dynamics | **Move** (an integrator), forces from ordinary math nodes |
| rendering | the Group each instance plays — DSP |

Two other precedents confirm the shape:

- **Granular synthesis** (Roads, *Microsound*, 2001) is a particle system in time:
  grains are short-lived instances with their own parameters. A Population born at a
  rate with a fixed lifetime *is* a grain cloud — the roadmap's granular test.
- **Attribute-per-point processing** (Houdini's particle attributes, VEX running once per
  point): operators are written for one element and run for each. Bazalt already works
  this way on the channel axis (`StereoChannels.md`); the instance axis is the same idea
  (`DataAndWavetable.md` D2).

## 3. How instances can interact — models from biology and physics

Every model below has the same structure: each individual reads a **summary of the
others**, then changes itself. That is the whole mechanism Bazalt needs — a **Sense**
node producing the summary, ordinary nodes deciding, a **Move** node or any parameter
changing.

### 3.1 Flocking and avoiding

- **Reynolds (1987), boids:** three local rules — *separation* (steer away from crowding
  neighbours), *alignment* (match their heading), *cohesion* (steer toward their centre).
  Flocks, schools and herds emerge; nothing global is coded.
- **Vicsek et al. (1995):** the minimal version — each particle takes the average heading
  of its neighbours plus noise; above a density/below a noise level the group orders.
- **Ballerini et al. (2008)** — real data from 3D reconstructions of starling flocks: a
  bird interacts with a **fixed number of nearest neighbours (about 6–7)**, not with
  everyone within a fixed distance ("topological, not metric"). This keeps a flock
  coherent when it stretches. **Consequence for Bazalt:** Sense should support "the k
  nearest" as well as "within a radius", and k = 7 is a defensible default.

With Sense giving *direction to the neighbours' centre*, *nearest distance*, *average
neighbour velocity*, the three boid rules are a Multiply and an Add each. Fleeing,
orbiting, swarming around the input are the same nodes with other signs and targets.

### 3.2 Synchronizing and alternating

- **Kuramoto (1975, 1984):** each oscillator's phase is pulled toward the others by a
  coupling strength K; above a critical K the population locks. The pull depends only on
  the **mean field** — the average of all phases, *r·e^{iψ}* — so it costs one average per
  block, not N² comparisons. r (0..1) is itself a musical signal: how in sync the
  population is.
- **Mirollo & Strogatz (1990):** *pulse-coupled* oscillators — when one fires, every
  other jumps its phase a little forward. Proven to synchronize fully (for their model).
  This is the **event** version and matches fireflies and chirping insects.
- **Buck (1988):** field and laboratory data on synchronous flashing fireflies — the
  biological case for both models.
- **Greenfield (1994):** chorusing insects and frogs either **synchronize or alternate**,
  with one mechanism — a call resets the neighbours' rhythm; whether that leads to
  overlap or to taking turns depends on timing. So "sync" and "avoid each other in time"
  are one rule with a sign.

**Consequence:** the Oscillator needs no new mode. A Population's oscillators read
Sense's mean phase (or its "someone fired" event) and nudge their own Phase input. The
one new thing is that Sense can average a *phase* correctly (as a unit vector, not as a
number that wraps).

### 3.3 Shared environment

- **Dolbear (1897), "The cricket as a thermometer":** the chirp rate of the snowy tree
  cricket follows air temperature closely enough to read the temperature from it (Dolbear's
  law). A real, measured relationship: an **environment value** every cricket reads.
- **Logistic growth (Verhulst, 1838)** and **Lotka–Volterra (1925–26)** — how a
  population's birth and death rates depend on its size and on other populations. They
  are just signals into Population's birth rate and lifetime, not special nodes.
- **1/f fluctuation (Voss & Clarke, 1975):** loudness and pitch fluctuations in music and
  speech follow 1/f spectra, as do many natural rhythms. Already covered by noise and
  `random.drift`; the report notes it so "natural variation" uses 1/f rather than white
  noise by default.

"Global, and locally if wanted" (the conversation's answer on the input) falls out of the
same idea: a global signal is the environment everyone reads; a local one is the same
signal attenuated by each instance's distance from where it comes from (§5).

## 4. The bottom layer — real sound models, and what they need

Each is an **example patch**, not a node. What matters is which generic nodes are missing.

| Sound | Model (paper) | What the patch is | Missing generic piece |
|---|---|---|---|
| **Cricket** | Bennet-Clark & Bailey (2002), "clockwork cricket": the wing's scraper strikes the file's teeth one at a time; each strike rings the harp, a resonator tuned near 4–5 kHz, so the carrier is the resonator's, held steady by the escapement. A pulse is one wing closure; a chirp is a few pulses; chirp rate follows temperature (Dolbear). | Clock (tooth strikes) → Impulse → Modal/peak resonator ~4.5 kHz, gated by a pulse/chirp envelope; Class traits for carrier, tooth rate, chirp rate. | Nothing essential — a good **impulse train with jitter** would help. |
| **Purr** | Remmers & Gautier (1972); survey: Peters (2002). Laryngeal muscles twitch about 25 times a second, on both the in- and the out-breath, with a brief pause where the breath turns; the glottis opens and closes with each twitch. | Clock ~25 Hz → short bursts of noise/impulses → formant filters; a breathing curve (Envelope, loop) shaping level and switching character between in- and out-breath. | Nothing essential. |
| **Bird song** | Gardner et al. (2001), Mindlin & Laje (2005): the syrinx's labia behave as a **nonlinear oscillator** whose state is steered by two slow gestures — air-sac **pressure** and syringeal **tension**; simple smooth gestures produce realistic syllables; the vocal tract filters the result. | Two Curves/Envelopes as the gestures → a driven nonlinear oscillator → a resonant tract filter. | **A driven nonlinear oscillator** (normal-form Hopf / van der Pol family) with two control inputs — generic: also useful for reeds, lips, chaotic drones. |
| **Fire** | Farnell, *Designing Sound* (2010): fire as **crackles** (sparse impulses, random size, each a short resonant pop), **hiss** (noise, high frequencies, flickering) and **roar/lapping** (low-passed noise, slowly modulated); Chadwick & James (2011) for the physical basis. | A Population born at a rate following the guitar's energy (Level / Onset), each instance a crackle with random traits; hiss and roar from noise modulated by the same energy. | Nothing — this is the Population redesign's showcase. |

Two generic additions, then: a **jittered impulse train** (small) and a **driven nonlinear
oscillator** (medium). Everything else exists.

## 5. Position and space

Minimal and correct now; the full scene stays in stage 4 (`SpatialScene.md`).

- **Distance:** spherical spreading, −6 dB per doubling (1/r), clamped near the source.
- **Air absorption:** high frequencies fade with distance (ISO 9613-1 gives the
  coefficients by frequency, temperature and humidity) — a gentle one-pole low-pass whose
  cutoff falls with distance is a faithful-enough first version.
- **Time of flight:** distance / 343 m/s — a delay; moving sources get Doppler for free.
- **Direction:** azimuth → pan for now (the listener is a single point at the origin; a
  Microphone node later).

**Vectors.** A position is three numbers. Recommendation: carry x / y / z on the existing
**channel axis**, with a quantity that says "vector" rather than "stereo". Then Add,
Multiply, Slew, Map and every other node work per component for free (exactly as they
already work per stereo side), and a few math nodes reduce a vector (length, distance,
normalise, dot). Wiring a vector into a stereo-only place is rejected by the connection
rules. This needs care in the type system and is the riskiest decision in this report —
it gets its own batch.

## 6. What this means for Bazalt — the vocabulary

**Principle: three layers, and biology never makes sound itself.**

| Layer | Question | Nodes |
|---|---|---|
| **Life** | who exists | **Population**, **Split**, **Merge** |
| **Identity** | what is each one | Population's identity outputs, **Class**, **Trait** |
| **Environment** | what they share and sense | **Sense**, **Move**, **Locate** (+ any global signal) |

Plus one container: **Group** (a named set of nodes with inputs and outputs, opened in
place) — what a Class's body is, and what keeps Split's branches readable.

- **Population** replaces Voice, Trigger and both Swarms. Three choices (born on / lives /
  when full), a size, a seed (fixed or new each run), an optional Class. Voice, Trigger,
  Swarm become Add-menu presets. A MIDI note is one way to be born — no special path.
- **Identity outputs** on Population: gate, index, birth order, pitch, velocity, rank (in
  pitch among the living), age, alive count, seed-random. Nested Populations each give
  their own.
- **Split**: sends each instance down one of N branches by a rule — pitch range, velocity
  range, birth order mod N (round robin), rank, or any condition signal. A branch runs
  only for its instances. **Merge** reduces the instance axis (sum / average), as today.
- **Class** (Data): named traits, each a distribution (fixed, uniform range, normal,
  1/f-varied over time), plus a body (a Group). **Trait** reads one trait by name as a
  per-instance signal. Not biological by nature: a "Snare hit" class works the same.
- **Sense**: per instance, a summary of the others — within a radius or the k nearest
  (default 7, Ballerini) — direction and distance to their centre, nearest distance, their
  count, the average of any wired signal (a phase averaged as a phase), "a neighbour
  fired". Also the population-wide average (Kuramoto's mean field). Results arrive one
  block later (~1–10 ms) — inaudible for movement and sync, and it keeps every node
  written for one instance.
- **Move**: force → velocity → position, with damping, a speed limit and bounds (none /
  wrap / bounce). Skip it and wire an LFO into position directly.
- **Locate**: position → distance gain, air absorption, delay, pan (§5).

**Ordinary patches get the same benefit:** per-voice chains by Split, unison as a nested
Population (each note born with N children), round robin, keyboard and velocity splits,
"top note gets the lead chain" by rank, all without a single biological node.

**Not in this stage:** evolution (traits only; inheritance can be added later on top of
Class without changing it), measured datasets (stage 3's audio data), the full spatial
scene and microphones (stage 4).

## 7. Performance

- 100 instances × a light chain (oscillator, envelope, filter ≈ 3 nodes) ≈ 14 M node-
  samples per second at 48 kHz — comfortable. Inactive instances are skipped.
- Sense compares every pair once per block: 100 instances = 10,000 distance checks per
  block — negligible; 1,000 = 1 M per block — still fine at block rate, and a grid can cut
  it later if ever needed. The mean field (Kuramoto) is O(N).
- Heavy nodes inside a population (reverb, long delay) multiply memory by the instance
  count. Proposal: Population shows the cost of what it plays; no hidden caps.
- Scheduling (one instance through the chain vs one node across all instances) is
  measured, not guessed (`InstanceAxis.md` §5).

## 8. Risks

1. **Vectors on the channel axis** touch the type system (§5) — isolate in its own batch,
   with rejection tests.
2. **Groups** are a feature of their own (editing in place, exposing ports, saving,
   instancing). Designed now because Class needs them; built before Class.
3. **Split's branches can sprawl.** Groups are the answer; the UI should make a branch
   collapse into one.
4. **One-block latency** of Sense is a deliberate rule; per-sample interaction is out of
   scope.
5. **The engine rewrite** (instance axis) is the foundation for everything — built beside
   the current engine and switched only when renders match.

## 9. Open questions for the plan

- Identity: all outputs on Population (visible provenance, longer cables), or a small
  **Self** node readable anywhere in the poly part? (Leaning: outputs on Population.)
- Trait distributions: which set is enough (fixed, range, normal, 1/f drift)?
- Split by "any condition": per-block decisions, or can an instance change branch while it
  lives (with a crossfade)?
- Locate's air absorption: one-pole approximation now, ISO 9613-1 curve later?

## References

- Reeves, W. T. (1983). Particle Systems — a Technique for Modeling a Class of Fuzzy
  Objects. *ACM Transactions on Graphics* 2(2), 91–108.
- Roads, C. (2001). *Microsound*. MIT Press.
- Reynolds, C. W. (1987). Flocks, herds and schools: A distributed behavioral model.
  *Computer Graphics (SIGGRAPH '87)* 21(4), 25–34.
- Vicsek, T., Czirók, A., Ben-Jacob, E., Cohen, I., Shochet, O. (1995). Novel type of phase
  transition in a system of self-driven particles. *Physical Review Letters* 75, 1226.
- Ballerini, M. et al. (2008). Interaction ruling animal collective behavior depends on
  topological rather than metric distance. *PNAS* 105(4), 1232–1237.
- Kuramoto, Y. (1984). *Chemical Oscillations, Waves, and Turbulence*. Springer.
  Overview: Strogatz, S. H. (2000). From Kuramoto to Crawford. *Physica D* 143, 1–20.
- Mirollo, R. E., Strogatz, S. H. (1990). Synchronization of pulse-coupled biological
  oscillators. *SIAM Journal on Applied Mathematics* 50(6), 1645–1662.
- Buck, J. (1988). Synchronous rhythmic flashing of fireflies. II. *Quarterly Review of
  Biology* 63(3), 265–289.
- Greenfield, M. D. (1994). Cooperation and conflict in the evolution of signal
  interactions. *Annual Review of Ecology and Systematics* 25, 97–126.
- Dolbear, A. E. (1897). The cricket as a thermometer. *The American Naturalist* 31, 970–971.
- Verhulst, P.-F. (1838). Notice sur la loi que la population suit dans son accroissement.
  Lotka, A. J. (1925), *Elements of Physical Biology*; Volterra, V. (1926).
- Voss, R. F., Clarke, J. (1975). '1/f noise' in music and speech. *Nature* 258, 317–318.
- Bennet-Clark, H. C., Bailey, W. J. (2002). Ticking of the clockwork cricket: the role of
  the escapement mechanism. *Journal of Experimental Biology* 205, 613–625.
- Remmers, J. E., Gautier, H. (1972). Neural and mechanical mechanisms of feline purring.
  *Respiration Physiology* 16, 351–361.
- Peters, G. (2002). Purring and similar vocalizations in mammals. *Mammal Review* 32(4),
  245–271.
- Gardner, T., Cecchi, G., Magnasco, M., Laje, R., Mindlin, G. B. (2001). Simple motor
  gestures for birdsongs. *Physical Review Letters* 87, 208101.
- Mindlin, G. B., Laje, R. (2005). *The Physics of Birdsong*. Springer.
- Farnell, A. (2010). *Designing Sound*. MIT Press (fire, chapter on fire).
- Chadwick, J. N., James, D. L. (2011). Animating fire with sound. *ACM Transactions on
  Graphics* 30(4).
- ISO 9613-1 (1993). Acoustics — Attenuation of sound during propagation outdoors — Part 1:
  Calculation of the absorption of sound by the atmosphere.
