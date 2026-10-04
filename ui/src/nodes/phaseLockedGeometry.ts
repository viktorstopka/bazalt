// PhaseLockedPreview.tsx's drawing geometry, shared with CycleBody.tsx's labels.

/** Mirrors PhaseSnapshot.h's phaseLockedCycles. */
export const PHASE_LOCKED_CYCLES = 4
export const HEADER_FLOATS = 2 // [playhead, frequencyHz]

/** The drawing's own coordinate space; the SVG stretches it to whatever box
    CSS gives it (preserveAspectRatio="none" — safe because every stroke is
    non-scaling). */
export const WIDTH = 400
export const HEIGHT = 100
export const PAD = 6 // vertical padding so a peak never touches the edge
export const HEADROOM = 1.15 // drawn range is ±HEADROOM; nominal ±1 sits inside it
export const PLAYHEAD_AUTO_MAX_HZ = 30

export const yOf = (v: number) => HEIGHT / 2 - (v / HEADROOM) * (HEIGHT / 2 - PAD)

/** Where value `v` sits, as a fraction of the preview box's height from the
    top — for labels drawn outside the SVG (CycleBody.tsx's ±1). */
export const levelFraction = (v: number) => yOf(v) / HEIGHT
