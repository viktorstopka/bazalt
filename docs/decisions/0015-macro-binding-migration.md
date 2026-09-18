# 0015 — Macro binding stays parameter/port-ID-addressed; no wireable Macro-node relay

## Status
Proposed (M14). Not implemented. Explicitly **not** adopting `VALUE_MODEL.md` §6's model as written.

## Context
`VALUE_MODEL.md` §6 proposes host automation flow entirely through a **Macro node**: a Constant that
also binds to a host slot and exposes its value as a wireable output port — "wire a Macro into it;
the target node's code is untouched." The doc's own §6 flags this as an open question for
reconciliation, since "today's pool binds directly to per-node parameter descriptors."

Three independent, already-reasoned decisions bear directly on this:
- `ARCHITECTURE.md` §4.3 **deliberately** chose a fixed 32-slot `AudioProcessorParameter` pool bound
  by `{targetNodeId, targetParameterId}` specifically because VST3's `restartComponent` (needed for
  any dynamic parameter list) behaves inconsistently across hosts.
- `NODE_EDITOR.md`'s own conflict log (item #10) already considered a wireable Macro node and
  **rejected** it, for the same cross-host-compatibility reason, in favour of "keep the host-facing
  parameter a plain 0–1 float unconditionally; Type/Shape/Enum are UI-side interpretation only."
- ADR-0013: only parameter-shaped values (`juce::RangedAudioParameter` via `WebSliderRelay`/
  `WebSliderParameterAttachment`) get real bidirectional, host-automation-aware UI binding today. A
  Macro node's output-port value would still need to resolve to a `RangedAudioParameter` to be
  host-automatable the same way — the fixed-32-slot array doesn't support growing dynamically as
  Macro nodes are added/removed without redesigning that relay mechanism too.

Full analysis: `RECONCILIATION.md` 1.2 — the single largest point of friction between the design
docs and prior, deliberate architecture decisions in this whole reconciliation.

## Decision
Keep the existing mechanism: a fixed pool of 32 `AudioParameterFloat`s
(`plugin/source/MacroParameters.h/.cpp`), each optionally bound via `MacroMapping{macroIndex,
targetNodeId, targetParameterId, rangeMin, rangeMax}` (`engine/include/bazalt/engine/patch/
PatchDocument.h`), applied every block through the one `Node::setParameter(id, value)` virtual every
node already exposes. Extend what a mapping's *target* can address: today it happens to work for
`DelayNode`'s `delay.basic.samples` port already, by accident, because that port's fallback value is
itself set through the same `setParameter()` call — formalize this as the real mechanism. A
`util.macro` node (`NODE_CATALOG.md` §I) is `util.constant` plus a slot binding, not a new relay
transport: its output value is driven by the bound slot instead of only by direct edits, sharing
`util.constant`'s implementation per `VALUE_MODEL.md` §6's own "a Macro is a Constant that also
binds" framing — that half of the doc's proposal is adopted; the wireable-relay-pool half is not.

## Consequences
- No change to ADR-0004's cross-host guarantee or to how host automation lanes work today.
- No change to ADR-0013's relay mechanism — it continues to bind only the fixed 32 slots, not an
  arbitrary number of Macro-node instances.
- A structural setting (`RECONCILIATION.md` 1.3's `isStructural` flag, ADR-0014) is never bindable,
  matching `VALUE_MODEL.md` §6's own rule, unaffected by this decision.
- If real host-side needs later outgrow 32 fixed slots, that's a separate, larger ADR revisiting
  ADR-0004 itself (VST3 dynamic-parameter-list support) — not something this decision should be
  read as blocking, only as declining to solve via a Macro-node workaround.
- `util.macro`'s slot reassignment (moving which of the 32 slots it claims) changes a live host-
  automation binding — the UI warning `VALUE_MODEL.md` §6 already calls for is still required.
