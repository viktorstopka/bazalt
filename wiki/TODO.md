# TODO — agreed, deferred

Things the user and Claude agreed are real but chose to do later. Each line says why it
waits and where it belongs. Remove a line when it is done; a design that grows out of
one gets its own `wiki/plans/` file.

## After the life-cycle suite (stage 2, `wiki/plans/LifeCycle.md`)

- **"Hold at birth" without Sample & Hold.** Today a value that should stay fixed for a
  sound's life (strength, pitch at the moment of the trigger) needs a Sample & Hold on
  the voice's Start. Fine for now (user, 2026-10-12: "one node more that makes total
  sense"); revisit once Population and identity exist.
- **Lifetime that isn't silence detection.** A voice ends 200 ms after its output goes
  quiet once released. An owl's call has a 2.5 s pause mid-call, so the Owl patch keeps
  a faint breath alive; the opposite problem is patches that never go silent (Fire's
  noise floor) and so can't be frozen in Ableton. Population's "lives: a fixed time /
  until its envelope ends / forever" (D2) is the fix.
- **More than two randoms per instance.** random1 / random2 run out fast (the Owl wanted
  pitch, length and quaver rate). Solved by the life-cycle suite's genotypes (Class +
  Trait) and per-instance seeded random nodes.

## Small, later

- **Resonator output level.** Modal Bank struck by a Mallet peaks around +20 dB (the
  Hot Paws patch scales it by 0.06). Resonators should come out near 0 dB for an
  ordinary strike.
- **Modal Bank is always stereo.** A mono chain after it needs a Downmix. A mono/stereo
  choice on the node, or a Pan that takes stereo, would remove it.

## Planned inside stage 2

- **Gesture: several curves on one timeline** — see `wiki/plans/LifeCycle.md`, batch 2e.
