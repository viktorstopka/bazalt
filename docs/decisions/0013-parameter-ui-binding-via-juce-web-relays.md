# 0013 — Plain parameter↔UI binding uses JUCE's Web*Relay mechanism, not a custom bridge

## Status
Accepted (built in M5, `PluginEditor.h/.cpp`, `ui/src/controls/MacroSlider.tsx`).

## Context
M5 needed the 4 macros already mapped to the hardcoded voice graph (osc shape, filter cutoff/
resonance, envelope release — ADR-0004, `MacroParameters`) to become real, host-automatable
controls in the WebView, with automation moves reflected back into the UI. Separately,
`docs/NODE_EDITOR.md` §6 (M7+) designs a UI→engine **command bridge** for graph-editing operations
(add node, connect, move, ...) using JUCE's `withNativeFunction`/`withEventListener`/`emitEvent`
primitives. It would have been possible to route plain parameter sync through that same future
bridge, or to build a lighter one-off version of it now for M5's simpler need.

## Decision
Use JUCE's own first-party mechanism instead: `WebSliderRelay` (`plugin/source/PluginEditor.h`,
one per macro) plus `WebSliderParameterAttachment` (binds a relay to a `RangedAudioParameter`,
mirroring how `SliderAttachment` works for a native `juce::Slider`), paired with the vendored
`@juce-framework/webview` package's `getSliderState()`/`SliderState` on the JS side
(`ui/src/controls/MacroSlider.tsx`). This is a **generic JUCE audio-parameter↔WebView sync
primitive**, not something built for Bazalt — it already handles both directions (UI drag → host
parameter, and host/automation change → UI update) and gesture bracketing
(`beginGesture`/`endGesture` for proper undo/automation-lane behaviour in a host), all inside JUCE's
own tested code.

The M7+ command bridge (NODE_EDITOR.md §6) remains the right tool for graph-editing commands
specifically — those aren't `RangedAudioParameter`s, need request/response semantics with a
rejection reason, and don't fit the relay model. The two transports solve genuinely different
problems and are expected to coexist permanently, not merge later.

## Consequences
- Any future *plain* host-automatable parameter that needs a WebView control (not a graph-editing
  operation) should use a `Web*Relay`/`Web*ParameterAttachment` pair the same way, not be routed
  through the M7+ command bridge — that bridge is for graph mutations, not parameter sync.
- `WebComboBoxRelay`/`WebToggleButtonRelay` are available in the same JUCE module for any future
  enum- or boolean-shaped plain parameter; no new C++ infrastructure is needed to add one.
- The vendored `@juce-framework/webview` package (see `ui/vendor/juce-webview/README-BAZALT.md`) is
  now load-bearing for parameter controls, not just a "nice to have" — keep it in sync with the JUCE
  version pin (ADR-0001) if that ever changes, or these relays silently stop matching the protocol
  the C++ side speaks.
