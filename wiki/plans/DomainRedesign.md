# Domain redesign — mono/poly as a port-level type, not a graph partition

**Status:** Proposed, mid-discussion, NOT approved for build. Written 2026-09-29,
live during the conversation that's designing it — treat this file as the
running record of that discussion, not a finished spec. Sections below are
marked as **Settled** (the user and Claude converged) or **Open** (still being
argued) so a later read of this file doesn't have to guess which parts are
decided. No milestone number assigned yet.

---

## 0. Origin — why this exists

Direct, heated, and correct user pushback on the M17 domain-splitting design
(`DomainSplitter`/`hasGlobalDomain`/reachability-based mono-vs-poly
inference). Verbatim, because it's the actual bar this plan has to clear:

> "The domain system is still extremely problematic and flat out broken...
> I want you to make this system again. Completely. Even rethink the whole
> concept. No sunken cost bias here, because it is TERRIBLE and just
> straight up useless."

And the concrete repro that started it: `instance.allocate.voice → env.adsr`
(now poly), separately `osc.analog → mix.gain` (both still mono, unconnected
to the voice region) — wiring `env.adsr → mix.gain` directly is **rejected**,
but only until `mix.gain` is first wired to `io.output` and *that* connection
happens to fold `mix.gain` into the poly region — at which point the exact
same `env.adsr → mix.gain` edge is accepted. Same two nodes, same edge,
order-dependent accept/reject. That's the concrete proof the current model
is broken, not just unpolished: whether an edge is legal depends on unrelated
wiring elsewhere in the graph, evaluated by whole-graph reachability recomputed
on every edit, not by anything local to the edge itself.

The user, separately, self-identified they can't hand over a finished design
("I have limited understanding of the engine and honestly I have no solution
I would be sure about") but pushed for a code-first framing: *what would the
compiler need to do, and what does that imply for nodes* — and arrived at a
concrete proposal on their own, independently, before Claude suggested
anything: **make mono/poly a real type**, global `T` vs. poly `T[]`, with
`T[] → T` being the already-existing `instance.mix` reduction and `T → T[]`
being free broadcast (exactly how `Channels::Mono → Channels::Stereo` already
works for free today).

---

## 1. Why the current model actually fails (root cause, not symptom)

**Settled.** Read `DomainSplitter.cpp` in full to ground this, not guessed at.

Two independent problems, both real, both present in the repro above:

1. **Domain is inferred from whole-graph reachability, recomputed on every
   edit, rather than being a stable, local property of a port/edge.**
   Without an `instance.mix` node present, the split is genuinely crude:
   *"anything not voice-reachable is unconditionally independentGlobal"* —
   no folding, no nuance. `mix.gain` is "global" purely because nothing
   voice-reachable happens to feed it *yet*. The moment something does
   (wiring it to `io.output`, which happens to be voice-reachable via the
   other branch), its classification silently flips. This is the direct
   mechanism behind the order-dependent bug above, and it's structural, not
   a missed edge case — the no-`instance.mix` code path is simply a cruder,
   earlier-written algorithm than the `instance.mix`-present path (which
   *does* have a "fold unclassified components" pass), and the two
   disagreeing is exactly what a user hits by accident.
2. **Exactly one `instance.allocate.voice` and one `instance.mix` per graph,
   full stop** — hard-rejected otherwise, with the rejection message
   admitting the reason: *"ExecutionPlan needs multi-output support first."*
   This is what makes "a seq-driven main voice and a separately-triggered
   sub-oscillator, each with its own sequencing" structurally impossible
   today — not hard, not unsupported-but-doable-by-hand, actually rejected
   at compile time.

These two are related but not the same bug, and the redesign below needs to
kill both, or it isn't done.

---

## 2. The proposed model: Multiplicity as a port-level dimension, parallel to Channels

**Settled as the direction.** Concrete mechanics still **Open** in places
(marked below).

The key move: this project has **already shipped exactly this pattern once**,
for a different axis. `Channels` (`Channels.h`... actually `PortDescriptor.h`,
`Channels::Mono`/`Stereo`/`Inherited`) already sits as an *orthogonal* tag on
an `Audio` port, independent of `SignalType`/`Quantity`. `canConnect` already
treats `Mono → Stereo` as free broadcast and `Stereo → Mono` as
`NeedsAdapters` via `mix.downmix` — a real, visible, explicit reducer node,
never a hidden coercion. That is *precisely* the shape of the user's `T`/`T[]`
proposal. Naming it plainly: **do to voice-multiplicity what was already done
to channel-multiplicity.**

### 2.1 The new dimension: `Multiplicity`

Add a field to `PortDescriptor` (and propagate it through `GraphCompiler`
resolution, the same way `Channels` already is):

```cpp
enum class Multiplicity
{
    Scalar,   // one value/stream — today's only reality
    Poly      // one value/stream PER ACTIVE VOICE LANE, from a specific origin
};
```

Critically — **Open, and load-bearing** — `Poly` can't just be a bare tag.
Two `Poly` ports from *different* `instance.allocate.voice` nodes (once
multiple are allowed, see §4) have no natural pairing: voice lane 3 of
allocator A has nothing to do with voice lane 3 of allocator B, and they
won't even generally have the same number of currently-active notes. So
`Poly` needs to carry **which allocator it originated from**:

```cpp
struct MultiplicityInfo
{
    Multiplicity kind = Multiplicity::Scalar;
    juce::String originId; // the instance.allocate.voice node's id; empty iff Scalar
};
```

A binary/N-ary node resolving its own multiplicity from its inputs (see §2.3)
must require that every `Poly`-tagged input among them share the *same*
`originId` — mismatched origins is a compile-time `Reject` (*"these two poly
signals come from different voice allocators and can't be combined
directly — reduce one to scalar first"*), never a silent/arbitrary pairing.
This is what makes "multiple independent voice regions" fall out for free
instead of needing its own bookkeeping (§4).

### 2.2 The two boundary nodes, redefined in terms of Multiplicity

- **`instance.allocate.voice`** becomes the (only) node whose *outputs* are
  `Poly` with `originId = its own node id`, produced from `Scalar` inputs
  (a `Note`-typed spawn trigger). It's a `Scalar → Poly` **producer** — the
  one deliberate place multiplicity is introduced, exactly mirroring how
  it's the one place `Note`-typed triggering happens today.
- **`instance.mix`** becomes the (only) node whose job is `Poly → Scalar`
  **reduction** (sum/average across the origin's active lanes) — this is
  *already* almost exactly what it does today (`NODE_EDITOR.md`/
  `InstanceMixNode.h`'s per-voice sum). Under this model it's not a special
  "domain boundary" node anymore, just an ordinary node whose declared
  input is `Poly` and whose declared output is `Scalar` — the same kind of
  ordinary, visible, mechanical adapter `mix.downmix` already is for
  `Channels`.

Every other node in the catalog needs **no changes to its own logic at
all** — it just resolves its multiplicity from what's wired to it (§2.3),
the same way `util.reroute`/`logic.select` already resolve `SignalType`/
`Quantity` from what's wired to them today (`PortPolymorphism`,
`InheritingPortsNode.h`). This is the strongest argument this isn't
hand-wavy: **the compiler already runs a "propagate a property along edges
to a fixed point" pass for exactly this shape of problem** — extending it to
one more dimension is not new machinery.

### 2.3 Resolution rule (per node, per compile)

For an ordinary node (not the allocator or the mix reducer):

- All `Scalar` inputs → node resolves `Scalar`. Runs once. Exactly today's
  behavior for anything outside the voice region.
- Any `Poly(originId=X)` input, and every other `Poly` input among them
  also `originId=X` (or `Scalar`, which broadcasts) → node resolves
  `Poly(originId=X)`. Needs `N` independent state slots (see §6).
- Two or more `Poly` inputs with *different* `originId`s → `Reject` at
  compile time (§2.1).

This is a straightforward extension of the fixed-point iteration
`GraphCompiler` already does for `PortPolymorphism` — same pass, one more
field resolved alongside `SignalType`/`Quantity`.

### 2.4 Correction, prompted directly by a sharp catch mid-discussion: this is NOT symmetric to `Channels` the way §2/§5.3 first implied

`Channels` is a **static per-port declaration** — a port genuinely *is*
`Mono` or `Stereo`, fixed at descriptor-authoring time, which is exactly why
`Mono → Stereo` and `Stereo → Mono` are two different, asymmetric
operations (one free broadcast, one lossy reduce via `mix.downmix`) and why
*direction* — which side is source, which is destination — is meaningful.

`Multiplicity`, as designed in §2.1-§2.3, is **not** that. An ordinary
node's ports don't declare a fixed `Multiplicity` at all — it's resolved
per compile from whatever's actually wired, exactly like `PortPolymorphism`
already resolves `SignalType`/`Quantity` today. There is no such thing as
"plugging a `Poly` signal into a `Scalar` port," because an ordinary port
doesn't have one to plug into.

Concretely, for a 2-input node like ring mod: feeding one input a
`Poly`-sourced signal and the other a `Scalar`-sourced one is **one case,
not two**, and it's direction-agnostic — it doesn't matter which of the two
physical input ports happens to carry the `Poly` source. The result is
always the same: the node resolves `Poly` overall, and the `Scalar`-sourced
input broadcasts into every one of the N slots. Which literal port gets
which source changes what the node's *math* does with each value; it
doesn't change whether resolution happens or which "direction" it goes,
because there is no direction to go in.

The real, Channels-style asymmetry only exists at exactly two places, and
it's there *because* those two nodes fix their multiplicity by declaration
rather than resolving it dynamically:

- **`instance.mix`'s input** is the one port in the whole catalog that
  should be **required** to be `Poly` — feeding it an already-`Scalar`
  source is meaningless. Open question (§8): hard `Reject`, matching this
  project's "don't silently do something arbitrary" posture, vs. a
  harmless passthrough no-op. Leaning `Reject`.
- **`instance.allocate.voice`'s relevant outputs** are the one place that
  **always** produce `Poly`, unconditionally, by construction — nothing
  resolves this dynamically; it's fixed by what kind of node this is,
  exactly the way `io.output`'s `hidden` flag is a fixed property of that
  one node rather than something resolved per compile.

So: the asymmetry your instinct is reaching for is real — it's just
confined to those two boundary nodes, not a general property of "`Poly`
ports" vs "`Scalar` ports" the way it is for Channels. Most nodes, ring mod
included, have neither.

---

## 3. Per-voice-varying properties (the user's own self-identified gap)

**Settled**, and it resolves cleanly under this model.

The concern: a *parameter* (knob-set value, `NodeInstance::parameters`) is
inherently scalar — the same literal float baked into every voice's own
compiled copy of a node, by construction (`GraphCompiler` compiles the same
`NodeInstance::parameters` map into every lane today). Fine for "reverb
size," wrong for "each voice should get an independently-different amount."

The fix is exactly the user's own instinct ("a Cycle node… feed it the
according prop for each voice"), and it isn't a bolt-on — it already exists
in miniature: `instance.allocate.voice` **already** outputs genuinely
per-lane-different values today (`instanceIndex`, `random1`, `random2`).
Under this model that's just "a node that produces `Poly`-typed *Control*
outputs, not only the `Note`-shaped gate/pitch ones." Any node wanting to
originate real per-voice variation — a per-voice random seed, a per-voice
index-driven detune — is free to expose a `Poly(originId=X)` Control output,
and it composes with an ordinary node's `Scalar`-vs-`Poly` port (e.g.
distortion's "amount") through the exact same resolution rule in §2.3: wire
that `Poly` output into a normally-scalar-fed parameter-port, and *that
node* resolves `Poly`, giving each of its `N` state slots a different value
for free. No separate mechanism needed — it's the general rule, not a
special case for randomness.

---

## 4. What this buys: the Mix-node question, and multiple independent voice regions

**Settled** — this directly answers "what about the Mix node you knob."

`instance.mix` already **is**, in spirit, almost exactly the `T[] → T`
reduce the user described (§2.2) — that part of the proposal isn't a new
idea, it's recognizing what the node already does and reframing its type.
What it *doesn't* do today is compose more than once: two `instance.mix`
nodes are a hard reject, and the rejection message says why —
`ExecutionPlan::externalInputNodeId` is a single field, one slot, and
nothing downstream (`PluginProcessor::finalizeInstanceMixIntoOutput`) has a
second one.

Once `Multiplicity` carries an `originId` (§2.1) rather than domain being a
single whole-graph partition, **"multiple independent voice regions" stops
being a feature that needs its own support** — it's just what the type
system already does when two different allocators exist: each produces its
own `Poly(originId=X)`, nothing forces them to pair, and each gets reduced
by its own `instance.mix` independently, whenever the graph actually wires
one. The seq-driven main voice and the separately-triggered sub-oscillator
the user actually wants become two ordinary, unremarkable allocator/mix
pairs — no special multi-region bookkeeping, no new node types, no
`DomainSplitter`-style whole-graph pass at all in the steady state. This is
the strongest practical win of the redesign, not a side effect of it.

(Mechanically this still needs `ExecutionPlan` to accept more than one
external-input slot — a map instead of one field — but that's a small,
contained plumbing change, not a redesign in itself; see §6.)

---

## 5. New question this session: unison, granular, and "operating on the actual wave"

**Open — this is today's live question, not yet fully settled, but a strong
recommendation has emerged.**

The user's question, precisely: if `Poly` becomes a first-class port
dimension, and a unison oscillator or granular sampler is *also* internally
"many simultaneous copies of a wave" (and, combined with stereo, potentially
"many copies × 2 channels"), does that multiplicity need to be exposed the
same way voice-multiplicity is? And if so, what does a node that
fundamentally operates *on the waveform itself* — ring modulation being the
sharpest example, since it multiplies two audio-rate signals sample-by-sample
— do when one or both sides are `Poly`? Their own candidate fixes: two
outputs on the oscillator, or moving `instance.allocate.voice` to *after*
the oscillator, or splitting "MIDI management" into its own node.

### 5.1 The key distinction: two genuinely different kinds of "many"

Voice-multiplicity (`instance.allocate.voice`'s `Poly`) and unison/granular
multiplicity are not the same shape of problem, and treating them as the
same graph-level dimension would be a mistake:

| | Voice multiplicity | Unison / granular multiplicity |
|---|---|---|
| Cardinality | Fixed, `numVoices` (8) | User/patch-configurable (unison layers), or dozens and constantly changing (grains) |
| Lifecycle | Tied to note-on/off, sparse (most lanes silent most of the time) | Static per patch (unison) or continuous spawn/death unrelated to note events (grains) |
| Intended downstream treatment | Stays independent through the **whole** per-voice chain (filter, envelope, effects) — only reduced once, at `instance.mix`, right before leaving the poly region | Summed **immediately**, inside the same voice, before the signal is even one "voice's" Audio output — a unison oscillator's whole point is to hand downstream nodes one blended wave, not N wireable ones |

That last row is the actual answer: unison/granular multiplicity should
**not** become a graph-visible `Poly` dimension at all. It should stay fully
internal to the node that creates it — `osc.unison` (or whatever it's
called) runs N detuned oscillators internally and sums them to **one**
`Audio` output, `Channels::Mono` or `Stereo` as appropriate, exactly the same
external shape `osc.analog` already has. `sampler.granular` manages its own
grain cloud internally and sums to one output the same way. This isn't a
new principle — it's the same one `filter.ladder`'s ZDF stages already
follow (internal complexity that never needs its own port).

### 5.2 So: no, not "2 outputs on an osc," and no, `instance.allocate.voice`
shouldn't move after the oscillator

Given §5.1, the user's own proposed fixes aren't needed:

- **Two outputs on the oscillator** (a "raw single wave" output plus a
  "domain-aware" one) — unnecessary, because there's only ever one kind of
  multiplicity that needs to be graph-visible (voice), and an oscillator's
  *unison-ness*, if it has any, never reaches the port boundary in the
  first place.
- **`instance.allocate.voice` moved to *after* the oscillator** — this
  would actually break the thing voice-multiplicity exists for: the whole
  point of `Poly` reaching the oscillator (via its `pitch` input, already
  true today) is that *different voices need different pitches*. An
  oscillator sitting before any allocator has no notion of "which note" at
  all — there'd be nothing for per-voice pitch to attach to. The allocator
  needs to stay upstream of anything pitch-dependent, exactly where it is
  today. What *would* be a legitimate generalization — allowing "allocate"
  to mean "fan out over something other than MIDI notes" (a grain clock, a
  unison-layer generator) — is a real, interesting idea, but it's a
  separate, bigger one (a generic fan-out primitive, not specific to
  voices) and is flagged as explicitly **out of scope** for this plan (see
  §8) rather than folded in.
- **"A different node for managing MIDI"** — already true today, and not a
  gap this redesign needs to create: `io.noteIn` (raw MIDI → pokes) and
  `instance.allocate.voice` (spawn trigger → voice-lane gate/pitch/etc.)
  are already two separate nodes with two separate jobs (ADR-0024). Nothing
  here needs to change; this part of the question is already answered by
  the existing split.

### 5.3 How ring mod (and anything else with 2+ Audio inputs) actually works under this model

Directly: yes, `Poly`-typed Audio plugs into a 2-input node like ring mod
exactly like any other `Poly` signal, via the ordinary resolution rule
(§2.3) — no special case needed:

- **Both inputs trace back to the same allocator** (e.g. self-ring-modding
  each voice's own oscillator against itself, or against another
  per-voice-generated tone): both resolve `Poly(originId=X)`, same origin →
  the node resolves `Poly(originId=X)` too, and *(mechanically, §6)* gets
  its own state slot per active lane — voice 3's ring mod only ever touches
  voice 3's two signals, never voice 5's. This is exactly the "detuned
  self-ring-mod per note" case and it falls out for free.
- **One input is `Poly`-sourced, the other `Scalar`-sourced** (a single
  shared LFO or fixed carrier not downstream of any allocator): broadcasts,
  exactly like a mono `Channels` source feeding a stereo port today — every
  voice's ring mod uses the *same* shared carrier, each still
  independently. **This is one case, not two — see §2.4**: it doesn't
  matter which of ring mod's two input ports happens to be the `Poly` one,
  since neither port declares a fixed multiplicity to begin with.
- **Both inputs are `Poly` but from *different* allocators**: `Reject` at
  compile time (§2.1) — there's no principled pairing between two
  unrelated voice regions' lanes, and silently picking one (e.g. "pair by
  lane index") would be exactly the kind of arbitrary, invisible behavior
  this whole redesign exists to get away from. The user must explicitly
  reduce one side with its own `instance.mix` first, making the "these are
  now being deliberately combined into one signal" step visible, the same
  way every other adapter in this codebase is visible rather than implicit.

So the honest answer to "could this be plugged into a mod": yes, cleanly,
with one hard rule (same-origin-or-scalar) that has to be enforced, and
zero special-casing for ring mod specifically — it's just an ordinary
2-input node under the general resolution rule.

### 5.4 Swarm and a future Position node family — generalizes, doesn't crumble

**Open, but a strong directional answer emerged.** Raised directly: does
this redesign foreclose a longstanding dream — a `Position`-category node
family simulating live per-instance spatial positions, feeding a per-
instance `Distance`-from-camera value into things like reverb size/
highpass/volume, plus a `Swarm Field` node visualizing the whole
population's distribution? This is exactly the thing `DomainSplitter`'s own
rejection message already names as deliberately out of scope today
("multiple simultaneous instanced regions are M28 Swarm territory, not
built yet"), and `wiki/reports/InstanceAllocator_2026-09-28.md` already
recommended Swarm become its own real node type later, "once actual runtime
machinery exists for them" — i.e. this was never expected to be free under
the *current* system either.

Walking it through against §2's model point by point, it holds up better
than the current system, not worse:

- **Cardinality.** A swarm's population is exactly the same shape problem
  `instance.allocate.voice` already solves: a fixed, compiled-in ceiling,
  most slots idle most of the time (Voice: 8 lanes, usually 1-3 held; a
  particle swarm: pick a ceiling, most particles alive or not at any given
  moment). No new mechanism — reusing Voice's own idle-lane pattern with a
  different ceiling.
- **Trigger/lifecycle.** Voice is spawned by `Note`-typed MIDI events.
  Swarm would be spawned by something else entirely (a spawn-rate
  parameter, a physics tick, whatever) — but that's just a **second,
  different allocator node type** producing `Poly(originId=itself)`, the
  same way `instance.allocate.voice` does today. The redesign doesn't care
  what triggers a `Poly` producer, only that something does.
- **Per-instance `Distance` (or x/y/z) feeding Reverb/Highpass/Volume.**
  This is *exactly* §3's "per-voice-varying property" mechanism — the same
  one that answers your own seed/index concern — just reused with `Distance`
  as the source instead of a random value. A `position.*` node outputting
  a `Poly`-tagged `Distance` Control, wired into Reverb's normally-Scalar
  `size`, makes Reverb resolve `Poly` and gives each swarm member its own
  independently-sized reverb tail, via the same rule already designed, not
  a new one.
- **The `Swarm Field` visual.** Reading *every* lane of a `Poly` signal at
  once (rather than reducing or previewing just one) is not a new
  capability this needs to invent — `instance.mix` already *requires*
  exactly that (it sums all N lanes every block). A `Swarm Field` viewer is
  the same category of consumer, rendering a scatter/field plot instead of
  a sum. This is genuinely richer than the single-lane Glance-preview
  default sketched in §8.2 (which is fine for "preview one voice's
  waveform," not for "show the whole population's spatial spread") — worth
  its own dedicated visualization path, not a blocker.

**The one honest new problem this surfaces, not present at Voice's N=8:**
the §6 execution-model rewrite gives every `Poly`-resolved node N
*independent, full* state slots. At N=8 that's cheap and already how
today's system works. At swarm scale (dozens to hundreds of alive
particles), giving *every* downstream node — including something as heavy
as a full reverb tail — N independent copies stops being free, and the type
system doesn't make that problem go away by itself. Concretely: "each
particle gets its own independently-sized reverb" almost certainly needs to
mean "one shared reverb, fed a blended signal, with each particle
contributing its own send level derived from `Distance`" rather than N
literal reverb instances — a real DSP/architecture decision for whoever
designs the actual Position/Swarm node family, not something this plan
resolves now. Flagged as a new open question (§8.6) so it isn't quietly
assumed away later.

### 5.5 Telemetry: a live per-node instance-count badge

**Settled as a real, wanted deliverable of this redesign** (explicitly
requested, not a nice-to-have bolted on after) — mechanics below still
**Open** in detail.

The ask: a small, grey, numeric badge near each node's top-right corner
showing how many instances of that node currently exist — specifically so
something heavy (a reverb, say) silently resolved to `Poly` and running N
independent copies is visible at a glance, for debugging: *"we know a
reverb is running foreach and it might not be a good idea."*

This is doable, and fits this redesign specifically rather than being a
bolt-on:

- It's actually two numbers worth distinguishing, not one: **(a)** the
  compiled ceiling — the fixed N a `Poly`-resolved node was compiled with
  (its origin allocator's `maxInstances`, already a real structural
  parameter — see `instance.allocate.voice.maxInstances`,
  `NodeDescriptorTests.cpp`), known the moment the graph compiles, no new
  runtime telemetry needed at all; **(b)** the live active count — how
  many of those N slots are actually gated/alive right now, the sharper
  debugging signal ("compiled ×8, but only 2 ever actually run"), which
  does need a real number off the audio thread. Recommend showing both
  together, one glyph — `"2/8"` — rather than picking one.
- The live count isn't new instrumentation invented from nothing: §6's
  execution-model rewrite already needs the scheduler to know, per
  `Poly`-resolved step, which of its N slots are currently alive (if only
  to skip processing idle ones cheaply — an obvious optimization once "8
  wholly separate plans" stops being the mechanism). Exposing that count is
  reading a number the scheduler already has to compute, not building a
  second counting mechanism.
- Getting it to the UI is genuinely new plumbing, not just a label —
  `CLAUDE.md`'s own notes already flag real per-node telemetry as unbuilt
  ("wiring real per-node/per-connection signals is M11's job, not done
  yet... don't assume subscribing a tap for an arbitrary graph node id
  produces real data today"). This feature is a legitimate, concrete reason
  to finally build that, via either **(i)** a new per-`Poly`-node
  `TelemetryHub` tap, throttled well below audio rate (a count changes on
  voice on/off, not every sample — nowhere near the existing 64-slot pool's
  bandwidth), or **(ii)** something cheaper, if the current allocator
  already internally tracks "which lanes are busy" for voice-stealing
  (plausible, not yet verified against the real source — check before
  assuming (i) is necessary).
- UI-side, there's a direct, already-proven precedent: `NodeCard.tsx`'s
  existing `DomainDot` (a small corner decoration on every node, driven by
  live graph state). Worth naming explicitly: **this badge is likely
  `DomainDot`'s actual replacement, not an addition alongside it** — "which
  domain is this node in" stops being a meaningful whole-graph question
  once §2 lands (there's no whole-graph partition left to be in), while
  "is this node Scalar or Poly right now, and how many instances" is
  exactly the new, correct, per-node-local question to show instead. One
  indicator replacing another, net simpler than today, not an added one.
- Recommend: absent entirely for a `Scalar`-resolved node (no badge = no
  noise for the overwhelming majority of nodes), visible only once a node
  resolves `Poly`.

### 5.6 Colour: Poly vs. Scalar Audio get distinct hues — decided, overrides §5.6's original recommendation

**Settled — a direct, explicit user decision, overriding the recommendation
this section originally made.** Recorded honestly rather than silently
rewritten, matching how `0019-adapter-table.md`'s own Amendment pattern
handles a reversal elsewhere in this project: the original recommendation
here was "don't give Multiplicity its own hue, reuse the neutral-grey
structural convention instead" (reasoning: colour is already fully claimed
by signal identity, and `Channels` already established that a structural
dimension gets a non-colour treatment, not a new hue). The user's explicit
call, verbatim reasoning: Poly and Scalar are "very tricky in plugging each
other" — exactly the highest-friction connection case this whole redesign
exists to fix — and that debugging value outweighs the colour-budget cost,
**for now** ("we can revert back later" — the user's own words, so this is
a real but explicitly reversible decision, not a permanent commitment).

**The decision (swapped once after the first pass — this is the final
assignment):**

- **Poly-resolved Audio becomes `#40FF69`.**
- **Scalar-resolved ("Summed") Audio stays Pink** — Audio's existing hue,
  unchanged.
- **`DomainDot` is removed entirely** — not replaced by an equivalent dot
  in a new colour, just gone. §5.5's instance-count badge is the only
  remaining per-node indicator taking its place.

One free, nice emergent property worth noting: a cable crossing the
Poly→Scalar reducer (`instance.sum`, §5.7) will now visibly flip from green
to pink exactly at the reduction point — a concrete, zero-extra-work visual
confirmation of exactly where a domain-crossing adapter is, reinforcing the
same "adapters are always real and visible, never hidden" principle the
rest of `canConnect` already lives by.

### 5.7 `instance.mix` renamed to `instance.sum`

**Settled.** "Mix" is genuinely overloaded — there's already a real,
unrelated `mix.sum` node (title "Mix," `mix.*` category, see §5.8) —
`instance.mix` and that node sharing conceptual vocabulary while doing
completely different jobs was confusing on its own merits, independent of
this redesign. Renamed to **`instance.sum`**: names what it literally does
(sums the per-voice signals) and pairs with the "reduce"/"collapse"
language the rest of this plan already uses for the Poly→Scalar direction.
Rule 3 is suspended (`CLAUDE.md`) so this is a free rename, no migration
needed — just bump `PatchDocument::currentSchemaVersion` for hygiene.

### 5.8 `mix.sum` folded into `math.add` — a real, verified duplication, not just a naming collision

**Settled as a recommendation, not yet built.** Read both `MixNode.h`
(`mix.sum`, title "Mix") and `AddNode.h` (`math.add`, title "Add") directly
to check, rather than assuming: they are almost line-for-line the same
node. Both are `GrowableGroupNode`s (2-16 inputs, 1 output);
`processSample` is `sum += inputs[i]` in both, identically. The only real
differences: **(a)** `mix.sum`'s ports are declared `Audio`, `math.add`'s
`Control`; **(b)** `math.add` additionally stores a per-slot value for an
unwired input (so "A + 5" needs no Constant node) — `mix.sum` never needed
this, since an unwired Audio input reading silence is already the sum
identity.

Given Control is already sample-rate and the same underlying
representation as Audio (established in `wiki/plans/AudioControlBridge.md`
§2 — *"Audio and Control are already the same underlying representation —
one float per sample"*), there is no DSP reason for these to be two node
types. **Recommendation: delete `mix.sum` outright, make `math.add`'s ports
`PortPolymorphism::SignalAndQuantity`** (the existing mechanism
`util.reroute`/`logic.select` already use — not a new concept) so it
inherits whatever's wired to it, Audio included, and keeps its own
per-slot stored-value convenience for the Control case. Under this
session's redesign, a polymorphic `math.add` also just resolves
`Multiplicity` the same way any ordinary node does (§2.3) — no special
case needed for the audio-summing role Mix used to have.

Two small follow-on details, not blockers: **(i)** a unified `math.add`
resolved to Audio needs to handle `Channels` the same way any Audio node
does (mono broadcasting into a stereo sum) — real but small, and doesn't
need new machinery, `canConnect`'s existing Channels rules already cover
it. **(ii)** `archive_docs/NODE_EDITOR.md` §7's Alt-drag gesture names
"Mix/Add/Multiply" as three separate quick-insert shortcuts — once Mix and
Add are the same underlying node, "Mix" as a shortcut label would just
insert `math.add` too (a UI-level synonym, not a second node type);
`math.multiply` stays separate and unaffected (it already explicitly
doubles as the ring-mod node when both inputs are audio-rate, per
`wiki/NODES.md`'s own note — nothing about this session's ring-mod
discussion needed a new node after all, it already exists).

---

## X. Communication convention adopted mid-discussion (not a code change)

When referring to a specific port in prose, write `node->port` for an
**output** port and `node<-port` for an **input** port — e.g.
`instance.allocate.voice->pitch` (an output), `env.adsr<-gate` (an input).
Adopted because the previous dotted style (`instance.allocate.voice.pitch`)
reads ambiguously against a dotted type id — the port looks like it could
be part of the same namespace. This is a standing conversational
convention, not a change to any real id.

**Open, needs the user's call (added to §8):** does this Poly/Scalar hue
split extend beyond Audio to `Control` and other `SignalType`s, or is it
Audio-only? Not assumed either way — the instruction given was specifically
about Audio.

---

## 6. The part that's genuinely a big rewrite, said plainly

**Settled that this is real; not yet settled exactly how far to take it.**

Today, "poly" is realized by brute force: `numVoices` (8) **entirely
separate** `ExecutionPlan`s, compiled from the same `NodeGraph`, each fully
independent — no shared state, no shared schedule, nothing crosses between
them except the one `instance.mix` external-input bridge.

The model in §2 wants multiplicity to live *per node*, which means the
architecturally-correct endpoint isn't "8 separate plans" — it's **one
compiled plan**, where a node resolved to `Poly` gets `N` independent state
slots (`N` separate `shared_ptr<Node>` objects instead of one) and the
scheduler loops over them for that step, while a `Scalar`-resolved node gets
exactly one slot and runs once. That is a real rewrite of `ExecutionPlan`'s
execution model — comparable in scope to the original per-voice design
itself, not a `DomainSplitter` patch. Concretely, at minimum:

- `ExecutionPlan::BlockStep` needs a per-step slot count (1 or N) and,
  for `Poly` steps, N independent buffers/state instead of one.
- The state-preservation-across-recompile mechanism
  (`GraphCompiler::compile()`'s `previousPlan`, keyed on `(id, type,
  parameters)`) needs to key per-slot, not per-node, once a node can hold N
  independent state objects.
- `NoteEvent`/`noteBuffers` routing (currently keyed by node, one Note
  input/output max per node) needs to decide how it interacts with a
  `Poly`-resolved node holding N slots — likely: N independent NoteEvent
  buffers, one per slot, matching how gate/pitch already vary per voice
  today.
- §4's multi-allocator support needs `ExecutionPlan`'s external-input
  mechanism generalized from one field to a map keyed by `originId`/mix
  node id.
- A node's *resolved* multiplicity can itself change across a recompile
  (e.g. an edit removes the only `Poly` input it had) — needs a defined
  rule for what happens to that node's state in that case, most likely:
  treated the same as any other `(id, type, parameters)` mismatch today,
  i.e. a fresh node, state not preserved, same as an ordinary parameter
  edit already behaves.

This is the right direction, but it is not a quick patch — it warrants its
own design pass on `ExecutionPlan`/`GraphCompiler` specifically, once the
type-system shape above (§2-§5) is agreed, not before.

---

## 7. Non-goals (this plan, this pass)

- **No generic "fan out over anything" primitive.** §5.2's "allocate could
  mean grain-clock-driven or unison-layer-driven fan-out, not just MIDI"
  is a real, separate idea — explicitly deferred, not folded into this
  redesign. `instance.allocate.voice` stays MIDI/Note-triggered only.
- **No change to unison/granular nodes' own internals.** They stay
  self-contained, summing to one Audio output internally (§5.1) — nothing
  about them needs to change for this plan; they were never broken, this
  plan just clarifies why they don't need a `Poly` port either.
- **No decision yet on whether `Multiplicity` literally becomes a new field
  next to `Channels` on `PortDescriptor`, vs. some other representation.**
  §2.1's sketch is the working shape, not a final API.
- **No decision yet on the exact `ExecutionPlan` rewrite mechanics (§6)** —
  flagged as real and necessary, deliberately not designed in detail here
  until the type-level shape is settled first.

---

## 8. Open questions (not yet resolved, need more discussion before build)

1. Does `Multiplicity` need a third state beyond `Scalar`/`Poly` for
   anything real today, or is binary enough? **Directional answer, §5.4:**
   binary is enough — Swarm doesn't need a third state, it's just a second
   `Poly`-producing allocator with a different origin, trigger, and
   ceiling. What it *does* need is §8.6, below.
2. How does a `Poly`-resolved Audio/Control port's **preview/telemetry**
   (Glance, scope) pick which of the N lanes to show? User's own suggestion:
   default to most-recently-triggered voice, configurable — not yet
   designed at the `TelemetryHub`/tap level.
3. Exact UI treatment for a `Poly` port — does the cable/port glyph need a
   visual tell distinguishing it from `Scalar`, the way stereo cables
   already do for `Channels`? Likely yes, not designed yet.
4. The generalized-fan-out idea (§7) — worth a future plan of its own, or
   dead end? Not decided, not urgent.
5. Whether/how `origin` needs to survive being written into a saved patch
   (an allocator's id is already a stable, hand-assigned string per Rule 3
   — probably fine as-is, not verified).
6a. **New, from §5.6.** Does the Poly/Scalar hue split (Pink/`#40FF69`)
   extend beyond Audio to `Control` and other `SignalType`s, or stay
   Audio-only? Not decided.
6. **New, from §5.4.** At Voice's N=8, giving every `Poly`-resolved node N
   independent full state copies is cheap and is already exactly how the
   current system behaves. At swarm/particle scale (dozens-hundreds), that
   stops being free — does every `Poly`-resolved node really get N full
   copies regardless of cost, or does the model need a cheaper "N
   independent scalar values feeding one shared instance's per-lane send"
   shape for heavy DSP (reverb, convolution) at large N? Not designed;
   flagged so it isn't quietly assumed away when a real Position/Swarm node
   family gets built.

---

## 9. Suggested sequencing (once discussion concludes — not started)

1. Settle §2's exact `PortDescriptor`/`Multiplicity` shape and the
   resolution rule (§2.3) as a final API — smallest possible diff to prove
   the model, likely on a throwaway test graph before touching real nodes.
2. `instance.allocate.voice`/`instance.mix` redefined in terms of it (§2.2)
   — should be closer to a rename/reinterpretation than a rewrite, since
   their actual runtime behavior barely changes.
3. `GraphCompiler`'s fixed-point resolution pass extended to propagate
   `Multiplicity` alongside `PortPolymorphism` (§2.3) — reuses existing,
   tested machinery.
4. Only then: the real `ExecutionPlan` execution-model rewrite (§6) — the
   biggest, riskiest piece, deliberately last.
5. Multi-allocator support (§4) — should fall out mostly for free once 1-4
   are real; mainly the `ExecutionPlan` external-input map.
6. UI mirror + preview/telemetry defaults (§8.2/8.3).

Not started. This plan is still being written live during discussion —
expect more sections/edits before any of this is approved for build.

---

## 10. Full implementation plan (approved via plan mode, 2026-09-29)

**Status: approved, no code written yet.** Everything below closes every
open question above with a concrete, decided answer — produced via a
dedicated plan-mode pass with three Explore agents verifying every
load-bearing file directly against the real source (not from memory or
this document's own earlier, more speculative sketches). Two corrections
this surfaced against §5 above: **`math.multiply` is not actually
polymorphic today** (§5.8's claim that it "already doubles as ring-mod"
was documentation-level, from `wiki/NODES.md`, not real — its ports are
fixed `Control`); and `instance.allocate.voice.maxInstances` is declared
but never enforced anywhere (`VoiceManager`'s pool is hardcoded to 8,
independent of it) — both are fixed as part of this work, not left as-is.

### 10.1 The one key architectural decision

**This is a compile-time reclassification rewrite, not a runtime
`ExecutionPlan`-internals rewrite.** §6 above flagged "one unified plan, N
state-slots per node" as the architecturally-pure endpoint for realizing
Multiplicity, but also flagged it as comparable in scope to the original
per-voice design itself. Working the mechanics through against the real
code changes the calculus: **keep the existing runtime shape** (N=8
independent physical `ExecutionPlan`s per voice-origin, driven by
`PlanSwapper`+`VoiceManager`, exactly as today) and **only replace how
nodes get sorted into "voice-bucket" vs. "global-bucket" graphs before
compile** — from `DomainSplitter`'s whole-graph reachability+fold to a new,
local, per-node fixed-point resolution pass. This delivers everything §2-§5
asked for (no more order-dependent rejections, multiple independent voice
regions per §4, the colour/badge features per §5.5/§5.6) without touching
`ExecutionPlan`'s buffer/scheduling internals at all, and without needing
the "`ExecutionPlan` needs multi-output support" prerequisite the old
`instance.mix`-count-limit rejection message named — that prerequisite was
specific to one plan having several simultaneous named outputs; under this
design each origin's voice-bucket is still its own separate plan, and the
global bucket is still exactly one plan with exactly one output, just with
more than one external-input feed into it.

**Multiplicity resolution is a new, universal compiler pass — not an
extension of `Node::resolveIncomingPort`/`InheritingPortsNode`.** §2.3's
original framing ("straightforward extension of the fixed-point iteration
GraphCompiler already does for PortPolymorphism") was directionally right
in spirit but wrong in mechanism, corrected here: that existing machinery
is opt-in (`hasPolymorphicPorts()`), used by only a handful of nodes, and
its single `bestPriority` jointly gates both `resolvedType` and
`resolvedQuantity` — it has no room for a third, independently-prioritized
resolved dimension. Voice-multiplicity has to apply to *every* node, since
any node could end up downstream of an allocator, so it's a separate pass,
gated by node **type id**, exactly the same way `DomainSplitter.cpp`
already special-cases exactly two type-id string constants
(`instanceMixTypeId`, `instanceVoiceTypeId`) today — this plan keeps that
same special-casing-by-type-id, it just replaces the algorithm built on top
of it. **No new `PortDescriptor` field is needed for the resolution
mechanism itself** — multiplicity is a per-compile *resolved fact* about a
node instance, not a static shape declared on a port, exactly like today's
`graphGetNodeDomains()` is already a separate round-trip from the static
`NodeDescriptor` JSON, not baked into it.

### 10.2 Batch 1 — Engine: the resolver, replacing `DomainSplitter`

**New:** `engine/include/bazalt/engine/graph/MultiplicityResolver.h` +
`engine/src/graph/MultiplicityResolver.cpp`, replacing
`DomainSplitter.h`/`.cpp` outright (delete both).

Algorithm (`MultiplicityResolver::split(const NodeGraph&)`, same
`DomainSplitResult`-shaped return — success/errorMessage/a `voiceGraph` per
origin/one `globalGraph` — but now a map of origin bundles instead of an
optional single one):

1. Find every node whose type is `"instance.allocate.voice"` → each is an
   **origin**, keyed by its own node id. `maxOrigins = 4` (new constant,
   mirrors `numAuxBuses`'s existing precedent for a small fixed
   multiplicity ceiling) — more than 4 is a compile error, same tone as
   today's "only one... found N" message.
2. Seed resolution: every output port of an `instance.allocate.voice` node
   (`gate`/`pitch`/`velocity`/`instanceIndex`/`instanceAge`/`random1`/
   `random2`/`start`/`stop` — the exact 9 ports verified in
   `InstanceVoiceNode.h`) is `Poly(originId = that node's own id)`. This
   node is a fixed producer by type id, not resolved dynamically.
3. Fixed-point pass over every other node (new, separate from
   `resolveIncomingPort`): a node resolves `Scalar` if every wired input is
   `Scalar` (or unconnected); resolves `Poly(X)` if any wired input is
   `Poly(X)` and no *other* wired input is `Poly(Y != X)`; two inputs
   resolving to different origins is a compile error — *"these two poly
   signals come from different voice allocators ('X', 'Y') and can't be
   combined directly — reduce one to Scalar first"* (§2.1's origin-mismatch
   rule, now actually enforced). Multiplicity is resolved **once per
   node**, applied uniformly to all its ports — ordinary nodes never have
   mixed per-port multiplicity; only the two boundary types do, by fixed
   declaration, not resolution (confirming §2.4's own conclusion).
4. Every node whose type is `"instance.sum"` (renamed from `"instance.mix"`,
   §10.3) is the other fixed-by-type-id boundary node: its `in` port is
   **required** to resolve `Poly` — resolving `Scalar` is a compile error
   (*"instance.sum's input must be a Poly signal — nothing to reduce"*,
   §2.4's "leaning Reject," now settled). Whichever origin its `in`
   resolved to is the origin it reduces; its own `out` is always `Scalar`.
   At most one `instance.sum` per origin (same ambiguity rule as today, now
   per-origin instead of per-graph); zero is valid (that origin's voice
   bucket simply has no global-visible output yet).
5. The graph's single designated output (`NodeGraph::getOutputNodeId()`)
   must resolve `Scalar` — same invariant `DomainSplitter` already
   enforced, now checked against the new resolution instead of a fold
   result.
6. Partition: one `NodeGraph` per origin (every `Poly(X)`-resolved node)
   plus exactly one `globalGraph` (every `Scalar`-resolved node, including
   every `instance.sum` node and the designated output).

**Delete:** `DomainSplitter.h`/`.cpp`. **Rewrite entirely:**
`tests/DomainSplitterTests.cpp` → `tests/MultiplicityResolverTests.cpp`,
porting all 22 existing verified cases plus new ones: 2 simultaneous
independent origins; a node fed by two *different* origins' Poly outputs
directly (the new origin-mismatch rejection); a 5th allocator rejected
(`maxOrigins` ceiling); and the concrete, no-longer-order-dependent version
of the exact repro that started this whole redesign (`env.adsr` connects to
`mix.gain` successfully *before* `mix.gain` is wired to anything else).

### 10.3 Batch 1b — `instance.mix` → `instance.sum`, plus the MIDI-independence fix

Registration line in `ProofGraphs.h` becomes `factory.registerType
("instance.sum", [] { return std::make_unique<nodes::InstanceMixNode>(); });`
(C++ class name unchanged, only the type-id string and title — now "Voice
Sum" — change). `PatchDocument::currentSchemaVersion` bumps 5→6 with a
no-op `migrateV5ToV6` (exact copy of the existing `migrateV4ToV5` pattern:
clone, bump the version property, return) — Rule 3 is suspended and this is
a pure rename, no id-rewriting needed.

**The MIDI-independence fix (real, necessary part of this batch, not a
side quest).** Root cause, confirmed against the real source: `VoiceStage
::Idle` lanes are skipped entirely by `renderVoiceRange`, and the *only*
thing that ever calls `VoiceManager::noteOn()` (which flips a lane out of
`Idle`) is `PluginProcessor::handleMidiEvent`, driven by real host MIDI. A
graph driving `instance.allocate.voice.spawn` purely from an internal
Note-typed producer (clock → seq → `note.assemble`, no `io.noteIn` at all)
has its Note event reach `InstanceVoiceNode::noteOn` correctly — but that
only matters if the lane is already rendering, which it never becomes
without a real MIDI note. This is the exact, previously-diagnosed-but-
unfixed "arp/seq is still MIDI-dependent" bug, and it directly blocks this
redesign's own motivating use case if left as-is.

**Fix:** add `std::atomic<int> spawnEventsThisBlock` (same pattern as the
aux-bus peak-level atomics in `updateAuxLevelsAndPassthrough`) to
`InstanceVoiceNode`, incremented inside its existing `noteOn`/`noteOff`
whenever they fire, from *any* source. Each origin bundle's
`PluginProcessor`-side driver reads this counter once per block and calls
that origin's own `VoiceManager::noteOn()`/`noteOff()` whenever it changed
— voice activation follows the graph's own wiring instead of being
hardwired to host MIDI. Real host MIDI keeps working unchanged.

### 10.4 Batch 2 — Runtime: multi-origin `PluginProcessor`

- Replace the single `voiceManager` + 8 `voicePlanSwappers` with
  `std::array<OriginBundle, maxOrigins> originBundles`, each `{
  VoiceManager; std::array<PlanSwapper, 8> voicePlanSwappers; juce::String
  originNodeId; bool active; }`. Only `active` bundles (an origin actually
  present in the current graph) render/compile.
- `globalPlanSwapper` stays exactly **one**. `ExecutionPlan::
  externalInputNodeId` (single `juce::String`) generalizes to
  `std::array<juce::String, maxOrigins> externalInputNodeIds`.
  `instanceMixScratchBuffer` becomes one per origin bundle (still mono per
  buffer — real per-voice stereo summing stays the pre-existing, documented
  limitation it already is, not solved or worsened by this redesign).
- `finalizeInstanceMixIntoOutput` becomes a loop over active origin
  bundles, each looking up its own `instance.sum` node in the one shared
  global plan and calling `setExternalBlock` with its own scratch-sum —
  same sum/average/silence logic as today, per-origin instead of singular.
- `handleMidiEvent`/`triggerVoiceNote`/pitch-bend broadcast generalize to
  iterate active origin bundles × their own 8 lanes; real host MIDI
  broadcasts identically to every active origin's `io.noteIn`, exactly as
  today (an internally-sequenced origin simply has no `io.noteIn` to
  receive it).
- `GraphEditController::recompileAndPublish()` rewritten: loop over
  `MultiplicityResolver::split()`'s per-origin `voiceGraphs` map (8 voice
  plans per active origin, `previousPlan` continuity per-origin exactly as
  today) plus exactly one unconditional global-graph compile.

**Tests:** `GraphEditControllerTests.cpp`'s `getNodeDomains()` case →
`getNodeMultiplicity()` with 2 simultaneous origins. New end-to-end
`tests-plugin` case: two independent allocator/sum pairs, each driven by a
different internal `clock`→`seq`→`note.assemble` chain, no `io.noteIn`
anywhere, both audibly render with zero MIDI — the direct regression test
for §10.3's fix.

### 10.5 Batch 3 — `mix.sum` deleted into `math.add`; `math.multiply` actually fixed

- Delete `MixNode.h` and its `"mix.sum"` registration.
- `AddNode.h`/`MultiplyNode.h`: add `.polymorphism =
  PortPolymorphism::SignalAndQuantity` to their growable-group ports (same
  shape `RerouteNode.h`/`LogicSelectNode.h` already use) — `math.multiply`
  needs this too, since (§10, corrected) it isn't actually polymorphic
  today despite the wiki claiming it handles audio-rate ring-mod; after
  this change that claim becomes true.
- `ProofGraphs.h`: `buildInitPatchGraph()`/`buildKarplusStrongProofGraph()`
  literal `"mix.sum"` → `"math.add"`.
- `NodeDescriptorTests.cpp:13`: registered count 78→77 (net −1: `mix.sum`
  removed, nothing added — the `instance.mix`→`instance.sum` rename is not
  a count change).
- New `CanConnectTests.cpp` cases: Audio into `math.add`/`math.multiply`'s
  growable ports resolves via `SignalAndQuantity`, same pattern as
  `util.reroute`'s existing tests.

### 10.6 Batch 4 — UI: colour, the instance-count badge, `DomainDot` removed

- `tokens.ts`: remove `domainVoice`/`domainGlobal`/`domainMono`. `portAudio`
  (`#e0339e`) becomes Scalar-resolved Audio's colour (value unchanged,
  meaning changed); new `portAudioPoly: '#40FF69'` for Poly-resolved Audio
  (final swapped assignment, §5.6). Retire `portPoly` (`#4ade80` — a
  *different* green from the new `#40FF69`) in favour of `portAudioPoly`
  everywhere it was used.
- `portUiKind.ts`: fold multiplicity into `classifyPortUiKind` properly
  (`'audio'` splits into `'audio-scalar'`/`'audio-poly'`, both real
  `PortUiKind` entries) — replaces the ad-hoc `port.isPolyPlaceholder`
  override sprinkled across 4+ `NodeCard.tsx` call sites. Audio-only for
  now, per §8's still-open question about Control/other types.
- New native call `graphGetNodeMultiplicity()` replacing
  `graphGetNodeDomains()`, same "separate round trip refreshed alongside
  every resync" pattern, returning **per port** (not per node, so the two
  boundary nodes' mixed per-port shape needs no UI-side special-casing):
  `{ [nodeId]: { [portId]: { kind, originId? } } }`, plus a badge payload
  `{ [nodeId]: { activeCount, maxCount } }` present only for `Poly` nodes.
  - `maxCount`: `instance.allocate.voice.maxInstances`, **now actually
    enforced** — `VoiceManager` gains `setMaxActiveVoices(int)`, closing
    the gap where this parameter was declared but read nowhere (confirmed
    against the real source; without this fix the badge would show a
    ceiling that lies).
  - `activeCount`: new `VoiceManager::getActiveVoiceCount()` (count of
    non-`Idle` lanes), exposed via a `std::atomic<int>` written once per
    block on the audio thread (same pattern as the aux peak-level atomics).
- `NodeCard.tsx`: `DomainDot` deleted entirely (confirmed exact removal
  site). New `InstanceCountBadge`, `"{activeCount}/{maxCount}"`, reusing
  `domainMono`'s hex (`#5f6672`) as a new `structuralGrey` token, rendered
  as a true `position: absolute` top-right corner badge — a new CSS
  treatment, since `DomainDot` was an inline title-bar flex child, not
  actually a corner badge (a correction against §5.5's "same corner"
  framing).
- `graphStore.ts`: `endpointFor()` is unaffected (multiplicity resolution
  is a separate universal pass, confirmed not part of the polymorphism
  mechanism it mirrors). `domains`/`GraphSnapshot.domains` renamed
  `multiplicity`/`GraphSnapshot.multiplicity`, full prop-chain rename
  through `GraphSurface.tsx`.
- `canConnect.ts`: new outcome-only origin-mismatch rejection branch,
  mirroring the resolver's new rule at the wire-drag-prediction level.

### 10.7 Batch 5 — Docs

`wiki/NODES.md` (`instance.sum` rename, `math.add`/`math.multiply`
polymorphism notes, `mix.sum` entry removed); `wiki/NODES.System.md`
(domain section rewritten around per-node resolution, replacing the
`DomainSplitter` description); `wiki/NODES_Gaps.md` (close the
`maxInstances`-never-enforced and MIDI-dependency gaps); `wiki/
NODES.Status.md` (node-count update); `wiki/MILESTONES.md` (new entry);
`CLAUDE.md` (the "Known interim simplifications" bullets naming
`instance.mix`/`DomainSplitter`/the one-allocator ceiling are rewritten,
not left stale).

### 10.8 Verification

`ctest` (both suites) green after each batch, each batch independently
buildable/committable in order. Concrete end-to-end proofs required before
calling this done: (1) the exact original repro
(`env.adsr`→`mix.gain` before `mix.gain` is wired to anything else) now
compiles; (2) two independent allocator/sum pairs, each driven by a
different internal clock→seq→`note.assemble` chain with no `io.noteIn`
anywhere, both audibly play with zero MIDI input; (3) manual Standalone
check — badge only on Poly nodes with a correct live count, Poly Audio
cables `#40FF69`, Scalar Audio cables pink, `DomainDot` gone everywhere.
