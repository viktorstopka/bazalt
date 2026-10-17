# Release — from today to a paid 1.0

**Status:** Proposed, 2026-10-17. Written from the user's release list (two messages,
2026-10-17) and Claude's additions, in the order the work should happen. Nothing here is
built yet. Each section links to its own plan file once one exists; a section that grows
into a design gets one in `wiki/plans/` before code.

**Decisions already made (the user's, 2026-10-17):**

- **No compatibility with any existing patch.** Every current patch, factory patch and
  test fixture patch is thrown away; nothing is migrated. Ids, ports and parameters may be
  renamed freely until the **ID freeze** (R0.6), which happens once, right before the
  first tester gets a build. After the freeze, CLAUDE.md rule 3 is unconditional again and
  every change to a saved shape needs a migration.
- **Classes are a must for release** — the Population redesign (R1) is not optional.
- **Snippets** replace Add-menu presets.
- Renames: Wavesets → **Cycle Cuts**, Interleave → **Weave**, Map → **Remap** (a new
  **Map** is the curve-based one), Crush → **Bitcrush**, Population → **Foreach**.
- **Mac is required** for release.
- **Not doing:** hosting VST3 plugins inside Bazalt; Match EQ (skipped for now).
- **"From Pro-Q" EQ import is the user's item, still open** (R4.6): Claude's concern is
  that Pro-Q's preset format is proprietary (reverse-engineering, a legal grey zone) — the
  user decides.
- The effects build is **Bazalt FX** (the name Erosion is dropped).

---

## The order, at a glance

| Step | What | Why here |
|---|---|---|
| **R0** | Reset and foundations: delete all patches, renames, value precision, bug list, UI↔engine tests | Everything later builds on final names and a clean base |
| **R1** | Classes and Entities — the life-cycle redesign (Foreach, Spawn, Class factory, stochastic part 2) | The biggest architectural change; presets and docs depend on it |
| **R2** | Particle MVP — a Position prop and a 3D view | The showcase of where Bazalt is going; small once R1 exists |
| **R3** | Workflow: errors, menus, Expose, Capture, retro-capture, synonyms, Snippets | Daily friction; independent of visuals |
| **R4** | Values and conversion: pitch picker, Convert nodes, Quantize, Map factory, Steps/Arp | New nodes, so before the ID freeze |
| **R5** | Native look: Add menu, dropdowns, tooltips, factories, settings | One design system before content is made with it |
| **R6** | Visual suite: visualizer fixes, resizable views, phase-density view, wave feedback | Uses R5's components |
| **R7** | Play view, macros, patch browser, tags | The face of every preset |
| **R8** | Bazalt FX and host modes | A second product from the same engine |
| **R9** | Performance | Before testers measure it for us |
| **R10** | Mac port (starts in parallel at R3) | Platform surprises found early, not at launch |
| **R0.6** | **ID freeze** — re-enable rule 3, migrations from here on | Testers' patches must survive |
| **R11** | Content: factory library, Tutorial, 808/trap pack | Made on frozen nodes, in the final UI |
| **R12** | Closed beta | Real feedback on a stable build |
| **R13** | Commercial: installers, signing, licensing, demo, updates, legal, store | Only once the product is settled |
| **R14** | Launch | |

R10 (Mac) runs alongside R3–R9 on its own track; R0.6 is listed where it happens in time.

---

## R0 — Reset and foundations

### R0.1 Delete every patch — **done 2026-10-17**
- Remove `ui/src/patches/factory/*.bazalt` and the plugin test that loads each one; the
  patch menu keeps Empty only. Remove the scratch "Life –" patches from the docs. Old
  migrations (`PatchSerializer.cpp`'s `migrateV*`) are deleted; `currentSchemaVersion` is
  reset to 1 for the new format (the loader rejects anything else with a plain message).
- CLAUDE.md says compatibility is dropped until R0.6.
- As built: the three factory patches, all 15 migrations (v1→v16) and their 14 tests are
  gone; the format is version 1 and any other version fails with "This patch was made by a
  different version of Bazalt". The factory-patch test stays and checks whatever lands in
  `ui/src/patches/factory/` (empty for now). Patches saved in AppData before today no
  longer open.

### R0.1b One file: the `.bazalt` container — **done 2026-10-17**
- A `.bazalt` file is a zip (one file you can send — never a folder): `patch.json` (the
  graph, readable) plus `assets/` — samples as FLAC, images as PNG/JPEG, large tables
  (wavetables from samples, recordings) as raw float32 — stored as they are and referenced
  by id from the JSON. Always this container, even with no assets. Assets stored without
  recompression (they are compressed already); only `patch.json` is deflated.
- DAW state: the same JSON, gzip-compressed, samples by reference (as today).
- Numbers written as the shortest decimal that reads back the same float (R0.3).
- A hidden "Save as readable JSON" stays for debugging.
- As built: `plugin/source/PatchFile.h` writes and reads the container (`patch.json`
  deflated, assets stored raw under `assets/<id>.<ext>`); inside the app a patch is still
  the JSON with base64 assets, so the UI, undo and PatchSerializer are unchanged. The
  patch library and "Export with samples" write it; `read` also takes plain JSON (factory
  patches in the repo stay plain JSON, loaded by the UI as text). The DAW state is
  gzip-compressed JSON (plain JSON still read). Tests: `tests-plugin/PatchFileTests.cpp`.
- **Not yet:** large node content (a wavetable made from a sample) as raw float32 assets —
  it is still JSON inside `patch.json`; done with the factory rework (R5.3).

### R0.1c Icon (the user designs it, 2026-10-17) — **app icon done 2026-10-17**
- Motif: basalt's hexagonal columns (columnar jointing). App icon, a `.bazalt` patch-file
  icon (one hexagon), optionally a Bazalt FX variant.
- From the user's 1024 px master (SVG if available): a Windows `.ico` (16–256 px) and a
  macOS `.icns`; set as the app/plugin icon in `plugin/CMakeLists.txt` (`ICON_BIG` /
  `ICON_SMALL`). File association and double-click-to-open come with the installers
  (R10/R13): Windows registers `.bazalt` with the icon and "open with Bazalt.exe" (the
  Standalone opens a file it is given, in the running window if one is open); macOS
  declares the document type in `Info.plist`. Dropping a `.bazalt` on the plugin window
  loads it.
- As built: `assets/icon/bazalt-icon.svg` (the user's single column) rendered by Chromium
  to PNGs (1024 → `ICON_BIG`, 256 → `ICON_SMALL`; JUCE makes the .ico/.icns) and to
  `bazalt.ico` (16–256 px) for the installer. Still to come: the patch-file icon, the FX
  variant, and (design) more contrast for dark backgrounds and 16 px.

### R0.2 Renames (all ids, titles, docs)
| Today | New | Notes |
|---|---|---|
| `waveset.capture` Wavesets | **Cycle Cuts** | cuts a sound at its zero crossings |
| `waveset.play` Waveset Play | **Weave** | weaves whole cycles of its inputs; the Interleave preset goes (R3.6) |
| `math.map` Map | **Remap** | the simple linear range map stays, renamed |
| (new) | **Map** | the curve-based map with a factory (R4.4) |
| Crush | **Bitcrush** | |
| `life.population` Population | **Foreach** | redesigned in R1 |
| Rössler etc. | — | ids stay ASCII; titles keep ö (fixed in R0.4) |

Every rename also updates NODES.md, the Add-menu categories and the tests.

**Done 2026-10-17:** `cycle.cuts` (output `cuts`, `Data(cycle-cuts)`), `cycle.weave`,
`math.remap`, `shape.bitcrush`; category Cycles; the Add-menu preset is now **Weave**.
Population → Foreach is left to R1, where the node itself is redesigned.

### R0.3 Values are what you type
- **Cause found:** parameters are `float` (`NodeInstance::parameters`). 0.5 is exact in
  binary, 0.3 is not — it becomes 0.300000011920929 when written to JSON as a double.
  That is the "sometimes".
- **Fix:** keep `float` on the audio thread, but store and serialise values as `double`
  (or write floats with the shortest decimal that reads back to the same float). Typed
  values commit as typed; dragged values snap to the displayed precision of the slider
  (its `decimals`), so a drag never stores more digits than it shows.
- Test: every value typed into a slider reads back as the same text after save/load.

### R0.4 Known bugs (from the user's testing)
| Bug | Likely cause | Fix |
|---|---|---|
| "RÄ¶ssler" | a non-ASCII C++ literal passed to JUCE as plain `char*` | build titles with `juce::String::fromUTF8` / `CharPointer_UTF8`; a test that every title is valid UTF-8 and round-trips through the descriptor JSON |
| Random 1 doesn't work | to investigate (R1 removes the port anyway, but find the cause — it may affect seeding elsewhere) | a test per output that it varies per member |
| Cycle-view nodes (Sine, Saw…) grow wider when dragged to the left | the card has no fixed width; shrink-to-fit uses the room to its right | an explicit card width, as Scope's panel has |
| Cycles viewer shows nothing, or an old wave, when spawned | preview data keyed by `node:<id>:<port>`; a new node can reuse an id and inherit stale frames | clear a tap's stored frames on subscribe; never reuse auto-generated ids in a session |
| Visualizer time window not saved / resets to max | the auto-window commit overwrites the user's value, or isn't saved | the user's value always wins; the auto value is never written once the user set one; saved with the patch |
| Spectrogram time window not settable | fixed at 6 s | a Time setting (R6) |
| Capture's menu covered by the node menu | right-click on a port opens both menus | the port menu stops the event; one menu at a time |
| Delete all gives a red warning instead of quietly leaving Master Out; voice nodes can't be deleted after complex voice setups | see R3.1 | R3.1 |
| Importing a sample / making a wavetable freezes the UI | work on the message thread; the file travels as base64 | R9 |

### R0.5 Tests for the seam between the UI and the plugin
The bugs found in the last sessions (dead sample import, the editor refusing valid cables,
a stale plan after many commands) were all in the seam between the WebView and C++, which
has almost no tests. Build one harness: the production UI in headless Chromium (Playwright,
already installed) talking to the real native functions through a test build of the
plugin layer, with scripted gestures: drop a file, wire a cable, type a value, save and
reload. Every R-step adds its own scenarios. CI runs it on every push.

### R0.6 ID freeze (happens after R9, before R11)
Re-enable CLAUDE.md rule 3 in full, write the migration framework back (one function per
version step), and add a test that a patch saved by the beta build still opens.

---

## R1 — Classes and Entities (the life-cycle redesign)

Replaces LifeCycle.md's D2–D6 as built in 2b. A new plan file, `wiki/plans/Classes.md`,
holds the full design before code; this is its outline.

### R1.1 The idea
A Population today is a node with a long list of outputs (gate, pitch, velocity, index,
order, age, alive, random1/2, position, start, stop) and a long list of settings. It only
really fits notes. The new model:

- A **Class** says what kind of thing exists: its **props**, each typed (a value with a
  quantity and range, a boolean, a pitch, a choice, a **Vector 3D**), each with how a new
  entity gets it (a fixed value, a random range, a distribution, from the source). Edited
  in a **Class factory**, like adding macros. A class is data, saved with the patch, and
  can be saved to the library.
- An **Entity** is one instance of a class. A cable of entities (today's green Note cable)
  becomes **"Entities of a Class"** — the stream of births, props and ends of that class.
- **A MIDI note is just a class.** The built-in **Note** class has Pitch, Velocity, Gate
  (held), Pressure, Channel. Note In outputs Entities of Note.
- **Foreach** (was Population) takes an entities cable and runs what follows once per
  entity. Its outputs are **the class's props** plus the life signals every entity has
  (Gate, Age, Index, Birth order) — no more Random 1 / Random 2: a random prop in the
  class does that, by name.
- **Spawn** makes entities of a class from events / a rate / always, with a lifetime
  (a fixed time, until released, forever) and a size limit with a "when full" rule.
  Spawn is what Born / Lives / Size / When full were; Foreach no longer carries them.
- **Nesting is a cable of entities, never a stray value.** A Spawn inside a Foreach
  spawns per outer entity (grains per voice, crickets per field); its entities cable into
  an inner Foreach. The accidental nesting of today (a value cable meeting another
  population) is gone.
- Split and Merge stay (Split by a prop: zones, layers, round robin; Merge sums).

### R1.2 Engine work
- `SignalType::Note` becomes **Entities**, carrying a class id; the per-sample
  `NoteEvent` buffer becomes an entity event buffer (birth with prop values, prop
  change, release, end). Only one entities input/output per node, as today.
- `MultiplicityResolver` resolves origins from entities cables only.
- Class props become per-instance signals; Foreach's output ports are built from the
  class (a Foreach whose class changes is a structural change — fresh node).
- Vector 3D props ride on the channel axis (three channels), the decision LifeCycle.md
  report §5 asked for.
- Host MIDI enters as Entities of Note; the special note-in path (`findNoteIn`) goes.

### R1.3 The second half of the stochastic suite, made part of classes
LifeCycle.md's "later" list, rethought so it doesn't add loose nodes:
- **Distribution** is how a class prop gets its value (uniform, normal, weighted list,
  drawn curve) — edited in the Class factory, not a separate node. The same distribution
  editor feeds Events' intervals.
- **Variation** per prop: 0 every entity the same, 1 every entity its own (was "Shared").
- **Markov** — a sequence with a grammar (birdsong syntax) — becomes a class prop source
  (each new entity's value follows the previous one by the transition table), plus a
  standalone node for sequences of values.
- Wander, Brownian, Fractal, Events, the chaos nodes stay as they are; inside a Foreach
  each entity gets its own (seeded by the entity).

### R1.4 UI
- The green cable is drawn as entities, labelled with the class name.
- Foreach shows the class name and its live entity count; its outputs are the class props.
- The Class factory: a list of props (add / remove / reorder like macros), each with type,
  range, unit, default and distribution; a preview of 20 sampled entities.
- Built-in classes: Note, Grain (position in a sample, length, pan), Particle (Position
  Vector 3D, Velocity Vector 3D, Size).

### R1.5 Done means
- A keyboard voice is Note In → Foreach, nothing else.
- Granular keys (10 grains per key, each its own position) is Note In → Foreach → Spawn
  (Grain class) → Foreach → Sampler, with no random-output tricks.
- 50 crickets from a Cricket class with different chirp rates and pitches, each born once.
- All of the above survive save/load, and the old Population tests are rewritten on the
  new model.

---

## R2 — Particle MVP (showcase)

Not ridiculous — with Classes it is small, and it shows where Bazalt is going better than
any preset. Scope kept deliberately tight:

- The **Particle** class (R1.4) with a Position (Vector 3D) prop.
- A **Particle system factory** (the user's term): a node whose interface is a 3D view
  (WebGL, orbit camera) drawing every entity of its class as a dot, coloured and sized by
  any prop; opened large like the other factories. In the MVP it shows and spawns —
  positions come from ordinary nodes wired to the Position props.
- Positions are driven by the nodes that exist: a Wander per entity is Brownian motion, an
  Oscillator per axis is an orbit, a Curve is a path.
- **Locate** (minimal): position → pan, distance gain and air (a gentle low-pass with
  distance), so you hear where each particle is.
- **A "Particle system" Snippet** (R3.6): Spawn + Foreach + Wander per axis + Locate + the
  particle factory, ready to drop.
- **Not in the MVP:** forces, collisions, Boids, Sense, Move (they are stage 2d's later
  work), the full spatial scene (stage 4).
- Done means: 30 particles drifting in 3D, each a sound, heard where it is; the view runs
  at 60 fps with 128 particles.

---

## R3 — Workflow

### R3.1 No errors in normal work
Every message the user sees during ordinary patching is a defect to remove, not to reword:
- **"Needs a Map node"**: never shown — a connection that needs a conversion inserts it
  (as `connectWithAutoAdapt` does on most paths today; find and fix the paths that don't).
- **Delete all** simply leaves Master Out — no red warning, no error (today it shows
  one). An unconnected Master Out is a normal, quiet state.
- **Voice nodes that can't be deleted** after nested setups: reproduce, fix, and test that
  any selection can always be deleted.
- An audit: run the R0.5 harness through 100 scripted editing sessions and fail on any
  error message.

### R3.2 Feedback loops, shown where they are
- The Settings option "Feedback Loop: Mute Audio Input" is JUCE's Standalone guard
  against microphone-to-speaker feedback, not graph loops. Replace it with an input
  on/off and an input meter in the top bar.
- Graph loops: the compiler already knows each feedback region; the canvas marks the
  cables of a loop with a small loop badge and a one-line explanation on hover.

### R3.3 Expose (hide / show props)
- A **+** button next to Bypass opens the node's prop list with eye toggles.
- Nodes may declare props hidden by default (the non-essential ones); unused props can be
  hidden; hidden props keep their values. Saved per node in the patch.
- The same list is where a prop is put on the **Play view** (R7) — one gesture.
- This is what makes the Envelope's override props (R5.4) possible without clutter.

### R3.4 Dropdowns
One dropdown component for every choice in the app: drawn by Bazalt (not the browser's),
opens in the overlay layer so it is never cropped by the node, flips near screen edges,
scrolls with a fade for long lists, type-to-filter for more than ~8 options, keyboard and
wheel support, the selected item marked. The engine's enum options feed it unchanged.

### R3.5 Capture and retro-capture
- Capture this: the menu clash is fixed in R0.4.
- **"Keep the last minute"** — always recording, a button in the top bar. **Cost is
  small:** a ring buffer on the master output of 60 s stereo is 60 × 48 000 × 2 floats ≈
  23 MB (46 MB at 96 kHz; half that stored as 16-bit); writing it is one copy per block —
  negligible next to any synth voice. Pressing the button pauses the writes, copies the
  ring (the Record node's Freeze path already does exactly this) and drops a Sample node
  with the last N seconds, trimmed of leading silence. Length 10 s / 30 s / 60 s.
  **Off by default everywhere** (the user's decision, 2026-10-17); a setting turns it on.

### R3.6 Snippets replace presets
- Population presets leave the Add menu (Voice, Trigger, Swarm, Unison, Grain Cloud,
  Interleave).
- **Snippets**: saved groups of nodes and cables (the clipboard fragment format that
  already exists), each with a name, a one-line description and a picture of its nodes.
  They live in the browser (R7.3) next to patches, and in the Add menu only as a search
  result section below nodes.
- The user saves any selection as a snippet.

### R3.7 Synonyms in search
Each node may list a few synonyms (sparingly: "Sidechain" → Audio In, "LFO" →
Oscillator, "ADSR" → Envelope, "Delay line" → Delay, "Panning" → Pan…). A search lists
direct matches first, then synonym matches with the synonym in a dim italic beside the
name, so the user sees why it appeared. Synonyms live in the node descriptor (C++), so
they stay next to the node.

---

## R4 — Values and conversion

### R4.1 The pitch picker, everywhere
A Pitch is shown and edited as a note — **F#2**, with cents (±50) and the absolute value
(30.00 st) shown beside it, never as a bare float. Used by every Pitch-quantity prop,
Pitch macros, and the Pitch side of the Convert nodes. A Frequency prop shows Hz with the
nearest note beside it, and accepts a typed note ("A4" → 440 Hz). Inspired by the Tune
viewer's look. Keyboard: up/down a semitone, shift for cents.

### R4.2 Convert nodes
One family, one look (the Tune-inspired face when unconnected, an ordinary node when
wired):
- Pitch → Frequency, Frequency → Pitch (the two that exist, made robust; replace the
  slider with the pitch picker when unconnected)
- Time → Frequency and Frequency → Time (a period ↔ a rate: "a 250 ms cycle is 4 Hz")
- Time → Samples, Samples → Time (at the running sample rate)
- Beats → Time, Time → Beats (at the host tempo)
- Gain → dB, dB → Gain
- Semitones → Ratio, Ratio → Semitones (transposition as a playback rate)
- Milliseconds → Seconds and Seconds → Milliseconds (the user's list). Both are already
  one quantity (Time) inside the engine, so these convert a *plain number* (a typed 250,
  an index, a count) into a Time and back — useful where a value arrives as a bare
  number. Every Time display also shows ms below 1 s.

### R4.3 Quantize and Bitcrush
- **Bitcrush** (was Crush): the audio effect.
- **Quantize**: for values — to a step (0.25), to a count of levels, to a scale (pitch:
  major, minor, pentatonic, custom), to musical divisions (time: 1/16, triplets), with a
  glide amount and a hysteresis so a wobbling value doesn't flicker between steps.

### R4.4 Map (the new one) and Remap
- **Remap** = today's Map (in range → out range, linear), unchanged apart from the name.
- **Map** = a 2D curve from input to output, edited in a **Map factory** (the curve
  editor, R5.3): custom ranges, output unit/quantity, curves (exponential, S, steps),
  presets including quantising curves and scales. Unwrap-able into Remap + Quantize where
  that is exact.

### R4.5 Steps (sequencing) and Data
- Today: a node that spawns 32 values is too much.
- **Steps**: a sequencer of 1–16 steps drawn like a curve in its body (drag bars, typed
  values on double-click), clocked by an event or the host tempo, with per-step gate and
  glide, output quantised by Quantize if wanted. That covers "simple arp/seq".
- **Arp**: Entities of Note in → Entities of Note out (up/down/random/as-played, rate,
  octaves, gate length) — a natural fit once notes are entities (R1).
- Showcase patches (R11) show the other way: sequences from math, curves and oscillators.

### R4.6 "From Pro-Q" EQ (the user's item — decision pending)
Load a FabFilter Pro-Q preset as a `data.eqCurve`. Risk: the preset format is
proprietary and undocumented — importing it means reverse-engineering, and FabFilter's
terms may forbid it. A safer alternative: import from the text a user copies out of
Pro-Q's band display, or from a common EQ text format. The user decides whether this
stays before release.

---

## R5 — The native look ("remove every trace of the web")

### R5.1 Principles
- Depth from two or three background tones and hairlines — no drop shadows, glows,
  glassmorphism, gradient fills, hover lifts, pill buttons or large rounded cards.
- Nature through motion and texture, not illustration: a faint grain on surfaces; motion
  that eases out like a damped spring; colour only where it means something (signal
  types, the spectrogram map).
- One type scale and one spacing scale from `tokens.ts`; no ad-hoc sizes in components.
- Copy: short labels with units ("Grain Size · 120 ms"); no sentences in controls, no
  slogans, no competitor names ("Serum-style" and similar go), no em-dash prose.
- Nothing the browser draws by default: menus, tooltips, scrollbars, focus rings,
  selects, file pickers styled or replaced.

### R5.2 The surfaces, in order
1. **Shared components** (built first, used everywhere): menu, dropdown (R3.4), tooltip,
   text field, number field, toggle, segmented control, panel, list with sections.
2. **Tooltips**: a small label after a short delay — name, value, shortcut. Rewrite every
   tooltip text; most become two to five words.
3. **Add menu**: rebuilt on the shared components — categories as a quiet column, results
   with icons, synonym matches (R3.7) and snippets (R3.6) as dimmer sections, keyboard
   first.
4. **Settings**: off the main UI into a small panel; legacy options removed (R3.2).
5. **Factories** (R5.3).

### R5.3 Factories share one frame
Curve, Envelope, Wavetable, EQ, Sample, Map, Class: the same window frame, header
(name, preset browser, undo/redo, close), toolbar and side panel, and **one preset
browser** for factory content (curves, envelopes, wavetables, EQs, samples, maps,
classes) with the R7 browser's look.
- **Curve editor**: free placement with optional snap; a labelled grid in real units
  (time in ms/s, value in the target's unit) with square cells; numeric entry for the
  selected point (time, value, tension); multi-select and move; smooth tension handles.
- **Wavetable editor**: drop the "Serum-style" wording; consistent with the curve editor.

### R5.4 Envelope: shape in the factory, numbers on the node
Hybrid, as the user proposed: the factory builds the shape and shows the A / D / H / S / R
times in ms editable as numbers; the Envelope node can **override** any of them with its
own props (wireable, automatable), hidden by default and exposed with Expose (R3.3). An
overridden time stretches its segment of the shape; the factory shows which are
overridden.

---

## R6 — The visual suite

- **Fixes:** saved time windows (R0.4), stale/blank viewers (R0.4), the node-width bug
  (R0.4).
- **Resizable views**: Scope, Spectrum, Spectrogram, Cycle, Ripple, Meter resize by their
  corner; the size is saved with the patch.
- **Quality follows zoom and size**: views redraw at the size they are shown (SVG or a
  canvas sized from the real on-screen size, never a bitmap stretched by the zoom). The
  Spectrogram already does this; the rest follow.
- **Spectrum and Meter** move off the legacy preview box onto the Scope-style panel (the
  Spectrogram's route).
- **Spectrogram**: a Time setting (2–30 s), as Scope has.
- **Ripple**: a nicer ripple (rings that decay and interfere, drawn as a few SVG strokes
  with opacity — no per-pixel work).
- **Cycle view at high modulation rates** (the AM problem): the live view stays for slow
  modulation; above a threshold (like the playhead's) it switches to a **phase-density**
  view — every cycle folded onto one phase axis and drawn as brightness where the signal
  spends time. AM becomes a band between its quietest and loudest shapes, FM a smeared
  shape; readable at any rate, cheap to compute on the analysis thread.
- **Oscillator looks like Sine/Saw**: the curve-player Oscillator gets the same cycle
  view as the fixed shapes, showing its current curve or wavetable frame.
- **Feedback for wavetables and custom waves**: the Wavetable node shows the current
  frame's cycle and a frame strip with the playhead; a Curve node shows its curve with a
  playhead per consumer.
- **Better oscilloscoping** (the user's words): the cycle view's two modes (live and
  phase density) also give a proper scope — a trigger on any cable, a held frame, and a
  cursor that reads value and time.
- **Wow, not noise**: one consistent visual language (thin traces, dim fills, the colour
  of the signal type), tuned against the nature direction once R5's principles exist.
- (One item of the user's list was cut off — "making more complex …" — to be added.)

---

## R7 — Play view, macros, browser

### R7.1 Macros
- Host parameter names follow the macro names (Ableton shows "Cutoff", not "Macro 7");
  renaming a macro updates the host (VST3 parameter info change notification; tested in
  Ableton, FL Studio, Bitwig, Reaper, Cubase).
- A macro bar at the top of the editor: reorder by drag; shows value and unit; click to
  find its node.
- The 32-slot limit stays an engine fact (ADR-0015) but disappears from the UI: exposing a
  prop to the Play view (R3.3) claims a slot automatically.

### R7.2 Play view
- The default view when a patch opens (Edit is one click away): the patch name, its
  picture(s) (from the patch's image assets), a short description, its tags, and the
  macros in their order — nothing else. Resizes cleanly; works as the face of a preset.
- Patch metadata gains description, tags, mode (R8), author, pictures.

### R7.3 Browser
- Patches and snippets in one browser: search, categories, **tags**, favourites, recent.
- **Behaviour tags** (fixed set): **MIDI Instrument**, **Effect**, **Audio Instrument**
  (played by audio — Fire Guitar), **Generative** (plays itself), plus free sound tags.
- Preview on hover/select for factory content (a short pre-rendered audio clip shipped
  with each factory patch, played by the engine).

---

## R8 — Bazalt FX and host modes

- Two plugins from one codebase: **Bazalt** (instrument) and **Bazalt FX** (audio effect).
  Plus the Standalone.
- The UI knows its mode. Nodes that cannot work in it get a small warning dot (never a
  block): Note In in FX without MIDI, Audio In main bus in the instrument.
- **Audio In** offers only the inputs that exist: Main + Sidechain in FX, Sidechain in the
  instrument, the device inputs in the Standalone.
- Patches save their mode; the browser filters by it (a patch tagged for both is fine).
- Done means: Bazalt FX loads as an audio effect in every DAW in the matrix and processes
  a track with its sidechain.

---

## R9 — Performance

- **Imports off the message thread:** decoding, FLAC storing, analysis (root, transients)
  and wavetable extraction run on a worker with a progress indicator; the UI never
  freezes. Files dropped on the canvas are read by path where the WebView gives one,
  instead of travelling as base64.
- **Telemetry stops when nothing looks:** no analysis work while the editor is closed.
- **CPU audit:** per-node cost table; denormal flushing everywhere
  (`juce::ScopedNoDenormals`); idle voices cost nothing; 64 grains and 16 voices of a
  typical patch measured on a mid-range laptop.
- **Recompile cost:** batch the multi-command gestures (`applyBatch`, CLAUDE.md's open
  note) so a gesture is one recompile.
- Done means: a published CPU budget per factory patch, and no UI stall over 50 ms in the
  R0.5 scenarios.

---

## R10 — Mac (parallel track, starting at R3)

What it takes:
- **Accounts and certificates:** the Apple Developer Program (yearly fee); a *Developer ID
  Application* certificate (signing) and *Developer ID Installer* (signing the .pkg);
  notarization with `notarytool`; the hardened runtime with entitlements (microphone for
  the Standalone's audio input, network client for licensing).
- **Build:** JUCE and the engine are already portable (CLAUDE.md rule 7 kept the engine
  free of platform code). Add **AU** to `FORMATS` (Logic only hosts AU); a **universal
  binary** (arm64 + x86_64); a minimum macOS version suited to WKWebView.
- **WebView:** on macOS JUCE uses WKWebView instead of WebView2. The WebView2-only setup
  in `PluginEditor.cpp` and the per-instance user-data folder are already Windows-specific
  and need `#if JUCE_WINDOWS`; `CopyUiDist.cmake` must copy `ui/dist` into the AU and
  `.app` bundles too; check the resource provider and the vendored JS bridge on WKWebView.
- **Paths:** `userApplicationDataDirectory` is `~/Library` on macOS — the sample library
  and patches belong in `~/Library/Application Support/Bazalt`; fix the path helpers.
- **UI:** Cmd instead of Ctrl for shortcuts; Ctrl-click as right-click; Retina pixel ratio
  in every canvas; trackpad pinch for zoom; the font stack.
- **CI:** a macOS runner (GitHub Actions) building, testing (both Catch2 suites),
  validating (`auval` and pluginval) and packaging a signed, notarized `.pkg`.
- **Installer:** a `.pkg` (pkgbuild/productbuild) putting the VST3 in
  `/Library/Audio/Plug-Ins/VST3`, the AU in `/Library/Audio/Plug-Ins/Components`, the app
  in `/Applications`, content in Application Support.
- **Test hosts:** Logic Pro, Ableton Live, FL Studio (Mac), Bitwig, Reaper.
- **Effort:** roughly one to two weeks of work once a Mac (or a reliable macOS CI runner)
  is available, plus Apple account set-up time. Starting early finds WKWebView and AU
  surprises while there is time.

---

## R11 — Content (after the ID freeze)

Quality over quantity, all following the nature direction, all made in the final UI:
- **Tutorial** category: one patch per idea, each with its Play-view description saying
  what to look at (a voice, a Foreach of crickets, granular keys, Weave, the Spectrogram,
  Capture, a particle field, Map and Quantize, an FX patch on a guitar).
- **808 and trap basses**: a dedicated pack inspired by modern underground sound (the
  user's references: Slayr, waera, ok) — long 808s with pitch glides, distortion and
  sub control on macros; plucks, bells, dark pads.
- Categories for instruments, textures, generative, FX.
- **Library content**: a small set of samples (recorded for Bazalt or properly licensed —
  every sample's licence recorded), wavetables, curves, EQ shapes, maps, classes and
  snippets.
- Each factory patch: tags, mode, description, a picture, a preview clip, macros named and
  ordered, CPU checked.

---

## R12 — Closed beta

- 5–15 chosen testers; builds expire after a set date; a version shown in the top bar.
- **"Send feedback"** in the app: attaches the patch, the log, the version, the host.
- **Crash safety:** autosave every minute; recovery offered on next start; a crash log
  written next to the autosave.
- A short questionnaire per build; a private channel (Discord or similar).
- DAW matrix run before each beta build: Ableton, FL Studio, Bitwig, Reaper, Cubase,
  Logic (Mac) — save/reopen/duplicate, automation, offline bounce, sample-rate change,
  10 instances, editor closed CPU.
- Beta exit criteria: no crash in the last two weeks of testing, every R0.4 / R3.1 item
  closed, testers' patches open in the next build.

---

## R13 — Commercial

- **Legal:** check the JUCE licence tier for the expected revenue; the current VST3 SDK
  licence and the VST logo terms (Steinberg changed the SDK licence recently — confirm
  the terms in force); a trademark search for "Bazalt" and the domain; an EULA, a privacy
  policy and a third-party licence notice (JUCE, fonts, any libraries, sample licences).
- **Store:** a merchant of record (Paddle, Lemon Squeezy, FastSpring) handles EU VAT —
  the right choice for a Czech seller.
- **Licensing:** a signed licence file (Ed25519 signature, public key in the plugin),
  activated once online, then working offline; a small number of machines per licence.
  Kept simple — copy protection costs honest users more than pirates.
- **Demo / free tier:** limited to Sine, Square, Saw and a small set of nodes for very
  simple patches — **but every factory preset opens and plays**, so the power is heard;
  editing a preset with full-version nodes asks for the full version. No time limit, no
  noise bursts.
- **Updates:** the app checks a small JSON on the website for the latest version and
  shows "Update available" with a link; installers update in place. No silent
  self-updating inside a DAW.
- **Installers:** Windows — Inno Setup or WiX, **code-signed** (avoids SmartScreen
  warnings), a guided install choosing VST3 / Bazalt FX / Standalone / content location,
  the WebView2 runtime checked and installed when missing (a blank UI on some Windows 10
  machines otherwise). Mac — the signed, notarized `.pkg` (R10).
- **Website:** product page, videos (more than text for this kind of tool), the manual
  (the public NODE_GUIDE that stage 1e planned), purchase, downloads, account.

## R14 — Launch

Release builds for Windows and Mac from CI, the content pack, the website, a launch video
of the Play view and of a patch being built, and the first patch-sharing channel.

---

## Later (not before 1.0)

- Bake, the Convolver and captured rooms/materials (Sampling.md 3b).
- Stage 2d: Sense, Move, Boids, Kuramoto sync — on top of R2's Particle MVP.
- Stage 4's spatial scene.
- Match EQ (skipped for now). Hosting VST3 inside Bazalt (not doing).

## Open questions

- Spawn and Foreach as two nodes, or Foreach with a source setting? (R1 plan decides.)
- Does Markov earn a standalone node, or only the class-prop source?
- Mac at launch, or Windows first with Mac within weeks? (The user: Mac is required —
  this asks only about the launch order.)
- The cut-off visualization item in the user's list.
