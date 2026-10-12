# Life Cycle — Population, Split, Groups, Class, Sense and Move

**Status:** Proposed, 2026-10-12 — draft for review. Shaped in conversation with the user
the same day; the research behind it is `wiki/reports/LifeAndBiology_2026-10-12.md`. No
code exists yet. Roadmap stage 2 (`wiki/ROADMAP.md`); the engine half is
`InstanceAxis.md`, which this plan builds on and does not repeat.

---

## 0. Origin

Stage 2 asks for a biological layer — instances that live, sense and move, grounded in real
models — and for the life-cycle mechanism, scattered across four allocators and an
eight-copies-of-the-patch engine, to become robust enough for ordinary patches too
(different chains per voice by order, pitch or anything). Both are the same work: one
modular vocabulary, built on one instance axis.

## 1. Decisions (from the conversation)

**Stochastic suite (second conversation, 2026-10-12).** The ChatGPT brief on the
Stochastic and Natural Suite (recorded with the roadmap on 2026-10-08) is the other half
of stage 2. Decisions: names say what a thing does when that is possible and use the
proper scientific name when a plain one would be forced (Wander, Events; Lorenz, Rössler,
Duffing, Logistic Map); every process is also a sound; candidates get built rather than
compared in listening tests, and the ones that don't earn their place are removed later;
`random.drift` stays. **Built first, because they need no instance axis:** Wander (OU),
Brownian, Fractal (1/f), Events (renewal + Hawkes), Lorenz, Rössler, Duffing, Logistic
Map — see batch 2s. **Later:** Distribution (Data, drawn in the Factory window; feeds
Class traits and Events), Markov (a sequence with a grammar — birdsong syntax), and a
**Shared** amount on random nodes inside a Population (0 every instance its own, 1 all
the same — correlated populations).


- **D1 — Three layers, biology never makes sound.** Life (who exists), Identity (what each
  one is), Environment (what they share and sense). Sound stays ordinary DSP; there is no
  Cricket, Purr or Bird node — those are example patches.
- **D2 — One Population replaces Voice, Trigger and both Swarms.** Born on: a note / an
  event / a rate / always. Lives: while held (then until silent) / a fixed time / forever.
  When full: steal oldest / steal quietest / ignore. A size, a seed (fixed, or new each
  run), an optional Class. The old four are Add-menu presets. A MIDI note is one way to be
  born — the special note path goes.
- **D3 — Identity is signals.** Gate, index, birth order, pitch, velocity, rank, age, alive
  count, seed-random — outputs of the Population that births the instance (nested
  Populations each give their own). Any knob can follow any of them.
- **D4 — Split / Merge.** Split sends each instance down one of N branches by a rule
  (pitch, velocity, birth order mod N, rank, a condition signal); a branch runs only for
  its instances. Merge reduces the instance axis, as today.
- **D5 — Group.** A named set of nodes with its own inputs and outputs, opened in place
  (like the Factory window). The one container concept — also what a Class's body is and
  what keeps Split's branches readable.
- **D6 — Class + Trait.** A Class (Data) holds named traits, each a distribution, and a
  body (a Group). A Population born from a Class samples every instance's traits from its
  seed; Trait reads one by name as a per-instance signal. Not biological by nature.
- **D7 — Interaction through Sense, one block late.** Sense gives each instance a summary
  of the others (radius or k-nearest, default 7; centre direction and distance, nearest
  distance, count, average of a wired signal, phase-correct averages, "a neighbour
  fired", and the population-wide mean). Instances never reach into each other directly.
- **D8 — Move and Locate.** Move integrates force → velocity → position (damping, speed
  limit, bounds). Locate turns position into distance gain, air absorption, delay and pan
  for a listener at the origin. The full spatial scene stays in stage 4.
- **D9 — The input is global, and local on request.** Everyone hears it; Locate-style
  attenuation by distance makes it local.
- **D10 — Traits, not evolution.** Inheritance can be added later on top of Class.
- **D11 — Determinism is a setting** on the Population's seed.

## 2. Batches

| Batch | What | Done means |
|---|---|---|
| **2s — Stochastic suite, first nodes** | Wander, Brownian, Fractal, Events, Lorenz, Rössler, Duffing, Logistic Map. **Built 2026-10-12.** | Each measured for its statistics: OU memory and spread, √t spreading, 1/f scaling exponent, Poisson / clock / bursty interval CV, Hawkes multiplicity, refractory gap, period doubling (`tests/StochasticTests.cpp`). |
| **2a — Instance axis** | `InstanceAxis.md`: one plan, nodes keep per-instance state, mono runs once, Merge is a step. Built beside the current engine. | Every existing render test matches through both engines; then switch. |
| **2b — Population, Split, Merge** | D2–D4. Remove Voice / Trigger / Swarm nodes (no migrations — factory patches rebuilt). Unison as a nested Population. | A keyboard split, a round robin, "top note → lead chain" and a 100-instance grain cloud built from these alone. Swarm (Transient)'s lost-release bug gone by construction. |
| **2c — Groups, Class, Trait** | D5–D6. Groups open in place, expose ports, save to a library. | A "Cricket" Class with a body, born 50 times with different traits. |
| **2d — Vectors, Sense, Move, Locate** | Report §5's vector-on-the-channel-axis decision first, then D7–D8. | Boids from Sense + math + Move; Kuramoto-style sync from Sense's mean phase; a moving source heard moving. |
| **2e — Sound models** | Jittered impulse train; driven nonlinear oscillator. Example patches: cricket field (Dolbear temperature), purr, bird (pressure/tension gestures), fire from the guitar. | Each patch in `ui/src/patches/factory/`, loaded by the factory-patch test. |

## 3. Risks

See the report §8: vectors in the type system, Groups as their own feature, Split
sprawl, one-block latency, and the engine rewrite underneath it all.

## 4. Open questions

- Identity outputs on Population, or a small Self node? (Leaning: on Population.)
- Which trait distributions are enough?
- Can an instance change Split branch while it lives?
- Locate's air absorption: one-pole now, ISO 9613-1 later?
- Names: Population, Split, Merge, Group, Class, Trait, Sense, Move, Locate — confirmed for
  Population; the rest to review.
