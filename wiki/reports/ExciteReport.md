# Excite + Resonate nodes — a port-by-port guide

A walkthrough of every node added in the PM Core batch's `excite.*`/
`resonator.*` families (2026-10-02), port by port, with concrete patch
examples and how to actually hear each one in the Standalone app. Companion
reference, not a replacement for `wiki/NODES.md` (the terse catalog entry) or
`wiki/NODES.System.md` (the architecture rules) — this is the "how do I
actually use this" document those two don't try to be.

**Before you start:** rebuild first. A real crash in `resonator.plate`'s own
`quality` knob was found and fixed right after this batch shipped (see
`wiki/NODES_Gaps.md`'s own write-up) — if your Standalone build predates that
fix, setting `quality` to "High" will hang the app. `cmake --build build
--config Debug` picks up the fix.

## The excite → resonate idea, in one picture

Every patch in this family has the same shape:

```
[an excite.* node]  --Audio-->  [a resonator.* node]  --Audio-->  [io.output]
     (a trigger/strike/pluck)        (the body that rings)
```

`excite.*` nodes produce a short, interesting transient — a click, a pluck, a
strike — on an `Event` trigger. `resonator.*` nodes take that transient as
their `excite` input and turn it into a sustained, pitched (or colored) tone
by making it ring. Neither half makes much sound alone: an excite node
without a resonator is just a short click; a resonator without excitation is
silent.

## How to place and wire nodes (if you haven't yet)

1. Right-click an empty spot on the canvas (or press Shift+A) to open the Add
   menu. Type a node's name (e.g. "pluck") or browse by category — excite
   nodes are under "Excite", resonators under "Resonators", `data.material`
   under "Data".
2. Click a result to place it.
3. Drag from an output port (right side of a node) to an input port (left
   side of another) to wire a cable. A rejected connection flashes red; a
   valid one commits.
4. Click a node to see its own parameter controls (knobs/sliders for
   Control ports, dropdowns for structural enum parameters like `geometry`
   or `quality`).
5. Wire your resonator's final output into `io.output`'s `in` (Master Out) to
   actually hear it.

## Getting a trigger with no MIDI keyboard needed

Every excite node needs something to fire its `trigger : Event` input.
The simplest, most reliable way to test any of them — no MIDI hardware,
no keyboard wiring — is `clock.pulse`:

- Place a `clock.pulse` node. Its `tick` output is `Event`-typed and fires
  repeatedly at a rate you set (its `clock.pulse.rate` knob, in Hz).
- Wire `clock.pulse`'s `tick` output straight into any excite node's
  `trigger` input.
- Set the rate low (e.g. 1–2 Hz) while you're listening for individual
  hits, or faster for a rhythmic pattern.

If you'd rather play it from a real MIDI keyboard: `io.noteIn`'s own voice
path (`instance.allocate.voice`) has a real `start` output, typed `Event`,
that fires once per new note-on — wire `instance.allocate.voice.start` into
an excite node's `trigger` exactly like `clock.pulse.tick`, and
`instance.allocate.voice.pitch` into a resonator's `pitch` input, and you
have a MIDI-playable instrument. The full recipe is in the "Complete patch
recipes" section at the end.

---

# `excite.impulse` — Impulse

**The simplest possible excitation.** A single sharp transient (or a short,
smooth pulse once widened) — nothing tonal of its own, just energy to knock
a resonator into motion.

| Port | Direction | Type | Range / default | What it does |
|---|---|---|---|---|
| `trigger` | in | Event | — | Fires the impulse. Any non-zero value this sample counts as "fired." |
| `excite.impulse.amplitude` | in | Control (Unipolar) | 0–1, default 1.0 | Peak loudness of the impulse. |
| `excite.impulse.width` | in | Control (ms) | 0–50ms, default 0 | `0` = an exact single-sample click (the purest, spectrally-flattest excitation). Above 0, widens into a smooth raised-cosine (Hann) bump instead of a harsh rectangular pulse. |
| `out` | out | Audio | — | The impulse itself. |

**Example:** `amplitude = 1.0`, `width = 0` gives you the sharpest possible
"knock" — feed it into `resonator.comb` or `resonator.modal` for a very
clicky, metallic attack. `width = 10ms` gives a softer, rounder thump —
try it into `resonator.plate` for something closer to a soft mallet tap.

**How to test it alone:** wire `excite.impulse.out` straight into
`io.output.in` with `clock.pulse` driving `trigger` at ~1Hz. You'll hear a
quiet, dry click (or soft thump at wider `width`) once a second — on its
own it's not very musical, which is expected; the interesting part happens
once you feed it into a resonator.

---

# `excite.pluck` — Pluck

**The one-node shortcut into `resonator.string`.** A pre-shaped plucked-string
excitation with pickup position and hardness already baked in, so you don't
have to hand-build the comb-notch spectrum yourself. Fixed, short (5ms)
burst — this node doesn't know what pitch it'll end up exciting; that's the
resonator's job.

| Port | Direction | Type | Range / default | What it does |
|---|---|---|---|---|
| `trigger` | in | Event | — | Fires the pluck. |
| `excite.pluck.position` | in | Control (Unipolar) | 0–1, default 0.3 | Where along an (imagined) string you "plucked." Changes the comb-notch coloring of the burst — different positions sound noticeably different (near 0 or 1 = brighter/thinner, near 0.5 = hollower, missing more of the even harmonics). |
| `excite.pluck.hardness` | in | Control (Unipolar) | 0–1, default 0.5 | `0` = a soft, dull pluck (fingertip/felt); `1` = a hard, bright pluck (fingernail/pick). Uses the same "Damping" direction every other node in this batch uses: higher = brighter. |
| `excite.pluck.amplitude` | in | Control (Unipolar) | 0–1, default 1.0 | Overall loudness of this pluck. |
| `out` | out | Audio | — | The shaped excitation burst. |

**A real design detail worth knowing:** `position`/`hardness`/`amplitude`
are all captured once, at the exact instant `trigger` fires — not tracked
live during the pluck's own brief decay. Turning the `hardness` knob while
a pluck is still ringing out won't change that pluck; it'll apply to the
*next* one. This matches how a real pluck works (you can't change your
fingernail mid-strike).

**Example:** `position = 0.1, hardness = 0.9` — a bright, near-the-bridge
electric-guitar-style pluck. `position = 0.5, hardness = 0.2` — a soft,
hollow, plucked-near-the-middle sound, more acoustic/nylon-string feeling.

**How to test it alone:** same as `excite.impulse` — wire straight to
`io.output.in`, drive `trigger` from `clock.pulse`. You'll hear a short,
textured click whose color changes as you move `position`/`hardness` — it
won't have a clear pitch yet (that only appears once you feed it into
`resonator.string`, see the recipe below).

---

# `excite.mallet` — Mallet / Collision

**Models a hammer or mallet strike, with real contact dynamics** — not a
fixed envelope. The contact itself can react to what it hits, if you wire
the `feedback` input.

| Port | Direction | Type | Range / default | What it does |
|---|---|---|---|---|
| `trigger` | in | Event | — | Starts the strike. |
| `excite.mallet.velocity` | in | Control (Unipolar) | 0–1, default 0.8 | How hard the mallet hits — directly scales the strike's loudness. |
| `excite.mallet.mass` | in | Control (Unipolar) | 0–1, default 0.3 | Heavier mallet = longer, duller contact. Lighter = shorter, brighter. |
| `excite.mallet.stiffness` | in | Control (Unipolar) | 0–1, default 0.5 | Stiffer mallet head = shorter, brighter contact (opposite direction from `mass`). |
| `feedback` | in | Audio (optional) | — | What the resonator is doing back at the contact point. Wire a resonator's own motion/output back here for real two-way coupling — see below. Leave unwired for a plain, uncoupled strike (reads silence, perfectly valid). |
| `out` | out | Audio | — | The contact force signal — a half-sine pulse shaped by `mass`/`stiffness`. |
| `contact` | out | Boolean | — | `true` for exactly as long as the mallet is physically touching the resonator, `false` the rest of the time. Useful to watch (wire it into a `view.glance`) or to drive something else (e.g. gate a separate effect only while the strike is happening). |

**The real coupling, if you want it:** wire `resonator.string`'s own
`motion` output into `excite.mallet`'s `feedback` input (and
`excite.mallet`'s own `out` into `resonator.string`'s `excite`). This closes
a real feedback loop — the string's own vibration measurably pushes back on
the mallet's effective strike velocity while contact lasts. This is the
first node pair in this whole project that forms a literal cycle in the
graph, and it's a real, tested, working one (see `wiki/MILESTONES.md`'s own
PM Core batch 4 entry for the full mechanism).

**How to test it alone:** wire `out` to `io.output.in`, `clock.pulse.tick`
to `trigger`. You'll hear a short, percussive "thip" each time it fires —
try sweeping `stiffness` from 0 to 1 while it's firing repeatedly and
you'll hear the click get noticeably shorter and brighter.

**How to test the real feedback coupling:** see "Struck/plucked string with
real mallet feedback" in the recipes section — this is the single most
interesting thing to actually go listen to out of this whole batch, since
it's new engine capability, not just a new sound.

---

# `resonator.comb` — Comb

**The cheapest real resonator**, and a self-contained building block (unlike
plain `delay.line`, which has no feedback of its own). Think of it as a
tunable metallic "ring" — not a full instrument body, more a building-block
effect or a cheap way to add pitched resonance to anything.

| Port | Direction | Type | Range / default | What it does |
|---|---|---|---|---|
| `in` | in | Audio | — | What to resonate. |
| `resonator.comb.frequency` | in | Control (Hz, audio-rate) | 20Hz–20kHz, default 220Hz | Sets the comb's own pitch (its delay length is `sampleRate / frequency`). |
| `resonator.comb.feedback` | in | Control (Bipolar) | −1 to 1, default 0.5 | How much energy recirculates each round trip — higher = longer ring, more resonant. Internally hard-limited to ±0.999 so it can never run away, no matter what you set here. |
| `resonator.comb.damping` | in | Control (Unipolar) | 0–1, default 0.5 | `0` = darkest/most damped (high frequencies die fastest), `1` = brightest/no extra damping. Same direction as `filter.onepole`'s own "Damping" knob, everywhere in this catalog. |
| `type` (structural, dropdown) | — | Enum | feedforward / **feedback** (default) | `feedback` is the real resonant mode (recirculates its own output — what you want for a ringing tone). `feedforward` only ever reads the raw input, never its own output — it can't ring or build up energy, just gives a fixed comb-filtered coloration in one pass (safe, always stable, good for subtle texturing rather than a sustained tone). |
| `out` | out | Audio | — | The resonated signal. |

**Example:** `frequency = 110Hz, feedback = 0.9, damping = 0.9, type =
feedback` on a noise or impulse input gives a clear, metallic, sustained
ring at 110Hz with lots of high-frequency shimmer. Drop `damping` to 0.2 for
a much duller, faster-decaying thud at the same pitch.

**How to test it:** `excite.impulse.out → resonator.comb.in →
io.output.in`, `clock.pulse.tick → excite.impulse.trigger`. You should hear
a clear pitched "boing" at whatever `frequency` you set, once per clock
tick. Try switching `type` to `feedforward` with the same settings — the
pitch disappears into a much subtler, un-sustained coloration; that
difference IS the point of the two modes.

---

# `data.material` — Material

Not an excite/resonator node itself, but `resonator.modal`'s **required**
input, so you need at least a basic understanding of it to use that node at
all. Describes a resonating object's set of overtones ("modes") from
physical-sounding parameters, instead of you typing in a frequency list by
hand.

| Port | Direction | Type | Range / default | What it does |
|---|---|---|---|---|
| `data.material.stiffness` | in | Control (Unipolar) | 0–1, default 0.3 | Combines with `inharmonicity` to stretch higher overtones sharp (a real stiff-string effect) — has no effect at all if `inharmonicity` is 0. |
| `data.material.density` | in | Control (Unipolar) | 0–1, default 0.3 | Combines with `damping` so higher overtones decay faster than the fundamental — has no effect if `damping` is 0 (both must be non-zero). |
| `data.material.damping` | in | Control (Unipolar) | 0–1, default 0.3 | See `density` above — they work together, not separately. |
| `data.material.size` | in | Control (Unipolar) | 0–1, default 0.5 | **Declared, not yet wired to anything** — a known, honest gap, not a silent bug. Changing it currently does nothing audible. |
| `data.material.inharmonicity` | in | Control (Unipolar) | 0–1, default 0.2 | The master on/off for overtone stretching — `0` keeps overtones perfectly harmonic regardless of `stiffness`. |
| `data.material.irregularity` | in | Control (Unipolar) | 0–1, default 0.0 | Randomly detunes each overtone a little (deterministically, from `seed` — same seed always gives the same detuning). `0` = perfectly regular. |
| `geometry` (structural, dropdown) | — | Enum | **string** (default) / bar / tube / membrane / plate / irregularSolid | The shape of overtone spacing. `string` = plain harmonics (1,2,3,4...). `tube` = odd harmonics only (hollow, clarinet-like). `bar` = a xylophone-bar-like spread (stretched, inharmonic by nature). `membrane` = drum-head-like (a real, non-integer overtone spacing). `plate` = even denser/stiffer than membrane. `irregularSolid` = fully irregular, bell/rock-like. |
| `modeCount` (structural) | — | Int | 1–64, default 32 | How many overtones to generate. |
| `preset` (structural, dropdown) | — | Enum | **wood** (default) / glass / metal / stone / ceramic / bone / ice / custom | Shapes how quickly higher overtones get quieter — `metal`/`glass` stay loud and bright at the top; `wood`/`stone` roll off faster and sound duller. |
| `seed` (structural) | — | Int | 0–999999, default 1 | Makes `irregularity`'s random detuning repeatable — same seed, same result, every time. |
| `data` | out | Data(modal-set) | — | The generated overtone set — wire this into `resonator.modal`'s own `modes` input. |

**Example:** `geometry = bar, preset = metal, modeCount = 24` gives you a
bright, clearly inharmonic, metallic-bell-leaning overtone set — a good
starting point for anything percussive and metallic once paired with
`resonator.modal`.

**How to "test" it on its own:** you can't hear `data.material` directly —
it only produces data, not audio. You'll hear its effect once it's wired
into `resonator.modal` (next section). If you want to *see* it's doing
something, changing `geometry`/`preset` and listening to the resulting
`resonator.modal` output is the real test.

---

# `resonator.modal` — Modal Bank

**The centre of the whole physical-modelling set.** A bank of up to 64
independent ringing resonators, one per overtone in whatever `data.material`
hands it, all driven by the same excitation and summed into a real stereo
output. This is the node that turns an abstract material description into
an actual, playable, pitched sound.

| Port | Direction | Type | Range / default | What it does |
|---|---|---|---|---|
| `excite` | in | Audio | — | What strikes/excites the body — any `excite.*` node's `out` goes here. |
| `modes` | in | **Data(modal-set), required** | — | Must come from `data.material` (there's no other producer of this data shape yet). The sound is silent (not an error, just silence) if nothing's wired here. |
| `pitch` | in | Control (absolute semitones) | 0–127, default 60 | The real fundamental pitch, MIDI-note-number style (60 = middle C). Every overtone scales off this. |
| `resonator.modal.decay` | in | Control (seconds) | 0.05–10s, default 1.5s | How long the fundamental rings before fading out (a 60dB decay time). |
| `resonator.modal.brightness` | in | Control (Unipolar) | 0–1, default 1.0 | `1` = overtones decay purely at whatever rate `data.material` already gave them (no extra damping). Turning this down mutes the top of the spectrum progressively faster over time, independent of `data.material`'s own character — the fundamental's own decay is never touched by this knob. |
| `resonator.modal.inharmonicity` | in | Control (Unipolar) | 0–1, default 0.0 | An EXTRA, live-tunable overtone stretch on top of whatever `data.material` already baked in — two separate knobs for two separate moments (the material's own fixed character vs. a patchable extra). |
| `resonator.modal.position` | in | Control (Unipolar) | 0–1, default 0.33 | Where you "strike" the body. **`0` is a real physical null — total silence** (exciting exactly at a vibration node cancels every mode), not a bug. Try values away from 0 and 1 — 0.3–0.4 is a good, characterful starting point. |
| `resonator.modal.spread` | in | Control (Unipolar) | 0–1, default 0.5 | How widely the overtones are spread across the stereo field. `0` = mono/centred, `1` = fully spread. Each overtone always lands at the same stereo spot on every run (deterministic, not random). |
| `maxModes` (structural) | — | Int | 1–64, default 64 | Caps how many of `data.material`'s overtones are actually used — lower it for a cheaper, simpler, "thinner" sound. |
| `out` | out | Audio (Stereo) | — | One real stereo cable — wire straight into `io.output.in`. |

**Example:** feed it `data.material` set to `geometry = membrane, preset =
wood`, `pitch = 48` (a low drum-ish note), `decay = 0.8s`, `position = 0.4` —
you get a believable low drum/tom-like thud. Raise `decay` to 4s and lower
`brightness` to 0.3 for something more like a struck gong instead — long
sustain, but the shimmer on top dies out much faster than the fundamental.

**How to test it:** `data.material.data → resonator.modal.modes`,
`excite.impulse.out → resonator.modal.excite` (or `excite.mallet`/
`excite.pluck`, all work), `resonator.modal.out → io.output.in`,
`clock.pulse.tick → excite.impulse.trigger`. You should hear a clear,
pitched, decaying tone every time the clock fires. Try setting `position`
to exactly `0` — confirm it goes completely silent (that's correct, not
broken); then move it back up and confirm sound returns.

---

# `resonator.string` — String

**The playable version of Karplus-Strong** — a real digital waveguide (a
circular delay line with a damping+dispersion filter in its loop), the
flagship node of this whole batch. This is what makes an actual, tunable,
pluckable string.

| Port | Direction | Type | Range / default | What it does |
|---|---|---|---|---|
| `excite` | in | Audio | — | What plucks/strikes the string — `excite.pluck` is the natural match, but any excite node (or `excite.mallet`, for a struck/hammered string) works. |
| `pitch` | in | Control (absolute semitones) | 0–127, default 60 | The string's real sounding pitch, same MIDI-note convention as `resonator.modal`. |
| `resonator.string.decay` | in | Control (seconds) | 0.05–30s, default 3s | How long the string rings before fading (a 60dB decay time). |
| `resonator.string.damping` | in | Control (Unipolar) | 0–1, default 0.5 | `0` = darkest (duller, faster-decaying highs), `1` = brightest (no extra high-end damping). |
| `resonator.string.stiffness` | in | Control (Unipolar) | 0–1, default 0.1 | Adds a real, audible inharmonic "detuning" to the string's overtones — real strings (especially wound/thick ones) aren't perfectly harmonic; this is why a piano's bass strings sound subtly "stretched." Keep it low (0–0.2) for a believable nylon/gut string, higher for a stiff, piano-wire or toy-piano character. |
| `resonator.string.position` | in | Control (Unipolar) | 0–1, default 0.15 | A real pickup-position effect — where along the string's own length you "listen" from. Changes the tone color noticeably (near 0 = brighter, thinner; near 0.5 = hollower). |
| `resonator.string.release` | in | **Boolean**, not a knob | default `true` (held) | `true`/unconnected = the string rings out normally at its own `decay` rate. Setting it `false` ramps a palm-mute/finger-lift effect in over ~15ms, killing the string much faster than `decay` alone would — a real design choice, not a fixed knob. |
| `motion` | out | Audio | — | The string's own live vibration at the point you're exciting it — wire this into `excite.mallet`'s `feedback` for real two-way coupling (see the recipe below). Not needed if you're not building a coupled patch. |
| `out` | out | Audio | — | The string's actual sound. |

**Example:** `pitch = 69` (A4, 440Hz), `decay = 4s`, `damping = 0.7`,
`stiffness = 0.05`, `position = 0.12` — a clean, bright, naturally-decaying
plucked string. Raise `stiffness` to 0.6 and you'll hear the overtones
noticeably detune/stretch — more toy-piano or music-box than guitar.

**How to test `release`:** wire a `logic.boolean` (or any Boolean source)
into `resonator.string.release` instead of leaving it unwired. Trigger a
pluck, let it ring a moment, then flip the boolean to `false` — you should
hear it cut off noticeably faster than it would on its own `decay`. Flip
it back to `true` before the next pluck to hear the normal, full ring
again.

**How to test it overall:** this is the main event — see "Playable
plucked string (the flagship patch)" below.

---

# `resonator.plate` — Plate / Membrane

**The hardest resonator in the catalog, and the last one in this batch.**
A real, honestly-documented simplification — not a literal 2D mesh
simulation (a project of its own), but a bank of resonators tuned to a
believable plate/membrane-like overtone spread, generated internally (no
`data.material` needed — this one is self-contained).

| Port | Direction | Type | Range / default | What it does |
|---|---|---|---|---|
| `excite` | in | Audio | — | What strikes it — `excite.mallet` is the natural match for this one (drum heads and plates are usually struck, not plucked). |
| `resonator.plate.size` | in | Control (Unipolar) | 0–1, default 0.5 | Smaller (toward 0) rings higher-pitched; bigger (toward 1) rings lower. Unlike `resonator.modal`, there's no separate `pitch` input — `size`/`tension` together set the actual pitch. |
| `resonator.plate.tension` | in | Control (Unipolar) | 0–1, default 0.5 | Higher tension rings higher-pitched (like tightening a drumhead). |
| `resonator.plate.decay` | in | Control (seconds) | 0.05–30s, default 2s | Same 60dB decay-time meaning as every other resonator here. |
| `resonator.plate.damping` | in | Control (Unipolar) | 0–1, default 1.0 | Same direction as everywhere else: `0` = darkest/heaviest extra damping on the overtones, `1` = brightest/none. |
| `resonator.plate.positionX` / `positionY` | in | Control (Unipolar) × 2 | 0–1, defaults 0.4 / 0.6 | Where on the 2D surface you strike it — two independent axes (unlike `resonator.modal`'s single `position`), since a plate is a surface, not a line. |
| `quality` (structural, dropdown) | — | Enum | low / **medium** (default) / high | How many internal resonators are used: low = 8, medium = 16, high = 32. Higher quality sounds richer/denser but costs more CPU. **This is the knob that used to crash — make sure you've rebuilt since the fix before trying it.** |
| `out` | out | Audio (Stereo) | — | One real stereo cable, panned automatically (no `spread` knob on this node — it's always on). |

**Example:** `size = 0.2, tension = 0.7` for a small, tight, higher-pitched
drum/plate sound; `size = 0.9, tension = 0.2` for something much bigger and
lower, closer to a gong.

**How to test it (including the now-fixed knob):** `excite.mallet.out →
resonator.plate.excite`, `resonator.plate.out → io.output.in`,
`clock.pulse.tick → excite.mallet.trigger`. Confirm you hear a struck,
plate-like tone on each hit. Then open `resonator.plate`'s own dropdown and
change `quality` from Medium to High (and back to Low) while it's playing —
this is exactly the gesture that used to hang the app; it should now just
work, with High sounding noticeably richer/denser than Low.

---

# Complete patch recipes

### 1. Playable plucked string (the flagship patch)

```
clock.pulse.tick ───────────────► excite.pluck.trigger
                                   excite.pluck.out ──► resonator.string.excite
                                   resonator.string.out ──► io.output.in
```

Set `clock.pulse.rate` to ~1Hz so you can hear individual plucks clearly.
Try changing `resonator.string.pitch` (e.g. 48, 60, 72) between plucks to
hear different notes; try `excite.pluck.position`/`hardness` for different
pluck characters at the same pitch.

### 2. Struck/plucked string with real mallet feedback (the new engine capability)

```
clock.pulse.tick ─────────────────────► excite.mallet.trigger
                                         excite.mallet.out ──► resonator.string.excite
                       ┌─────────────────────────────────────┘
                       │
         resonator.string.motion ──► excite.mallet.feedback   (closes the loop)
                                         resonator.string.out ──► io.output.in
```

This is the literal cycle this whole batch proved the engine can compile
and run. Compare how it sounds against recipe 1 (pluck, no feedback) —
the mallet's own contact genuinely reacts to the string's motion here,
which plucking never does.

### 3. A struck metal bell (data.material + resonator.modal)

```
data.material (geometry=bar, preset=metal, modeCount=24) ──► resonator.modal.modes
clock.pulse.tick ──► excite.mallet.trigger
                      excite.mallet.out ──► resonator.modal.excite
                      resonator.modal.out ──► io.output.in
```

Set `resonator.modal.pitch` to taste, `decay` around 3–5s, `position`
around 0.3. Try swapping `preset` to `wood` or `glass` with everything
else the same and listen to how differently the SAME strike/pitch/decay
settings ring.

### 4. A metallic comb drone (no pitch tracking, just a resonant color)

```
clock.pulse.tick ──► excite.impulse.trigger
                      excite.impulse.out ──► resonator.comb.in
                      resonator.comb.out ──► io.output.in
```

Set `resonator.comb.feedback` near 0.95 and `damping` near 0.9 for a long,
ringing, bell-like comb tone at whatever `resonator.comb.frequency` you
set — the cheapest way to get a pitched resonance in this whole batch.

---

## Automated test coverage, if you want to see it verified another way

Every node and example above has real, automated Catch2 coverage in
`tests/PMCoreNodesTests.cpp` (48 cases across the whole batch, plus 2 more
regression cases for the `resonator.plate.quality` crash fix). Run the test
binary directly with Catch2's own tag filter —
`./build/tests/Debug/EngineTests.exe "[PMCore]"` — (plain `ctest -R` won't
match here: it filters by test NAME, and `[PMCore]` is a Catch2 TAG, not
part of any test's name) if you want to see the exact numeric behavior each
port is held to, including the real compiled-graph test that proves the
`excite.mallet`↔`resonator.string` feedback cycle actually plays.
