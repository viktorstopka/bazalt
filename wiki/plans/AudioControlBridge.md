# Audio → Control Bridge — plan

**Status:** Proposed, not built. Written 2026-09-28, from a direct conversation about
audio-rate modulation. No milestone number assigned — slot this into
`wiki/MILESTONES.md` when it's actually picked up.

This is the first file in a new `wiki/plans/` directory — distinct from
`wiki/reports/` (investigative write-ups answering a specific question, like
`wiki/reports/InstanceAllocator_2026-09-28.md`) and from `wiki/NODES_Gaps.md`
(found-defect tracking for existing behavior). A plan here designs a
**not-yet-decided new capability** before any code exists for it. `CLAUDE.md`
doesn't list this directory yet — worth a one-line pointer added there once this
arc has more than one file in it.

---

## 0. Origin

Came out of a direct question: is `Control` (white/yellow/orange) actually computed
at sample rate, and if so, could `Audio` and `Control` become one type, with Audio
pluggable straight into a modulation input? That's a real, working idea in modular
synthesis generally (Eurorack/VCV Rack don't distinguish "audio" from "CV" at the
type level), and it's a real onramp to FM/ring-mod/audio-rate modulation in this
engine specifically.

The follow-up decision (the user's own call, not this plan's invention): **don't
merge the types.** Audio stays pink, stays its own `SignalType`, specifically
because it already carries something Control never needs to (`Channels` —
mono/stereo today) and may carry more in the future. This plan designs the
alternative: a real, visible, auto-insertable **adapter** that bridges the two
without erasing the distinction between them.

---

## 1. Why Audio and Control stay separate types

- **`Channels` is real and load-bearing for Audio, meaningless for Control.** The
  whole flat-channel-slot compiler mechanism (`wiki/NODES.System.md` §9) exists
  because a stereo Audio port is two buffer slots, not one. Control ports have no
  such concept and never needed one. Folding Audio into Control would mean either
  giving Control a `Channels` field it has no other use for, or stripping Audio of
  one it genuinely relies on.
- **Control's whole point is the `ValueContract`** (kind/quantity/curve/bounds,
  `wiki/NODES.System.md` §2) — that's what makes a Control port host an inline
  editable default, seed auto-adapters by quantity, and render a UI-appropriate
  widget. Audio has none of that today, on purpose: there's no meaningful "default
  value" or "curve" for a waveform.
- **Room to grow, on both sides, without cross-contamination.** The user's own
  framing: Audio may one day carry "more complex data inside" than a flat stereo
  pair — more channel configurations (the `resonator.junction` branch group and
  `sampler.granular`'s multi-output hint at this already), or some other future
  per-sample richness. Keeping Audio a distinct nominal type means whatever it
  grows into later never has to be reconciled with Control's own evolution (richer
  quantities, curve types, etc.), and vice versa. A shared type would force every
  future addition to either type to be re-justified against the other's contract.
- **Matches the project's own stated philosophy** (`wiki/NODES.System.md` §1): keep
  the type list small, but each type real and load-bearing, not two types collapsed
  because their *current* representation happens to coincide.

None of this blocks the actual goal. The representation coinciding (see §2) is
exactly what makes a cheap, mechanical adapter possible instead of a real DSP
conversion.

---

## 2. What's actually missing

Control is **already** computed at sample rate everywhere in this engine, not just
"sometimes" — `AdsrNode.h`'s Control output runs through the same `processSample()`
loop every Audio node does, and `wiki/NODES.System.md` §4's own table footnote
confirms it: *"no rate distinction is actually enforced in the current engine —
every Control port already accepts either."* Rule 6 in `CLAUDE.md` (no node's math
may depend on block size) is exactly why: there is no separate block-rate compute
path to begin with. Audio and Control are already the same underlying
representation — one float per sample, in `blockBuffers`, recomputed every sample.

So nothing about *rate* needs to change. The actual gap is narrower and purely
structural: **`canConnect` hard-rejects `Audio → Control` today.** The catalog names
`env.follower` as the existing manual bridge, but explicitly flags it as
"❌ not auto-inserted — today this is a straight `reject` unless placed by hand"
(`wiki/NODES.System.md` §4's matrix). There is no way, today, to route a raw
waveform into a modulation-shaped port at all without manually pre-inserting
`env.follower` — and `env.follower` doesn't even do what audio-rate FM/ring-mod
needs (see §3).

---

## 3. Two different jobs, easily conflated

`Audio → Control` sounds like one gap. It's actually two different, legitimate
jobs that happen to cross the same type boundary:

| Job | Answers | Node | Status |
|---|---|---|---|
| "How loud is this signal, smoothed?" | Amplitude/envelope tracking — sidechain-style ducking, dynamics-driven modulation | `env.follower` (real, ✅) | Exists today, hand-placed only. Auto-insertion for this pair was actually already promised — see §5 — and never shipped. **This plan does not change `env.follower` itself.** |
| "Use this signal's actual waveform as a modulator" | FM, ring modulation, audio-rate filter/pan/delay-time modulation, physical-model cross-coupling | **new, this plan** | Doesn't exist in any form — not even hand-placeable — because nothing converts a raw waveform's *instantaneous* value into a Control-typed cable at all. |

`env.follower` rectifies and smooths (independent attack/release ballistics,
`wiki/NODES.md`'s own entry) — by design, it throws away the waveform and keeps
only a slow-moving loudness contour. That's exactly wrong for FM/ring-mod, where the
waveform's instant-by-instant value **is** the modulator. Conflating the two would
mean auto-inserting a smoother exactly where the point is to *not* smooth — see §5
for why this plan picks the new node as the default, not `env.follower`.

---

## 4. Design

### 4.1 New node: `adapt.audioToControl`

A small, mechanical, allocation-free adapter — same spirit as `adapt.map`/
`adapt.normalise`, not a creative DSP effect like `env.follower`.

- **Category:** `adapt.*` (alongside Map/Remap/Normalise/Threshold/Sample & Hold).
- **In:** `in` — `Audio`, **mono only** (see §4.2 for why stereo isn't handled here).
  `depth` — `Control · Unipolar · 0–1 (or similar bounded range) · linear · 1.0`,
  `hasFallbackWhenUnconnected` (unpatched = full-strength passthrough, matching the
  "unpatched is just as loud/present as before" precedent `mix.gain.gain` already
  set — `wiki/NODES_Gaps.md`'s `modulation-only-port` fix).
- **Out:** `out` — `Control`, quantity `Bipolar` (canonical −1…1 — matches every
  other raw-ish Bipolar Control source already in the catalog: `random.stepped`,
  `random.drift`, the future `lfo.shape`).
- **Behavior:** `out = clamp(in * depth, -1, 1)`, per sample. Clamping matches
  `random.drift`'s own stated behavior for the same Bipolar contract ("clamped to
  stay in range") — without it, a signal that transiently exceeds ±1 (clipping,
  self-oscillating physical models) would silently break the *destination's* own
  seeded-range assumption when a second adapter (`adapt.map`) is chained after this
  one.
- **Title:** something plain, not "Audio to Control" verbatim — `wiki/NODES.System.md`
  §7's naming test suggests **"To Modulation"** (the codebase's own prose already
  calls Unipolar/Bipolar "the modulation quantities," §6) or **"Audio to
  Modulation."** Small, open call — not load-bearing for the rest of this plan.

### 4.2 `canConnect` additions (`engine/src/graph/CanConnect.cpp`)

A new branch alongside the existing `Control → Event` heterogeneous-pair branch (the
only one that exists today), added to `canConnect()` before the catch-all reject:

```cpp
if (from.type == SignalType::Audio && to.type == SignalType::Control)
{
    if (from.channels == Channels::Stereo)
        return reject ("Stereo source into a Control-typed port needs mix.downmix first");

    AdapterStep first { "adapt.audioToControl", "in" };

    if (isRealQuantity (to.quantity))
    {
        // Two-step chain: cross the type wall, then rescale into the
        // destination's real quantity — mirrors connectControl()'s own
        // Unipolar/Bipolar -> real-quantity case (adapt.map, seeded from
        // the destination's range), just reached from Audio instead of
        // from an existing Control source.
        AdapterStep second { "adapt.map", "in" };
        second.seedFromDestinationRange = true;

        CanConnectResult result;
        result.outcome = ConnectionOutcome::NeedsAdapters;
        result.adapterChain = { first, second };
        result.reason = "Raw audio into a real-quantity port needs Audio to Modulation, then Map";
        return result;
    }

    return needsAdapter (first, "A raw audio signal into a modulation port needs Audio to Modulation");
}
```

Worth flagging for whoever implements this: **every existing heterogeneous-pair
branch in `CanConnect.cpp` today only ever returns a 1-element `adapterChain`** (the
"two different real quantities" case was deliberately collapsed to one node,
`adapt.remap`, per ADR-0019's M20 amendment). This would be the *first* real 2-step
chain actually assembled inside `CanConnect.cpp` itself. The splicing side
(`GraphEditController::connectWithAutoAdapt`) already handles a 1- or 2-step chain
generically today — the loop, the midpoint-vs-thirds node placement, the per-step
`seedFromSourceRange`/`seedFromDestinationRange` handling are all already written
and already proven (the M20 amendment's own comment names this exact future need:
*"the later waves above [Envelope Follower, Sample & Hold, Note gate/value] may need
the 2-step path this loop already supports"*) — so this is exercising existing,
tested machinery, not building new plumbing.

**Stereo is a hard reject in this plan's v1**, not an auto-chain — see §6.

### 4.3 UI mirror (`ui/src/graph/canConnect.ts`)

Same rule, hand-mirrored, per the project's standard "engine is the sole authority,
UI predicts" pattern (`wiki/NODES.System.md` §4).

### 4.4 Colour / cable rendering

**No new UI rule needed at all.** Colour is already derived purely from
`(signalType, quantity)` per port (`wiki/NODES.System.md` §6) — the cable feeding
`adapt.audioToControl.in` stays pink (it's still `Audio`), and the cable leaving its
`out` (or the following `adapt.map`, if chained) is coloured exactly like any other
Bipolar/real-quantity Control cable today. The type crossing is visually legible
simply because it happens at a real, visible node, not hidden in a cable.

---

## 5. Must be resolved explicitly: this revises ADR-0019

`archive_docs/decisions/0019-adapter-table.md` already commits to an answer for
`Audio → Control`, in its original (M16) decision text: *"Later waves add `Audio` →
`Control` via `Envelope follower` (M20)."* That wave was never executed — today
`Audio → Control` is a straight reject, and `env.follower` stays fully manual. But
the *plan on record* says the eventual auto-insert target should be `env.follower`,
not a new node.

**This plan recommends overriding that, not fulfilling it** — auto-insert
`adapt.audioToControl` for `Audio → Control`, and leave `env.follower` exactly as it
is today: real, useful, hand-placed only. Reasoning:

- Every other adapter this codebase auto-inserts (`adapt.map`, `adapt.normalise`,
  `adapt.remap`, `adapt.threshold`) is a **mechanical, opinion-free** crossing — it
  rescales or edge-detects, but never throws away information the user didn't ask
  it to. `env.follower` is a real *creative* DSP choice (attack/release ballistics,
  peak-vs-RMS) — auto-inserting it on every plain wire-drag would silently bake in
  an opinion the other adapters deliberately don't.
- Auto-inserting `env.follower` by default would **actively defeat** the motivating
  use case: dragging an oscillator into a filter's cutoff to get FM-through-the-
  filter would silently become a smoothed envelope-follow instead of the raw
  waveform the user was reaching for.
- Nothing about `env.follower` changes. It stays real, stays correct for
  sidechain/ducking-style patches, stays available to place by hand — this plan
  only changes what the *bare wire-drag* auto-inserts by default.

**This needs the user's explicit sign-off before implementation** — it's revising a
standing decision, not just adding a new one. If approved, the right way to record
it is a new **Amendment** section in `0019-adapter-table.md` itself, matching the
M20 amendment already living in that same file — not a silent rewrite of the
original text.

---

## 6. Non-goals (v1)

- **No `SignalType` merge.** Audio keeps its identity, its `Channels` field, and
  room to grow further (§1) — this is the whole point of building a bridge instead.
- **No reverse `Control → Audio` bridge.** Not requested, no concrete use case
  identified in the conversation this plan comes from (an LFO's slow wiggle plugged
  somewhere expecting a raw waveform doesn't obviously do anything useful, since
  Control cables already free-broadcast at whatever rate a destination reads them).
  Flagged as an open symmetric question for later, not scoped here.
- **No `Quantity` table changes.** The new node's output reuses the existing
  `Bipolar` quantity as-is (`wiki/NODES.System.md` §2) — no "raw audio range"
  quantity is introduced.
- **No stereo auto-chain in v1.** `Audio (stereo) → Control` stays a hard `Reject`
  ("insert `mix.downmix` by hand first"), the same posture `Audio (stereo) → Audio
  (mono)` held before the real stereo-cable redesign. A `mix.downmix →
  adapt.audioToControl` 2-step auto-chain is a plausible v2 (the splicing mechanism
  already supports 2 steps generically — see §4.2), but combined with the
  real-quantity case's own 2-step need, a stereo source into a real-quantity Control
  port would need **3** adapters, over this codebase's own "at most two, or reject"
  ceiling (`wiki/NODES.System.md` §4) — that combination stays a hard reject
  regardless, built by hand. Keeping stereo out of v1 avoids half-solving this.
- **No change to how Control itself is computed.** Already sample-rate everywhere
  (§2) — nothing to touch.

---

## 7. Affected files

- **New:** `engine/include/bazalt/engine/nodes/AudioToControlNode.h` — shape follows
  `NormaliseNode.h` (small, single-purpose adapter node) rather than `EnvelopeFollowerNode.h`
  (real ballistics/state).
- `engine/src/graph/CanConnect.cpp` — new heterogeneous-pair branch (§4.2).
- `engine/include/bazalt/engine/graph/ProofGraphs.h` — this is where every other
  real node type's `factory.registerType(...)` call actually lives (confirmed by
  reading it directly, not assumed) — `adapt.audioToControl` needs the same
  registration line as `adapt.map`/`adapt.normalise`/`adapt.threshold`.
- `ui/src/graph/canConnect.ts` — mirrored rule (§4.3).
- `wiki/NODES.md` — new `adapt.audioToControl` entry in the `adapt` section; a
  cross-reference note on `env.follower`'s own entry clarifying the two jobs are
  deliberately different (§3).
- `wiki/NODES.System.md` §4 — new matrix rows for `Audio → Control` (mono, both the
  Bipolar/Unipolar and real-quantity cases) and `Audio (stereo) → Control` (reject).
- `archive_docs/decisions/0019-adapter-table.md` — new Amendment section, **only
  once §5 is actually agreed**, not before.
- **Tests:**
  - `tests/CanConnectTests.cpp` — the new pure-function rule, both the 1-step and
    2-step cases, plus the stereo reject.
  - `tests-plugin/ConnectWithAutoAdaptTests.cpp` — real end-to-end auto-insertion
    (this is the file that already covers `mix.downmix`'s own auto-insertion, so it's
    the natural home for this one too).
  - A real audio-rate integration test (`tests/` or via `RenderCli`) proving the
    actual musical payoff — e.g. two `osc.sine`s FM'd through
    `adapt.audioToControl` producing real, measurable sidebands, not just that the
    graph compiles.

---

## 8. Suggested sequencing

1. **Engine only:** new node + `CanConnect.cpp` rule + `tests/CanConnectTests.cpp` +
   registration. Fully headless, testable via `ctest`, no UI risk.
2. **Auto-insertion + UI mirror:** `canConnect.ts`, `ConnectWithAutoAdaptTests.cpp`,
   a real wire-drag check in the Standalone app.
3. **Docs:** `wiki/NODES.md` + `wiki/NODES.System.md` §4 updated to reflect reality,
   same discipline every other landed gap-fix in this arc has followed.
4. **§5's ADR amendment**, once agreed — not before, and not silently.
5. *(Stretch, not v1)* the stereo 2-step auto-chain (§6); a distinct visual
   treatment for the adapter node itself so it reads as "the FM/ring-mod gateway"
   rather than an anonymous grey box — pure polish, no functional dependency on
   anything else here.
