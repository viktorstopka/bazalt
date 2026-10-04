#pragma once

namespace bazalt::engine
{
    /** How many cycles a phase-locked preview shows — fixed, never rate-
        dependent, so the aspect ratio of a waveform is constant by
        construction (PreviewKind::PhaseLocked). */
    static constexpr int phaseLockedCycles = 4;

    /** Resolution of a phase-locked frame: points per displayed cycle. 256
        per cycle (1024 over the 4 shown) keeps a saw's reset a clean vertical
        edge at every canvas zoom — the jump spans 1/256 of a cycle, well
        under a screen pixel even fully zoomed in. */
    static constexpr int phaseLockedPointsPerCycle = 256;
    static constexpr int phaseLockedPoints = phaseLockedPointsPerCycle * phaseLockedCycles;

    struct PhaseSnapshot;

    /** A phase source's waveform as a pure function of a snapshot and a
        position along the displayed cycles (0..phaseLockedCycles). A plain
        function pointer, not a call into the node: AnalysisThread evaluates
        it off the audio thread, and a static function outlives every plan
        and node that could have produced the snapshot. */
    using CycleRenderFn = float (*) (const PhaseSnapshot&, double cyclePosition);

    /** What a phase source (Node::isPhaseSource()) publishes once per block:
        everything needed to draw its current waveform, plus where it is.
        Plain data, copied whole through Tap's seqlock. `params` is the
        render function's own private layout. */
    struct PhaseSnapshot
    {
        CycleRenderFn render = nullptr;
        double frequencyHz = 0.0;
        double sampleRate = 0.0;
        float playhead = 0.0f; // position at the end of the block, 0..phaseLockedCycles
        float params[6] {};
    };
}
