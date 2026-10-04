// Everything view.tune shows, derived from one pitch value in semitones
// (MIDI note numbers: 60 = C4, 69 = A4 = 440 Hz). Note, cents and frequency
// all come from `semitones` directly — never from each other after rounding.

/** Within this many cents of the nearest note, the readout is "in tune". */
export const TUNE_TOLERANCE_CENTS = 5

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

export interface PitchReadout {
  note: string // "C#2"
  cents: number // -50..+50, continuous
  centsText: string // "+38", "-4", "+0"
  semitonesText: string // "60st", "60.38st"
  hzText: string // "69.30 Hz", "1.05 kHz"
  inTune: boolean
}

export function describePitch(semitones: number): PitchReadout {
  const nearest = Math.round(semitones)
  const cents = (semitones - nearest) * 100
  const roundedCents = Math.round(cents)
  const name = NOTE_NAMES[((nearest % 12) + 12) % 12]
  const octave = Math.floor(nearest / 12) - 1
  const hz = 440 * Math.pow(2, (semitones - 69) / 12)

  const semitonesText = Math.abs(semitones - nearest) < 0.005 ? `${nearest}st` : `${semitones.toFixed(2)}st`
  const hzText = hz >= 1000 ? `${(hz / 1000).toFixed(2)} kHz` : `${hz.toFixed(2)} Hz`

  return {
    note: `${name}${octave}`,
    cents,
    centsText: `${roundedCents >= 0 ? '+' : '-'}${Math.abs(roundedCents)}`,
    semitonesText,
    hzText,
    inTune: Math.abs(cents) <= TUNE_TOLERANCE_CENTS,
  }
}
