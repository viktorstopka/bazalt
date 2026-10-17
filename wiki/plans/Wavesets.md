# Wavesets — sound cut at its zero crossings

**Names (2026-10-17, Release.md R0.2):** the nodes are now **Cycle Cuts** (`cycle.cuts`)
and **Weave** (`cycle.weave`); this file keeps CDP's word, waveset, for the idea.

**Status:** W1 built 2026-10-16 (see §7); W2–W3 proposed. Agreed with the user in
conversation (they want the Live Interleave first); scheduled as part of stage 3a
(`wiki/ROADMAP.md`, `wiki/plans/Sampling.md` 3a.4).

---

## 0. Origin

The user heard Composers Desktop Project's **Distort: Interleave** in Sound Thread
(Jonathan Higgins) and asked whether Bazalt could do it. CDP is LGPL 2.1 and its source is
public (`github.com/ComposersDesktop/CDP8`); the algorithm below is read from
`dev/distort/distintlv.c` (mode 1, "interleave wavecycles") and `distresize.c` (mode 2,
"impose wavecycle-lengths of 1st file on wavecycles of 2nd"). Bazalt reimplements the
idea; no CDP code is copied.

## 1. What CDP does

- A **waveset** (Trevor Wishart's term; CDP calls it a wavecycle) is one positive half and
  one negative half — the stretch between two zero crossings in the same direction. On
  anything but a pure tone these pseudo-cycles have very uneven lengths, which is where
  the character comes from.
- **Interleave:** waveset 1 of A, waveset 1 of B, waveset 2 of A, … copied whole, sample
  for sample, no resampling, no crossfade — every join is at a zero crossing, so nothing
  clicks. If B starts with the opposite polarity, every B waveset is inverted so the joins
  line up. Each file advances only by what it contributed, so the output is about as long
  as both inputs together: each sound plays at roughly half speed, at its own pitch.
- **Resize (mode 2):** each waveset of B is resampled to the length of the matching
  waveset of A — B's timbre, A's pitch and rhythm.

## 2. Why it needs rethinking for Bazalt

Offline, consuming each source at half speed is free. Live, a source can't be consumed
slower than it arrives forever (the same causality limit as "speeding up a signal",
2026-10-12): the backlog grows without end. So there are two modes:

- **Live** (the user's favourite) — both sources keep running in real time. When the
  playing waveset ends, the next one is the **newest complete** waveset of the next
  source. Whole wavesets, joins at zero crossings, polarity matched; each source loses
  what the other one is playing instead of being stretched. Latency: at most one waveset.
- **Stretch** — faithful to CDP: wavesets are consumed in order and the output stretches;
  a **Max backlog** (e.g. 0.5 s) jumps back to "now" at a zero crossing when the delay
  grows too large. Unbounded on a recorded sample (stage 3).

## 3. The design: two primitives, not one node per CDP effect

Every CDP DISTORT process is "cut into wavesets, then choose which ones to play, how".
Bazalt splits that into a capture (analysis + live buffer, as Data) and a player.

### `waveset.capture` — Wavesets

- **In:** `in` (audio).
- **Out:**
  - `wavesets` — **Data(wavesets)**: a rolling buffer of the most recent complete
    wavesets with their boundaries (a fixed ring, ~100–200 ms, allocated in `prepare()`);
  - `boundary` — Event at each completed waveset;
  - `length` — the last waveset's length (s); 1/length is a crude pitch;
  - `peak` — the last waveset's peak;
  - `count` — running count.
- **Settings:** **Min length** (shorter wavesets are merged into the next — keeps noise
  from becoming pure fizz); **Group** (N wavesets count as one, CDP's group size);
  **Direction** (upward or downward crossings).
- Useful alone: trigger anything from the sound's own vibration, or turn the zero-crossing
  rhythm of a guitar into a modulation.

### `waveset.play` — Waveset Play

- **In:** a growable group of `wavesets` Data inputs (A, B, C …).
- **Out:** `out` (audio); `switch` (Event when the source changes); `source` (index of the
  input playing).
- **Settings:**
  - **Pattern** — wavesets per input per turn (1:1 is CDP's Interleave; 3:1, 1:2:1 …).
  - **Mode** — Live / Stretch; **Max backlog** for Stretch.
  - **Polarity match** — invert a waveset that starts on the other side.
- Later step options, per pattern step: **Repeat** ×N (stutter time-stretch), **Omit**
  (every Nth), **Reverse** (each waveset backwards), **Shuffle**, **Average**,
  **Multiply** (transpose by cycle multiplication) — the rest of CDP DISTORT as options of
  one player, not twenty nodes.

### `waveset.imprint` — Imprint (mode 2)

Lengths from A, shape from B: each output waveset is B's newest complete waveset resampled
to A's newest waveset length. Latency: one waveset of A. May later become a Play option.

### Interleave is a preset, not a node

```
Audio In (guitar) ──► Wavesets A ──┐
                                   ├──► Waveset Play (pattern 1:1, Live) ──► out
Synth voice ──────► Wavesets B ────┘
```

```
A (guitar):  |a1 ...|a2 ..|a3 ....|a4 ..|
B (synth):   |b1|b2|b3|b4|b5|b6|b7|b8|b9|
Output:      |a1 ...|b4|a3 ....|b7|...
             each turn takes the newest whole waveset of the other source
```

## 4. Engine notes

- Capture keeps a ring buffer of samples plus a ring of boundary indices; Play reads by
  index through the Data cable (`DataPublisher`, like Curve / Wavetable). Everything is
  preallocated; no allocation on the audio thread (rule 2).
- A waveset longer than the ring (a very low or silent signal) is cut at the ring size —
  Play never waits forever; silence plays as silence.
- Stereo: per channel lane, but channel boundaries differ, so stereo Play follows the left
  channel's boundaries by default (open: per-channel independence as an option).
- Per instance: Capture and Play are ordinary nodes, so a waveset player inside a
  Population runs per grain.
- Block-size invariant (rule 6): boundaries and switches are found per sample.

## 5. Batches

| Batch | What | Done means |
|---|---|---|
| **W1** | `waveset.capture` + `waveset.play` (Live, pattern, polarity) + the Interleave preset | A sine and a saw interleaved 1:1 measure as alternating whole cycles; every join is at a zero crossing; latency ≤ one waveset; block-size invariant. |
| **W2** | Stretch mode with Max backlog; `waveset.imprint` | Stretch on a recorded sample matches CDP's output length (≈ sum of both); Imprint of a sine onto noise has the sine's pitch. |
| **W3** | Step options: Repeat, Omit, Reverse, Shuffle, Average, Multiply | Each measured against its CDP counterpart on a recorded sample. |

## 6. Open questions

- Ring length: fixed (e.g. 200 ms) or a setting? Longer rings make Stretch's backlog limit
  meaningful live.
- Does `wavesets` Data also feed a **Waveset view** (stage 3's Spectrogram family) — the
  boundaries drawn over the waveform?
- Polyphony: a guitar chord has no single clean zero-crossing rhythm — that *is* the
  sound; no attempt to "fix" it.

## 7. As built

**W1 (2026-10-16).**
- `waveset.capture` (`WavesetNodes.h`) is built on the Sampling stage's live-buffer contract
  (`DataBuffer::Live`, `wiki/plans/Sampling.md` 3a.3): its `wavesets` output is a live
  sample (one second of the input, a ring) tagged `Data(wavesets)`, plus a ring of 4096
  waveset starts in absolute frames (`Live::marks`) and the frame count (`Live::written`).
  At sample s of a block of n a reader's "now" is `written − n + s`, so a mark found later
  in the block is still in the future for it — block-size invariant without a per-sample
  companion. A Sampler accepts `Data(wavesets)` too (it is a live sample).
- Min length, Group and Direction as designed; a waveset is cut at 0.25 s at most.
- `waveset.play`: a growable group of 2–8 `Data(wavesets)` inputs (A, B, …), **Count**
  (wavesets per input per turn, 1–16, wireable) and **Polarity match**. Live mode only:
  a turn starts on the source's newest complete waveset; within a turn the next ones
  follow in order once complete (else the newest repeats). Outputs `out`, `switch`,
  `source`.
- The **Interleave** preset (Add menu, `ui/src/graph/presets.ts`): two Wavesets into one
  Play.
- Not yet: a different count per input (the pattern 3:1, 1:2:1 — wants a pattern editor,
  probably the node's content), stereo, Stretch and Imprint (W2), the step options (W3).
- Tests: `tests/WavesetTests.cpp` (a cycle per crossing, Group; a sine and a saw interleave
  as alternating whole cycles of 240 and 150 samples with every join next to zero; Count 3;
  identical output at block sizes 64/97/480/512); `tests-plugin/WavesetPluginTests.cpp`
  (the patch compiles through the controller and keeps playing when a third source is
  wired in).
