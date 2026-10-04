// The phase-locked preview's per-node playhead override, stored as a plain
// number in NodeInstance.properties["preview.playhead"] (graphStore.ts's
// setPreviewPlayheadMode): 0 Auto (the default — shown below ~30 Hz, where it
// can be followed), 1 On, 2 Off.
import type { PlayheadMode } from './PhaseLockedPreview'

const MODES: readonly PlayheadMode[] = ['auto', 'on', 'off']

export function playheadModeFromProperty(value: number | undefined): PlayheadMode {
  return MODES[value ?? 0] ?? 'auto'
}

/** The stored number for the mode after `mode` (Auto -> On -> Off -> Auto). */
export function nextPlayheadMode(mode: PlayheadMode): number {
  return (MODES.indexOf(mode) + 1) % MODES.length
}
