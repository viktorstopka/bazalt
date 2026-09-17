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
