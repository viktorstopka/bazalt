# 0004 — Fixed macro-parameter pool for host automation

## Status
Accepted (mechanism); pool size open (see ARCHITECTURE.md §13).

## Context
VST3 hosts expect a stable parameter list (for automation lanes, undo, project recall). Bazalt's
graph is dynamic — the set of node parameters changes as the user edits the graph (in later
milestones). VST3 does have a mechanism for restructuring the parameter list at runtime
(`restartComponent` with the parameter-info-changed flag), but host support for it is inconsistent,
and depending on inconsistent host behavior for a core feature (automation) is the wrong foundation
for an instrument meant to last years.

## Decision
Expose a fixed pool of N generic macro parameters (`Macro 1..N`) as ordinary
`AudioProcessorParameter`s — stable for the life of the plugin, identical in every host. Each macro
optionally maps to one or more node-parameter targets (stable param ID + range + curve); the
mapping table is stored in the patch, not in the host's project file. Node parameters without a
macro mapping are still fully saved/restored in the patch; they're just not host-automatable until
mapped.

## Consequences
- Automating "whatever the user has mapped to Macro 3" works identically in every VST3 host,
  including ones with poor dynamic-parameter support.
- A node parameter is only automatable via a macro slot — there's a hard ceiling (N macros) on how
  many *simultaneously host-automated* graph parameters a patch can have. This is the accepted
  trade-off; N should be generous enough that it's rarely the limiting factor (proposed default:
  32).
- Revisiting per-host dynamic parameter lists later (once specific host support is known and
  matters enough) is entirely a `plugin/` change — the engine has no concept of "macro," it just
  receives smoothed parameter values by stable ID.
