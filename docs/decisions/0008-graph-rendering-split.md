# 0008 — Node editor rendering: raw WebGL2 for grid/cables/nodes, confirmed by a stress-test spike

## Status
Accepted (M8). NODE_EDITOR.md §10 proposed this split as a hypothesis; this ADR records the
benchmark that confirms it, not a fresh proposal.

## Context
NODE_EDITOR.md §3's performance budget: a 500-node/1000-cable patch must pan and zoom at the
display refresh rate (target 120 fps) with live telemetry-driven activity on every visible cable,
text crisp at every zoom level. §10 proposed WebGL for the grid/cables/previews (batched draw calls,
suits many small coloured primitives at once) and DOM for node titles/labels/text inputs (real text
layout, IME, accessibility), with the actual split to be confirmed or revised by a benchmark before
any real node-editor visuals are built on top of it — M8's own milestone scope, not deferred further.

## What was built and measured
A throwaway spike (`ui/src/canvas/StressTestCanvas.tsx`, reachable via a dev-only "Run stress test
(M8)" button — not part of the real product UI, deleted once M9/M10 build the real thing): raw
WebGL2 (no rendering library), a synthetic 500-node/1000-cable layout
(`ui/src/canvas/stressGraph.ts` — deliberately not a real engine graph; see that file's comment and
ADR-0009 for the companion engine-scalability test using a *real* 500-node graph instead), pan/zoom
identical to `InfiniteCanvas.tsx`'s conventions, and the same adaptive dot grid technique, all drawn
in three draw calls per frame (grid points, cable lines, node triangles). Cables currently inside the
viewport subscribe to synthetic dynamic taps (ADR-0009) up to the pool's headroom and their colour
pulses from real (if synthetic) telemetry — this measures the rendering split *together with* live
telemetry load, not the renderer in isolation, which is the condition that actually matters.

**Measured** (Debug Standalone build, Windows, this machine's display): steady **60 fps** — the
measurement is display-refresh-capped (WebView2 vsyncs by default), not a hard ceiling on what the
renderer itself can sustain; frame time stayed well under the ~16.7ms budget with headroom to spare
at this node/cable count, with 6 taps concurrently subscribed and pulsing. No dropped-frame stutter
observed panning or zooming.

## Decision
Confirmed as proposed: WebGL2 for the grid/cables/node bodies, DOM for text/controls/menus, built on
top of M5's `InfiniteCanvas` pan/zoom conventions rather than replacing them (blueprint §3's own
instruction). M9+ builds the real node editor on this foundation.

## Consequences
- This was measured on a Debug build; Release is expected faster, not slower — no reason to
  re-benchmark before shipping, but worth re-confirming once M9/M10's real (heavier — real text,
  real per-node state) rendering exists, since a synthetic spike is a lower bound on cost, not an
  upper one.
- The hybrid "WebGL background + absolutely-positioned DOM overlay for text" approach for node
  bodies (NODE_EDITOR.md §10) is still unverified by this spike (it drew plain rectangles, no DOM
  overlay at all) — that's real, not-yet-measured work for M9's component gallery.
- `StressTestCanvas.tsx`/`stressGraph.ts` are dev-only scaffolding, matching `ui/src/App.tsx`'s M4
  benchmark spike's fate (CLAUDE.md) — delete them once the real node editor renders enough to
  stress-test itself directly, don't try to preserve or build on them.

**Amendment (M9):** the line above turned out to overstate what the component gallery needs. The
gallery (`ui/src/nodes/NodeCard.tsx`, ADR-0007) renders node bodies as plain DOM/CSS, not the
WebGL-background/DOM-overlay hybrid — a gallery has no pan/zoom camera transform to keep the two
layers synchronized against, and no node count (a few dozen, not 500) that a WebGL background would
win anything from. The hybrid approach's real justification is amortizing draw calls across many
panning/zooming nodes, which only exists once M10 puts nodes on the live, transformable canvas —
that's where hybrid-sync verification actually belongs, and where it's now deferred to. `NodeCard`
itself is reused as-is by M10 either way; only the layer it's mounted in changes.

**Amendment (M10):** the hybrid-sync question above is answered, and the answer is narrower than a
literal "WebGL-background + DOM-overlay per node" — node *bodies* stay plain DOM (`NodeCard`,
unchanged rendering contract), not a WebGL-drawn background at all; M10's node count (a hand-built
demo graph, not the M8 stress patch) never demonstrated a need for that specific optimization. What
did migrate to WebGL2, per this ADR's original decision, is the grid (ported directly from M5's
Canvas2D `drawDotGrid`) and — new in M10 — the cables (`ui/src/canvas/webgl/nodeEditorRenderer.ts`),
tessellated bezier curves with a dash-capable line shader for the rejected/hover wire-feedback states
(`docs/decisions/0010-wire-feedback-colours.md`). The actual sync mechanism, resolving M9's deferred
question directly: **there is no second transform to keep in sync at all.** `InfiniteCanvas.tsx`
applies the camera's pan/zoom as a single CSS `transform` on one DOM "world" container (holding every
`NodeCard`), updated imperatively once per `requestAnimationFrame` tick — never through React state,
per ARCHITECTURE.md §7. The same per-frame tick then reads each port glyph's already-transformed
on-screen position straight back out via `getBoundingClientRect()` (`ui/src/graph/portAnchors.ts`)
and feeds those screen-space coordinates directly to the WebGL cable/grid draw calls. DOM is the one
and only source of truth for where a node/port currently sits; WebGL just paints on top of whatever
that measurement says, every frame. This sidesteps an entire class of transform-drift bugs a second,
independently-computed WebGL-side transform could otherwise introduce, at the cost of one
`querySelectorAll`/`getBoundingClientRect` pass per frame — measured as negligible at M10's node
counts (dozens, not hundreds); worth re-profiling only if a future milestone's node count grows enough
to make that pass itself the bottleneck, which M10 did not observe.

**Amendment (M20):** the inline node-preview rendering surface question (M20 plan's Part C4) is
settled the same direction as node bodies: Canvas2D per node, not WebGL. `NodePreview.tsx` mounts one
small `<canvas>` per declared preview inside the node's own (still plain-DOM) `NodeCard`, drawn by the
one shared render loop (`previewRenderLoop.ts`, C3) using M5's own `drawScope`/`drawSpectrum`/
`drawMeter` verbatim (`telemetryDraw.ts`) — no new rendering technology, matching the plan's own
"refactor, not new DSP-adjacent work" framing. This is a direct extension of NodeCard staying DOM
(this ADR's M9/M10 amendments), not a fresh decision: a preview canvas is just another element inside
a DOM node body, with no camera transform of its own to keep in sync — the same reasoning that kept
`NodeCard` off WebGL in the first place applies unchanged.

**What this amendment does NOT confirm:** the plan's own exit criterion for C4 ("re-run the M8 stress
patch — 500 nodes — with every visible node's preview active, confirm the 60fps floor holds") could
not be run as written. `StressTestCanvas.tsx`/`stressGraph.ts`, the UI-triggerable spike this ADR's
own "what was built and measured" section describes, was deleted in M10 polish exactly as this ADR's
Consequences section said it eventually would be — there is no live UI harness left to reproduce a
500-node *rendered* patch against. Separately, M20 step 8 deliberately gave real `previews[]`
declarations to only two node types (`osc.analog`, `mix.gain` — "a small number... to prove the
end-to-end path, not a catalog-wide sweep"), so even the real 500-node graphs the engine side *can*
build in a fraction of a second (`GraphEditController::applyBatch`, `tests-plugin/
StressGraphGenerator.h`, exercised by `tests-plugin/GraphEditControllerTests.cpp`'s stress-graph
tests) wouldn't currently produce "every visible node has a live preview" once loaded into the live
canvas — most nodes in that generated graph have no preview declared at all yet. Correctness was
verified directly instead (previews render correctly at multiple zoom levels, across multiple live
instances, with no dropped frames observed by eye) rather than a formal frame-time measurement. A real
FPS re-verification needs both pieces rebuilt together — a UI-reachable way to load a large real graph
(`StressGraphGenerator.h`'s shape, or `graphRestoreSnapshot` with a generated large patch) and a
broader `previews[]` rollout across the node catalog — and should happen once that broader rollout is
itself in scope, not be fabricated now against a harness and a preview catalog that don't exist yet.
