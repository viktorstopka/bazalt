#pragma once

#include <juce_core/juce_core.h>

namespace bazalt::engine
{
    /** M20's small, closed taxonomy of preview kinds (docs/MILESTONES.md,
        this plan's own Part A) — generic and reusable rather than one
        bespoke kind per node, which is what lets "a preview declaration in
        a node's C++ header, zero NodeCard.tsx edits" actually hold as the
        catalog grows. Waveform/Spectrum/Meter are built in this pass
        (ported from M5's TelemetryScope.tsx); the rest are declared here
        now — the taxonomy is the "perfect base" the plan calls for — but
        have no real producer yet and shouldn't be used by any node's
        getPreviews() until their own AnalysisThread/render-side support
        lands:
          - ShapeWithPlayhead: a mostly-static shape (an envelope's stage
            curve, an LFO's waveform, a wavetable's current frame, a
            data.table's drawn curve) with one or more moving position
            markers. Covers NODE_CATALOG.md's EnvelopeWithPlayhead,
            PhaseMarker, osc.wavetable's "current frame index, table
            preview," and data.table's per-consumer playhead in one kind.
          - RollingHistory: a scrolling bar/step chart of discrete recent
            values (random.stepped's own spec).
          - EventImpulse: sparse trigger-rate visualization, distinct from
            Meter because it's about event rate, not a continuous level
            (noise.dust's own spec).
          - Spectrogram: a scrolling time-frequency waterfall — not named
            in any doc yet, proposed new addition for breadth.
          - Goniometer: a stereo phase/correlation X-Y plot — named in
            ARCHITECTURE.md §6.1's own parenthetical, never followed up;
            needs a genuinely stereo signal to be meaningful.
    */
    enum class PreviewKind
    {
        Waveform,
        Spectrum,
        Meter,
        ShapeWithPlayhead,
        RollingHistory,
        EventImpulse,
        Spectrogram,
        Goniometer
    };

    /** view.scope's own spec (NODE_CATALOG.md): free-running, or
        re-triggered on a rising edge / on each new note. ADR-0029: `Free` and
        `RisingEdge` are analysed; `PerNote` needs note times a tap doesn't
        carry, so it is declared but not offered, and TapSettings::fromPreview
        treats it as `Free`.
    */
    enum class ScopeTriggerMode
    {
        Free,
        RisingEdge,
        PerNote
    };

    /** view.meter's own spec (NODE_CATALOG.md). ADR-0029: `Peak`, `Rms` and
        `TruePeak` are analysed; `Histogram` needs a new frame payload and
        drawer, so it is declared but not offered, and
        TapSettings::fromPreview treats it as `Peak`.
    */
    enum class MeterMode
    {
        Peak,
        Rms,
        TruePeak,
        Histogram
    };

    /** One node's declared preview — what to show, on which of its own
        ports, and the kind-specific display parameters. Flat and additive
        (every field defaulted), matching PortDescriptor/ParameterDescriptor's
        own established convention rather than an opaque key-value map, so
        every call site can keep using designated-initializer syntax and a
        future kind's params are just more defaulted fields, never a
        breaking change to this struct's shape.

        These fields are what AnalysisThread applies (ADR-0029): whenever a
        subscription is attached, the processor reads the LIVE node's
        getPreviews() and hands the matching entry to TapSettings::fromPreview.
        A node whose settings are per-instance (view.scope and friends) builds
        the entry from its current parameters; a node that declares a fixed
        preview (osc.analog's waveform) gets the values written here. Before
        ADR-0029 they were serialized to the UI and used by nothing.
    */
    struct PreviewDescriptor
    {
        PreviewKind kind = PreviewKind::Waveform;
        /** Which of the node's own ports (input or output — the node's
            getPreviews() knows which) this preview taps. Resolved into a
            tap id of the form "node:<nodeId>:<portId>" (NODE_EDITOR.md §9)
            once the node is actually placed in a live graph — this field
            only ever holds the port's own stable id, never an instance id.
        */
        juce::String portId;

        // ---- Waveform ----
        float timeWindowSeconds = 0.05f;
        ScopeTriggerMode triggerMode = ScopeTriggerMode::Free;

        // ---- Spectrum ----
        int fftSize = 2048;
        float tiltDbPerOctave = 0.0f;
        float averaging = 0.0f;

        // ---- Meter ----
        MeterMode meterMode = MeterMode::Peak;
    };
}
