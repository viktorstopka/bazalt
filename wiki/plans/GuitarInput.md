# Guitar input — hearing a played instrument

**Status:** Steps 1–2 built (2026-10-11). Steps 3–4 proposed, not started.

## Origin

Direct request (2026-10-11), while playing a guitar through a hex interface into
Bazalt: pluck a note on the high E string, have Bazalt know the note fast, and play
things with it (a sine underneath, and more). The question was which missing nodes
would make that powerful.

## Step 1 — the ears (built)

- **`analysis.pitch`** (Pitch): YIN within a note range. The range is the key
  design choice: one string's range makes tracking several times cheaper and
  nearly free of octave errors (a plucked string's 2nd harmonic is often the
  loudest). Outputs pitch, frequency and confidence. About two periods of the
  lowest note in range of delay (≈ 6–12 ms on a high E string; test: under 12 ms).
- **`analysis.onset`** (Onset): fast vs slow envelope, so a re-pluck on a ringing
  string counts. Strength is the attack's peak over 5 ms (a velocity).
- **`analysis.notes`** (Audio to Notes): pitch + onset into a `Note` stream, straight
  into `life.voice`. Starts on an onset once the pitch is confident, follows bends,
  ends on fade or unclear pitch, retriggers on a new pluck. Monophonic per node.

## Step 2 — the phase (built)

- **`analysis.phase`** (Track Phase): the played note's phase as a signal, locked to
  its zero crossings (a low-pass following the measured frequency, hysteresis), with
  Multiply / Divide — Divide 2 is an octave down. Into an Oscillator's Phase (Frequency
  at 0) it plays any shape or wavetable in the instrument's own period.
- The curve Oscillator and the fixed Sine/Saw/Square/Triangle band-limit by how fast
  their read position really moves (frequency + change of Phase), so a shape driven
  by Phase alone stays alias-free.

## Step 3 — hex input (proposed)

Six strings as one six-channel cable: per-channel (laned) analysis nodes then track
six strings independently, six note streams, each string its own voice. Needs an
input with enough channels (the plugin has main + four stereo sidechains; a DAW like
Ableton offers one sidechain to an instrument).

## Step 4 — spectral (proposed)

`analysis.partials` (the strongest partials as Data → sine-bank resynthesis, a
modal resonator tuned to the guitar), a harmonic filter keyed to the tracked pitch,
a transient/tonal split. Would give the reserved `Spectral` signal type its first
real use.
