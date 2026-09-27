# 0029 — `view.scope/spectrum/meter`: per-instance preview settings, input-port taps, taps that survive a recompile

## Status
Accepted and fully implemented (M21), 2026-09-25/27 — the three open questions below were answered as
recommended, and all four steps are done. Verified live in the Standalone app: an `osc.analog` with nothing
wired to its allocator, feeding a Scope, a Spectrum and a Meter, showed a real saw waveform, a real rolled-off
spectrum and a lit true-peak reading — the whole subscribe → attach → analyse → draw path, end to end.

## Context
`view.scope`, `view.spectrum` and `view.meter` (NODE_CATALOG.md) are placeable nodes whose whole job is to show
the signal wired into them. Their settings are per *instance* — `timeWindow`/`triggerMode`, `fftSize`/`tilt`/
`averaging`, `mode` — but the M20 preview machinery was built around per-*type* declarations. Reading the code
to see what that means turned up four facts, all verified against the current tree rather than assumed:

1. **No preview setting is consumed anywhere.** `PreviewDescriptor` carries `timeWindowSeconds`, `triggerMode`,
   `fftSize`, `tiltDbPerOctave`, `averaging` and `meterMode`, and `NodeDescriptorJson` serializes them, but
   `AnalysisThread` uses fixed values (a 2048-point FFT; the scope frame is 128 min/max buckets over whatever
   `readLatest` returns, at most 8192 samples; the meter is peak-with-ballistics plus RMS) and `NodePreview.tsx`
   draws with functions that take none of them. So even `osc.analog`'s declared 50 ms window is descriptive
   only. Adding settings to a node would change nothing on screen.
2. **Settings are per type, the node's are per instance.** `getPreviews()` is a `const` virtual, and
   `describeAll()` calls it on a default-constructed instance, so the UI's descriptor can never reflect a placed
   node's own values.
3. **A tap can only attach to an output.** `subscribeVisualizationTap` resolves through
   `ExecutionPlan::outputBufferIndexByNodeAndPort`; `PreviewDescriptor::portId`'s comment says "input or output"
   but only outputs work. A view node has inputs only.
4. **Taps do not survive a recompile** (CLEANUP.md P1 #5, verified: 512 samples before a parameter edit, 0
   after, for a global-domain tap). This already affects `osc.analog`'s and `mix.gain`'s previews, and it is
   fatal for a scope node, whose settings are edited by recompiling.

## Decision
**1. The node instance is the single source of truth for its settings.** `view.scope` etc. hold their
parameters and override `getPreviews()` to return a `PreviewDescriptor` built from the *current* values. The
engine reads it from the live node whenever it attaches a tap. The UI needs no settings plumbing: `NodePreview`
keeps drawing whatever frames arrive. Everything that changes what is shown — including the spectrum tilt —
is applied in `AnalysisThread`, so nothing is computed in TypeScript (CLAUDE.md rule 1). The per-type
descriptor fields stay as defaults for the component gallery.

**2. A view node's tap is keyed by the view node, and resolves through its input.** Tap name
`node:<viewNodeId>:in`; the engine finds the output buffer feeding that input port (a new
`ExecutionPlan` lookup by input port) and attaches there. Keying by the view node rather than the source means
a scope and a spectrum on the same cable get independent taps with independent settings. An unconnected input
is accepted as pending (see 3b) and the card shows its empty state until a cable arrives.

**3. Live subscriptions are a registry, re-applied after every publish.** The processor records each active
`(nodeId, portId, kind)` on the message thread; `GraphEditController::recompileAndPublish()` ends by
re-resolving every entry against the fresh plans (both domains) and, from step 3 on, re-reading
`getPreviews()`. That fixes fact 4 for every existing preview, and it is also what makes a *parameter edit* of
a view node take effect: the edit recompiles, the re-attach picks up the new settings.

**3a. Built as a per-plan enable flag, not per-voice slots.** The M20 voice-domain protocol (a slot holding a
buffer index the audio thread read back) can't survive a recompile - the index changes with the layout - and
updating it in place races the audio thread. Instead every voice plan carries the tap and
`ExecutionPlan::previewTapsEnabled` decides which one pushes; `pointVoiceTapsAtCurrentVoice()` sets it to the
most recently triggered voice on every note-on and at every block start (8 relaxed loads), so a mismatch caused
by a note-on landing mid-publish heals within a block. The audio thread no longer needs a buffer index at all,
and `VoiceDomainTapSlot` is gone. `Tap::getTotalPushed()` was added so tests (and later the analysis side) can
tell a live tap from stale ring contents.

**3b. Found while building step 2 — three consequences of "a viewer taps the buffer wired into its input".**
- *A buffer needs several taps.* A source's own preview (`osc.analog`'s waveform on `out`) and a `view.scope` on
  that cable resolve to the same buffer, and the plan held one tap pointer per buffer, so the second attach
  silently displaced the first. `ExecutionPlan` now holds `maxTapsPerBuffer` (4) per buffer, with
  `addTapForBufferIndex()`/`removeTap()`; a fifth is refused rather than displacing anyone.
- *A viewer is placed before it is wired.* Its first subscribe therefore finds an input with nothing behind it,
  and refusing it would leave the scope blank for good after the user connects a cable. A subscription to a port
  that exists but doesn't resolve is accepted as **pending**: the hub tap is claimed, nothing is pushed, and the
  next recompile's re-attach binds it. `subscribeVisualizationTap` returns `false` only for a port the node
  doesn't have. (The same applies to an output buried in a per-sample region: pending forever, not refused.)
- *Port ids must be unique across a node's inputs and outputs*, because `findTappableBufferIndex()` and the tap
  name `node:<id>:<port>` identify a port by id alone. A test now asserts it for every registered type.
- Known limit: unplugging a viewer leaves its last captured frame on screen (the ring keeps its contents and the
  analysis republishes them), the same as a scope in a stopped host.

**4. Per-tap settings are a small block of relaxed atomics in the hub's slot**, written on the message thread
at attach time and read by `AnalysisThread` once per drain. Nothing on the audio thread changes.
Built as `TapSettings` (`telemetry/TapSettings.h`), stored per slot in `TelemetryHub`. The defaults are exactly
what every tap got before, so the five baseline taps of the M5 panel are untouched. The processor fills it from
the live node's `getPreviews()` on every attach (`TapSettings::fromPreview`), which is what makes a parameter
edit apply: the edit recompiles, the re-attach re-reads. `AnalysisThread` gained one FFT and window per size
(built in `prepare()`), per-slot smoothing state, and `processSlotForTesting()` so each setting is tested
against known input without racing a live thread.

Decisions taken while building it: the meter frame stays `{ line, bar }` for every mode (the UI draws it as it
always did, so it needs no change): `Peak` is the ballistic sample peak over an RMS bar as before, `TruePeak`
swaps in a 4x inter-sample peak, `Rms` puts the line on the bar. The true peak is a 16-tap windowed-sinc
estimate (a first cubic-interpolation version read about 1 dB low on a full-scale fs/4 sine, so it was dropped);
it is not the ITU-R BS.1770 measurement and the node says so. Spectrum tilt and averaging are applied before
publishing; averaging runs on raw magnitudes so a tilt change never contaminates it, and a new FFT size starts a
fresh average. A rising-edge trigger uses the middle of the observed range as its level, so it works for a
unipolar control signal as well as a bipolar one. Frame buffers and the WebView read buffer are now sized by one
constant (`maxTelemetryFrameBytes`) that covers an 8192-point spectrum.

**5. What each setting does, and what is deliberately not in the first cut.**

| Setting | Effect | Note |
|---|---|---|
| `scope.timeWindow` | bucket the latest `window` samples into the 128 min/max buckets | capped at the tap ring's capacity (see Q1) |
| `scope.triggerMode` | `Free`: latest window. `RisingEdge`: start at the most recent rising zero-crossing that leaves a full window after it, else fall back to `Free` | `PerNote` needs note times the tap doesn't carry (see Q2) |
| `spectrum.fftSize` | 512…8192, one preallocated FFT + window per order; bin count already travels in the frame | needs tap capacity ≥ 8192 (it is exactly 8192, `PluginProcessor::prepareToPlay`) **and** frame buffers larger than today's 16384 bytes: 4096 bins is 16 KB plus the header |
| `spectrum.averaging` | per-slot exponential smoothing of bin magnitudes | ~1 MB across 64 slots |
| `spectrum.tilt` | dB/octave about 1 kHz applied to the bins | display slope, still done engine-side |
| `meter.mode` | `Peak` (today), `Rms` (today), `TruePeak` (4× oversampled peak, analysis side) | `Histogram` needs a new frame payload and drawer (see Q2) |

**6. Ports.** `view.scope` and `view.meter` take Audio *or* Control: a polymorphic input using the
ADR-0027 `InheritingPortsNode` mechanism, exactly as `util.reroute`. `view.spectrum` is Audio-only. None has an
output. They use the existing Horizontal card layout, which already mounts one large centred preview, so
`NodeCard.tsx` needs no per-node code.

## Order of work — all four steps done
1. Subscription registry and re-attach after publish, with a regression test — fixes CLEANUP P1 #5. (`b317a30`)
2. Input-port taps, then the three nodes with the *current* fixed analysis. (`20908ba`)
3. The settings, one consumer at a time in `AnalysisThread`, each with a test that the frame really changes.
   (`c1db739`)
4. UI + two real bugs found verifying it live, below.

**Step 4.** `parameterOptions()` in `NodeCard.tsx` renders a real enum parameter's `enumOptions` (label list)
as a `TriggerSelect` dropdown exactly like a mock descriptor's `options`, keyed on `kind === 'enum'` — every
real enum parameter's value already IS its option index (checked across every registered node type), so no
new mapping was needed. A Horizontal-layout node with no output (every viewer) gets a taller preview
(`.node-horizontal-preview-viewer`, keyed off "no output" so any future viewer gets it for free) instead of
the 32px thumbnail meant for a node whose preview is secondary to its value.

Verifying step 4 live (craft a saved patch via a throwaway test that calls `getStateAsJson()`, swap it into
`%APPDATA%\Bazalt\Bazalt.settings`, launch the Standalone app, screenshot, restore the real settings file —
see `bazalt_project_status.md`'s UPDATE for the recipe) found two real bugs, both fixed here, neither
specific to this feature:

- **`frameTypeForPreviewKind(kind) ? …` treated `TelemetryFrameType.Oscilloscope` (0) as falsy.** Every
  Waveform preview — `osc.analog`'s own, and now Scope's — silently rendered nothing, since M20. Fixed to
  `!== undefined` at all four call sites (`NodePreview.tsx`, `NodeCard.tsx`).
- **An oscillator with nothing wired to its pitch was silent DC**, even though its card displays "Frequency
  440 Hz": `PolyBlepOscillator` starts at 0 Hz and only a connected pitch/frequency port, or a saved
  parameter, ever calls `setFrequency()`. Invisible through M0–M20, where every `osc.analog` sits in a voice
  graph and the allocator always drives pitch; real as soon as M21's mono-graph path lets one play on its
  own. Fixed: `OscillatorNode::prepare()` now calls `setFrequency(440.0f)` — the same constant the port's own
  descriptor default already names — before the compiler applies any saved parameter, so a saved frequency
  still wins. Regression test: "An oscillator with nothing wired to its pitch sounds at its displayed 440 Hz".

## Decisions on the open questions (2026-09-25)
- **Q1 - scope window length:** capped at the tap ring (8192 samples, ~186 ms at 44.1 kHz) for now, and the
  node says so. A per-slot rolling min/max history is the follow-up that lifts it.
- **Q2 - `PerNote` trigger, `Histogram` meter mode:** stay in the enums, are not exposed, and the nodes say so.
- **Q3 - enum dropdowns:** yes, fixed as part of this work (step 4), since two of the three view nodes are
  enum-driven.

## Consequences
- Every existing preview gains the recompile fix as a side effect.
- The hub slot grows a settings block and `AnalysisThread` grows per-slot state (smoothing, FFT-order dispatch);
  both stay preallocated in `prepare()`.
- `PreviewDescriptor`'s per-type fields become defaults rather than the live truth; the doc comment says so.
