# Bazalt — Reference patches

Nine patches from the planning prompt, expressed as graphs against `NODE_CATALOG.md`. Notation:
`[typeId] "instance label"` for a node, `.port` for a port, `──signal──>` for a connection, `(P1,
P2)` for parameters set on the node. A dashed arrow `╌╌╌╌>` marks an auto-inserted adapter. Where a
patch hits one of `NODE_CATALOG.md`'s gap-list items, that's called out explicitly rather than
papered over — per the planning prompt's own instruction, an undrawable patch is a finding.

---

## 1. Karplus-Strong — single-sample feedback loop

```
[io.noteIn] "midi" .notes ──Note──> [instance.allocator] "voice" (configuration=Voice) .spawn
[instance.allocator].Gate ──Control(bool)──> [excite.impulse] "pluck" .trigger  ╌╌╌╌> (Gate→Event via Note gate/threshold adapter)
[instance.allocator].Pitch ──Control·Pitch──> [adapt.map] "pitchToSamples" (min/max seeded to delay's range) .in
    [adapt.map].out ──Control·Time──> [delay.basic] "string" .delay.basic.samples
[excite.impulse].out ──Audio──> [mix.add2] "feedbackSum" .a
[delay.basic].out ──Audio──> [mix.add2].b
[mix.add2].out ──Audio──> [delay.basic].in                              ← closes the loop
[delay.basic].out ──Audio──> [filter.onepole] "damping" .in (coefficient tuned for brightness decay)
[filter.onepole].out ──Audio──> [instance.voiceMix] "mix" .in
[instance.voiceMix].out ──Audio──> [io.output] "out" .in
```

**What this proves:** the `delay.basic`→`filter.onepole`→feedback-edge loop compiles into a
per-sample region (ADR-0011's Tarjan-SCC routing), exactly the mechanism M2's original hardcoded
Karplus-Strong proof graph already exercises — this patch shows it's now buildable from named,
addressable nodes instead of hand-assembled C++. Also exercises `delay.basic`'s fractional-length
upgrade (`RECONCILIATION.md`/`NODE_CATALOG.md` 🟡): `Pitch`→delay-samples through `adapt.map` changes
continuously as a note plays (e.g. pitch-bend), which is inaudible as zipper noise only once the
interpolation fix lands.

---

## 2. MIDI remapped to a scale (C Lydian), modularly

```
[io.noteIn] "midi" .notes ──Note──> [instance.allocator] "voice" (configuration=Voice) .spawn
[instance.allocator].Pitch ──Control·Pitch──> [data.quantize] "scaleSnap" .in     ← GAP 1, does not exist
[util.constant] "cLydian" (value = Data reference to a baked C-Lydian Data(scale) buffer) .out ──Data(scale)──> [data.quantize].scale
[data.quantize].out ──Control·Pitch──> [osc.basic] "voice" .frequency  ╌╌╌╌> (Pitch→Frequency adapter, Map)
[osc.basic].out ──Audio──> [instance.voiceMix] .in
[instance.voiceMix].out ──Audio──> [io.output] .in
```

**Finding:** this patch **cannot be fully drawn** against Part A as specified — it needs Gap 1
(`data.quantize`, `NODE_CATALOG.md`) and a way to author/select a scale `Data` buffer (`util.constant`
holding a `Data` reference is a plausible shape, but no node in Part A currently produces or edits a
`Data(scale)` value; that's a second, smaller gap this patch surfaces — a "Scale editor" source node,
not in the original coverage list). Everything downstream of the snap (feeding a quantized `Pitch`
into an ordinary oscillator) is otherwise trivial and proves the type system's `Pitch` quantity does
its job: no special-casing needed once a quantized `Pitch` value exists, it's just another
`Control·Pitch` signal.

---

## 3. Arpeggiator and chord generator, from note-stream nodes

```
Arpeggiator:
[io.noteIn] "midi" .notes ──Note──> [note.holdMemory] "held" .in    ← GAP 2, does not exist
[lfo.complex] "clock" (shape=square) .out ──Control·Bipolar──> [adapt.threshold] "clockGate" .by
[adapt.threshold].onThreshold ──Event──> [note.holdMemory].advance
[note.holdMemory].out ──Note──> [instance.allocator] "voice" .spawn
... (rest identical to patch 7's voice chain)

Chord generator:
[io.noteIn] "midi" .notes ──Note──> [note.harmonizer] "chord" (intervals = [0, 4, 7]) .in   ← GAP 3, does not exist
[note.harmonizer].out ──Note──> [instance.allocator] "voice" .spawn (3 simultaneous instances per input note)
```

**Finding:** neither half of this patch can be fully drawn — both hit gaps (2 and 3) that are
distinct nodes solving related but different problems (holding a *set over time* vs. fanning out
*one event into several*). Worth noting the two gaps share no obvious common primitive that would
close both at once — they're genuinely two separate pieces of native work, not one.

---

## 4. Struck/plucked body — excitation → modal bank, with material data

```
[io.noteIn] "midi" .notes ──Note──> [instance.allocator] "voice" (configuration=Voice) .spawn
[instance.allocator].Gate ──Control(bool)──> [excite.impulse] "strike" .trigger  ╌╌╌╌> (adapter)
[util.constant] "wood" (value = Data reference, tag=modal-set, baked material table) .out ──Data(modal-set)──> [resonator.modal] "body" .modes
[excite.impulse].out ──Audio──> [resonator.modal].excite
[resonator.modal].out ──Audio──> [instance.voiceMix] .in
[instance.voiceMix].out ──Audio──> [io.output] .in
```

**What this proves:** `Data(modal-set)` flowing from a source (`util.constant` holding a reference,
standing in for a future dedicated material-table editor node — out of this catalogue's scope, same
class of gap patch 2 surfaces) into `resonator.modal`'s required `modes` input, with `canConnect`
rejecting any attempt to wire a differently-tagged `Data` buffer in (e.g. accidentally connecting a
`Data(scale)` here) at compile time per `SIGNAL_TYPES.md` §2's semantic-tag rule. Swapping `wood` for
`metal`/`glass`/etc. is just swapping which `Data` buffer the constant holds — no DSP code changes,
which is the entire point of making physical-modelling material first class.

---

## 5. Stone dragged across asphalt — density-driven dust → stick-slip → modal resonator

```
[util.macro] "speed" (slot=1, range 0–1) .out ──Control·Unipolar──> [adapt.map] "speedToDensity" (min=5, max=300) .in
[adapt.map].out ──Control·Dimensionless──> [noise.dust] "grains" .density
[util.macro] "pressure" (slot=2, range 0–1) .out ──Control·Unipolar──> [excite.stickSlip] "scrape" .pressure
[util.macro "speed"].out ──Control·Unipolar──> [excite.stickSlip].speed
[noise.dust].pulse ──Event──> [excite.stickSlip].trigger
[util.constant] "asphalt" (Data reference, tag=modal-set) .out ──Data(modal-set)──> [resonator.modal] "surface" .modes
[excite.stickSlip].out ──Audio──> [resonator.modal].excite
[resonator.modal].out ──Audio──> [io.output] .in
```

**What this proves:** this patch is a *single continuous voice*, not a swarm — no
`instance.allocator` needed, matching `NODE_CATALOG.md`'s "Scrape" factory-group entry exactly. Two
`util.macro` nodes driving both `noise.dust`'s density (via a `Map` adapter scaling 0–1 to a real
event-rate range) and `excite.stickSlip`'s two inputs directly demonstrates a single macro fanning
out to multiple destinations (ordinary port fan-out, no special mechanism needed) and the
`Unipolar`→real-quantity `Map`-adapter pattern in the same patch.

---

## 6. Transient swarm (water bubbles) and persistent swarm (cicadas)

```
Bubbles (transient):
[noise.dust] "spawnRate" (density = macro-controlled) .pulse ──Event──> [instance.allocator] "bubbles" (configuration=Swarm-transient, maxInstances=32) .spawn
[instance.allocator].Random1 ──Control·Bipolar──> [adapt.map] "randPitch" (min=800, max=2400) .in
[adapt.map].out ──Control·Frequency──> [util.constant-per-instance equivalent: fed directly, see note below]
[instance.allocator].Start ──Event──> [excite.impulse] "pop" .trigger
[excite.impulse].out ──Audio──> [resonator.modal] "waterBubble" (small 2–3 mode Data(modal-set), centre frequency modulated by randPitch — see note) .excite
[resonator.modal].out ──Audio──> [instance.voiceMix] "mix" (mode=sum) .in
[instance.voiceMix].out ──Audio──> [io.output] .in

Cicadas (persistent):
[instance.allocator] "swarm" (configuration=Swarm-population, maxInstances=40, always live) — no spawn port
[instance.allocator].Random1 ──Control·Bipolar──> [adapt.map] "randRate" (min=2, max=6) .in
[adapt.map].out ──Control·Frequency──> [lfo.complex] "chirpClock" .rate
[lfo.complex].out ──Control·Bipolar──> [adapt.threshold] "chirpGate" .by
[adapt.threshold].onThreshold ──Event──> [noise.burst] "chirp" .trigger
[noise.burst].out ──Audio──> [filter.svf] "chirpTone" (per-instance Random-tuned cutoff) .in
[filter.svf].out ──Audio──> [instance.voiceMix] "mix" (mode=average — many instances, average avoids loudness runaway per NODE_CATALOG.md's mode note) .in
[instance.voiceMix].out ──Audio──> [io.output] .in
```

**Finding (minor):** feeding a per-instance `Random`-derived value into `resonator.modal`'s *static*
`Data(modal-set)` center frequency isn't directly expressible — `Data` is immutable and shared across
instances by design (`DOMAINS.md` §6: "never copied per instance"). The workable version (as drawn
above via `randPitch` feeding a pitch-shift-style modulation ahead of the resonator, or accepting a
coarser "which of N preset mode-sets" selection via `Instance Index` instead of continuous
`Random`) is a real, useful distinction to have surfaced: **per-instance variation of a *shared* Data
table needs its own mechanism** (e.g. a cheap pitch-shift of the whole modal response, not a
per-instance-unique table) — worth a note in a future ADR on `Data`, not a blocking gap for this
catalogue.

---

## 7. Basic subtractive patch (Serum-style)

```
[io.noteIn] "midi" .notes ──Note──> [instance.allocator] "voice" (configuration=Voice) .spawn
[instance.allocator].Pitch ──Control·Pitch──> [osc.basic] "osc" .frequency  ╌╌╌╌> (Pitch→Frequency Map adapter)
[instance.allocator].Gate ──Control(bool)──> [env.adsr] "amp env" .gate
[instance.allocator].Gate ──Control(bool)──> [env.adsr] "filter env" .gate
[osc.basic].out ──Audio──> [filter.svf] "filt" .in
[env.adsr "filter env"].out ──Control·Unipolar──> [adapt.map] "envToCutoff" (min=200, max=8000) .in
[adapt.map].out ──Control·Frequency──> [filter.svf].cutoff  (cutoff promoted to a real port, NODE_CATALOG.md's filter.svf note)
[filter.svf].out ──Audio──> [amp.vca] "amp" .audio
[env.adsr "amp env"].out ──Control·Unipolar──> [amp.vca].gain
[amp.vca].out ──Audio──> [instance.voiceMix] .in
[instance.voiceMix].out ──Audio──> [io.output] .in
```

**What this proves:** the ordinary "osc → filter → amp, two envelopes" patch — the thing most users
will build most often — stays exactly as simple as it is in any commercial subtractive synth despite
everything else in this catalogue being built for much stranger patches. No `Data`, no swarms, no
adapters beyond the one `Pitch`→`Frequency` and envelope→cutoff `Map`s a Serum user would expect as
"just how modulation routing works."

---

## 8. Per-voice effects — distortion and delay inside the instanced region

```
[io.noteIn] "midi" .notes ──Note──> [instance.allocator] "voice" (configuration=Voice) .spawn
... (osc/filter/env chain as in patch 7, omitted for brevity) ...
[amp.vca].out ──Audio──> [fx.waveshaper] "drive" .in        ← GAP 5, does not exist
[fx.waveshaper].out ──Audio──> [delay.basic] "perVoiceEcho" .in
[delay.basic].delay.basic.samples ← constant 6000 (≈125ms at 48kHz)
[delay.basic].out ──Audio──> [instance.voiceMix] "mix" (silenceHoldTime raised to cover the echo tail) .in
[instance.voiceMix].out ──Audio──> [io.output] .in
```

**Finding:** blocked on Gap 5 (`fx.waveshaper`/distortion, not in this catalogue's requested
coverage — see `NODE_CATALOG.md`'s gap list). Everything else works exactly as intended and is the
patch that most directly exercises `instance.voiceMix`'s reconciled silence-detection (3.2): each
voice's delay tail keeps that voice's instance alive past its envelope's own release, verified by
placing `instance.voiceMix` *after* the per-voice delay rather than before it — the placement itself
is the proof, not a separate mechanism.

---

## 9. Hexaphonic guitar — six aux inputs → per-string onset/pitch → note events → instanced physical model

```
for each string s in 1..6:
  [io.audioIn] "string{s}" (bus = Aux {s}) .out ──Audio──> [analysis.onset] "onset{s}" .in
  [io.audioIn "string{s}"].out ──Audio──> [analysis.pitch] "pitch{s}" .in
  [analysis.onset "onset{s}"].onset ──Event──> [note.assembler{s}] .trigger   ← GAP 4, does not exist
  [analysis.pitch "pitch{s}"].pitch ──Control·Pitch──> [note.assembler{s}].pitch
  [analysis.pitch "pitch{s}"].confidence ──Control·Unipolar──> [note.assembler{s}].gate   (suppresses spurious triggers on low confidence)
  [note.assembler{s}].out ──Note──> [instance.allocator] "voice{s}" (configuration=Voice) .spawn
  ... (excitation → resonator.modal physical-model chain per string, as in patch 4) ...
  [resonator.modal "body{s}"].out ──Audio──> [instance.voiceMix "mix{s}"] .in
[instance.voiceMix "mix1"].out ──Audio──> [mix.add2] "sumAB" .a
[instance.voiceMix "mix2"].out ──Audio──> [mix.add2 "sumAB"].b
... (chain of mix.add2 to sum all 6 strings, or a growable Mix group once port groups land) ...
──> [io.output] .in
```

**Finding:** blocked on Gap 4 (`note.assembler`, the Event+Control→Note combiner). Structural choice
worth recording: each string gets its **own** `instance.allocator`/`instance.voiceMix` pair rather
than merging all six analysis chains into one shared allocator — avoids needing a Note-stream-merge
primitive (which doesn't exist either) and matches the physical reality of six independently-excited
strings better than a single shared voice pool would. The six per-string `instance.voiceMix` outputs
then sum at the ordinary audio level via `mix.add2`, or a single growable `Mix` node once
`SIGNAL_TYPES.md` §6 lands (`RECONCILIATION.md` 2.5) — six fixed `mix.add2` nodes chained is the
interim shape.
