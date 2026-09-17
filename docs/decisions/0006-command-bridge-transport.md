# 0006 — Command bridge transport: JUCE native functions, not the telemetry fetch path

## Status
Accepted (M7).

## Context
NODE_EDITOR.md §6 designs a UI→engine command channel for graph-editing operations (add node,
connect, delete, disconnect, set parameter value — the M7 slice; more arrive in M10-M12). Unlike
telemetry (ADR-0005 — high-rate, pull-based, best-effort-fresh, no response needed) a command needs
reliable delivery and a real response: did it succeed, and if not, why. Two transports were already
available in this codebase by M7: the telemetry resource-provider `fetch()` path, and JUCE's
`WebBrowserComponent::Options::withNativeFunction`/`emitEvent` mechanism (already partially wired —
`withNativeIntegrationEnabled()` — for M5's macro `WebSliderRelay`s, ADR-0013).

## Decision
Commands go over `withNativeFunction`, not the telemetry `fetch()` path. `PluginEditor::
withGraphCommands()` registers five native functions — `graphAddNode`, `graphDeleteNode`,
`graphConnect`, `graphDisconnect`, `graphSetParameterValue` — each a thin lambda that extracts
positional arguments from the JS call's `Array<var>`, calls the matching `GraphEditController`
method (`plugin/source/GraphEditController.h`), and resolves the JS call's Promise via
`NativeFunctionCompletion` with `{ success, errorMessage }`. This is a request/response RPC shape,
not a shared-state fetch: exactly matches what a command needs (a definite success/failure, with a
reason), and it's a JUCE-native mechanism already proven for ADR-0013's plain-parameter relays, not
something built from scratch.

M7 wires the transport and the receiving end (`GraphEditController`) but not the sending end — no UI
exists yet to call `getNativeFunction("graphAddNode")(...)` (NODE_EDITOR.md's own M7 scope: "commands
are driven by a test harness... not real UI"). `tests-plugin/GraphEditControllerTests.cpp` drives
`GraphEditController` directly, with no WebView involved, proving the mechanism the transport calls
into rather than the transport itself — that's covered structurally (the native functions are
registered and compile against the real `Options` builder) but not exercised end-to-end from JS
until a real UI exists to do it (M10+).

## Consequences
- Two separate transports now coexist permanently, by design: telemetry's pull `fetch()` for
  high-rate visualization data, and native-function RPC for commands. Don't try to unify them — they
  solve different problems (ADR-0013 makes the same point for plain-parameter binding specifically).
- Every future command (bypass, splice, Unwrap, decorations, ...) follows the same shape: a
  `GraphEditController` method returning `CommandResult`, wrapped in one more `withNativeFunction`
  registration in `PluginEditor::withGraphCommands()`. Adding a command is additive in both places,
  never a transport change.
- `NativeFunctionCompletion` can be called from any thread per JUCE's own doc comment, but every
  `GraphEditController` method is message-thread-only (NODE_EDITOR.md §6) — the lambdas here call it
  synchronously and immediately, on whatever thread JUCE invokes the native function callback on.
  Revisit if that's ever not the message thread in practice; nothing here assumes otherwise yet, but
  nothing has proven it in a real WebView call either (see the untested-end-to-end note above).
