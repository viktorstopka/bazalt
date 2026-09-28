# `instance.allocator`: a critical look

**Written:** 2026-09-28, in response to the user's direct questions after the domain-splitter
reachability bug was diagnosed. Grounded in the actual current source
(`InstanceAllocatorNode.h`, `VoiceManager.h`, `DomainSplitter.cpp`), the original design draft
(`archive_docs/DOMAINS.md`), and `archive_docs/decisions/0020-instance-allocator-lifetime.md` —
not from memory. Where the design doc and the shipped code disagree, that's called out explicitly,
not smoothed over.

**Verdict up front, so it doesn't get buried:** the two-boundary-node model (Instance Allocator /
Voice Mix) is the right call and I wouldn't unwind it. The "one node, four configurations" shape
of the allocator specifically is weaker than I'd have designed it, for a concrete reason (§3
below), and I think there's a real, scoped improvement available. `random1`/`random2` have a good
reason to exist as allocator ports rather than separate nodes — but the current implementation
doesn't actually deliver the property that reason depends on (§4). The poly/mono visual-indicator
question was already asked and half-answered by the original design doc, three years before this
conversation (§6) — the user's instinct to distrust "just make it a color" was correct, and for
the exact reason the doc gives.

---

## 1. What it actually does today (not what the catalog describes)

`instance.allocator` is **Voice-configuration only**. The `configuration` enum (Voice /
Swarm-population / Swarm-transient / Trigger) is a real, visible dropdown in the UI today, but
`setParameter()`'s own comment says it plainly: *"a non-zero value here doesn't switch behaviour
yet."* Three of the four options in that dropdown do nothing. This matters for the "frightens
beginners" concern below — it's not just that the node is conceptually dense, it's that a third of
its own configuration surface is currently decorative.

What actually runs: `io.noteIn` pokes this node's `noteOn()`/`noteOff()` (via a real `Note`-typed
`spawn` connection since M18); on note-on it sets `gate=true`, records `pitch`/`velocity`, bumps
`instanceIndex`, and draws two fresh random values. `PluginProcessor` compiles the whole
voice-reachable subgraph `numVoices` (8, hardcoded) times, and `VoiceManager` — a genuinely
substantial, separate piece of machinery — decides which of those 8 compiled instances is "this"
note, stealing the oldest released one if all 8 are busy (`VoiceManager::stealVoice()`), fading a
stolen voice's old content out over 256 samples rather than cutting it, and only freeing a voice
once its own rendered output has been below a threshold for a hold time (`updateSilenceAndCheckFinished`) —
not when its envelope finishes. That's real, working, and exactly what a per-voice delay or reverb
tail needs to ring out correctly instead of getting chopped.

## 2. Why not just give `io.noteIn` a poly output directly?

This is the real question underneath "why not simpler," so it's worth answering precisely rather
than just asserting the current design is fine.

`DOMAINS.md` §2 is explicit that domain crossing happens through **exactly two boundary node
types** — Instance Allocator (mono→poly) and Voice Mix (poly→mono) — and that a domain "is not a
signal type and not a channel count on a port... inferred by the compiler from graph structure, so
the user never declares it directly." If `io.noteIn` itself had a poly output, you'd be baking a
domain-boundary decision into a specific I/O node's type rather than into a dedicated node whose
only job is being that boundary — and you'd still need a separate mechanism for the other three
spawn sources DOMAINS.md describes, since Swarm-population isn't driven by notes *at all* (fixed
count, always live, no spawn stream) and Swarm-transient/Trigger are driven by `Event`, not `Note`.
A "poly-native Note In" only ever covers one of the four cases. You'd end up needing a second (and
third, and fourth) entry point anyway — each wanting its own copy of the stealing/silence-freeing/
buffer-preallocation machinery `VoiceManager` provides once, today. That's the "two node types that
grow apart" trap §3 of the design doc names directly, and I think it's a correct thing to have
avoided.

So: the *existence* of a dedicated allocator node, separate from `io.noteIn`, is justified — not
by an abstract love of modularity, but by there being four genuinely different spawn sources that
all need the *same* lifecycle bookkeeping. That bookkeeping (steal-with-fade, free-on-silence, not
free-on-envelope) is where the real complexity lives, and it has to live *somewhere* regardless of
how the graph nodes are typed.

## 3. Is "one node, four configurations" actually the most elegant shape? — I don't think so, and here's a concrete alternative

The design doc's own reasoning ("implement **one** node type with a configurable source, not two
that grow apart") is sound for the *shared machinery*. But it doesn't follow that the *user-facing
node* has to be one thing too, and I think conflating those two is the actual mistake.

Compare to `mix.downmix`, the catalog's other multi-mode node: its `mode` enum (sum / left / mid /
right / side) is a legitimate single node because **every mode has the same port shape** — two
audio inputs, one audio output, always. The enum only changes which line of math runs.

`instance.allocator`'s four configurations do **not** share a port shape:

- Voice needs a `Note`-typed `spawn` input.
- Swarm-population needs no spawn input at all — it's fixed-count and always live.
- Swarm-transient and Trigger need an `Event`-typed spawn, and (per §3's own table) Trigger's
  instance context includes a "payload" the other three don't have.

That's not "one node with a mode switch," that's four node shapes wearing one dropdown. A user in
Swarm-population mode would still see a `spawn` input port sitting there doing nothing (or the
descriptor would need to conditionally hide/change ports per configuration — itself a form of
special-casing the catalog doesn't do anywhere else today). Today this is invisible because only
Voice is real, but it's a real structural mismatch waiting to surface the moment Swarm-population
actually gets built.

**What I'd build instead:** keep one internal engine class (the lifecycle machinery — stealing,
silence-freeing, instance context generation) and expose it through **separate node types** —
`instance.voice`, `instance.swarmPopulation`, `instance.swarmTransient`, `instance.trigger` — each
with only the ports and parameters that configuration actually has. Shared implementation, honest
per-type surface. This is the same "content vs. ports vs. structural params" split
`wiki/NODES.System.md` §8 (Factories) already argues for elsewhere in the catalog — I'd be applying
an already-accepted principle here, not inventing a new one.

This also directly answers the "frightens beginners" worry: a beginner today faces one node with a
4-option dropdown (3 dead), 9 output ports, and 2 structural parameters, all at once. A beginner
who just wants MIDI polyphony would instead place `instance.voice` — a node with exactly the ports
Voice mode uses, no dead dropdown, no Swarm-only concepts staring at them before they've played a
single note.

I'd sequence this **after** the reachability fix, not instead of it — it's a real redesign
(new node types, a migration question, `DomainSplitter`'s single-allocator-per-graph limit would
need revisiting too, see §7) and shouldn't get tangled up with the correctness bug that's already
agreed.

## 4. `random1` / `random2` — the real reason, and a real gap I found while checking it

**The stated reason (`DOMAINS.md` §4) is good and specific:** "Random — a stable random value for
the instance's lifetime... Determinism: randoms derive from a patch-level seed plus the instance's
spawn ordinal. The same patch, the same MIDI, the same seed produce bit-identical output. This is
required for the offline render CLI to be a useful regression tool." An ordinary `random.*` node
free-runs at block rate — its state depends on elapsed time, not on *which note this is*. Replaying
the same MIDI file against the same patch through such a node would not reproduce the same
per-note randomization. Tying the random draw to `noteOn()` — one fresh value per spawn, held for
the instance's whole life — is the only way to get "the same 5th note always gets the same detune
amount," which is a real, useful, and not-trivially-reproducible-elsewhere property. So: no, I
don't think this is achievable more elegantly with a plain `random.*` node wired into the region —
that node type structurally can't know when a new instance started.

**But — I checked whether the shipped code actually delivers that property, and it doesn't yet.**
`InstanceAllocatorNode::prepare()`:
```cpp
random.setSeedRandomly(); // per-process-lifetime — patch-level determinism (DOMAINS.md §4)
                          // needs a real seed parameter, not built yet
```
The seed is randomized once per plugin-instance-lifetime, not derived from a real patch-level seed
+ spawn ordinal as the design doc specifies. Two consequences: (1) the exact determinism guarantee
this feature exists to provide — "same patch, same MIDI, same seed → bit-identical output" — isn't
actually true today; render-cli regression tests built against `random1`/`random2` output would not
be reproducible across runs. (2) this is a real, findable gap, not a hypothetical — worth a real
milestone item (add a `seed` structural parameter, derive per-instance state from
`hash(seed, spawnOrdinal)`) whenever this gets picked up, separate from the bigger conversation
here.

**Also worth naming:** the design doc says "several independent ones, addressable by index" —
the shipped shape is exactly two, fixed, named ports. That's a real simplification, and I don't
think it's wrong on its own (two is probably enough for most real patches — detune + pan, or
detune + brightness), but if a third is ever needed, the honest options are either a third named
port (fine, additive, no migration) or turning it into a growable port group the way `mix.sum`'s
`in.N`/`level.N` already works. I'd default to just adding `random3` when/if something actually
needs it rather than pre-building a growable group for a need that doesn't exist yet — matching
this project's own stated bias toward "ship the real primitive when something needs it," not ahead
of time.

## 5. The modularity philosophy — two genuinely different things, worth not conflating

You mentioned I'd said something about being able to "put a swarm node... into the same node" —
I want to be precise about which of two real ideas that actually was, since they're easy to
conflate and only one of them exists in any form today:

- **"One node type, several configurations"** (`DOMAINS.md` §3) — this is what actually exists
  (partially: Voice only). It's what §3 above is critiquing. This is almost certainly what I meant.
- **"Groups"** (`DOMAINS.md` §8) — a completely separate, **entirely unbuilt** idea: a named
  subgraph with its own exposed ports that behaves like an ordinary node from the outside, inlined
  and flattened at compile time (so it costs nothing at runtime), placeable inside an instanced
  region (compiles once per instance automatically) or a mono region (compiles once). This is a
  real, well-specified design — it's genuinely how most of the "nature" reference patches
  (Karplus-Strong, Bubble, Crackle, Scrape) are meant to ship — but it doesn't exist in the engine,
  the UI, or the patch format today. Nothing about it is implemented; there's no group node, no
  inlining pass, no group-authoring UI. If this is what you were picturing for "a swarm node in the
  same node," it's a much bigger, currently-zero-progress feature, not something the allocator
  already provides a taste of.

Given the actual current gap you hit (a totally different problem — reachability, not
modularity), I don't think Groups is relevant to what we're fixing right now, but it's worth
knowing it's real design, not vapor, whenever composability across the whole catalog comes up
again.

## 6. Should there be a poly/mono indicator? — this was already asked, and half-answered, in 2026

This isn't a new question. `DOMAINS.md` §11, "Open questions," item 1, verbatim:

> How is poly versus mono shown in the UI? It must not collide with the type palette, so the
> proposal is **line style or weight rather than colour**. This is still the open item from the
> node editor design.

Your instinct — "maybe not color, I don't know, but just something" — lines up exactly with what
the original design draft already concluded, for a concrete reason: this engine's port-type palette
(the six-colour Audio/Control/Event/Note/Data/Boolean scheme, plus now the teal Note fix from
Milestone 0.7) already spends color as a resource. A second, independent color axis for poly-vs-mono
would either collide with it or force a much larger palette that's harder to read at a glance.
Line weight or style (a per-voice cable rendered slightly heavier, or dashed, or double — exact
treatment undecided) doesn't compete with that axis at all.

I think this is worth building, separately from the reachability fix and separately from §3's
node-split question — it's real information (which signal, right now, is running eight of itself
vs. one) currently only discoverable by reading the graph's shape and mentally tracing reachability
from the allocator, or by hitting a compile error after the fact. It's a UI-only change (cable
rendering already reads live port-anchor positions; it would additionally need to know a given
wire's resolved domain, which — once §7's `DomainSplitter` fix lands — the compiler already
computes and could expose the same way it exposes anything else for the UI to read). Sequenced
after the reachability fix and (I'd argue) after §3's node split, not before — the visual
distinction only needs to be exactly right once the underlying domain-splitting is correct and the
node surface it's describing is honest.

## 7. Two things I found that aren't part of your question, but are real

- **`archive_docs/decisions/0020-instance-allocator-lifetime.md` still says "Status: Proposed
  (M17). Not implemented."** M17 shipped Voice mode for real, months ago by this project's own
  timeline. The ADR record was never updated to reflect that. Worth a one-line status fix
  regardless of anything else decided here.
- **That same ADR's own decision says:** *"Generalize `DomainSplitter` to identify N allocator
  regions instead of exactly one fixed node type."* The shipped `DomainSplitter.cpp` still hard-
  rejects a second `instance.allocator` ("Only one instance.allocator node is supported per graph").
  That generalization was never carried out. Not urgent on its own, but relevant background if the
  §3 node-split ever leads toward nested or parallel instanced regions (a synth with two
  independently-triggered voice groups, say) — that would need this limit lifted too, and it's
  already a documented, un-actioned decision, not a new ask.

## 8. Where I'd actually start, if asked

1. The reachability fix (already agreed, unrelated to everything above).
2. `random1`/`random2`'s real seeding (§4) — small, contained, fixes a genuine correctness gap
   against the feature's own stated purpose.
3. The ADR-0020 status/scope corrections (§7) — trivial, just accuracy.
4. The node-split (§3) — real work, real payoff (both for beginners and for Swarm/Trigger actually
   being buildable without a port-shape hack), but a genuine redesign — worth its own scoping pass,
   not a quick add-on.
5. The poly/mono cable indicator (§6) — after 4, since it's describing 4's output.

None of this is committed by writing it down — you asked for critical, not a yes-man, and I've
tried to actually disagree where I think the current shape is weaker than it should be (§3, §4)
rather than defend everything that's already built. Tell me where you disagree.

---

## Addendum (later the same day): the four configurations in detail, chord/arp readiness, the
## cricket-swarm walkthrough, and a real answer on the random question

Follow-up questions from the same conversation. Re-checked against the actual catalog and node
source rather than restated from memory.

### 9. The four configurations, one by one, with a concrete example each

Per `DOMAINS.md` §3's table. **Only Voice is built.** The other three are real, specified designs
with zero lines of engine code behind them — I want to be explicit about that before describing
them, so "detail" doesn't read as "exists."

- **Voice** (✅ built, M17-M18). Spawn source: a `Note` stream. One instance per held note, freed
  when its own rendered signal goes silent (not when its envelope finishes). This is the Init
  Patch's own shape: `io.noteIn → instance.allocator (Voice) → osc.analog ×2 → filter.ladder →
  env.adsr → instance.mix`. Every "play an instrument" patch uses this one.

- **Swarm (population)** (📋 catalog-only). Spawn source: none — a *fixed count*, all instances
  live for the whole patch's lifetime, no note or event ever creates or destroys one. Instance
  context is just index + seeded randoms (+ `Position` for spatial placement) — there's no `Pitch`/
  `Velocity`/pitch-tracking at all, because nothing is being "played." **Example** (this is
  literally `wiki/NODES.md`'s own reference patch): a field of cicadas as background ambience —
  `instance.allocator (Swarm-population, maxInstances=40)` → each instance is its own
  `clock.pulse` (jittered, so they don't chirp in lockstep) → `excite.burst` → `filter.formant` →
  `resonator.modal` (the insect's body), with `random1`/`random2` driving each instance's own pitch
  offset and `Position` panning it somewhere in the stereo field. Nothing ever triggers this — it's
  just always there, 40 independently-random instances running continuously, the moment the patch
  loads.

- **Swarm (transient)** (📋 catalog-only). Spawn source: an `Event` stream (not `Note` — no pitch/
  velocity in the context, just seeded randoms per spawn). Each event creates ONE new short-lived
  instance that runs its own lifecycle and dies. **Example**: rain — a fast, irregular `Event`
  generator (`clock.pulse` with jitter, or `noise.dust` thresholded into events) feeds
  `instance.allocator (Swarm-transient)`; each event spawns one droplet (`excite.burst →
  resonator.modal`, tiny, damped, over almost immediately), with `random1` picking that specific
  drop's pitch/size and `random2` its stereo position. A thousand of these overlapping is what
  reads as "rain" rather than "a repeating single sample." This is the *inverse* of Swarm-
  population: population is a fixed cast that never changes; transient is arbitrarily many
  short-lived instances, bounded by `maxInstances` (with the same "what happens when spawn rate
  exceeds the cap" question `DOMAINS.md` §11 item 6 leaves explicitly open — drop, steal, or
  throttle density isn't decided yet).

- **Trigger** (📋 catalog-only). Spawn source: an `Event` stream, but **one instance at a time** —
  a new trigger doesn't add an instance alongside the existing one, it's the single-active-instance
  case (closer to old-school monophonic retrigger than to Swarm-transient's "let them all pile up").
  Instance context includes a "payload" the other three configurations don't have (§3's table names
  it without pinning down its exact shape yet). **Example**: a percussive one-shot sampler slot —
  every incoming event (a drum trigger, a sequencer step) restarts the SAME instance's envelope/
  playback position rather than layering a new overlapping copy, useful for exactly the cases where
  overlap would sound wrong (a kick drum retriggering faster than its own decay).

Read side by side, the pattern is: Voice and Trigger are both "one instance responds to one event
source," differing in whether concurrent instances are allowed to overlap; Swarm-population and
Swarm-transient are both "many instances, no per-instance triggering event," differing in whether
the population is fixed or constantly being born/dying. That's a real, coherent 2×2, which is
exactly the kind of shared-structure argument that makes "one underlying engine" (§2/§3 above)
reasonable — but it's also four genuinely different port/parameter surfaces, which is exactly my
§3 argument for four node types sharing that one engine rather than one node with a mode switch.

### 10. Is this ready for a node-built chord or arpeggiator? — No, and here's exactly what's missing

Checked `wiki/NODES.md`'s `note.*`/`clock.*` families directly. Every single node either idea would
need is catalog-only:

- `note.hold` 📋 — remembers currently-held notes (what an arpeggiator cycles through)
- `note.select` 📋 — picks one from a set, driven by a clock/index
- `note.chord` 📋 — one note in, several out, harmonized against a `data.scale`
- `note.assemble` 📋 — the general Event+Control→Note assembler
- `clock.counter` 📋 — the sequencer engine underneath both patterns
- `clock.pulse`/`clock.divide` 📋 — tempo-synced timing

The catalog's own reference-patch table already sketches both, unbuilt:
- **Arpeggiator**: `note.hold → clock.pulse → clock.counter → note.select`
- **Chord**: `note.chord with data.scale`

Both would feed their output `Note` stream into `instance.allocator (Voice)`'s `spawn` exactly like
`io.noteIn` does today — the allocator itself doesn't need anything new to support this, it's
already just "whatever produces `Note` events." So the honest answer is: the allocator is ready;
the note-generation layer in front of it isn't built at all. This is real, scoped, and it's already
been planned for — not a surprise gap, just an unbuilt one.

### 11. The cricket-swarm case, specifically

This is Swarm-population almost exactly as described in §9 above — a fixed cast of always-live
insects, each independently randomized, no MIDI or event stream driving any of them. The one thing
worth being precise about: **you don't need "a swarm of the same sound"** to be the same *node
instance* repeated — each of the `maxInstances` compiled copies runs its own copy of whatever chain
you build inside the instanced region (in the cricket case: `clock.pulse (jittered) → excite.burst
→ resonator.modal`), so "40 crickets" really is 40 independently-running, independently-randomized
copies of one patch fragment, not one cricket sound played 40 times. That's the actual payoff of
the instanced-region model versus, say, hand-placing 40 separate un-linked node chains: one edit to
the shared chain (make the body resonator brighter) changes all 40 at once, while `random1`/
`random2`/`Position` still give each one its own identity. Concretely useful, and — per §3 above —
this is the use case where I think a dedicated `instance.swarmPopulation` node type, with only the
ports Swarm-population actually needs (no `spawn` input sitting there unused, since nothing ever
triggers this configuration), would read far more clearly than the current single dropdown does.

### 12. The random question — you're not wrong, and I can now say exactly why

Checked `adapt.sampleHold`, `random.drift`, and `random.stepped` directly rather than reasoning
about them abstractly, because this is a genuinely checkable question, not a matter of opinion.

**The mechanism you proposed is real and already buildable today**: `random.drift` (or
`random.stepped`) → `adapt.sampleHold`'s `in`, with `instance.allocator`'s `start` `Event` output
→ `adapt.sampleHold`'s `trigger`. `adapt.sampleHold` genuinely does exactly what you'd want — it
samples `in` on every `trigger` event and holds that value until the next one. Wired downstream of
the allocator, it compiles once per voice, each with its own independent hold state. This is not a
hypothetical — I read the actual node and it does this.

**But there's a real gap, and I can point at exactly where it lives.** `random.drift`'s and
`random.stepped`'s `seed` is a **structural parameter**, not a port — meaning it's a fixed value
baked into the node at patch-authoring time, identical across every one of the 8 compiled per-voice
copies. Two voices with the same seeded `random.drift` run the *exact same deterministic sequence*.
They only end up reading different current values because a voice's internal state only advances
while that voice is *active* (`PluginProcessor::renderVoiceRange` skips idle voices entirely) — so
two notes played at different times are at different *positions* within the same sequence, which
in practice looks different enough. But two notes triggered **in the same block** — a chord,
exactly the thing you asked about two questions earlier — start reading that identical sequence
from the identical position, in lockstep, and stay correlated for as long as both stay active. Not
"different but similar" — literally the same value, every sample, until one of them is released or
stolen. For a chord where you want each note independently detuned or panned, that's a real,
audible failure of the "just use nodes" approach, not a nitpick.

The allocator's own `random1`/`random2` don't have this problem because each is drawn from a
`noteOn()`-time C++ call (`random.nextFloat()`), a genuinely independent draw per spawn, regardless
of what any other voice is doing in the same block. So: no, you're not wrong to feel like the
node-based version is a workaround — it structurally can't reach the same guarantee, specifically
for simultaneous spawns, and now there's a concrete, verified reason why, not just a feeling.

**What would actually close the gap, if it mattered enough to build:** a random node whose seed (or
an internal offset) is derived from `instanceIndex` — e.g. `random.drift` gaining an optional
`seed` *input* (Control, integer) wired from `instance.allocator.instanceIndex`, so voice N reseeds
from `hash(baseSeed, N)` instead of every voice sharing one literal seed. That would make the
node-built version fully correct, including for chords, without needing the allocator to bake in
`random1`/`random2` at all. I'm not recommending that as a real milestone item right now — it's a
bigger change than fixing `random1`/`random2`'s own seeding gap (§4 above), which is smaller and
fixes the thing that's actually shipped — but it's the honest answer to "could this be more
elegant": yes, if random nodes could be instance-seeded, and that's a real, buildable idea, just
not a small one.

### 13. Making the node list more dimensional

**Category grouping already exists** — `AddMenu.tsx` already groups every entry by
`NodeDescriptor.category` (a flat, alphabetically-sorted list of category groups, each showing its
members) — `instance.allocator` and `instance.mix` both currently live under "Domain." So some of
what you're picturing is already there; it's worth knowing that before assuming a big UI project is
needed. What ISN'T there: any grouping *within* a category — under §3's split, `instance.voice`/
`instance.swarmPopulation`/`instance.swarmTransient`/`instance.trigger`/`instance.mix` would all
land as five flat entries under "Domain," not a nested "Domain → Allocate → Voice/Swarm/..." tree.
Getting genuine two-level nesting is a real, separate UI feature (`AddMenu.tsx`'s `byCategory` map
would need to become a tree, plus rendering for a second level) — small, but not free, and I'd
sequence it after §3's node split actually exists rather than before (nesting five things that
don't exist yet doesn't buy anything).

On the **id** question specifically (`instance.allocate.voice` vs. `instance.voice`): every node
type id in the catalog today is exactly two dotted segments — `category.name` (`osc.analog`,
`filter.svf`, `mix.gain`, `instance.allocator`, `instance.mix`). A three-segment id would be a new
convention, not an extension of an existing one — worth deciding deliberately rather than by
default. I'd lean toward keeping it two-segment (`instance.voice`, `instance.swarmPopulation`,
`instance.swarmTransient`, `instance.trigger`, `instance.mix` unchanged) and letting the Add menu's
category ("Domain") do the grouping work you're picturing, rather than encoding the hierarchy into
the id itself — matching how every other multi-node family in the catalog already reads
(`filter.svf`/`filter.ladder`/`filter.onepole` don't share a `filter.iir.*`-style third segment
either, and nobody's found that confusing). But this one's a genuine style call, not something I
can verify against the code the way the rest of this addendum is — your instinct here is as valid
as mine.
