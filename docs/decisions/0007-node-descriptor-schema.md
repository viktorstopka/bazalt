# 0007 — Node descriptor schema: one shape, two producers, JSON over the command bridge

## Status
Accepted (M9). NODE_EDITOR.md §3/§4 proposed this schema; M7 built the C++ side
(`PortDescriptor`/`NodeDescriptor`/`NodeFactory::describeAll()`) without a transport to the UI. This
ADR records the M9 completion: the JSON wire shape, the transport choice, and the mock-descriptor
contract.

## Context
The UI must never hardcode a node type (CLAUDE.md's non-negotiable rule 1's sibling rule for the
node editor, NODE_EDITOR.md §1). Every node — real or, for demonstration purposes the blueprint
calls out (mock nodes, decorations not yet built as real engine types), mock — has to render from
the same descriptor shape, sourced from two different places: `NodeFactory::describeAll()` for real
engine types, and a hand-written table in `ui/src/graph/mockDescriptors.ts` for everything else.

## Decision
`plugin/source/NodeDescriptorJson.{h,cpp}` serializes `bazalt::engine::NodeDescriptor` to a
`juce::var` tree field-for-field matching `ui/src/graph/descriptorTypes.ts`'s `NodeDescriptor`
TypeScript interface exactly:
- `SignalType`/`NodeLayoutVariant` serialize to the same lowercase strings both sides' unions use
  (`"audio"`, `"standard"`, ...) rather than raw enum integers — a wire format a human can read in
  DevTools' network/console output without a lookup table, and one that survives a future enum
  reordering on the C++ side without corrupting old union values on the JS side.
- `std::optional<float>` (`PortDescriptor::minValue`/`maxValue`) serializes to a JS `null`
  (`juce::var()`, the default-constructed "void" var) when unset — never `0`, never an omitted key.
  `0` is a legitimate bound; the UI needs to tell "bounded at zero" apart from "unbounded" to decide
  whether a numeric port renders as Modulation vs. Value (NODE_EDITOR.md §5) and whether a value-pill
  fill bar has anything to compute a ratio against (blueprint §6.5: "a background fill shows the
  ratio within bounds, only when the parameter defines both min and max").

**Transport**: a new `getNodeDescriptors` native function (`PluginEditor.cpp`'s
`withGraphCommands()`), not the telemetry `fetch()` resource-provider path. This is a one-shot RPC
call at editor load, not a per-frame pull — exactly the shape ADR-0006 chose the native-function
transport for, even though this particular call never mutates the graph (no recompile, no undo
step). Filed here rather than reopening ADR-0006: the same request/response RPC contract applies,
this is just its first non-graph-editing user.

**Mock descriptors**: `ui/src/graph/mockDescriptors.ts` exports `NodeDescriptor[]` in the identical
TypeScript shape, each entry flagged `isMock: true` (a UI-only field, never produced by
`nodeDescriptorToVar` and never read by anything on the C++ side) and namespaced under `mock.*`
(demonstration-only nodes: MIDI Note, MIDI CC, Audio In, Trigger by Threshold, Random, Macro,
Predelay, four Singleton-chain examples) or `deco.*` (Frame/Header/Image — real type IDs
NODE_EDITOR.md §4 already named, whose real engine implementation MILESTONES.md defers to M12).
`util.reroute` (the Knob decoration) is real and not mocked — it's the one Decoration-variant type
that already exists (M7).

One field, `PortDescriptor.isPolyPlaceholder`, is UI-only and mock-only, matching blueprint §4's own
scoping ("green marks polyphonic audio flowing into Sum Voices... poly/mono encoding will be
redesigned later") — used exactly once, on `mock.sumVoices`'s input port, so nothing generic ever
branches on it.

## Component gallery (M9's other deliverable)
`ui/src/nodes/NodeCard.tsx` renders any `NodeDescriptor` — real or mock — for all four layout
variants (standard/horizontal/singleton/decoration), deriving each port's colour/glyph purely from
`ui/src/graph/portUiKind.ts`'s `classifyPortUiKind()` (NODE_EDITOR.md §5's `SignalType` + numeric
metadata rules), never from a node-type-specific lookup. `ui/src/gallery/ComponentGallery.tsx`
fetches real descriptors once (`fetchNodeDescriptors.ts`, same `getNativeFunction` pattern
`StressTestCanvas.tsx` established in M8, including the `typeof window.__JUCE__ === 'undefined'`
guard for plain-browser iteration), merges them with the mocks, and renders one example of every
layout variant, one swatch per port-type/glyph, one card per node state (default/hover/selected/
connected/bypassed/listening/error, blueprint §8 deliverable #2), and the full catalog grouped by
category. Reachable via a dev-only "Component gallery (M9)" button next to M8's stress-test button
(`App.tsx`), same convention.

Standard-node row ordering (merged pass-through rows, then remaining input ports, then parameters,
then a divider, then remaining output ports) is a judgment call reverse-engineered from
`docs/Frame 1 Bazalt.png`'s three worked examples (MIDI Note, Predelay, Trigger by Threshold) — the
reference is a hand-drawn mockup, not a formal spec, and its divider placement isn't perfectly
self-consistent across examples. This ordering was chosen because it reproduces all three examples
exactly; exact per-example layout fidelity was not chased further than that.

## Verified
`tests-plugin/NodeDescriptorJsonTests.cpp`: unbounded numeric ports serialize `minValue`/`maxValue`
as void (never 0), `NodeLayoutVariant::Decoration` serializes to `"decoration"`, every one of the 16
real registered types serializes with a non-empty `typeId`. `ui/src` type-checks and production-
builds clean (`npm run build`). Manually verified in the Debug Standalone app (UI Automation +
screenshots): the gallery reports "16 real + 14 mock descriptors" (confirming the bridge round-trip
actually happened, not just that mocks render), the poly-placeholder green is pixel-confirmed on
Sum Voices' input only (`RGB(74,222,128)` ≈ `#4ade80`) while its own output renders audio pink
(`RGB(224,51,158)` ≈ `#e0339e`), and all 7 node states are visually distinct.

## Consequences
- Any future node type — real or mock — needs no gallery/renderer changes to show up; adding a
  `registerType()` call or a `mockDescriptors.ts` entry is sufficient, which is the whole point of
  "the UI never hardcodes a node type."
- `NodeCard` is what M10 mounts on the live canvas — this ADR's DOM-only rendering choice is amended
  into ADR-0008 (hybrid WebGL/DOM node bodies are M10's job, once there's a pan/zoom transform worth
  syncing against).
- The gallery's visual-regression baseline (MILESTONES.md M9 exit criteria) is the screenshots taken
  during this verification pass, not yet captured as an automated snapshot suite — that's M13's job
  per blueprint §8 deliverable #5.
