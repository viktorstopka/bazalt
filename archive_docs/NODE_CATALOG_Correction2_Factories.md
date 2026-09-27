# Node catalogue — correction 2: factories

Amends `NODE_CATALOG.md`, and flags two things that must be settled in `VALUE_MODEL.md` and the patch format **before** building starts, because both are cheap now and expensive once the first patch has been saved.

A factory is a node with a full editor of its own — an EQ, a curve editor, a wave editor, a sample editor, a note editor, a material editor. The concept does not fight the architecture, but it lands on three places where the architecture is currently silent: where a node's *content* lives, how a custom editor is declared, and what "unwrap" means at the engine level. This document settles those and specifies the six factories.

Each section names the part of the design it amends.

---

## 1. What a factory is

A factory is **a native node with three things an ordinary node does not have**:

1. **Content** — an editable document it owns (bands, curve points, a recording, a pattern, a material definition). Content is neither ports nor structural parameters; see §2.
2. **A custom editor** — its own UI, declared rather than hardcoded; see §4.
3. **A declared unwrap expansion** — the graph of ordinary nodes it is equivalent to; see §5.

A factory is **not a group**. Groups are inlined subgraphs and are deferred until after the catalogue ships; factories must not wait for them. A factory compiles like any other node, and unwrap uses the same declarative expansion mechanism as Assist recipes, not group inlining.

Factories come in two shapes, and the difference decides what unwrap produces:

- **Producers** own content and output a `Data` buffer. Curve, Wave, Material. Unwrap yields a `data.*` producer plus the consumer that was implied.
- **Processors** own content that configures their own DSP and pass audio or notes through. Spectral (EQ), Sample, Notes. Unwrap yields the chain of primitives that does the same job.

---

## 2. Amends `VALUE_MODEL.md` — node content is a third category

The model has two categories today: ports (connectable, modulatable, with inline defaults) and structural parameters (not connectable, may reallocate). A factory's content fits neither. An EQ's eight bands, a drawn curve's point list, a 30 MB recording with its slice markers — these are **documents**, not settings.

Add a third category:

```
NodeContent {
  schemaId        stable string, versioned independently of the node type
  schemaVersion   integer, with its own migrations
  payload         structured data (JSON), or an asset reference (§3)
  externalized    optional map: element id → node id (see §5)
}
```

Rules:

- Content is **not modulatable, not automatable, and never bindable to the host macro pool.** To modulate part of a factory's content, unwrap that part into a node (§5). This is the deliberate boundary between content and processing, and it is what makes unwrap meaningful rather than cosmetic.
- Content is serialised in the patch under its own schema version, so a factory's editor can gain features without touching the node type's port IDs or the patch's top-level version.
- Content changes are **published like `Data`**: rebuilt on a worker thread, swapped atomically, crossfaded by the consumer. Editing while audio runs must never allocate on the audio thread and must never click.
- A factory may still expose ordinary **ports** for whole-document controls that are genuinely continuous — output gain, a global tilt, a morph amount, a playback position. Those follow the normal rules and are modulatable. Content is what is edited; ports are what is played.

This also removes an existing inconsistency: `data.table`'s drawn curve and `seq.steps`' step data are currently described as structural, which under the strict definition of structural they are not.

---

## 3. Amends the patch format — an asset store, decided now

Sample content cannot live inside a JSON patch as base64. Patch format v1 is scheduled in M3; if it ships without an asset section, every factory that owns audio will either bloat patches or invent its own storage.

Add to the patch format now:

- An **asset store**: content-addressed binary blobs (audio, images, impulse responses) with deduplication, referenced by hash from node content.
- Two storage modes per asset, as already decided for canvas images: **embedded** (self-contained patch, the default for anything small) and **referenced** (path plus hash, for large recordings), with a clear warning when a referenced asset is missing on load.
- A per-patch size budget with a visible indicator, so a patch does not silently grow to hundreds of megabytes.
- Assets are immutable: editing a recording writes a new asset and updates the reference, which makes undo and version history work without copying.

---

## 4. Amends `SIGNAL_TYPES.md` — custom editors and Data tags

### 4.1 The custom-editor contract

A node descriptor may declare an editor instead of relying on generic port-and-parameter rendering:

```
ui {
  kind        "generic" | "inline" | "custom"
  editorId    stable string, resolved against a registry of editors in the UI
  contentRef  which NodeContent schema the editor edits
  taps        which telemetry taps the editor needs while open
}
```

- **The editors are separate, deliberately.** An EQ works on bands with type, frequency, gain and Q, drawn against a frequency axis under a live spectrum; a curve editor works on points and segment tension against a normalised axis; a wave editor works on one cycle and its harmonics; a sample editor works on a recording, slices and loop points. Their data models and their visualisations have nothing in common, and one editor serving all four produces four bad ones.
- **What is shared is infrastructure, not appearance**: the content/`Data` publishing path, canvas gestures (pan, zoom, select, Shift for fine mode, snapping, undo), the fast render layer, and the design tokens. Every editor must feel identical under the hand while looking nothing alike.
- **An open editor is a telemetry subscriber in its own right.** This extends the viewport-driven subscription model: an open EQ means input and output spectra at full rate; closed, it means nothing. Closing unsubscribes.

### 4.2 Data tags

`Data` gains one tag, and the rules around tags are tightened:

| Tag | Produced by | Consumed by |
|---|---|---|
| `curve` | Curve factory, `data.table` | `adapt.remap`, `data.lookup`, `env.curve`, `lfo.shape`, `shape.waveshaper`, `resonator.tract`, `excite.contact` |
| `eq-curve` | Spectral factory | Spectral factory only |
| `wavetable` | Wave factory, `data.load` | `osc.wavetable` |
| `sample` | Sample factory, `data.load`, `data.record` | `sampler.player`, `sampler.granular`, `data.analyseModes` |
| `scale` | `data.scale`, Notes factory | `note.quantize`, `data.lookup` |
| `modal-set` | Material factory, `data.material`, `data.analyseModes` | `resonator.modal`, `filter.formant` |

- **Tags never convert implicitly.** An `eq-curve` is bands with gains in dB across a log-frequency axis; a `curve` is values across a normalised axis. Wiring one into the other is rejected by `canConnect`, never silently reinterpreted.
- Where the conversion is genuinely wanted it is an **explicit node**: `data.eqToCurve` (§9) renders an EQ's magnitude response as a plain curve, so a shape designed in the EQ editor can drive a waveshaper or a remap. One-way, by design.

---

## 5. Unwrap — the engine-level contract

Unwrap turns part or all of a factory into ordinary nodes, so it can be modulated, automated and rewired. It is what keeps factories from becoming black boxes, so its rules are strict.

**It is a graph transform, not a UI trick.** The factory descriptor declares its expansion as data (nodes and connections, parameterised by content), and the command layer applies it as **one undoable command**, on the same machinery Assist recipes use.

**It is sound-preserving.** Rendering the patch before and after an unwrap must produce the same audio. For linear factories that is a bit-exact test and belongs in CI; for anything containing a chaotic or self-oscillating node, the statistical comparison from correction 1 applies.

**It is one-way.** There is no re-wrap. Rebuilding a factory from an arbitrary graph is not a transform anyone can define safely, and pretending otherwise invites data loss.

**Two levels.** A single element (one EQ band, one slice, one pattern) or the whole factory.

**An externalised element leaves a trace.** When one element is unwrapped, it is removed from the factory's processing and its content entry is marked `externalized: <nodeId>`. The editor shows it greyed, with a link to the node. Without this, the same band exists twice and the patch has two truths.

**Content does not unwrap; processing does.** A recording, a drawn shape, a note pattern are documents — after unwrapping their use, they remain as a `data.*` node feeding the resulting chain. This line decides every case in §8.

**A factory mode that cannot be expressed by the primitives must not exist.** If an EQ offered a linear-phase mode that no chain of `filter.*` nodes reproduces, that mode would break the sound-preserving rule. Either the primitive is added first, or the mode is not offered. A factory is never allowed to be more capable than the catalogue.

---

## 6. Amends the namespace rules — `stock.*` replaces `factory.*` for shipped groups

`DOMAINS.md` §8 reserves `factory.*` for shipped groups, which collides with the factories described here. Shipped groups move to **`stock.*`**. The namespaces are now `core.*` (native primitives), `stock.*` (shipped groups), `user.*` (user groups and saved factory content), `lab.*` (no stability promise).

Factory node types are native and live in `core.*` like everything else: `core.factory.eq`, `core.factory.curve`, and so on, written below without the `core.` prefix.

---

## 7. Amends the catalogue index

| Family | Nodes |
|---|---|
| `factory.*` | eq, curve, wave, sample, notes, material |
| `data.*` | load, table, record, scale, material, analyseModes, lookup, eqToCurve |

---

## 8. The factories

#### `factory.eq` — Spectral Factory (EQ)
**Shape:** processor. **Content:** an ordered list of bands `{id, type (bell, low/high shelf, low/high cut, notch, allpass), frequency, gain, q, enabled, externalized}`.
**In:** `in` — `Audio`; `tilt : float·Bipolar·−1–1·linear·0 [audio]` (global spectral tilt); `mix : float·Unipolar·0–1·linear·1`; `outputGain : float·Gain·0–4×·log·1 [audio]`.
**Out:** `out` — `Audio`; `curve` — `Data(eq-curve)`.
**Structural:** `maxBands` (default 16), `oversampling`.
**Editor:** bands on a log-frequency axis, the magnitude response drawn over a live input spectrum; drag to move, scroll for Q, click to add.
**Taps:** input spectrum, output spectrum, computed response.
**Unwrap:** one band → the matching `filter.peak`, `filter.shelf`, `filter.svf` or `filter.allpass` with its values as inline defaults, spliced into the chain. Whole factory → the full series chain in band order, followed by `mix.gain`.
**Deferred:** dynamic bands, mid/side, linear phase. None of these unwraps into today's primitives, so per §5 they are not offered yet.

#### `factory.curve` — Curve Factory
**Shape:** producer. **Content:** points with per-segment tension, plus loop and polarity settings; optionally a second shape to morph toward.
**In:** `morph : float·Unipolar·0–1·linear·0 [audio]`.
**Out:** `data` — `Data(curve)`.
**Structural:** `resolution`.
**Editor:** the drawn shape against a normalised axis, with live read positions from every consumer using it.
**Taps:** the curve, plus each consumer's current read position.
**Unwrap:** → `data.table` (retaining the shape) plus the consumer implied by how it was being used: `lfo.shape`, `env.curve`, `adapt.remap` or `shape.waveshaper`. A single point does not unwrap; it is content.
**Note:** this is the shared, reusable source of shapes. A node with its own inline curve (`adapt.remap`, `data.table`) keeps a *local* copy as content; connecting a `Data(curve)` port overrides it, and the UI must show plainly which of the two is active.

#### `factory.wave` — Wave Factory
**Shape:** producer. **Content:** one or more single-cycle frames, each editable as a waveform or as harmonic amplitudes and phases.
**In:** `position : float·Unipolar·0–1·linear·0 [audio]` (frame scan, passed through to the consumer).
**Out:** `data` — `Data(wavetable)`.
**Structural:** `frameSize`, `frameCount`, `normalise`.
**Editor:** the cycle drawn directly, with a harmonic view beside it; editing either updates the other.
**Taps:** the cycle, its harmonic spectrum, the current frame.
**Unwrap:** → `data.table` or `data.load` plus `osc.wavetable`. An additive expansion into `osc.sine` plus `mix.sum` is possible in principle; until it is specified, the additive view is a view, not an unwrap target.

#### `factory.sample` — Sample Factory
**Shape:** processor with owned content. **Content:** an asset reference (§3), slice markers, loop points, root note, gain, trim.
**In:** `trigger : Event`; `slice : int·Count·0–127·linear·0`; `pitch : float·Pitch·−48–48 st·linear·0 [audio]`; `start : float·Unipolar·0–1·linear·0 [audio]`; `level : float·Gain·0–2·log·1 [audio]`.
**Out:** `out` — `Audio`; `data` — `Data(sample)`; `ended` — `Event`.
**Structural:** `interpolation`, `loopMode`.
**Editor:** waveform with slices, loop points and a playhead; trimming, normalising, and automatic slicing by transient.
**Taps:** waveform, playhead, input level while recording.
**Unwrap:** the use unwraps, the recording does not. One slice → the existing asset (or `data.load`) plus a `sampler.player` configured to that region. Whole factory → the asset plus one `sampler.player` per active slice, selected by `logic.select` on `slice`.
**Recording is a separate node, not part of the editor:** see `data.record` in §9. Capture is a real-time concern with its own preallocated buffer and thread handoff; an editor is the wrong place for it.

#### `factory.notes` — Notes Factory
**Shape:** processor on the note stream. **Content:** chord definitions, arpeggio patterns, step patterns, scale definitions.
**In:** `notes` — `Note`; `tick` — `Event` (external clock; internal when unconnected); `rate : float·Frequency [audio]`; `gateLength : float·Unipolar·0–1·linear·0.5`; `swing : float·Bipolar·−1–1·linear·0`; `humanize : float·Unipolar·0–1·linear·0`.
**Out:** `notes` — `Note`; `scale` — `Data(scale)`.
**Editor:** pattern grid and chord list, with held notes and a playhead shown live.
**Taps:** held notes, playhead, output note events.
**Unwrap:** arpeggio → `note.hold` + `clock.pulse` + `clock.counter` + `note.select`; chord → `note.chord`; scale → `data.scale` + `note.quantize`; humanise → `note.humanize`.
**Output is always `Note`.** The "MIDI or pitch/frequency" choice from the original sketch is not a mode: a second representation of a note is exactly the parallel truth the reconciliation warns about. Pitch and gate as plain values come from `note.value` or from the instance allocator, both of which already exist.
**Deferred:** MIDI clips. A clip needs a timeline, a piano roll and host transport sync; it is a project of its own and should not delay the rest of this factory.

#### `factory.material` — Material Factory
**Shape:** producer. **Content:** geometry, material preset, and any per-mode overrides made by hand.
**In:** `stiffness`, `density`, `damping`, `size`, `inharmonicity`, `irregularity` — as on `data.material`, all modulatable.
**Out:** `data` — `Data(modal-set)`.
**Structural:** `geometry`, `modeCount`, `seed`.
**Editor:** the mode spectrum, editable per mode, with a window for analysing a recording (hit a real object, drop in the file, take its modes) and an audition button.
**Taps:** the mode set, and a connected resonator's per-mode energy.
**Unwrap:** → `data.material` (or `data.analyseModes` when the set came from a recording) plus `resonator.modal`. A single mode does not unwrap by default; at small mode counts it could become a `resonator.comb`, which is a curiosity rather than a feature and is deferred.

---

## 9. New nodes required by the factories

#### `data.record` — Record
**In:** `in` — `Audio`; `trigger : Event` (start); `stop : Event`; `threshold : float·Gain·−80–0 dB·log·−48 dB` (auto-start on signal); `maxLength : float·Time·0.1–600s·log·30s`.
**Out:** `data` — `Data(sample)` (published when recording stops); `recording` — `bool`; `level` — `float·Gain [audio]`.
**Structural:** `preRoll` (captures the moment before the trigger), `channels`.
**Behavior:** writes into a buffer preallocated from `maxLength`; on stop, hands it to a worker thread, which writes an immutable asset and publishes the reference. Nothing is allocated on the audio thread, and a recording in progress never blocks it. **Taps:** live waveform, level, elapsed time. **Native:** real-time capture with preallocation and thread handoff.

#### `data.eqToCurve` — EQ to Curve
**In:** `eq` — `Data(eq-curve)`; `lowFrequency`, `highFrequency` — `float·Frequency` (the span to render); `normalise : bool·true`.
**Out:** `data` — `Data(curve)`.
**Behavior:** renders an EQ's magnitude response as a plain curve, so a shape designed in the EQ editor can drive a waveshaper, a remap or a tract profile. One-way, explicit, never automatic.

---

## 10. What must be decided before building

1. **The asset store in patch format v1 (§3).** M3 ships the patch format; adding assets afterwards means migrating every saved patch.
2. **`NodeContent` as a category in `VALUE_MODEL.md` (§2).** It changes what "structural" means for `data.table` and `seq.steps`, which are already specified.
3. **The `ui.custom` descriptor field (§4.1).** Cheap now; a per-node bolt-on later, exactly as the reconciliation warned.
4. **The `stock.*` rename (§6).** Free today, a breaking ID change once one patch references a shipped group.
5. **Unwrap as a declared expansion on the recipe mechanism (§5).** Built as UI-side special-casing instead, six factories become six implementations and none of them testable.

Nothing else here blocks the first wave of the catalogue. Factories can ship one at a time, and each one that ships brings its unwrap expansion and its equivalence test with it.
