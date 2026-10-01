# 0014 — Port/parameter unification via a shared value contract

## Status
Accepted and implemented (M14). This Status line went stale — a later audit (the
post-`util.macro`-ship sweep, 2026-10-01) found it still said "Not implemented" despite the
decision having shipped for several milestones: `PortDescriptor`/`ParameterDescriptor`
(`engine/include/bazalt/engine/graph/PortDescriptor.h`) carry exactly the fields this ADR
decided to add — `ValueKind kind`, `Quantity quantity`, `Curve curve`, `Polarity polarity`,
`enumOptions`, `softMin`/`softMax`, `isStructural` — all real, all in active use by real
nodes (e.g. `InstanceVoiceNode.h`'s own structural parameters) since M14.

## Context
`VALUE_MODEL.md` proposes one canonical `ValueContract` (`kind`, `quantity`, `min`/`max`, `default`,
`curve`, `polarity`, `enumOptions`, `step`) used identically by DSP, UI, host automation, and patch
serialisation. The code has two separate structs, `PortDescriptor` and `ParameterDescriptor`
(`engine/include/bazalt/engine/graph/PortDescriptor.h`), neither of which has a `quantity`, `curve`
enum, `polarity`, or `enumOptions` field — `ValueTypes.h`'s factory helpers (`frequencyParameter`,
`timeSecondsParameter`, etc., added ahead of this ADR) are a real but narrow first step: they remove
literal-drift between call sites but attach no discoverable tag a consumer could branch on. Two
ports built from different helpers that happen to share numeric bounds are indistinguishable today.
Full analysis: `RECONCILIATION.md` §1 (items 1.1, 1.3, 1.5, 1.7).

## Decision
Extend `PortDescriptor`/`ParameterDescriptor` with new, defaulted fields: `kind` (float/int/bool/
enum), `quantity` (Frequency/Pitch/Time/Gain/Ratio/Unipolar/Bipolar/Count/Phase/Dimensionless),
`curve` (linear/exponential/logarithmic/custom-ref — realized internally via the existing `skew`
float and `isLogScale` bool, not by deleting them), `polarity`, `enumOptions`, `step`, `softMin`/
`softMax` (per `VALUE_MODEL.md` §9's own recommendation to add these now rather than later), and
`isStructural` (per `RECONCILIATION.md` 1.3's audit requirement). Every field defaults so every
existing aggregate-init literal across all 16 node headers keeps compiling unchanged — the same
pattern already used for `hasFallbackWhenUnconnected` and `isInteger`. `osc.basic.shape` migrates
from a bare float with hardcoded quantization to a real `kind=enum` value as the first end-to-end
test case, closing `RECONCILIATION.md` 1.3's ambiguity for that specific parameter at the same time.

The `hasFallbackWhenUnconnected` NaN-sentinel mechanism is **kept as-is**, not replaced by
`VALUE_MODEL.md` §5's "compiler bakes a constant" proposal — that proposal would freeze an
unconnected port's effective value at compile time, breaking any node (`DelayNode` today) whose
value must stay live-adjustable via `setParameter()` after compilation. This is a deliberate,
reasoned rejection of one specific paragraph of the design doc, not an oversight (`RECONCILIATION.md`
1.4).

## Consequences
- `PortDescriptor` and `ParameterDescriptor` remain two structs, not one merged type — the doc's
  "no second, UI-only notion of what a value is" goal is met by both structs embedding the same
  value-contract fields, not by collapsing the port/parameter distinction itself (which
  `RECONCILIATION.md` 1.3 shows still has a real, if narrower, structural reason to exist).
- `NodeDescriptorJson.cpp` and `ui/src/graph/descriptorTypes.ts` both grow new fields; regenerate the
  TS mirror via the existing ADR-0007 codegen path rather than hand-editing it a second time.
- Any parameter that's implicitly an enum today (only `osc.basic.shape` currently) needs a
  float-index → enum-option-ID migration if it's ever persisted in a real patch before this lands —
  low risk today since no real patch persistence exists yet (M10's editor is disconnected from the
  engine).
- Resolves nothing about *how* a value becomes host-automatable — that's ADR-0015.
