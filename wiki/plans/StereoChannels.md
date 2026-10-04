# Stereo Channels — width belongs to the cable, not the node

**Status:** Proposed, 2026-10-04. No code yet. Prerequisite for the basic sound
palette (`noise.colored`, `shape.*`, `space.diffuser`, `space.reverb` —
`Reverb.md`), so every new effect is born channel-aware instead of retrofitted.

---

## 0. Origin

Direct feedback, 2026-10-04: "Many times, I have found Bazalt to add Downmix node as
an adapter to places where I wouldnt expect them or want them."

## 1. The problem

`NODES.System.md` §9 made stereo one real cable, but only eight node types declare a
`Channels::Stereo` port (`space.pan`, `space.width`, `io.output`, `mix.downmix`,
`stereo.split`, `stereo.combine`, `resonator.modal`, `resonator.plate`). Every filter,
delay, gain, shaper and resonator input is `Mono`. So the moment a signal is stereo —
after `space.pan`, from a stereo resonator, from `io.audioIn` — the next effect gets a
silently auto-inserted `mix.downmix` (`CanConnect.cpp::connectAudio`,
`GraphEditController::connectWithAutoAdapt`), and the stereo image is gone without the
user ever choosing that. Two defects in one:

1. **Processors are mono-only** although nothing about a filter is mono.
2. **A lossy adapter is inserted silently.** Every other auto-adapter (`adapt.map`,
   `normalise`, `threshold`) converts *meaning*; downmix *throws away information*.

## 2. The model: channels resolved per port, like multiplicity

`Poly` already works this way: a node doesn't declare "I am polyphonic", the
`MultiplicityResolver` decides per port per compile, and the same node runs once per
voice. Channels get the same treatment:

- **`Channels::Inherited` becomes real** (today it's a declared-but-unused value,
  `PortDescriptor.h`). An Inherited Audio port's width is *resolved at compile time*
  from whatever is wired into it.
- **A node runs once per channel — a *lane* — exactly like it runs once per voice.**
  The node's author still writes mono DSP (`processSample(inputs, outputs)` on one
  channel); the compiler builds one node instance per lane, each with its own state
  (filter memory, delay line, oscillator phase). No node file needs to learn about
  stereo to become stereo.
- **Only nodes whose channels *interact* declare a fixed width**: `space.pan`,
  `space.width`, `stereo.split/combine`, `io.output`, the stereo resonators, and
  later `space.reverb`, `space.diffuser`, a stereo-linked compressor, a ping-pong
  delay. These keep `Channels::Stereo` and see both channels at once.

This is the "rethink the old practice" answer: a hardware rack has mono and stereo
*units*; a digital patch has signals of some width and processes that follow it.

### 2.1 Resolution rules

- A node is **lane-able** iff it has at least one `Inherited` Audio input and every
  one of its Audio outputs is `Inherited`. (A node with an Inherited input but a
  fixed-width or Control output — e.g. `env.follower` → Control — is *not*
  lane-able; see §3.)
- **Lane count** = the maximum resolved width over its Inherited inputs (1 if
  nothing wired). Its Inherited outputs then carry that width.
- **Per-lane input binding:** an Inherited input of width W feeds lane *k* from
  channel `min(k, W−1)` — a mono source broadcasts to every lane (same free
  broadcast rule as today). Non-Inherited inputs (Control, Event, Data, Note, and
  fixed-width Audio) are **shared**: every lane reads the same buffer. So one cutoff
  envelope drives both channels of a stereo filter, but a *stereo* modulator wired
  into an Inherited audio-rate mod input gives each channel its own modulation —
  stereo modulation falls out for free.
- **Feedback cycles:** widths only grow (1 → 2) and are capped, so a fixed-point
  iteration over the cycle converges in at most `maxChannels` passes.
- **Cap:** `maxChannels = 2` now. The resolver stores a *count*, not an enum, so
  multichannel/binaural intermediate formats (`SpatialScene.md` Stage 4) are a
  constant change later, not a redesign.

### 2.2 What becomes Inherited

Every audio-in → audio-out processor whose DSP is per-channel:

- filters: `filter.svf`, `.ladder`, `.onepole`, `.allpass`, `.peak`, `.shelf`,
  `util.dcBlock`
- `delay.line`, `util.gain`, `shape.clip`, `mix.crossfade`, `resonator.comb`,
  `resonator.string` (its excitation input)
- polymorphic math/routing (`math.add`, `math.multiply`, `util.reroute`) already
  inherit their source's shape through `resolveIncomingPort` — extend that to width
- `instance.sum` (already declares Inherited — today it's a no-op, it becomes real)
- viewers (`view.*`, `util.listen`) — they accept any width; display lane 0 now,
  a real stereo display (or Mid) is their own later polish

Every *new* palette node (`noise.colored` excepted — a source, §3) is authored
Inherited from day one.

## 3. What stays mono, and how stereo reaches it

Some inputs are genuinely one signal: an envelope follower's detector, an exciter's
trigger audio, `adapt.audioToControl`, a sidechain detector. For these:

- **No silent auto-insert.** `canConnect` stops returning `NeedsAdapters(mix.downmix)`
  for stereo→mono and returns a new outcome, `NeedsChoice`, carrying the options.
- **The UI asks once, at the drop:** a small inline chooser on the new cable —
  **Mid (L+R)** · **Left** · **Right** · **Side** — which inserts `mix.downmix` with
  that mode (it already has all four, `DownmixNode.h`). Esc cancels the connection.
  The choice is visible afterwards because the downmix node is visible.
- Stereo → Control (today a reject: the downmix + bridge + map chain exceeds the
  two-adapter ceiling) gets the same chooser, the chosen reduction counting as the
  user's own first step, not an auto-adapter.
- **Sources declare their own width.** A generator has no input to inherit from, so
  `noise.colored` (and later `osc.*` with unison spread) gets a structural `stereo`
  option producing two decorrelated channels; off by default.

## 4. Implementation sketch

### Engine
1. **`ChannelResolver`** — a compile pass after `MultiplicityResolver`, in topological
   order with the cycle fixed point: resolves each Audio port's width and each node's
   lane count. Result lives beside `MultiplicityResult`.
2. **`GraphCompiler`**: `channelCountOf()` reads the resolved width instead of the
   static descriptor (it's already the one place widths enter the flat-slot layout,
   `GraphCompiler.cpp:56`). For a lane-able node with lane count *L*, emit *L*
   `BlockStep`s, one node instance each, with the per-lane binding rules of §2.1; its
   Inherited outputs become *L* flat slots, so downstream sees an ordinary stereo port.
3. **State reuse** (M17 `previousPlan`) keys by `(nodeId, lane)`. A node going mono →
   stereo keeps lane 0's state and gets a fresh lane 1.
4. **Live edits**: `pendingParameterUpdates` and `LiveParameterEdits` address every
   lane of a node id (they already address every voice).
5. **Telemetry / phase sources / previews** read lane 0 (same as today's "first flat
   channel" rule, §9.4). Bypass copies per lane — stereo bypass becomes correct where
   today it drops the right channel.
6. **`canConnect`**: Inherited ↔ anything is `Ok` (already true); stereo → Mono Audio
   becomes `NeedsChoice`.
7. **Lanes inside per-sample feedback regions** use the same per-step expansion — the
   region path already iterates flat slots (§9.1).

### UI
- `canConnect.ts` mirrors the new outcome; the inline Mid/L/R/Side chooser on drop.
- **Make width visible**: a stereo cable draws as a doubled line (two thin strokes)
  so you can see where the image lives and where it ends. This is the cure for the
  original surprise as much as the engine change is.

### Patches
No shape change for existing patches: ports keep their ids; a Mono → Inherited port
accepts everything it accepted before. Downmix nodes that older auto-connects
inserted stay in the patch and keep working (deletable by hand). Schema version
untouched unless the `NeedsChoice` work changes a stored shape.

## 5. Tests

- `pan → svf → output`: left ≠ right preserved end to end; each lane's filter state
  independent (an impulse in L only leaves R silent).
- Mono graphs compile to exactly today's plan (one lane everywhere) — bit-exact
  render against the pre-change engine.
- Broadcast rules: mono modulator shared across lanes; stereo modulator per lane.
- Feedback cycle with a stereo source converges; per-sample region lanes.
- State reuse per lane across recompiles; live edit reaches both lanes.
- Block-size invariance for a stereo chain.
- `canConnect`: stereo → Mono returns `NeedsChoice`; nothing auto-inserts downmix.

## 6. Cost and risks

- **CPU doubles for the stereo part of a chain** — that's the real cost of stereo
  sound, paid only where the signal is actually stereo. Per voice, most chains stay
  mono until `instance.sum`/`space.pan`, so typical patches barely change.
- **Inside voices**: a stereo per-voice chain costs 2 lanes × 8 voices; fine, but it
  makes per-voice reverb (`Reverb.md`'s "quality" option) matter.
- Risk: lane expansion multiplies step counts and buffer counts in the compiler —
  keep it one code path (lanes of 1 for mono), not a parallel stereo path.

## 7. Done means

A stereo signal stays stereo through every per-channel processor without any node
gaining stereo-specific code; no adapter is ever inserted that loses information
without the user picking it; stereo cables are visibly stereo; all suites green.
