# 0029 — `view.scope/spectrum/meter`: per-instance preview settings, input-port taps, taps that survive a recompile

## Status
Proposed (M21). Not implemented — awaiting sign-off on the three open questions at the end.

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

## Decision (proposed)
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
makes `subscribe` return `false`, and the card shows its empty state.

**3. Live subscriptions are a registry, re-applied after every publish.** The processor records each active
`(nodeId, portId, kind)` on the message thread; `GraphEditController::recompileAndPublish()` ends by
re-resolving every entry against the fresh plans (both domains, and the voice-domain slots) and re-reading
`getPreviews()`. That fixes fact 4 for every existing preview, and it is also what makes a *parameter edit* of
a view node take effect: the edit recompiles, the re-attach picks up the new settings.

**4. Per-tap settings are a small block of relaxed atomics in the hub's slot**, written on the message thread
at attach time and read by `AnalysisThread` once per drain. Nothing on the audio thread changes.

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

## Order of work
Each step builds, tests and commits on its own, so the first is worth doing whatever is decided below.
1. Subscription registry and re-attach after publish, with a regression test — fixes CLEANUP P1 #5.
2. Input-port taps, then the three nodes with the *current* fixed analysis — a working scope/spectrum/meter
   end to end before any setting has an effect.
3. The settings, one consumer at a time in `AnalysisThread`, each with a test that the frame really changes.
4. UI: render `enumOptions` as a dropdown (see Q3), verify all three in the running Standalone app.

## Open questions
- **Q1 — how long may a scope window be?** `view.scope` on an LFO (the headline use of the *Control* input) wants
  seconds; the tap ring holds ~186 ms at 44.1 kHz. *Recommend:* cap at the ring in this milestone and state the
  limit on the node; a per-slot rolling min/max history is the follow-up that lifts it.
- **Q2 — defer `PerNote` trigger and the `Histogram` meter mode?** Both are in the catalog, both need something
  new (a note marker from the engine; a new frame type). *Recommend:* keep them in the enums, don't expose them
  yet, and say so.
- **Q3 — fix the enum-dropdown UI gap alongside?** The UI never reads `enumOptions`, so `triggerMode` and
  `mode` would render as integer sliders (already reported, and listed in MILESTONES.md as not M21's to fix).
  Two of the three view nodes are enum-driven. *Recommend:* yes, as step 4.

## Consequences
- Every existing preview gains the recompile fix as a side effect.
- The hub slot grows a settings block and `AnalysisThread` grows per-slot state (smoothing, FFT-order dispatch);
  both stay preallocated in `prepare()`.
- `PreviewDescriptor`'s per-type fields become defaults rather than the live truth; the doc comment says so.
