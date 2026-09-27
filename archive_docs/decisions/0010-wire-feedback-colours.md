# 0010 — Wire-drag feedback: source-port colour, not a new hue, plus a dash shader for rejection

## Status
Accepted (M10).

## Context
NODE_EDITOR.md §10 (echoing blueprint §6.3) needed an in-progress-wire feedback scheme that's
unambiguous against the port-type colour palette (`docs/decisions/0007-node-descriptor-schema.md`'s
6-colour table) without inventing a 7th/8th hue: the prototype this project's blueprint drew from
used green for "valid," but green is already reserved for the poly-audio placeholder
(`tokens.color.portPoly`). §10 proposed reusing the *source port's own type colour* for a valid
drag, the same colour at reduced opacity for "not yet over a port," and the existing error-red for
"rejected" — as a proposal, not yet implemented against a real wire-drag interaction. M10 is the
first milestone with an actual drag-to-wire gesture to hang this off of.

## Decision
Implemented exactly as §10 proposed, in `ui/src/canvas/InfiniteCanvas.tsx`'s per-frame render loop
and `ui/src/canvas/webgl/nodeEditorRenderer.ts`/`shaders.ts`:

- **Valid** (hovering a compatible input port, `ui/src/graph/wireRules.ts`'s `canConnect`): the
  temporary cable renders in the *source output port's own* `portUiStyle` colour
  (`ui/src/graph/portUiKind.ts`) at full alpha — the exact colour the finished cable will have once
  committed, so the preview previews the real thing, not an abstract "yes" signal.
- **Not yet over a port**: the same source-port colour at ~40% alpha (`InfiniteCanvas.tsx`'s
  `frame()`, the `alpha = 0.4` default before any hover is found) — reads as "still deciding," not a
  fourth colour.
- **Rejected** (hovering an incompatible input, or a self-connection): the existing
  `tokens.color.error` red, at full alpha, **and** dashed — colour alone was ruled out as
  colour-blind-unfriendly and easy to miss mid-drag, so rejection gets a distinct line style on top,
  not just a colour swap. Dashing needed real shader work: `shaders.ts`'s `cableLineVertexShader`/
  `cableLineFragmentShader` gained a per-vertex arc-length attribute (`a_dist`) and dash flag
  (`a_dash`), with the fragment shader discarding fragments where `mod(v_dist, u_dashPeriod)` falls
  in the "off" half of the period — one extra draw call's worth of attributes, not a second shader
  permutation to switch between, so solid and dashed cables render together in the same batch.
- **Splice-target highlight** (ghost placement hovering a spliceable wire, ADR-0008's per-frame
  DOM/WebGL sync): reuses the same tessellation (`tessellateCable`, exported from
  `nodeEditorRenderer.ts` specifically so the hit-test and the draw call never compute two different
  curves for the same wire) for a point-to-segment distance hit-test, within a fixed pixel tolerance
  scaled by nothing else — deliberately not zoom-scaled, since a screen-space tolerance is what
  actually matches "how close does my cursor need to be" regardless of how zoomed in the canvas is.

## Consequences
- No new colour token was added — `tokens.color.error` (already used for the node error badge) and
  every existing `portUiKind` colour are the complete palette this scheme draws from, exactly as §10
  intended by ruling out a dedicated "valid" green up front.
- The dash shader is generic, not rejection-specific: `a_dash`/`u_dashPeriod` exist as raw plumbing
  a future milestone can reuse for the Listen node's dotted-cable convention (blueprint §4/§6.4,
  M12) without touching the shader again — that reuse hasn't happened yet, but the mechanism is
  already in place.
- This scheme colours the **in-progress drag only**. Committed/settled wires (once connected) render
  in their source port's colour at full alpha, never dashed, unconditionally — there's no
  "valid/rejected" concept for a wire that already exists, matching how every other node editor this
  blueprint draws from treats the distinction.
- Auto-inserting a Map node for a Modulation→Value connection (NODE_EDITOR.md §5) is explicitly
  **not** implemented this milestone — such a connection is simply valid, coloured the same as any
  other valid drag, with no converter node spawned. `ui/src/graph/graphStore.ts` is local/UI-only
  this milestone (CLAUDE.md's interim-simplifications note), so there's no engine-side Map node to
  insert yet; revisit once real command-bridge wiring lands and a real `adapt.map` (renamed M14, was
  `util.map`) instance can be
  spliced in the same way `spliceInsert` already splices a manually-chosen node into a wire.
