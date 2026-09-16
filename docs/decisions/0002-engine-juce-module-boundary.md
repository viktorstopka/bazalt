# 0002 — engine/ depends on JUCE data modules only, never plugin/GUI modules

## Status
Accepted.

## Context
The brief requires the DSP/graph engine to be "a standalone C++ library with no dependency on the
plugin wrapper or the UI, testable and runnable headless." JUCE is useful for more than plugin
glue — `juce_audio_basics` gives us `AudioBuffer`/`MidiBuffer`, `juce_dsp` gives us FFT,
`SmoothedValue`, and `Oversampling` — but pulling in `juce_audio_processors` or `juce_gui_basics`
would quietly re-couple the engine to plugin/UI concerns.

## Decision
`engine/` may link `juce_core`, `juce_audio_basics`, `juce_dsp` only. It may never link
`juce_audio_processors`, `juce_gui_basics`, `juce_events` (message-loop-shaped), or `plugin/`
itself. This is enforced by which targets `engine`'s `CMakeLists.txt` lists as dependencies — not
just a convention — so a build breaks immediately if it's violated, rather than the boundary
eroding silently over time.

## Consequences
- `tools/render-cli` links `engine` alone and runs with no plugin host, no GUI, no message loop.
- Every Catch2 test in `tests/` links `engine` alone.
- Any future engine code that "just needs one GUI utility" from JUCE is a signal to either
  implement that utility locally in `engine/` or reconsider whether it belongs in `engine/` at all.
