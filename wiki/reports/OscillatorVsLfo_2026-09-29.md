# Oscillator vs LFO: one node family or two?

**Written:** 2026-09-29, capturing a design conversation that started from a direct question —
"now that the Audio → Control Bridge exists, why separate Oscillator and LFO nodes at all? Doesn't
that just clutter the catalog?" — and grew into a bigger one: is the goal here a competent modular
synth with its own interface (PhasePlant, Vital, etc., with the serial numbers changed), or a real
re-examination of inherited hardware vocabulary ("LFO," "VCO") now that the constraints that
produced that vocabulary don't apply to software? Not a decision record — explicitly parked for
discussion later, per the user's own framing. Nothing here is committed.

**Where this landed, for now:** the *rate*-based reason to split them is gone (it was never really
load-bearing in this engine — see §1). But "merge them into one node" doesn't follow from that, and
would trade one kind of clutter for another (§2). The actual live question is more interesting than
"one node or two": should "LFO" and "Oscillator" be hardcoded node *types* at all, or *roles* a
smaller set of generative primitives can be dressed up as — and this codebase already has a
half-built mechanism for exactly that (`stock.*`/`factory.*`, M29, §3). One tempting shortcut (a
dual Audio+Control output on one oscillator node, so no adapter is ever needed) turns out to cut
against a principle the Bridge itself just established on purpose (§4).

---

## 1. Where "LFO vs oscillator" actually comes from

Not a conceptual distinction — a component-cost artifact of analog voltage control. A VCO needed
1V/octave tracking precision across many octaves and had to run cleanly to 20kHz+ without thermal/
voltage drift; building that was genuinely hard and expensive. An LFO never needed audio-rate
bandwidth or that tracking precision, so it could be built out of cheaper, "sloppier" circuitry (a
leaky RC integrator, a relaxation oscillator) that would fall apart if pushed to VCO duty. The split
exists in hardware because one circuit doing both jobs well was hard to build economically.

None of that applies to a `double` doing `phase += rate / sampleRate`. It's exactly as accurate at
0.01Hz as at 15kHz, for free — this engine's own `Control` signal is already computed at sample
rate everywhere, with no separate block-rate path to begin with (`wiki/plans/AudioControlBridge.md`
§2, itself grounded in `NODES.System.md` §4's footnote). The *engineering* reason for the split is
real in hardware and simply absent here.

## 2. Why that doesn't mean "merge the nodes"

Once the rate/cost argument is gone, what's left isn't "audio-rate vs sub-audio-rate" — it's **what
the periodic signal is for**. An audio-rate modulator is valued for its waveform's harmonic content
(FM/AM sidebands are literally made of the modulator's shape). A "gesture" modulator is valued for
its relationship to musical time. Same math, genuinely different jobs.

`lfo.shape`'s own catalog spec (still 📋, never built) already reflects this — it isn't "an
oscillator with a lower rate ceiling," it's:

- `sync : Event` — retrigger/phase-reset (per-note sync).
- `rateMode` — tempo-synced divisions, not just free Hz.
- `cycle : Event` — fires once per cycle, for triggering other things in lockstep.
- `shape`/`shapeB` + `shapeMorph` — live blend between two drawn curves.
- `fade`, `depth`, `smooth` — fade-in, a baked-in modulation-amount knob, click-free changes.

None of that belongs on `osc.analog`/`osc.sine` — those are used inside FM stacks and per-voice DSP
where being cheap and band-limited is the entire point (`osc.sine`'s own doc comment: "must be as
cheap as possible because FM stacks... use many"). Bolting retrigger-per-note and tempo-sync onto
them clutters that node for every user who never wanted an LFO — it moves the clutter, doesn't
remove it. And if those features have nowhere else to live, an LFO user loses tempo sync and
retrigger outright, which are the actual reasons a dedicated LFO exists in every DAW/synth that has
one.

The Bridge (`adapt.audioToControl`) already lets someone who wants *none* of that — "just a slow
oscillator, no conveniences" — build exactly that today, with the node that already exists. That's
the Bridge doing its intended job, not a reason to delete `lfo.shape`.

## 3. The more interesting reframe

The live question isn't "one node or two" — it's **should "LFO" and "Oscillator" be hardcoded node
*types* at all, or *roles* that a smaller set of generative primitives gets dressed up as?**

That already has a half-built answer in this project: `stock.*`/`factory.*` (M29, not built —
`NODES.System.md` §8's "a factory ≠ a group" framing). Under that model:

- One generator primitive underneath (a phase accumulator + waveform table — what `osc.sine`/
  `osc.analog` already are).
- "LFO" becomes a preset/identity wrapped around it, surfacing `sync`/`rateMode`/`cycle`/
  `shapeMorph`/`fade` as its front panel — because that's what someone reaching for "give me an
  LFO" wants to see first.
- "VCO" is a different preset of the same underlying thing, surfacing pitch-tracking and
  band-limiting instead.
- Someone who wants neither preset uses the raw primitive directly, same as today.

This is a genuinely different thing from "PhasePlant with a different interface" — PhasePlant,
Serum, Vital all still hardcode "this is the LFO module, this is the oscillator module" as fixed,
separate things. What this points at is closer to how Bitwig's Grid treats some of its own modules:
the same underlying block, presented differently depending on context. It's a real redesign
question, and one this project's own architecture already has a slot reserved for — it just hasn't
been connected to this specific question before.

## 4. One tempting shortcut that cuts against a principle just adopted on purpose

The obvious "make it frictionless" move: give every oscillator a second, Control-typed output port
that mirrors its Audio output, so using one as a modulator needs no adapter at all.

Worth naming directly: this would contradict a principle the Bridge itself just established.
`Data` never converts implicitly in this engine — not even between two tags that are secretly the
same array-of-floats (`data.table`↔`data.scale`) — specifically so a type crossing is never hidden
inside a cable, and every adapter this engine has is a real, visible, placed node
(`wiki/plans/AudioControlBridge.md` §1/§4, `archive_docs/decisions/0019-adapter-table.md`). A silent
dual-typed output would be a *bigger* invisible conversion than anything the adapter system
currently allows to be invisible.

So if the eventual direction is "generator as a role, not a fixed type," the `adapt.audioToControl`
hop should probably stay exactly as visible as it is today — the redesign is about which node types
exist and what they're named, not about making a type crossing disappear.

## 5. Open questions, for whenever this gets picked back up

- If "LFO" becomes a `stock.*` preset rather than a hand-maintained node type, does it still get its
  own catalog entry (`wiki/NODES.md`) at all, or does the catalog start documenting presets
  differently from native node types? No precedent for this yet — `stock.*` groups today are
  specified as fixed signal-chain recipes (Karplus-Strong, Bubble, etc.), not "the same node,
  restyled."
- `rateMode`'s tempo-synced divisions and `sync`'s retrigger are genuinely reusable — would they end
  up as structural options on the shared generator primitive itself (available to everyone, gated
  behind an "advanced" disclosure so `osc.sine`'s cheap-stack use case isn't taxed), or does the
  LFO-specific wrapper own them exclusively? This is the actual crux of "does merging clutter the
  audio-rate case," and it's not resolved here.
- `shape`/`shapeB`/`shapeMorph` are, under the hood, `data.table` + `data.lookup` — does the eventual
  `lfo.shape` (or its replacement) get built as a thin assembly of existing nodes (like a `factory.*`
  unwrap target) rather than its own from-scratch DSP class? Worth deciding before writing any code,
  not after.
- Does this same question apply one level up — is "Envelope" (`env.adsr`) also just a role of the
  same underlying "shaped signal over time" primitive, or is its stage-based (attack/decay/sustain/
  release) structure different enough in kind (not just role) to stay its own thing regardless? Not
  investigated here at all — flagged because the reasoning in §2/§3 would apply to it too if it
  generalizes.
