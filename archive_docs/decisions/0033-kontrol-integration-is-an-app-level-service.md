# 0033 — NI Kontrol keyboard integration is an app-level service, not a graph node

## Status
Proposed, 2026-10-06 — part of `wiki/plans/KontrolIntegration.md`'s phase 1. Written before
implementation, per that plan's own process; update to Accepted once phase 1 lands.

## Context
Direct instruction: drive a Native Instruments Kontrol keyboard's 8 knobs + display against
Bazalt's existing 32-slot macro pool (`MacroParameters`, `wiki/plans/UtilMacro.md`), the way
Ableton's own NI integration does. The question this ADR answers: does the Kontrol connection
belong to a specific patch (something a user could add/remove/configure as a node in the graph,
save in a `.json` patch, duplicate per voice, etc.), or does it belong to the *application* — one
keyboard, one connection, independent of whatever graph happens to be loaded?

Two existing precedents already answer this the same way for comparable concerns:
- `bazalt::engine::OutputLimiter` — a real safety/behaviour-affecting stage, deliberately **not** a
  patchable node (its own header comment: "not something a patch has to remember to wire in
  itself"), owned as a plain member of `BazaltAudioProcessor` and run unconditionally every block.
- `MacroParameters` itself — the 32-slot pool exists once, for the whole plugin instance, entirely
  independent of which `util.macro` nodes a particular loaded patch happens to contain.

A Kontrol keyboard is physically the same kind of thing: one piece of hardware, connected to the
whole application, whose job is to reflect and drive the CURRENT macro pool state — not a signal
source or sink inside any one patch's own signal flow. It has no audio or control-rate signal of
its own to contribute to a graph at all (CLAUDE.md's own node-system rules are about signal-
flow — this has none). Patches already can't name 32 real macro targets without `util.macro`
nodes existing in the graph in the first place; the hardware's only job is to reflect whatever
macros THIS patch happens to have claimed, which is exactly the shape `OutputLimiter`/
`MacroParameters` already established as "application concern, not graph concern."

## Decision
Implement the Kontrol integration as a new class (name TBD in the implementation plan, something
like `KontrolSurface`) owned as a plain member of `BazaltAudioProcessor`, declared alongside
`macroParameters`/`outputLimiter`, constructed once per plugin instance and living for the whole
process — never appearing in `NodeGraph`, `NodeFactory`, `PatchDocument`, or any Add-menu/node-
editor surface. It talks to `MacroParameters` directly (reading claimed-slot metadata from the
live `NodeGraph` the same way `GraphEditController::deriveMacroMappings` already does, writing
knob input through the exact same lock-free message-thread → audio-thread path host automation
already uses) rather than through the graph-editing command bridge at all — a knob turn is not a
`graphSetParameterValue` call, it's the same kind of direct smoothed-parameter write host
automation already is.

Standalone-app-only for now (the "Phase 1" scope is explicitly the standalone build; a VST3
instance hosted inside a DAW that already has its own Kontrol integration — e.g. Ableton — would
otherwise have two hosts fighting over the same DAW port, which is exactly the "only one
application can own the DAW port" failure case constraint 4 already requires handling gracefully
regardless of why the port is busy).

## Consequences
- A loaded patch's own `.json` has and needs zero Kontrol-related fields — nothing about this
  integration round-trips through `PatchDocument`/`PatchSerializer.cpp`, so no schema version bump
  is needed for phase 1.
- The keyboard reflects whichever macros the CURRENTLY loaded patch has claimed; switching patches
  (or editing macros in the currently loaded one) re-derives and re-sends slot 0–7×page state the
  same way the top-bar macro panel already redraws itself — both are downstream of the same
  `recompileAndPublish()` hook, not two independently-maintained copies of "what macros exist."
- Because it's not a node, it cannot be instantiated twice, duplicated per-voice, or exist in only
  part of a graph — matching the real hardware constraint (one keyboard, one DAW port, one
  connection) exactly, with no modelling mismatch to paper over.
- Follow-up phases (transport, 4-D encoder browsing) stay app-level too for the same reasoning —
  transport already has an app-level host boundary (`io.transport`'s own `HostInputs` struct, ADR-
  0028) this can eventually feed the same way, not a reason to reconsider this decision.
