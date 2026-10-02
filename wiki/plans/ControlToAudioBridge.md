# Control → Audio Bridge — plan

**Status:** Built, 2026-10-02 (engine node + `CanConnect.cpp` rule + UI mirror +
tests, all landed same session this plan was written in — the durable project
record, written at completion rather than mid-discussion, same convention
`wiki/plans/UtilMacro.md` already uses). Closes the "open symmetric
question for later" `wiki/plans/AudioControlBridge.md` §6 explicitly deferred: *"No
reverse Control → Audio bridge. Not requested, no concrete use case identified...
Flagged as an open symmetric question for later, not scoped here."* Direct question
from the user — "does [Audio → Control] have an opposite?" — followed by "proceed on
working." This plan is short on purpose: the hard architectural call (keep `Audio`
and `Control` as separate `SignalType`s, don't merge them) was already made and
reasoned through in `AudioControlBridge.md` §1 and isn't revisited here — only the
direction that plan left open.

---

## 1. Why this is worth building now

`AudioControlBridge.md` §2 already established that `Control` is computed at full
sample-rate resolution everywhere in this engine — "one float per sample... Audio and
Control are already the same underlying representation." The only thing stopping a
modulation signal from being wired anywhere an `Audio` port is expected is the type
tag itself, not a real rate/representation gap. Concrete use case the original plan
couldn't name: **sonification/monitoring** (literally hearing an LFO/envelope/random
source as a tone, instead of only seeing it on a preview) and **feeding a Control
source into an audio-rate processing chain** that only exposes `Audio`-typed ports
(a shaper, a filter's own audio input used as a cheeky "DC-coupled" signal path,
etc.) — both real, both blocked today by a straight `canConnect` reject.

## 2. Design

### 2.1 New node: `adapt.controlToAudio` — "To Audio"

Mirrors `adapt.audioToControl`'s own shape exactly (`AudioToControlNode.h`) — small,
mechanical, allocation-free, `Adapters` category.

- **In:** `in` — `Control`, **polymorphic** on quantity
  (`PortPolymorphism::Quantity`, the exact mechanism `MapNode.h` already uses via
  `hasPolymorphicPorts()`/`resolveIncomingPort()`): resolves to whatever the
  connected source declares (`Unipolar` by default until resolved, `Bipolar` if the
  source is). Deliberately reuses `MapNode.h`'s own polymorphism code shape rather
  than inventing a second one.
- **Out:** `out` — `Audio`, mono (no `.channels` override needed — `Channels::Mono`
  is every other mono Audio output's own default, e.g. `OscillatorNode.h`'s `out`).
- **Behavior:** `Bipolar` input passes through directly, clamped to `[-1, 1]`;
  `Unipolar` input (0..1) expands to the full audio swing via `in * 2 - 1` before the
  same clamp — a 0..1 Control source has no natural "centre" in audio terms, so using
  only the input's full excursion (not just the positive half) is the more useful,
  less surprising default, matching how `adapt.map` already treats a resolved
  `Unipolar` source as "already 0..1, used as-is" only because ITS OWN destination
  range is arbitrary; here the destination range is always the audio ±1 full swing,
  so the symmetric expansion is the correct read of the same underlying convention.
  Clamping matches `adapt.audioToControl`'s own stated reasoning (a value that
  transiently exceeds the canonical range shouldn't silently break anything chained
  after it).

### 2.2 `canConnect` additions (`engine/src/graph/CanConnect.cpp`)

A new branch, symmetric to the existing `Audio → Control` one:

```cpp
if (from.type == SignalType::Control && to.type == SignalType::Audio)
{
    if (isRealQuantity (from.quantity))
    {
        // Real quantity -> Audio needs the SOURCE's own range collapsed to
        // Unipolar first (adapt.normalise, seeded from the SOURCE range —
        // the exact same node/seeding this file already uses for
        // Control(real) -> Control(Unipolar/Bipolar)), then the bridge.
        // Mirrors adapt.audioToControl -> adapt.map's own two-step shape,
        // just with the real-quantity step on the SOURCE side this time
        // instead of the destination side.
        AdapterStep first { "adapt.normalise", "in" };
        first.seedFromSourceRange = true;
        AdapterStep second { "adapt.controlToAudio", "in" };

        CanConnectResult result;
        result.outcome = ConnectionOutcome::NeedsAdapters;
        result.adapterChain = { first, second };
        result.reason = "A real-quantity modulation source into Audio needs Normalise, then To Audio";
        return result;
    }

    return needsAdapter ({ "adapt.controlToAudio", "in" },
                          "A modulation signal into an Audio-typed port needs To Audio");
}
```

No stereo complication on this side at all — `Control` ports have no `Channels`
concept to begin with (confirmed: nothing in this engine gives a Control port a
stereo pair), so there's no reject case to design here the way `Audio (stereo) →
Control` needed one. A mono `Audio` output into a port that actually wants stereo
still free-broadcasts for free (existing `Audio (mono) → Audio (stereo)` rule) —
nothing new needed for that either.

### 2.3 UI mirror (`ui/src/graph/canConnect.ts`)

Same rule, hand-mirrored — the project's standing "engine is the sole authority, UI
predicts" pattern.

### 2.4 Colour / cable rendering

No new rule needed, same reasoning as the forward bridge: colour already derives
purely from `(signalType, quantity)` per port. The cable feeding `in` stays whatever
colour its own quantity already is; the cable leaving `out` is Audio-pink, same as
any other Audio cable.

## 3. Non-goals (v1)

- **No `SignalType` merge** — unchanged from `AudioControlBridge.md` §1's reasoning.
- **No stereo `Audio` output from this bridge** — a Control source is inherently
  single-valued; stereo-izing it would mean inventing a stereo-spread behaviour this
  plan doesn't scope. Mono out, same as `adapt.audioToControl`'s own `in` is
  mono-only.
- **No change to `adapt.normalise`/`adapt.map`** — both reused exactly as they
  already exist.

## 4. Affected files

- **New:** `engine/include/bazalt/engine/nodes/ControlToAudioNode.h`.
- `engine/src/graph/CanConnect.cpp` — new heterogeneous-pair branch (§2.2).
- `engine/include/bazalt/engine/graph/ProofGraphs.h` — `factory.registerType`.
- `ui/src/graph/canConnect.ts` — mirrored rule.
- `wiki/NODES.md` — new `adapt.controlToAudio` catalog entry, `adapt.*` summary row
  bumped 10→11.
- `wiki/NODES.System.md` §4 — new matrix rows for `Control → Audio` (Unipolar/Bipolar
  direct, real-quantity two-step).
- **Tests:** `tests/CanConnectTests.cpp` (the new rule, both 1- and 2-step cases),
  `tests-plugin/ConnectWithAutoAdaptTests.cpp` (real end-to-end auto-insertion),
  a new node-level unit test file mirroring `AudioToControlNode`'s own tests.
