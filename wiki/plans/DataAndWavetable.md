# Data and Wavetable — roadmap stage 1

**Status:** In progress. Decisions in §1 were taken with the user while planning
(2026-10-08); everything else is the recommended shape, open to change batch by batch.
Built so far (batch 1a): one value family in the connection rules (1a.1), the bridge
adapters removed with schema v12 (1a.2), and the merges with schema v13 (1a.3) — Gain
into Multiply, Clamp into Clip (Low/High), Envelope Follower into Level, Crossfade and
Select into **Blend** (`math.blend`), the three history scopes into one **Scope**
(`view.scope`, styled by its source), and the new **Switch** (`logic.switch`). A port
that inherits both type and quantity never asks for a Map. 1a.4: one `Signal` type
(Boolean and Audio are quantities), channels on every Signal, colours by meaning (Data
teal, Notes green, no poly colour), the card stack for poly nodes, and categories with
ids following them (schema v14). Batch 1a is complete.
1b is built: node content (schema v15), the curve model and `data.curve`, one curve
player as **Oscillator** and **Envelope** (`source.oscillator`, `source.envelope`), the
Factory window with the curve editor, and schema v16 migrating osc.*, lfo.shape,
env.adsr and data.table onto them (the old nodes are removed). Known gaps from 1b:
phase-locked previews of a curve oscillator fold real samples instead of rendering
its shape; an envelope's single stage times can't be modulated (Time Scale scales
the whole curve); pulse-width modulation is gone with the fixed shapes; mipmap levels
are per octave, so a sweep changes brightness in octave steps. Still open: the
Swarm (Transient) and Trigger titles. Next: 1c (Wavetable).

Builds on `Factories.md` (content, editors, unwrap) and replaces its "docked editor
panel" with a full Factory window (§1, D10). Stage 2's engine work — one plan with an
instance axis instead of eight plans per allocator — is its own plan,
`InstanceAxis.md`; this plan only prepares the model and the visuals for it.

---

## 0. Origin

`wiki/ROADMAP.md` stage 1: turn anything into anything; Curve, Wavetable and EQ Curve
editors as Factories; a Spectrum view built on the EQ design; data types and
manipulation polished until anything sensible connects; and — explicitly — "one of the
last opportunities to think destructively": remove what exists only because analog-era
tools had it (To Mod / To Audio / From Bool, Gain next to Multiply, Clip next to Clamp,
LFO), clean up categories and names, add `README.md` and a public `NODE_GUIDE.md`. The
user's mockups: `EDIT CURVE` and `EDIT WAVETABLE` windows, and the node sheet with
Switch, Listen, Oscillator, Wavetable Frame, Curve, EQ Curve, Wavetable and Spectrum.

## 1. Decisions

**D1 — One numeric signal.** `Audio` and `Control` merge into one type, `Signal`.
They are already the same thing in the engine — the same per-sample float block
buffers; the adapters between them are an identity (To Audio) and a gain-and-clamp
(To Mod). The type only ever existed to say "these don't belong together", which
contradicts "anything sensible connects". What a signal *means* is carried by its
quantity (Hz, s, pitch, unipolar, …) plus a new quantity, **Audio** — a waveform meant to
be heard. Whether a port is computed per sample or per block stays a compiler concern
(today: always per sample; block-rate is an optimisation for later, not a type).

- **Boolean** becomes a value kind of `Signal` (0/1) — no From Bool, a gate wires
  straight into an amount. It keeps its own colour.
- **Event** and **Note** stay types: a moment and a note are not values over time.
  Note is the MIDI-effects layer ("Notes", like Ableton's MIDI effects).
- **Data** stays a type (immutable buffers: curves, tables, scales, material).
- **Spectral** stays reserved.

**D2 — Signals are bundles with two axes.** A cable carries copies of one signal along
up to two axes; nodes are written for one copy and the compiler runs them per copy (the
channel-lane mechanism, `StereoChannels.md`, generalised):

| Axis | What | Belongs to | Reduced by |
|---|---|---|---|
| Channels | stereo (later more) | the signal | Downmix |
| Instances | voices, swarm members, grains | the computation (an allocator) | Merge (was Voice Sum) |

Stage 1 defines the model and moves *all* signals onto the channel axis — controls
too, so a stereo modulation (an Oscillator with a per-side phase offset into a cutoff)
works with no duplicated chain, and costs nothing until something stereo feeds it.
Stage 2 (`InstanceAxis.md`) implements the instance axis in the engine.

**D3 — Colour means what a signal is, never whether it may connect.** Connectability
is shown while dragging (green / rejected), as today.

| Meaning | Colour | Change |
|---|---|---|
| Audio — meant to be heard | pink `#e0339e` | — |
| Value — a real quantity (Hz, s, dB) | white `#e8e8ea` | — |
| Modulation — unipolar / bipolar | orange `#FFB094` | — |
| Integer / index | yellow `#FFFF4D` | — |
| Boolean | blue `#7cc6f7` | — |
| Event | violet `#8c7fff` | — |
| Data — curves, tables | teal `#3ecfc0` | was brick `#c1665a`; teal per the mockups |
| Notes | green `#40FF69` | was teal; takes the green poly audio frees |
| Poly | — | no colour any more (D4) |

Pink into orange connects; the colours only tell you that you are modulating with a
waveform.

**D4 — Poly is drawn on the node, as a stack of cards.** A node that runs once per
instance is drawn with one or two offset outline copies behind it and a `×8` badge.
Local — moving the node anywhere changes nothing — and it says exactly what is true:
this node exists in several copies. Cables stay clean (colour = meaning, two strands =
stereo). The boundary reads by itself: Voice Sum has a stack on its input side and a
single card out. Nested instances (stage 2) show as `×8 × 20`. Rejected: a thick cable
(poly is not a property of a cable), a tinted region around the poly part (stretches
across the canvas when one node is moved away). Viewers inside a poly part should later
show every instance overlaid — the newest brightest (open, §6).

**D5 — One Oscillator, driven by a shape.** `osc.analog`, `osc.sine/saw/square/
triangle` and `lfo.shape` are replaced by one node. Its shape is a curve (a Factory
port); Sine, Saw, Square and Triangle are curve presets. Ports, following the mockup:
**Shape** (Data: a curve or a wavetable, with an inline preview and an edit button),
**Trigger** (restarts the phase), **Frequency**, **Amplitude**, **Phase**, **Loop**,
plus a tempo **Sync** so it covers what an LFO did. Pitch / Fine / Pulse Width go:
pitch arrives through Pitch to Frequency, pulse width is a shape.

- Every shape is played band-limited: when a curve is published the engine renders it
  into a mipmapped wavetable (one table per octave, partials above Nyquist dropped) and
  the oscillator reads the right level for its frequency — sub-audio it plays the curve
  exactly, so the same node is a clean LFO and an alias-free oscillator.
- The node is a phase source, so its phase-locked preview keeps working.

**D6 — Envelope is the same node in time mode.** A curve has a time base:
**Cycle** (x is one period, speed from Frequency — the Oscillator) or **Time** (x in
seconds, points have real times — the Envelope). In Time mode the curve carries named
markers (A, D, S, H, R, …); S holds while the gate is high; moving a marker rescales the
segments around it, and the whole envelope can be scaled in time. One engine node, two
Add-menu entries — Oscillator and Envelope — so both are easy to find. `env.adsr`
migrates to it (its four times become marker positions).

**D7 — Wavetable through the Shape port.** A Wavetable node owns the frames and has a
**Frame** port; its output plugs into the Oscillator's Shape, exactly as in the mockup.
A Data buffer cannot change per sample, so the cable carries the table *plus* the live
Frame as a companion signal; the Oscillator interpolates between frames itself (needed
anyway for band-limiting). To the user it is one cable. Wavetable Frame (a single static
frame out of a table) stays possible as a small extra node.

**D8 — Blend replaces Crossfade and Select.** `math.blend`: A, B, Amount, law (linear /
equal power), short click-free smoothing. Select was Blend with a boolean Amount. A
separate **Switch** (mockup) is for A/B comparison: N growable inputs, a **Next** event,
the active input shown, a short crossfade so it never clicks.

**D9 — Listen** is never saved with the patch (stage 0).

**D10 — The Factory window.** Editing a factory replaces the canvas with a full window
(mockups `EDIT CURVE`, `EDIT WAVETABLE`): title, back, undo/redo, import, save as
preset, preset menu. One window type shared by every editor; the node shows a compact
live preview and the edit button. This supersedes `Factories.md`'s docked panel.

## 2. The sweep — what goes, what merges

Every removal ships with a `PatchSerializer` migration (schema bump), so older patches
load into the new nodes.

| Today | Becomes | Migration |
|---|---|---|
| `adapt.audioToControl` (To Mod) | a plain wire | `depth ≠ 1` → a Multiply |
| `adapt.controlToAudio` (To Audio) | a plain wire | unipolar source → a Map 0…1 → −1…1 |
| `adapt.boolToControl` (From Bool) | a plain wire (0/1) | values ≠ 0/1 → a Map |
| `adapt.normalise` | Map (out 0…1) | parameters move |
| `util.unipolarToBipolar`, `util.bipolarToUnipolar` | Map presets | → Map |
| `mix.gain` (Gain) | `math.multiply` | `gain` → the second input's value |
| `mix.crossfade` | `math.blend` | ports move |
| `logic.select` | `math.blend` | condition → Amount |
| `math.clamp` | `shape.clip` with Low/High | clip gains low/high (default ±1); hard / soft / limiter stay |
| `osc.analog`, `osc.sine/saw/square/triangle`, `lfo.shape` | Oscillator | shape → curve preset |
| `env.adsr` | Envelope (Oscillator, Time mode) | times → markers |
| `data.table` (32-point bank) | Curve (content-backed) | points → content |
| `view.scope.control`, `view.scope.modulation`, `view.gate` | one **Scope** (range and style from the source's kind) | type id change |
| `env.follower` (Envelope Follower) | `analysis.level` (Level) | detection → mode (explicit, Follower defaulted to Peak), attack/release carry over, `out` → `level`. Level already did everything Follower did, plus stereo, dB and a better RMS (a 50 ms power window before the ballistics; Follower ran the ballistics on the power, which reads high with asymmetric times) |

Kept on purpose: Map, Threshold (Signal → Event, hysteresis), Sample & Hold, Gate
Length, Pitch ↔ Frequency (exact, not linear), Downmix / Stereo Split / Combine, Meter
(a view of Level), Random and Drift (until stage 2 replaces them with the stochastic
suite).

**Gaps to fill while sweeping:** a **Curve → Signal** reader is `data.lookup` today;
check that every Data producer has a viewer and a consumer, and that every value kind
has a way in and out (e.g. Event → Signal: Sample & Hold, Gate Length — enough?).
Decide per gap in batch 1a; no speculative nodes.

## 3. Categories and names

Categories describe what a node does, not which analog unit it imitates. Nature names
only where they are earned — "Life-cycle" for allocators, because instances are born,
live, release and die; physical terms (Excite, Resonators) are already literal. Never by
force: Curve stays Curve, Filter stays Filter.

| Category | Contents |
|---|---|
| IO | Audio In, Master Out, Note In, MIDI Control, Transport |
| Sources | Oscillator, Envelope, Noise, Dust |
| Excite | Impulse, Noise Burst, Pluck, Mallet |
| Resonators | Modal, String, Plate, Comb |
| Filters | SVF, Ladder, One-Pole, Allpass, Shelf, Peak, DC Block, EQ (1d) |
| Shape | Waveshaper, Fold, Rectify, Crush, Clip |
| Dynamics | Compressor, Gate |
| Spectrum | Frequency Shift (and later spectral tools) |
| Space | Reverb, Diffuser, Pan, Width |
| Channels | Downmix, Stereo Split, Stereo Combine |
| Time | Delay, Clock, Divide, Counter, Step Sequencer, Euclidean, Slew, Sample & Hold, Gate Length |
| Math | Add, Subtract, Multiply, Divide, Abs, Min/Max, Power, Round, Modulo, Blend, Map, Pitch ↔ Frequency |
| Logic | And, Or, Xor, Not, Edge, Latch, Toggle, Compare, Threshold, Event Group, Switch |
| Data | Curve, Wavetable, EQ Curve, Scale, Material, Lookup |
| Notes | the `note.*` family |
| Life-cycle | Voice, Swarm (Population), Swarm (Transient), Trigger, Merge |
| Random | Random, Drift (stage 2 rethinks this whole category) |
| Analysis | Level (absorbs Envelope Follower) |
| View | Listen, Scope, Spectrum, Meter, Cycle, Ripple, Tune, Count |
| Utility | Constant, Macro |
| Decorations | Reroute, Header, Comment, Box, Image |

Type ids follow the category (`life.voice`, `time.delay`, …; the never-rename rule is
suspended, so this is one migration).

**Titles — decided 2026-10-08:** technical names stay wherever they name what a node
really is (State-Variable Filter, Ladder, One-Pole, Slew, Waveshaper, Wavefolder,
Bitcrush, Step Sequencer, Euclidean, Stereo Split …) — the user: "U ničeho mi nevadí"
(technical names are fine everywhere). Titles change only where §2 merges or removes a
node (Oscillator, Envelope, Blend, Switch, Clip, Scope, Level, Curve) and in the
Life-cycle category. Voice Sum becomes **Merge** (decided 2026-10-08). Still open: Swarm (Transient) →
**Swarm** and Trigger → **Spawn**, or other names.

## 4. Batches

Each batch builds, passes its tests, and is committed before the next starts.

### 1a — Signal model, sweep, colours, names (engine + UI)
1. `SignalType::Signal` replaces Audio + Control; `Quantity::Audio`; Boolean as a value
   kind. `CanConnect` simplifies accordingly (most adapter cases disappear; Map,
   Pitch ↔ Frequency, Threshold, Downmix remain as auto-inserts).
2. Channel axis for every signal (controls included).
3. The §2 merges and removals with their migrations.
4. Colours (D3) in `tokens.ts` / `portUiKind.ts`; the card-stack poly look (D4), driven
   by the multiplicity the resolver already reports.
5. Categories and ids (§3), after the user signs off the name table.

Tests: every migration round-trips an old patch into identical audio; `CanConnect`
matrix; existing render tests (Init Patch, voice render, reverb, …) unchanged.

### 1b — Factory window, Curve, Oscillator / Envelope
1. `NodeContent` on `NodeInstance` + `Node::setContent()` (`Factories.md` §3, decision 2).
2. The Factory window (D10) and the shared editor kit: pan/zoom grid, points with
   tension, snapping, selection, undo, presets (save / import / menu).
3. Curve factory: points, per-segment tension, loop range; presets Sine, Saw, Square,
   Triangle, Ramp, Steps; markers for Time mode.
4. Oscillator (D5) and Envelope (D6) — one node; band-limited mipmaps; live content
   edits stream to the running node and crossfade (no clicks); commit on release.
5. Migrations from §2 for the oscillators, LFO, ADSR and `data.table`.

Tests: content round trip; band-limiting (aliasing below a threshold at high pitch,
measured); a Sine preset matches today's `osc.sine` within tolerance; envelope marker
timing; live edit without discontinuity.

### 1c — Wavetable
1. Wavetable content: N frames of one cycle each.
2. Editor (mockup): waveform preview strip, per-frame curve drawing, an FFT draw editor
   for harmonics (as in Serum 2), keyframe management (add, delete, reorder,
   interpolate between keyframes).
3. Frame companion signal on the Shape cable (D7); frame interpolation and per-frame
   mipmaps in the Oscillator.
4. Import of a single cycle or a frame set from audio waits for stage 3's audio data;
   drawing, the FFT editor and morphing between keyframes come first.

### 1d — EQ Curve and Spectrum
1. EQ Curve content: a list of bands (type, frequency, gain, Q); editor over a live
   spectrum in the Factory window.
2. An **EQ** processor in Filters that plays an EQ Curve (unwraps into Peak / Shelf /
   SVF — `Factories.md` §3, decision 5; bit-exact unwrap test).
3. **Spectrum** view (mockup, upper): a read-only node drawn with the EQ editor's
   frequency/gain grid, FFT size selectable, a cursor reading frequency and note
   (shares `pitchReadout.ts`). The scrolling spectrogram (mockup, lower) is stage 3.

### 1e — README and NODE_GUIDE
`README.md` (what Bazalt is, how to build, a link) and `NODE_GUIDE.md` — every node,
every port and prop. Generated from the node descriptors (title, ports, units, ranges,
descriptions) so it cannot drift; a test fails when the committed guide is out of date.
Descriptions move into the descriptors where they are missing.

## 5. Risks

- **The signal merge touches everything.** Mitigation: it is mostly type-system and UI
  (the buffers are already identical); migrations are tested by rendering old patches
  before and after.
- **Band-limited arbitrary curves** need mipmaps rebuilt on every content change; build
  them off the audio thread and swap atomically (the Data publisher already does this).
- **Editors are expensive UI.** One shared window and kit first; one factory at a time.
- **Names are taste.** The name table is reviewed as a whole before 1a renames
  anything.

## 6. Open questions

- Viewers inside a poly part showing every instance overlaid (D4) — here, or stage 2?
- Should a factory always have an Unwrap (`Factories.md` §3, decision 5)? Clear for EQ;
  less clear for Curve and Wavetable, whose primitives would be `data.lookup` + a
  phasor.
- The final hex of the new Data teal and Notes green, checked side by side on the canvas.
- Whether block-rate evaluation is ever needed for performance once everything is one
  signal type — measure before deciding.

## 7. Done means

An old patch with Gain, Crossfade, To Mod, an LFO and an ADSR loads into Multiply,
Blend, a wire, an Oscillator and an Envelope and sounds the same; a curve drawn in the
Factory window plays as an alias-free oscillator and as an LFO, and shapes an envelope
with markers; a wavetable with a modulated Frame plays through the same Oscillator; an
EQ Curve carves a sound and the Spectrum view shows the result; `NODE_GUIDE.md`
describes every node and is checked by a test.
