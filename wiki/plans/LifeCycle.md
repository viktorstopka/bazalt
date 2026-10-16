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
| **2a — Instance axis** | `InstanceAxis.md`: one plan, nodes keep per-instance state, mono runs once, Merge is a step. Built beside the current engine. **Built 2026-10-12** (`InstanceAxis.md` §6): matched bit for bit, then the per-voice engine was removed. | Every existing render test matches through both engines; then switch. |
| **2b — Population, Split, Merge** | D2–D4. Remove Voice / Trigger / Swarm nodes (no migrations — factory patches rebuilt). Unison as a nested Population. | A keyboard split, a round robin, "top note → lead chain" and a 100-instance grain cloud built from these alone. Swarm (Transient)'s lost-release bug gone by construction. |
| **2c — Groups, Class, Trait** | D5–D6. Groups open in place, expose ports, save to a library. | A "Cricket" Class with a body, born 50 times with different traits. |
| **2d — Vectors, Sense, Move, Locate** | Report §5's vector-on-the-channel-axis decision first, then D7–D8. | Boids from Sense + math + Move; Kuramoto-style sync from Sense's mean phase; a moving source heard moving. |
| **2e — Sound models** | Jittered impulse train; driven nonlinear oscillator; **Gesture** — one Factory curve document with several named lanes on one timeline (an owl's loudness, pitch and quaver; a bird's pressure and tension), one output per lane, one Time Scale (agreed 2026-10-12: the Owl patch needed four Envelopes kept in step by hand). Example patches: cricket field (Dolbear temperature), purr, bird (pressure/tension gestures), fire from the guitar. | Each patch in `ui/src/patches/factory/`, loaded by the factory-patch test. |

## 2b in detail (2026-10-16)

The order of work moved stage 3a/3b in between 2b and 2c (`wiki/ROADMAP.md`); 2b is next.

**2b.1 — Counts and caps (engine). Built 2026-10-16.**
- An allocator's instance count is its own setting (Population's **Size**; the legacy
  nodes' Max Instances / Population Size until they go), read at compile time off a
  scratch node with the patch's parameters (`GraphEditController::publishPlan`) — the
  plan holds exactly that many copies, no longer a fixed 8. Trigger is 1. Cap 128 per
  allocator (`maxInstancesPerGroup`, `InstanceGroups.h`); the real bound is CPU and memory
  (every per-instance node exists Size times — a 2 s delay inside a 128-voice Population
  is 128 delay lines).
- VoiceManager is preallocated to the cap in `prepareToPlay` and narrowed by
  `setMaxActiveVoices`, so nothing is resized under the audio thread. Every allocation is
  also bounded by the count of the plan running right now (`noteOn (id, limit)`): a plan
  swap and its new ceiling are not one atomic step. Instances past a smaller new count
  are freed.
- Allocators per patch: 4 → 16. The per-allocator Merge limit is gone (each Merge is its
  own reduce). A reduce takes 16 instances (32 channels in stereo, the per-node port
  limit); a larger group reduces through a tree — leaves (`life.reduce.first`) summed by a
  combine (`life.reduce.combine`), which divides in Average mode.
- The dead per-voice-era fields on `ExecutionPlan` (`externalInputNodeIds`,
  `sumOutputs`, its own `maxOrigins`) are gone.

**2b.2 — Population (`life.population`). Built 2026-10-16** (`LifePopulationNode.h`,
`tests/PopulationTests.cpp`). As built: births from inside the patch go through a **birth copy**
(`pop@birth`, added by the instance expansion) that hears the inputs and, at the exact sample of a
birth, asks the driver for a slot (`InstanceBirthSink`, implemented by the processor on its
VoiceManager); the member starts at that sample in the same block (a hidden `births → afterBirths`
cable orders the birth copy first). This replaces "instance 0 listens and the processor relays one
block late" for Population. MIDI still pokes each member's own note-in copy, as Voice did. Born
**at a rate** was left out of the node: an Events (or Clock) node wired into Born is the rate, with
Regularity and Clustering for free. "Until silent" is Duration near 0. The plan below is the
proposal it was built from.
- **Born:** on a note (Note input) / on an event (Event input) / at a rate (Rate, Hz, with
  Regularity — the Events node's renewal process built in) / always (all alive from the
  start).
- **Lives:** while held, then until silent / a fixed time (Duration) / until silent /
  forever. "Until silent" is today's 200 ms silence detection; the explicit choices are
  what the Owl's breath hack and Fire's never-silent floor needed (`wiki/TODO.md`).
- **When full:** steal oldest / steal quietest / ignore the new one.
- **Size**, **Seed** (fixed, or new each load).
- **Identity outputs:** gate, pitch, velocity, index (which instance), order (birth
  ordinal), age, alive (how many live now), random1 / random2, position, start / stop.
- MIDI is just "born on a note": the Note input takes `io.noteIn` as today.

**2b.3 — Split (`life.split`). Built 2026-10-16** (`LifeSplitNode.h`). As built: four
fixed branch outputs (`branch.0`–`branch.3`, `in` passed through with its quantity) and
`branch` (index); rules **Ranges** (At 1–3), **Modulo**, **Rank** (highest key first;
lowest = a negated key); **Key** defaults to `in`; **Decide** at birth or always (read at the
end of the block, so a member born mid-block is sorted by its own key). The layout marks
every node only one branch reaches (`InstanceLayout::branchesOfNode`); the plan gates those
steps on their instance's Split copy (`BranchGate`) and zeroes their outputs when skipped.
Rank at birth compares with members born earlier only (`InstanceGroupRuntime::birthOrder`),
so it doesn't depend on step order. Kept nodes get their runtime / key-table pointers on the
audio thread at the new plan's first block (`ExecutionPlan::pendingBindings`) — a kept node
may still be running in the old plan. Velocity layers and conditions are Ranges on another
Key. The proposal it was built from: One instance stream in, N branches out; each instance
belongs to exactly one branch by a rule — pitch ranges, velocity ranges, birth order
mod N, rank (highest / lowest note), or a per-instance condition signal. A branch's nodes
run only for its instances: the plan tags those steps with the branch, and an instance's
run flag is set per branch (no CPU for instances that aren't in it). Merge after the
branches sums them back.

**2b.4 — Removal. Built 2026-10-16.** Voice, Trigger, Swarm (Transient) and Swarm
(Population) are gone, along with `InstanceOriginNode` and the processor's "instance 0
listens, relay a block late" path; the Add menu offers Voice, Trigger, Swarm (Transient),
Swarm (Population) and Grain Cloud (Events → Population) as presets
(`ui/src/graph/presets.ts`). Texas Windchimes and the user's AppData patches were rebuilt
on Population. A held member nothing can release any more (its note source unwired) is
released at the new plan's first block. The proposal: Voice, Trigger, Swarm (Transient) and Swarm (Population) go; each
becomes an Add-menu preset of Population (no migrations — user's instruction; factory
patches and the user's AppData patches rebuilt). Docs. After 2b, "never rename ids"
(CLAUDE.md rule 3) is proposed for re-enabling — saved patches are now a real workflow.

**2b.5 — Nested Populations (unison).** A Population inside a Population's poly part
expands as a second axis (`osc@3@2`). Last, because it touches expansion and the
runtime's instance bookkeeping most; may slip to 2c if 2b.1–2b.4 grow.

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
