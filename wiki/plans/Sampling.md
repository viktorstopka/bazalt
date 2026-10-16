# Sampling — audio as a material, not just a sound

**Status:** Proposed, 2026-10-16. Stage 3 of `wiki/ROADMAP.md` (3a, then 3b), planned with
the user after 2b landed. No code exists yet. `Wavesets.md` is part of this stage and
builds on the live buffer defined here. `BakedPhysics.md`'s event track turns into a
special case of the sample defined here (§4.5).

---

## 0. Origin

The user, 2026-10-16, asked for a plan for the sampling stage that also covers:

- turning a sample into a wavetable;
- using samples to *control* things, not only to be heard;
- **baking** a computed part of a patch into a sample, so that what was unthinkable in
  real time becomes possible (example given: 100 entities, each with its own reverb);
- doing all of this in a way that is **not a hidden pro feature** but friendly to beginners;
- where a convolver fits, since an IR is already "audio used as data", and how to make
  a convolver more powerful than the usual one;
- capturing other things from the real world the way an IR captures a room: materials,
  resonators, and more.

This plan answers all of them. In short: **one sample type that can hold any signal, and
two verbs — Capture and Bake.** The convolver, the sampler, the wavetable importer,
material capture and the baked physics are all consumers or producers of that one type.

## 1. The idea: a sample is a recorded cable

Everything in Bazalt that moves on a Signal cable already has a meaning (its `Quantity`):
audio, pitch, frequency, gain, time, a gate, an event. A sample in most software is "audio
on disk". Here, **a sample is a recording of a cable**, with that cable's meaning kept:

- A recorded guitar is a sample whose channels are `Audio`.
- A recorded pitch curve is a sample whose channel is `Pitch`. It plugs straight into a
  pitch port — no Map, because it still knows it is a pitch.
- A recorded gate or onset track is a sample with a `Boolean` or `Event` channel. Each
  non-zero value is an event, and its value is the event's strength.
- A recorded swarm is a sample with one channel per member.

Two consequences follow:

1. **"Audio used as data" stops being a special case.** An IR, a control recording, a
   wavetable source and a baked simulation are all the same object, shown the same way,
   stored the same way, edited in the same factory editor.
2. **The beginner never has to learn the concept.** They record what they see on a cable,
   and they plug the recording wherever that kind of cable fits. The cable colour tells
   them where it goes, exactly as it does today.

### `Data(sample)` (`DataTag::Sample`, already reserved in `Data.h`)

| Field | Meaning |
|---|---|
| frames × channels | interleaved `float`, any length, any channel count (1–64) |
| sample rate | of the recording; players resample |
| per-channel `Quantity` | what each channel means (Audio, Pitch, Event, …), plus an optional label ("x", "member 7") |
| markers | loop start/end, slice points, cue points (seconds) |
| axes (optional, §4.3) | for a baked lattice: the parameter each channel group stands for, and its steps |
| identity | content hash (for the library and for caching), display name |

`DataTag::ImpulseResponse` becomes an alias for a sample used as an IR, not a separate
format: any sample can be an IR.

## 2. The building blocks (3a)

### 2.1 Getting sounds in

- **Drop a file on the canvas** (WAV, AIFF, FLAC, MP3/OGG through JUCE) → a `data.sample`
  node appears there, already showing its waveform. This is the same gesture as dropping
  an image (Decorations), so it is learnt once.
- **Drop a file on a port** that takes a sample (a Sampler's Sample input, a Convolver's
  IR input) → the node is created and wired in one go.
- Decoding, resampling of the overview and hashing happen on a worker thread. The audio
  thread only ever sees a finished, immutable `DataBuffer` (rule 2).

### 2.2 Where samples live

The patch asset cap (5 MB, for images) is far too small for audio, and embedding every
sample in every DAW session is wasteful. So:

- **A sample library** in `%APPDATA%\Bazalt\Samples\`. Files are stored once, by content
  hash, as FLAC. A patch refers to a sample by its hash plus its original file name.
- **Save with samples** (the default for "Export patch…") embeds the referenced samples as
  FLAC inside the `.bazalt`, so a patch can be shared as one file. Opening such a patch adds
  its samples to the library.
- **A missing sample** never breaks the patch. The node shows "missing — find…" and keeps
  its settings. Relinking searches the library by hash first, then by name.
- **DAW state** stores references, not audio. If the library copy is gone, the plugin asks
  for it, the same way samplers do.

### 2.3 `data.sample` — Sample (producer)

- **Content:** the sample reference, plus a non-destructive region (start, end, loop,
  fades, gain, reverse, normalise). Edits never touch the file.
- **Out:** `sample` (`Data(sample)`), `length` (seconds), `pitch` (detected root, if any).
- Double-click opens the **Sample factory**: a waveform and spectrogram, region and loop
  handles, slice markers (manual, or "slice at transients" using `analysis.onset`), and the
  operations of §3 and §5.6 (Make wavetable, Use as IR, Fit modes…).

### 2.4 `source.sampler` — Sampler (player)

One player for every use. Under a Population it becomes polyphonic; with many short
members it becomes granular — no separate granular node (3b proves it).

- **In:**
  - `sample`;
  - `start` (Event: play from Position);
  - `position` (where to start, or, with Rate 0, where to *be*: scrubbing — the granular
    read head);
  - `rate` (1 = original speed) or `pitch` (relative to the sample's root);
  - `slice` (Count: plays slice N — drum kits and chopping);
  - `channel` (Count: for many-channel samples, which channel; fractional values blend
    two neighbours — this is what reads baked lattices, §4.3).
- **Settings:** loop (off / forward / ping-pong / sustain loop while held), direction,
  interpolation (linear / cubic / sinc for highest quality), end action (stop / hold).
- **Out:** `out` (the channels, with their quantity — a pitch recording plays out a Pitch),
  `position`, `end` (Event), `gate` (high while playing).
- **Aliasing:** a sample played far above its rate reads from a band-limited mipmap, the
  same idea curves and wavetables already use.

### 2.5 `data.record` — Record (live buffer)

The live half: Bazalt as a live effect on guitar needs to hold the last seconds of what
is happening.

- **In:** `in` (any Signal; the quantity is inherited and kept in the recording),
  `record` (gate or toggle), `clear` (Event), `feedback` (overdub amount).
- **Settings:** length (up to e.g. 60 s), mode (ring: always the last N seconds / one-shot:
  stop when full / loop: overdub like a looper).
- **Out:** `sample` — a **live** `Data(sample)` that readers (Sampler, Wavesets, Convolver)
  can play while it is being written. Also `position` (the write head).
- **Freeze** (button, or a `freeze` Event) copies the buffer, off the audio thread, into a
  normal library sample and turns it into a `data.sample` node next to it.

**Engine note — the one new mechanism.** Data is immutable today. A live buffer is the one
exception, so it gets its own small contract: a ring allocated in `prepare()`, one writer
(the Record node, on the audio thread) and any number of readers. A reader may read any
sample that is not in the region currently being written, which is exactly one block ahead
of the write head; the published view carries the write head. No locks, no allocation, no
copying per block. `Wavesets.md`'s capture ring is this same buffer.

### 2.6 Seeing sound

- **`view.spectrogram`** (Spectrogram): a scrolling STFT of the input, on the shared log
  axis (`logSpectrum.ts`). It has the same cursor read-out as Spectrum (frequency, note).
  Options: FFT size, range, freeze. The ROADMAP has asked for it since 2026-10-08.
- **Waveform previews:** any sample cable can preview its waveform on the node, and a
  live buffer shows its write head moving.

## 3. Sample → wavetable (side task)

**Decision: in the Wavetable factory, not as a node** — with one live exception (§3.3).
Extracting a wavetable is analysis with choices (which part, how many frames, how to align
them), and the result should become ordinary, editable keyframes. A node would hide all
of that behind knobs and produce something you can't touch.

### 3.1 Wavetable factory → "From sample…"

Drop a file or pick a `data.sample`, choose a region, then a method:

| Method | For | How |
|---|---|---|
| **Cycles** (default for pitched sounds) | voices, synths, instruments | Find the fundamental (YIN, `analysis.pitch`'s core). Cut N single cycles spread evenly over the region, at matching zero crossings. Resample each cycle to the table size, phase-align them (rotate each so its fundamental's phase matches the first) and normalise. Result: N **curve** keyframes. |
| **Harmonics** | smooth morphs, no phase artefacts | At N points, measure each harmonic's amplitude (and phase, optionally) with a pitch-synchronous FFT. Result: N **harmonics** keyframes — they morph without phase cancellation. |
| **Texture** (unpitched sounds) | noise, breath, rooms | Take the spectrum at N points and give it a fixed random phase per bin, so you get one cycle that keeps the colour of the sound. Interesting rather than faithful. |

- **Frames:** 4 / 8 / 16 / 32 / 64 / 256, placed along the sample's time. The table's
  **Frame** then *is* time in the original sound: a Frame sweep with an envelope replays
  the sound's evolution at any pitch, without it speeding up — resynthesis on a plain
  oscillator.
- **Preview** plays the result at the detected pitch beside the original (A/B).
- After import it's an ordinary wavetable: every keyframe can be edited, deleted, morphed.

### 3.2 Related imports in the same dialog

- **Single cycle file** (the common "AKWF" format): one keyframe.
- **Serum-style multi-cycle WAV** (2048-sample frames back to back): each frame becomes a
  keyframe. This gives users their existing wavetable libraries for free.

### 3.3 Live: `data.wavetable` with a Sample input

For playing live, a wavetable can follow a live buffer: a `source` input
(`Data(sample)`, typically from Record) and a `capture` Event. On each capture, a worker
thread runs the Harmonics method over the last moment of the buffer and publishes a new
table. **Example:** "the synth takes on my guitar's timbre whenever I hit the footswitch."
This uses the same extraction code as §3.1, run in the background, never on the audio
thread.

## 4. Baking — when a sample holds a computation

### 4.1 What baking is

**Baking** renders part of a patch faster than real time, on a worker thread, into a
sample, and puts a Sampler in its place. Expensive work done once, cheap work done live.
Unlike a DAW's "freeze track", it works on any group of nodes, keeps the original for
re-baking, and (§4.3) can leave chosen knobs alive.

The engine already has what it needs: a sub-graph compiles into its own `ExecutionPlan`
(GraphCompiler), and `tools/render-cli` already renders headless. Baking means compiling
the selection on its own, feeding its inputs (constants, or captured samples), running it
for the requested time on a worker thread, and writing the outputs into a sample.
Determinism — fixed seeds, `(seed, ordinal)` randoms — makes a bake reproducible.

### 4.2 What can be baked (and what can't)

A part can be baked over **time** when everything feeding it is known in advance: constants,
seeds, a fixed note, a captured sample. If a live input reaches it (a knob still moving, the
guitar), it can't be baked over time alone. The editor says this in plain words
("Cutoff changes live, so this part can't be baked yet — make Cutoff an axis, or keep it live").

### 4.3 Axes: baking a knob

The step that makes baking more than freezing: **a knob that should stay alive becomes an
axis.** The bake runs once per step of that knob and stores each run as a channel group. The
Sampler's `channel` input (fractional, blending neighbours) then plays the right one, so
turning the knob still works — on a recording.

- **One axis** (e.g. velocity × 8 steps): an 8-channel sample. Velocity picks and blends.
  This is a multisample, made automatically from a synth patch.
- **Two axes** (pitch × velocity, 12 × 8): a lattice. The Sampler blends four neighbours.
- **Cost:** memory = length × steps. The bake dialog shows the size before it runs
  ("12 × 8 × 2 s stereo = 37 MB").
- Things a recording can't fake (a filter swept fast) stay live: the dialog suggests keeping
  the cheap part live and baking only the expensive part (the 64-mode resonator, not the
  envelope before it).

### 4.4 Scenarios — what used to be unthinkable in real time

**A. A hundred entities, each in its own place in a reverberant room.** The user's own
example. One convolution reverb per entity is 100 convolutions: impossible. But a room is
linear, and each entity's reverb is just the room's response *from that position*. So:

1. **Bake the room** (offline): compute or capture IRs on a grid of positions — a
   multi-channel sample with a position axis.
2. **Decompose** (offline): the IRs of one room are very alike. Find the K basis IRs
   (K ≈ 4–8) whose weighted mixes reproduce every grid IR closely (a principal-component
   fit). Store the K bases, plus each grid point's K weights (a small table).
3. **Play** (live): each Population member looks up its K weights from its position
   (interpolated) and sends its dry sound into K shared buses with those weights. Each bus
   runs **one** convolution with its basis IR.

Total cost: K convolutions, whatever the entity count — 100 reverbs at the cost of 6. For
the user it is one node, Room, with a `position` input under a Population, and a room
chosen in the factory (preset, computed from a box shape, or captured, §6).
This is the same principle as ambisonic or spherical-harmonic rendering, applied to the
reverb itself.

**B. A swarm's motion.** Boids, flocking, orbiting particles or chaos for 100 members cost
interactions that grow with the square of the count. Bake the motion instead: simulate
offline and store one channel per member and per coordinate (x, y, z = 300 channels at a
low control rate, e.g. 200 Hz — small). Live, each member's Sampler reads its own channels
by `index` (`channel` = index × 3 + coordinate) and moves its sound. The motion is free;
only the sound is computed live. Variation comes from a different seed or a different
start offset per performance.

**C. Expensive per-note synthesis.** A physical model with 64 modes and a nonlinear string,
too heavy for 16 voices of unison: bake it over pitch × velocity (§4.3) into a lattice and
play it with Samplers. Keep the cheap live parts (filter, amp envelope, effects) live.
Result: 16 voices of something that used to fit 2.

**D. Heavy maths as a table (tabulation).** Some shapers and circuit models are a function
of the input value, not of time (a diode clipper, a tube stage). Bake **over the input value**
instead of time: the sample's frames are the output for inputs from −1 to 1. Live, a Lookup
(`data.lookup`) or Waveshaper reads it — a transfer curve. With a second axis (drive, or the
circuit's state) it becomes a 2D table. This is the classic "baked LUT" from real-time
graphics, applied to audio.

**E. Spectral bakes.** An STFT of a sound stored as a sample (channels = frequency bins,
frames = time) is a spectral recording. Played back through a resynthesis node, it gives
freeze, time-stretch without pitch change, and smooth morphs between two sounds, bin by
bin. It uses the `Spectral` signal type that already exists.

**F. Performance capture.** Record the *analysis* of a performance, not its audio: a
guitarist's loudness, pitch and brightness (`analysis.level/pitch`, brightness from a
filter) on three channels. It is a "MIDI with timbre": play it into any synth later, or
loop it as an expressive modulation source. It is recorded with the same Record node, on
the analysis cables.

**G. Gesture loops.** Record a macro or a modulation's movement for a few seconds and loop
it — an LFO drawn by hand, in real time. It is a control sample, which can also be
converted to a `data.curve` to edit it.

**H. Baked physics (BakedPhysics.md).** A simulation's impacts become a sample with Event
channels (strength as the value) plus control channels (contact position, speed). **No
separate `Data(event-track)` type is needed**: the event player is a Sampler playing Event
channels. One concept fewer.

### 4.5 Making it beginner-friendly

Not a hidden pro feature:

- **Two verbs, everywhere the same.**
  - **Capture** = record something live (from the world or from a cable).
  - **Bake** = render part of the patch ahead of time.
  - Both create the same thing: a Sample node with a thumbnail, wired where its source was.
- **Capture this.** Right-click any output port → *Capture* (or Alt-click the port's
  preview). Recording starts, and a stop button sits on the cable. The result is a Sample
  node wired in place, with an **A/B switch** (live ↔ captured) to compare.
- **Bake this.** Select nodes → *Bake*.
  - The dialog asks in plain words: *How long?* *Which knobs should stay alive?* (ticks;
    each becomes an axis with a step count).
  - It shows the result before committing: *saves ~40 % CPU, costs 18 MB*.
  - A baked group shows as a frame with a "Baked" badge. Double-click un-bakes it, and
    editing anything inside asks "re-bake?".
- **Bake suggestions.** The editor already knows each step's cost. When a heavy part has
  no live input, a small hint appears on it: *"This part never changes — bake it to save
  30 % CPU."* Nothing happens without a click.
- **Meaning travels with the data.** A captured pitch plugs into a pitch port, a captured
  gate into a gate port. Colours and Maps work as they do on cables today.
- **Presets that teach.** Factory patches show each idea once: *Capture: Guitar Loop*,
  *Bake: 100 Fireflies in a Hall*, *Capture: My Kitchen Table* (material capture, §6).

## 5. The Convolver is part of this stage

### 5.1 Why it belongs here

A convolver *is* this system's oldest instance: an IR is a captured response, played as
data rather than heard. So it is not "add a convolution reverb". It is the **response
player**: a `Data(sample)` consumer like the Sampler. Anything that is a sample can be its
response — a room, a guitar body, a bell, a captured drive's linear part, a drum hit,
a baked chunk of the patch. Moving it from stage 4 to stage 3 (ROADMAP decision of
2026-10-08) fits this exactly.

### 5.2 `space.convolve` — Convolve

- **In:** `in` (audio, or any Signal — convolving a control signal with a gesture
  "kernel" is a custom smoothing); `response` (`Data(sample)`); `mix`; `gain`.
- **Engine:** uniform partitioned FFT convolution with a short first partition, for low
  latency. It is preallocated in `prepare()`, and a response change crossfades from the old
  to the new, never clicking. A long response's tail partitions may run on a worker thread
  with a fixed look-ahead (a later optimisation, measured first).
- **Channels:** mono, stereo, and true stereo (a 4-channel response: L→L, L→R, R→L, R→R).

### 5.3 Making it more powerful than a usual convolver

Each item below reshapes the response without re-recording it, and live where possible:

1. **Shape the response live** — start, end, pre-delay, fade-in/fade-out, reverse, and a
   **decay** knob that multiplies the tail by an envelope (shorter or longer rooms from one
   capture). Rebuilt in the background on change, crossfaded in.
2. **Stretch** — resample the response (bigger/smaller room, larger/smaller body), with
   pitch kept (spectral stretch) or with it (tape-style).
3. **EQ the response** — a `data.eqCurve` input applied to the response itself (a darker
   room without an EQ after it).
4. **Morph two responses** — two response inputs and a Morph amount. Spectral morph steps
   are pre-computed in the background (a baked axis, §4.3); live, it crossfades the two
   nearest.
5. **Dynamic convolution** — several responses captured at different input levels (a
   nonlinear device, a drum shell hit softly and hard); the input's level picks and blends
   them. This is the approach of the dynamic-convolution hardware of the 2000s, done with
   samples.
6. **Positional responses** — the K-basis method of §4.4 A, so a Population of 100 members
   costs K convolutions.
7. **Response → modes** — fit a response with damped sinusoids (a matrix-pencil or ESPRIT
   fit, offline) and output a `Data(modal-set)` for `resonator.modal`. A captured bell then
   becomes a playable resonator that can be struck anywhere, re-pitched and damped, which
   no convolution can do.
8. **Freeze** — hold the current reverberant tail as a sustaining drone (the convolver's
   state looped spectrally).

## 6. Capturing the world — the IR idea, generalised

An IR is "a room captured into data". The same move works for many things. Each capture
ends in a data type Bazalt already has, so existing nodes use it at once:

| Capture | How (assistant in the factory) | Result → used by |
|---|---|---|
| **Room** | Play an exponential sine sweep through the output, record through the input, deconvolve. A *Quick* mode takes a hand clap (onset-gated, denoised). | IR sample → Convolve, Room (§4.4 A) |
| **Material / object** | Tap the object a few times near a mic. Onsets are found, and the decays are fitted with damped modes (as in §5.3, 7). Several taps of different strength give velocity behaviour. | `Data(modal-set)` → `resonator.modal`. *"My kitchen table"* becomes a resonator. |
| **Instrument body** | Record the body's response to a tap, or pickup vs. mic (two inputs, deconvolved). | IR → Convolve: a piezo/DI guitar gets its acoustic body back. |
| **Exciter** | Record a pluck, bow or breath. Fit and remove its resonant modes (the modal fit above), keep the residual. | Exciter sample → Sampler into a resonator: a real pick attack driving any modelled string. |
| **Drive** (pedal, amp) | Sweeps at several levels, then a Wiener–Hammerstein fit: IR before, static curve, IR after. | Two IRs + a curve → Convolve + Waveshaper + Convolve. A pedal becomes three editable parts. |
| **Timbre** | A recorded note (§3). | `Data(wavetable)` → Oscillator. |
| **Tone balance** | Record a reference sound; its long-term spectrum is matched (match EQ). | `data.eqCurve` → EQ. |
| **Tuning** | Play the notes of an instrument; their pitches give the scale. | `data.scale` → note.quantize. |
| **Groove** | Record a rhythm; onset deviations from the grid give a timing template. | `data.curve` (timing) → clocks, humanize. |
| **Gesture** | Record a performance's analysis (§4.4 F). | Control sample → anything. |
| **Noise print** | Record silence or a texture; its spectrum shapes noise. | `data.eqCurve` → filtered noise, or de-noise later. |

Every assistant follows the same three steps: **prepare** (what to do, in one sentence:
"tap the object three times, softly to hard"), **capture** (big record button, level
meter), **check** (play it back; A/B with the original). Rooms and drives play test signals,
so they warn about volume first.

## 7. Engine notes

- **Rule 2 (audio thread):** decoding, resampling, analysis, IR shaping, modal fits and bakes
  all run on worker threads. The audio thread only swaps in finished buffers, through
  `DataPublisher` (or the live-buffer contract of §2.5).
- **Memory:** samples live in RAM in 3a. A soft limit warns at 500 MB per patch. Disk
  streaming of very long samples is not needed for this stage's uses (loops, one-shots,
  grains, IRs); it is a later item if real projects ask for it.
- **Interpolation quality** is a setting per Sampler, so 100 granular voices can use cubic
  while a solo instrument uses sinc.
- **Determinism:** a bake records its seeds and settings, so re-baking gives the same
  result, and a patch can always re-bake from scratch if a baked sample is missing.
- **Effect build:** the Erosion build (ROADMAP 3b) comes with this stage. Its main input is
  `io.audioIn`, and Record, Wavesets, Convolve and Spectrogram are exactly what a live
  guitar effect needs.

## 8. Batches

| Batch | What | Done means |
|---|---|---|
| **3a.1** | `Data(sample)` (channels with quantity, markers), sample library + references + "save with samples", file drop, `data.sample` with region/loop | A dropped WAV appears as a node with its waveform; the patch reopens on another machine via "save with samples"; a missing file shows "missing — find…". |
| **3a.2** | `source.sampler` (start, position, rate/pitch, slice, channel, loops, cubic/sinc, mipmaps); Sample factory (waveform, region, slices at transients) | A sampled piano under a Population plays 16 voices; slice N plays the Nth slice; a control-sample (Pitch) plays into a pitch port without a Map. |
| **3a.3** | `data.record` with the live-buffer contract; Freeze; **Capture this** on ports | A ring buffer of the guitar read live by a Sampler, no glitches, no allocation (trap on); Capture this on a pitch cable gives a Pitch sample. |
| **3a.4** | Wavesets W1 on the live buffer (`Wavesets.md`) | As in Wavesets.md (Live Interleave). |
| **3a.5** | `view.spectrogram`; waveform previews on sample cables | Spectrogram matches Spectrum's axis and read-out; a live buffer's write head is visible. |
| **3a.6** | Wavetable factory "From sample…" (Cycles / Harmonics / Texture, multi-cycle and single-cycle WAV import); live `capture` on `data.wavetable` | A recorded "aah" becomes 16 frames whose Frame sweep replays the vowel change at any pitch. |
| **3b.1** | `space.convolve` (partitioned, true stereo, crossfaded swaps) + response shaping (start/end/decay/stretch/EQ/reverse) | A 3 s hall IR at 64-sample blocks, under 10 % of a core; changing Decay never clicks. |
| **3b.2** | Granular from Population + Sampler (presets: Grain Cloud from a sample, Freeze, Stretch) | A granular time-stretch of a recording with no granular node; 64 grains. |
| **3b.3** | **Bake** over time (worker thread, sub-graph plan), baked frame + badge, un-bake | Baking a 64-mode resonator chain plays identically (null test) and costs a Sampler's CPU. |
| **3b.4** | Bake **axes** (1–2 knobs), lattice playback through the Sampler's `channel` | A pitch × velocity bake of a physical model plays across the keyboard and dynamics with smooth blends. |
| **3b.5** | Capture assistants: Room (sweep + clap), Material (modal fit), Body | A tapped table becomes a `resonator.modal` you can strike at any pitch. |
| **3b.6** | Positional responses (K-basis, §4.4 A) and the Room node | 100 Population members each heard from their own place in one room, at K convolutions. |
| **3b.7** | Effect build "Erosion" (effect/instrument distinction, warnings, limits) | Loads as an audio effect in a DAW; refuses or warns on instrument-only patches. |
| later | Dynamic convolution, response morph, response → modes in the Convolver, Drive capture (Wiener–Hammerstein), spectral bakes and resynthesis, performance capture tools, bake suggestions | Each measured against its own reference. |

The order inside 3a follows what the user needs first: playing and recording audio
(3a.1–3a.3), then Live Interleave (3a.4), which they asked for, then seeing (3a.5) and the
wavetable import (3a.6). 3b starts with the Convolver, because baking, rooms and captured
bodies all play through it.

## 9. Risks and open questions

- **The live-buffer contract** is the one exception to "Data is immutable". It must be
  small, documented and tested under the allocation trap before anything builds on it.
- **Sample library across machines and DAWs:** references by hash plus embedding on export
  should cover it. Open: should "save with samples" be the default for every save, or only
  for export? *Leaning:* export only; DAW state stays small.
- **Bake correctness:** a bake must refuse anything it can't reproduce (a live input, a host
  transport dependency) rather than bake something subtly different. The null test (bake
  vs. live, identical) is the acceptance test for every bake.
- **Modal fitting robustness** (room noise, overlapping decays): prototype offline on real
  recordings before designing the assistant's UI around it.
- **K-basis quality:** how many bases a real room needs for inaudible error. This needs to
  be measured; K may need to vary per room.
- **Latency** of convolution and of the live buffer's look-behind must be reported to the
  host (plugin latency) when it isn't zero.
- Open: should Capture and Bake also exist as nodes (for automation by the patch itself,
  e.g. a footswitch that bakes), or only as editor actions? *Leaning:* Record is the node;
  Bake stays an editor action until a real use asks otherwise.
