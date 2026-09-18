# M10 node editor — self-review: bugs, imperfections, and tests

Every feature built this session (`ui/src/canvas/InfiniteCanvas.tsx`, `ui/src/graph/*`,
`ui/src/canvas/webgl/nodeEditorRenderer.ts`, `ui/src/nodes/NodeCard.tsx` additions), reviewed
critically for bugs and rough edges, plus concrete tests for each. **Test everything in the actual
Standalone build** (`build/plugin/BazaltPlugin_artefacts/Debug/Standalone/Bazalt.exe`), not a
browser — WebView2 has its own focus/input quirks a browser tab won't reproduce (see the
cross-cutting section below).

Add your own observed problems under each feature (or a new section) and hand this back.

---

## Your notes

*(Add whatever you've observed here — reference section numbers from below if it's easier, or just
write freely. Leave this section for Viktor; Claude shouldn't fill it in.)*

-

---

## 0. Cross-cutting issues (read this section first — these affect almost everything below)

### 0.1 Overlay UI clicks leak through to canvas interactions (highest priority)
`InfiniteCanvas`'s `mousedown`/`contextmenu` listeners are attached to the **outer container**
(needed so clicks on nodes/ports, which live in a DOM layer *sibling* to the `<canvas>`, are
detected). But this container also wraps the top bar (macro sliders, Snap-to-grid checkbox, Fit
View/Gallery/Stress-test buttons), the Add menu, and the node context menu — and **none of those
stop propagation**. Concretely:
- Clicking any top-bar button/checkbox, or dragging a macro slider, falls through `closestNodeId`/
  `closestPortAnchor` (neither matches), lands in the "empty canvas → box-select" branch, and calls
  `setSelection([])` on mouseup — **the current node selection gets silently cleared every time you
  touch a top-bar control.**
- Dragging a macro slider a visible distance will actually **draw and show the blue selection-box
  rectangle** across the canvas while you're just adjusting a knob.
- Right-clicking inside the Add menu or the node context menu (if the user misclicks) starts a
  canvas pan instead of doing nothing.
- This is a **regression I introduced**, not a pre-existing M5 issue — M5's listener was scoped to
  the `<canvas>` element itself, which the top bar was never inside.

**Tests:** click every top-bar control with a node selected → selection must survive. Click-drag a
macro slider → no selection-box must appear, selection must survive. Right-click inside the Add
menu / node context menu → must not pan or open a second menu.

### 0.2 Drag state gets stuck if the mouse button is released outside the window
`panning`, `nodeDrag`, `wireDrag`, and `boxSelectStart` are only cleared by `mouseup`. If the mouse
button is released while the cursor is outside the app window (or the window loses focus mid-drag —
plausible in a DAW where clicking a different plugin window, or the host itself, steals focus),
no `mouseup` ever arrives and the drag stays "stuck" — the next mousemove inside the window will
silently keep applying it.

**Tests:** start a node drag, then Alt-Tab away and release the mouse button outside the window;
come back and move the mouse — the node must not still be following the cursor. Same for pan,
wire-drag, and box-select. Also test: start dragging, then click on a native JUCE control outside
the WebView (e.g. the "Options" button) without releasing the mouse first.

### 0.3 `spaceHeld` can get stuck `true`
Same root cause as 0.2: if focus is lost while Space is held (no `keyup` delivered), `spaceHeld`
stays `true` forever, silently turning every subsequent left-drag into a pan.

**Tests:** hold Space, Alt-Tab away, release Space outside the window, come back — left-drag must
select/box-select normally, not pan.

### 0.4 WebView2 focus/keyboard reliability, specifically in the plugin context
The project's own earlier notes flag that "plugin WebViews inside DAWs often don't receive keyboard
focus reliably." This session added several keyboard-dependent features (Shift+A, Delete/Backspace,
Escape, Ctrl+Z/Shift+Z/Y) plus two `autoFocus` inputs (Add-menu search, rename field) that were never
tested inside the actual WebView2 host, only reasoned about in code.

**Tests:** in the Standalone app, click into the native title bar or the "Options" menu, then click
back into the canvas — do keyboard shortcuts still work? Open the Add menu — does the search field
actually receive keyboard focus (cursor blinking, typing filters immediately) inside WebView2,
or does the first keystroke get eaten? Same test for the rename input. Later, repeat inside a real
DAW host (VST3), where focus stealing is more aggressive than Standalone.

### 0.5 DPI/monitor changes at runtime
`resize()` (which recalculates `devicePixelRatio`) only runs from a `ResizeObserver` callback, which
fires on **content-box size changes**, not DPI changes alone. Dragging the Standalone window from a
1x to a 2x (or fractional-scaled) monitor without also resizing the window may leave the canvas
rendering at the stale DPI until some unrelated resize happens.

**Tests:** with two monitors at different scaling, drag the Standalone window from one to the other
without resizing it — grid/cable sharpness and hit-testing accuracy must stay correct.

### 0.6 WebGL2 context loss
Nothing listens for `webglcontextlost`/`webglcontextrestored`. A GPU driver reset (sleep/wake,
driver crash, remote desktop session change) would leave the grid/cables permanently blank with no
recovery, while node interaction would keep working.

**Tests:** put the machine to sleep and wake it with the Standalone app open and the canvas visible;
check whether the grid/cables come back.

---

## 1. Pan (right-drag, Space+left-drag)
- Right-click-drag while the Add menu is open, or while a node's context menu is open — does it pan
  underneath the open menu unexpectedly, or does opening either menu correctly block it?
- Two-button chords: start a node/wire drag with the left button, then also press the right button
  before releasing — panning starts on top of the still-active drag; releasing the buttons in
  different orders can leave the node's visual position "frozen" mid-pan before resuming. Very much
  an edge case, but worth a quick manual check since both buttons are independently tracked.
- No test for "trackpad two-finger pan" vs. wheel — not implemented at all; confirm that's
  acceptable for now (mouse-wheel-as-pan is common trackpad behavior in other apps).

**Tests:** right-drag pans; Space+left-drag pans; releasing Space mid-pan (while still holding the
mouse button) should not suddenly switch to box-select or node-drag.

## 2. Zoom-on-cursor (mouse wheel)
- Zoom speed is tuned for a typical mouse wheel's `deltaY` (`Math.exp(-deltaY * 0.001)`); trackpads
  and different OS wheel-acceleration settings report very different `deltaY` magnitudes/`deltaMode`
  values, which this doesn't account for — zoom could feel too fast or too slow depending on the
  input device.
- No explicit min/max zoom feedback (e.g. a cursor change or a subtle clamp indicator) when already
  at `MIN_ZOOM`/`MAX_ZOOM` and still scrolling.

**Tests:** zoom in/out to the clamps and confirm nothing breaks (grid step, hit-testing tolerances)
at `MIN_ZOOM=0.1` and `MAX_ZOOM=8`. Test with an actual trackpad, not just a mouse wheel.

## 3. Box-select (partial-touch)
- See 0.1 — the empty-click-clears-selection fallback is exactly what box-select's "zero-size box"
  path resolves to, so this feature and bug 0.1 are the same code path.
- Shift+box-select only **adds** hit nodes to the existing selection; it never removes/toggles nodes
  already selected that fall inside the box. Confirm this is the wanted behavior (most editors do
  this, but worth a deliberate decision, not an assumption).
- Box-select uses plain axis-aligned bounding-box overlap against each node's real
  `getBoundingClientRect()` — decoration nodes (Frame especially, which is meant to be a large
  backdrop) will behave like any other node for selection purposes; there's no "frame selects
  everything inside it" behavior (not in scope this milestone, but worth listing so it isn't mistaken
  for a bug later).

**Tests:** drag a box partially touching several nodes at different zoom levels; drag a box that
starts on empty canvas and ends over the top bar (does it still compute correctly, or does the
top-bar-click issue in 0.1 interfere?); Shift+box-select over a mix of selected/unselected nodes.

## 4. Fit view (auto + toolbar button)
- Padding is a fixed 80px regardless of window size — on a very small window this can produce a
  degenerate/negative usable area, clamped to `MIN_ZOOM` rather than failing, but untested visually.
- Auto-fit only fires once, the first time the node count goes from 0 to non-zero. Deleting every
  node and adding a new one later will **not** re-trigger auto-fit. Decide if that's wanted.
- Fit view is computed from nodes' *current* on-screen rects, inverted through the *current* camera
  — if called in the same tick as an unapplied camera change (the world transform is only written
  once per animation frame), the very first Fit View call after a pan/zoom could measure against a
  one-frame-stale transform. Likely unnoticeable in practice; worth a couple of rapid
  pan-then-fit-view tests to be sure.

**Tests:** Fit View with 1 node, with all nodes overlapping at one point, with nodes spread very far
apart, immediately after a fast pan/zoom, and after resizing the Standalone window very small.

## 5. Add menu (Shift+A / right-click, search, categories, keyboard nav, auto-flip)
- Opening via Shift+A before the mouse has ever moved (keyboard-only session) opens the menu at
  `(0,0)` — the top-left corner — since there's no prior mouse position to fall back to.
- The auto-flip effect only re-measures on `[x, y]` changes, not on window resize — resizing the
  Standalone window while the menu happens to be open won't re-flip it if it's now off-screen.
- Real descriptors can arrive **after** the menu is already open (async `fetchNodeDescriptors`);
  the category list and flat index recompute, but the keyboard-focused index isn't remapped to the
  same item, so the highlighted row can silently jump.
- See 0.1's category re: right-click starting a pan if it lands just outside the menu's actual
  rendered bounds but still "inside" the container.

**Tests:** open via Shift+A immediately after launch with no prior mouse movement. Open near each
screen edge/corner and confirm it flips instead of clipping. Resize the window while it's open. Type
a search query, then wait for real descriptors to load (if running against the real engine) and
confirm the list/highlight still make sense. Press Enter with an empty/no-match search.

## 6. Ghost placement (click-to-place, splice-on-wire-hover, Escape-cancel)
- **Splicing an incompatible node silently does nothing.** `spliceInsert` requires the chosen type to
  have at least one input *and* one output; picking a source-only node (MIDI Note), a sink-only node
  (Master Out), a Macro, or any decoration while hovering a wire produces **no feedback and no
  node placed** — the click is simply swallowed. This needs either a fallback (place it unconnected
  instead of splicing) or a rejected-state indicator instead of silent failure.
- **Splice bypasses type-checking entirely.** Unlike a normal drag-to-wire connection,
  `spliceInsert` never calls `canConnect` — it always wires the new node's first input/primary output
  into the existing wire's endpoints regardless of whether the types actually match. A node whose
  first input isn't audio-compatible could get silently spliced into an audio wire.
- A click-drag while the ghost is armed (mousedown on one spot, drag, mouseup elsewhere) relies on
  the browser correctly firing a native `click` event despite the movement — untested; if it doesn't
  fire, the ghost would be left attached to the cursor with no way to place or cancel except Escape
  or a subsequent clean click.
- Splice/placement hit-tolerance (10px) is a fixed screen-space value, not zoom-scaled — reasonable,
  but confirm it doesn't feel too generous when zoomed out (many close-together wires) or too tight
  zoomed in.

**Tests:** try to splice MIDI Note / Master Out / Macro / a Frame into an existing wire and observe
what happens (currently: nothing). Try splicing a type-incompatible node into an audio wire and check
whether the resulting connection is actually wrong. Click-and-drag (not a clean click) while a ghost
is attached. Right-click to cancel placement, including while hovering a splice target.

## 7. Drag-to-wire (connect) with valid/rejected/hover feedback
- Hovering an input port that's **already connected to something else** shows the same "valid"
  (source-colored, full-alpha) feedback as an empty port — there's no visual distinction for "this
  will silently replace an existing connection." A user could disconnect something without realizing
  it.
- **Deleting the dragged node's own endpoint mid-drag via keyboard** (Delete/Backspace isn't
  disabled while a wire-drag is in progress) can commit a wire referencing a node id that no longer
  exists in the store. Same risk, lower odds, for the hover target's node.
- Hover tolerance (16px) is fixed screen-space regardless of zoom — verify it doesn't cause ambiguous
  "nearest port" picks when zoomed out with many closely-spaced ports.
- Dropping on a hovered-but-*invalid* port (wrong type) behaves identically to dropping on empty
  space: if the drag detached an existing wire, that wire is deleted rather than snapping back to its
  original connection. Confirm this is the wanted behavior, not just what happened to fall out of the
  implementation.

**Tests:** drag onto an already-occupied input and confirm you notice the replacement happening.
Start a wire drag, then press Delete/Backspace while still dragging (with the source node selected).
Drop on an incompatible port and confirm the detached wire is gone, not restored. Test at extreme
zoom in both directions with tightly packed ports.

## 8. Wire drag-off-to-delete / drag-onto-reconnect
- Grabbing an existing wire's input end and dropping it back on its **own original port** should be a
  no-op visually but actually creates a brand-new wire id under the hood (same connection,
  different identity) — harmless today, but worth knowing if wire identity ever matters later (e.g.
  for telemetry taps keyed by connection id in M11).
- No distinction between "dropped on empty canvas" and "dropped on a rejected port" — both delete the
  detached wire (see §7). Worth an explicit product decision either way.

**Tests:** detach and immediately re-drop on the same port. Detach and drop on empty space. Detach
and drop on an incompatible port.

## 9. Node move (drag, single + multi-select)
- **Multi-node drag + snap-to-grid doesn't preserve relative offsets.** Each selected node's absolute
  position is snapped independently, so a multi-selection that wasn't already grid-aligned relative
  to itself can visibly "shear" apart during a snapped drag instead of moving as a rigid group.
- Clicking an already-multi-selected node **without** dragging and without Shift leaves the whole
  multi-selection intact rather than collapsing to just that node (differs from some editors'
  convention, e.g. Figma). Worth a deliberate decision.
- Deleting a node mid-drag (see 0.1/§7's keyboard-during-drag class of issue) — the dragged DOM
  element becomes detached from the document; the drag completes harmlessly but is worth a
  regression test.

**Tests:** multi-select several non-grid-aligned nodes, drag with snap enabled, confirm they move as
a rigid group (or decide this is acceptable and document it). Click an already-selected node in a
multi-selection without dragging — confirm the expected selection behavior. Delete a node via
keyboard while it's being dragged.

## 10. Node select (click, Shift-click, box-select)
- Covered mostly under §3 and 0.1. Additional: rapid double-click on a node's **body** (not the
  title) — does it just select twice (harmless) or trigger anything unintended? Should be harmless,
  worth a quick check since rename only triggers on the title specifically.

**Tests:** rapid double/triple-click on a node body vs. its title.

## 11. Node delete (Delete/Backspace)
- No confirmation for deleting a large multi-selection — acceptable per spec (no confirmation dialog
  was asked for), but worth confirming that's still fine now that undo exists as the safety net.
- Interaction with an in-progress drag/wire-drag — see 0.1/§7/§9.

**Tests:** select a large number of nodes and delete them at once; confirm undo restores all of them
plus their wires in one step.

## 12. Node rename (double-click title, inline edit, right-click → Rename)
- **Double-click-to-rename silently does nothing on Singleton and Decoration-layout nodes** (Frame,
  Header, Image, Reroute, Master Out, Sum Voices, etc.) — those layouts don't render a `.node-title`
  element at all, so the double-click handler's target check never matches.
- **Right-click → Rename on those same node types *does* open the rename input, but it's positioned
  for the Standard/Horizontal layout's title-bar geometry** — for a Singleton/Decoration node (very
  different size/shape) the input will appear misaligned, not overlapping the actual title text.
- Renaming to an empty string (or all-whitespace) silently resets to the descriptor's original title
  rather than rejecting the edit or keeping the previous custom title. Confirm that's the wanted
  default.
- Escape-then-blur double-commit race was found and fixed this session (`renameResolvedRef` guard) —
  worth an explicit regression test since it's exactly the kind of thing that could silently regress.

**Tests:** try renaming every layout variant (standard, horizontal, singleton, decoration) via both
double-click and the context menu. Rename to an empty string. Start editing, press Escape, and
confirm nothing gets committed (watch history/undo stack — Escape must not create an undo entry).
Start editing, click away (blur) without pressing Enter — confirm the typed value *does* commit.

## 13. Node bypass toggle (title-bar icon + context menu)
- Rapid double-click on the bypass icon toggles it twice (net no-op) rather than being debounced —
  low priority, but confirm it doesn't feel broken/unresponsive to a real user double-clicking fast.
- Bypassed nodes' *outgoing* cables render identically to normal cables (no dimming/visual cue that
  the source is bypassed) — purely cosmetic today since nothing is actually computing signal flow,
  but worth listing so it isn't mistaken for an oversight later.

**Tests:** toggle bypass via the icon and via the context menu on every layout variant that supports
it (Macro nodes and Singleton/Decoration nodes don't render a bypass icon at all — confirm that's
intentional, not a gap).

## 14. Right-click node context menu (Rename / Toggle Bypass / Delete)
- **Doesn't reposition near screen edges** the way the Add menu does — right-clicking a node close to
  the window edge can render the menu partially off-screen.
- Right-clicking a node does not change the current selection first — deleting/renaming via the menu
  only ever affects that one node, even if a different multi-selection is active. Confirm that's the
  wanted behavior (vs. some apps that select-then-act).

**Tests:** right-click a node near every screen edge/corner. Right-click a node that is *not* part of
the current multi-selection and delete it via the menu — confirm only that node is removed.

## 15. Snap-to-grid
- Toggling the checkbox mid-drag should take effect on the very next mouse-move (verify no
  one-frame lag causes a visible jump).
- Grid size (`sizeWorldUnits`) itself isn't exposed in any UI, only enable/disable — pre-existing
  limitation from M5, not new this session, but worth listing since it's now actually load-bearing.
- See §9 for the multi-select-snap-shearing issue specifically.

**Tests:** toggle snap on/off mid-drag. Confirm ghost placement and node drag snap consistently to
the same grid.

## 16. Undo/redo (Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y)
- Selection changes are **not** undo-tracked by design — Ctrl+Z after only changing selection (no
  other edit) silently undoes whatever the last *actual* mutation was instead. Confirm this matches
  user expectations; most node editors do exclude selection, but it's easy to find surprising in the
  moment.
- **Undo/redo fired while a drag is in progress** (node-drag, wire-drag) restores the store to a
  different state than what the in-progress drag's captured "origin" snapshot assumes — the drag can
  commit a stale/wrong position on mouseup after an undo happens mid-gesture. Very much an edge case
  (requires pressing Ctrl+Z with the other hand while dragging) but a real one given how easy it is
  to trigger by accident.
- No visible feedback that an undo/redo happened (no flash, no status text) — purely a polish gap.

**Tests:** perform an edit, change selection only, Ctrl+Z — confirm which action actually gets
undone. Start a node drag, press Ctrl+Z mid-drag without releasing the mouse, then release — check
the resulting position/state for corruption. Exceed 100 history entries and confirm the oldest ones
correctly drop off.

## 17. WebGL2 grid rendering (migrated off Canvas2D)
- No context-loss recovery — see 0.6.
- Grid geometry is fully recomputed and re-uploaded to the GPU every frame regardless of whether the
  camera moved — fine at this node count, but worth a note if a future session revisits performance
  at higher node/cable counts (the M8 stress-test scale, not attempted this session).

**Tests:** confirm the grid still renders correctly (no missing/duplicated dots, correct minor/major
tiers) across the full zoom range, and after resizing the window.

## 18. WebGL2 cable rendering (bezier tessellation, dashed-line shader for rejected/hover states)
- Dash period is fixed screen-space (10px), not zoom-scaled — dashes stay a constant *screen* size
  regardless of zoom rather than a constant *world* size. Confirm this reads correctly at both
  extremes rather than looking wrong at one of them.
- Fixed 24-segment tessellation per cable regardless of length — very long cables zoomed in close
  could show visible faceting instead of a smooth curve.
- No color/alpha transition when a wire-drag's hover state flips (valid ↔ not-yet-over-a-port ↔
  rejected) — it's an instant per-frame pop, not animated. Cosmetic only.

**Tests:** zoom in tightly on a single long cable and check curve smoothness. Watch the color/alpha
change as you sweep a drag across a valid target, an invalid target, and open space in quick
succession — confirm it's readable even without animation.

## 19. DOM/WebGL port-anchor sync (`portAnchors.ts`, the "DOM is the source of truth" design)
- `measurePortAnchors` does a full `querySelectorAll` + `getBoundingClientRect` pass over every port
  in the graph, every single animation frame. Already flagged in the ADR as fine at this node count
  and worth re-profiling only if node counts grow significantly — listing here so it isn't forgotten.
- A node added this exact frame could theoretically be measured before its first layout/paint
  completes, leaving its cables briefly undrawn for one frame. Low risk given React's commit timing,
  but untested explicitly.

**Tests:** place a new node and *immediately* (same gesture, no pause) start dragging a wire from it
— confirm the cable renders from frame one, not one frame late. Eventually, once the graph is larger
than a couple dozen nodes, re-measure actual frame time to confirm this approach still holds up.

## 20. Local graph store lifecycle (`graphStore.ts`, no persistence, module-level singleton)
- **No persistence** — reloading/relaunching resets to the hardcoded seed graph, losing everything
  placed. This is a deliberate, already-documented scope limitation for this milestone, not a bug to
  fix now — listed so it's not mistaken for an oversight when noticed.
- **Switching to the Component Gallery or Stress Test and back unmounts and remounts the whole
  canvas.** Graph *data* (nodes/wires, since it's module-level) survives this, but the **camera
  (pan/zoom) resets to default**, and auto-fit-view re-triggers on the way back. Confirm this is the
  wanted behavior rather than an accidental side effect of how those views are switched in `App.tsx`.
- `fetchNodeDescriptors()`'s promise has no `.catch` — a rejected fetch (vs. an empty/successful
  result) would produce an unhandled-rejection warning instead of failing gracefully. Low practical
  risk but cheap to fix.

**Tests:** place several nodes, pan/zoom somewhere non-default, open the Component Gallery, close it
— confirm what actually happens to the camera and to the graph. Force `fetchNodeDescriptors` to throw
(e.g. temporarily break the native function) and confirm nothing worse than a console warning occurs.

## 21. `NodeCard.tsx` additive changes (`instanceId` prop, `data-*` port attributes)
- Not re-verified visually this session: confirm the M9 Component Gallery still renders pixel-for-
  pixel identical to before (it should — `instanceId` is optional and every new attribute is gated on
  it — but this session's only screenshot was of the live canvas, not the gallery).

**Tests:** open the Component Gallery and compare against the last known-good screenshot/behavior
from M9.

## 22. Descriptor catalog merge (real + mock) and the seeded demo graph
- Real descriptors that arrive asynchronously *after* the Add menu is already open aren't remapped
  cleanly to the keyboard-focused index (see §5).
- Seed graph positions are hand-picked for the *current* mock descriptors' sizes; if those mocks'
  content changes later, the seed layout may start overlapping.

**Tests:** none additional beyond §5/§20 above — listed for completeness.

---

## 23. Retrospective — would I build this the same way again?

Written deliberately ignoring what it cost to build the current version — the question is only
whether it's the right shape going forward, not whether it's worth redoing.

### Verdict, short version
The **rendering split** (DOM for node bodies, WebGL2 for grid/cables, one CSS transform driving
both) I'd keep exactly as-is — it's validated by ADR-0008's benchmark and is what M11's real
telemetry-driven cable activity needs anyway. The **interaction/event-handling architecture** I
would *not* build the same way again — it's the direct cause of bug 0.1 and the ghost stale-closure
bug I had to catch mid-session, and it will keep producing bugs of exactly that shape as more UI gets
added on top of it. The rest is a mix of "keep" and "change one specific thing."

### What I'd change: interaction handling
The current model is **opt-out**: canvas gestures (pan, box-select) start unless something else
*claims* the event first (a node, a port, a menu that remembered to `stopPropagation`). Every new
overlay control — the top bar, the Add menu, the context menu, and every future one (Assist menu,
Constraints popover, parameter sliders) — has to remember to opt out, and forgetting is invisible
until someone clicks it and the selection mysteriously clears. That's exactly what happened with the
top bar.

I'd flip it to **opt-in**: a canvas gesture (pan/box-select) may only *start* when the mousedown
target is the bare canvas or world-background element itself — `e.target === canvas ||
e.target === worldContainer` — never "whatever's left after checking known exceptions." Nodes,
ports, and all overlay UI are then excluded structurally, not by convention, and a future menu needs
zero awareness of the canvas to be safe.

I'd also stop hand-rolling the imperative-ref-plus-React-state duplication I used for `ghost`
(`ghostRef` for synchronous reads inside the mount effect's closures, `ghost` state to trigger a
render) — that duplication is exactly what caused the stale-closure bug this session. Two honest
options instead: (a) treat interaction state as its own tiny external store (`subscribe`/`snapshot`,
same shape as `graphStore.ts` and `telemetryClient.ts` already use), so there's one source of truth
and React only ever reads a snapshot of it; or (b) split the one 700-line mount effect into several
small, independent gesture handlers (pan, box-select, node-drag, wire-drag, ghost-placement), each
owning only its own slice of state, tried in priority order by a thin dispatcher — closer to how
Figma-style canvas tools are usually structured, and each piece becomes independently testable
instead of one shared closure where a bug in one gesture can leak into another (the two-button-chord
edge case in §1 is a symptom of this).

### What I'd change: rendering, for correctness and being "as optimized as possible"
Two concrete changes, both real, both currently absent:

1. **Stop redrawing every frame unconditionally.** The `requestAnimationFrame` loop currently runs
   forever, at full rate, even when the canvas is completely idle — no pan, no drag, nothing changed.
   For a synth plugin that's realistically going to sit open for hours in a DAW session, that's
   continuous GPU/CPU burn for nothing. I'd add a dirty flag (camera moved, a node moved, a wire
   changed, a drag/ghost is active) and only schedule the next frame when something actually needs
   redrawing — falling back to a redraw-on-demand model instead of a permanent 60–120Hz loop.
2. **Stop measuring every port's DOM position every single frame.** `measurePortAnchors` currently
   does a full `querySelectorAll` + `getBoundingClientRect` pass over the whole graph on every frame,
   which is fine at M10's node count but is pure waste during steady-state panning/idle — the ports'
   positions *relative to their own node* never change, only the node's own `(x, y)` and the camera
   do. The better shape: measure each node's port offsets once (on first mount, and again only if a
   `ResizeObserver` on that specific node reports its size actually changed — e.g. its descriptor's
   content changed), cache those local offsets, and compute every port's screen position per frame
   with plain arithmetic (`node.x + offset.x` fed through the camera transform) instead of a DOM
   read. This keeps the same "DOM/CSS defines the real layout" principle (the cache is *seeded* from
   a real measurement, never hand-computed/guessed), it just stops re-measuring what hasn't changed.

I would **not** revisit WebGL2 vs. Canvas2D for the grid/cables — that was a real decision made
together this session (not something I'm second-guessing in hindsight), and it's the right call
specifically because M11 needs it for real per-cable telemetry activity; doing it now avoided a
second rewrite.

### Making live values "as close as possible" (relevant now, load-bearing once M11 lands)
This isn't really an M10 rendering question — it's about which pipeline feeds the pixels. The
project already has the right piece for this: M4/M5's telemetry client
(`ui/src/telemetry/telemetryClient.ts`) polls taps in its own `requestAnimationFrame` loop and
linearly interpolates between the last two received frames, entirely outside React, specifically so
motion stays smooth at display refresh even though telemetry itself arrives at a lower, uneven rate
(ARCHITECTURE.md §6.4). The right move for M11 is to have every value-driven visual element — cable
activity pulse, inline node previews, a live parameter readout — read from that *same* interpolated
snapshot directly inside the node-editor's own per-frame loop (the `frame()` function this session
built), the same way it now reads `graphStore`'s snapshot for wire endpoints. Not a new pipeline, not
routed through React state, not re-derived per component — one interpolated source, read at render
time by whichever layer (WebGL cable color, or a DOM text readout) needs it that frame. I'd keep this
architecture exactly as the existing M4/M5 design already sets it up; M10 didn't need to build
anything new here, and M11 shouldn't either beyond wiring the read.

One thing I'd fix now rather than let compound: `spliceInsert` and the Add menu's splice-hover path
currently bypass `wireRules.canConnect` entirely (§6) — a value-accuracy concern in spirit, since a
type-mismatched splice would make the graph "lie" about what's actually connected. I'd make splicing
go through the same validity check a normal drag-connection does, and grey out (or simply not offer)
node types that can't validly splice into whatever wire is currently hovered.

### Keyboard shortcuts inside an actual VST3 host
This is the one area where I'd do real investigation *before* writing any interaction code, not
after. Right now every keyboard shortcut (Ctrl+Z/Shift+Z/Y, Delete/Backspace, Shift+A, Escape) is a
`window`-level `keydown` listener inside the WebView2 page's own JS context, and I only reasoned
about it — I never verified it against the actual plugin host. Two concrete risks that are specific
to being a plugin, not a Standalone app or a browser tab:

1. **JUCE's `WebBrowserComponent` may not forward keyboard focus into the embedded WebView reliably
   inside a DAW.** NODE_EDITOR.md §6.7 already flags this as an open investigation ("Plugin WebViews
   inside DAWs often don't receive keyboard focus reliably... decide where the computer-keyboard
   piano is available") — it was written down as a TODO for the future piano feature, but it applies
   identically to every shortcut this session built. I built five keyboard-dependent interactions on
   top of an assumption that was never actually confirmed for the VST3-hosted case.
2. **Common shortcuts are usually claimed by the host first.** Ctrl+Z is almost universally the
   DAW's own project-level undo; Space is very often global play/stop; Delete may be bound to "delete
   selected track/clip" at the host level. Depending on the host and OS, these can be intercepted by
   the DAW's own accelerator/menu system before the keystroke ever reaches a plugin's embedded view,
   regardless of what the plugin's own code does with `keydown`.

If I were building this again, I'd resolve both of those — with a real JUCE-side test, not
speculation — before relying on keyboard as the *only* way to trigger anything. Concretely: every
keyboard-only action should have a visible UI fallback. I already did this for Fit View (a button)
and Shift+A (a right-click alternative) and Delete (also in the node context menu) — I did **not**
do it for Undo/Redo, which currently has no button or menu entry at all and is the single most likely
shortcut to be intercepted by a host. That's worth adding regardless of what the JUCE investigation
finds.

### Testing loop
The thing that actually went wrong this session (typing into the user's VS Code via blind
screen-coordinate automation) has a real fix I'd set up from the start next time, not after being
corrected: WebView2 is Chromium-based and supports launching with a remote-debugging port
(`--remote-debugging-port`), which means a real automation tool (Playwright, or raw Chrome DevTools
Protocol) can attach to the **actual embedded WebView inside the real Standalone/VST3 process** —
not a plain browser tab standing in for it, and not blind OS-level mouse/keyboard simulation that
can't tell which window has focus. If I were setting this project's UI-testing story up again, I'd
wire that in early (even just a small documented recipe for launching the Standalone build with that
flag and pointing Playwright at it) rather than falling back to manual screenshot spot-checks or,
worse, synthetic input against absolute screen coordinates.

# MY FEEDBACK Some things I noticed

- I cannot zoom when mouse hover over a node. Only when on the background
- Some nodes have the property sockets still wrong and this is a big deal because it means it is BUILT WRONG. The nodes are not supposed to be hardcoded. The Random node for instance still has an arrow input socket INSIDE of it. That is wrong, it is supposed to lie on the border. But fixing it by just editing the Random node is not a real fix - the fix shall modularize and make the system for the components, where almost nothing is hardcoded. Now granted, the random node, even on the design looks different than a classic one. That is on purpose. More complex nodes, such as oscillators, generators, granulators,... will be "custom made". But they should STILL keep as much logic from the generic node as possible.
- Sliders don't work at all yet, I presume this is by design as in it hasnt been worked on yet. So I understand. We can defer.
- The "webview" arrangement is extremely noticable. For example: everything is selectable, scrolling behaviour is glitchy and not smooth. I am wondering if the web view was a good idea, especially with what you said previously. But if it is, I at least demand it not be so glitchy and "web like". 
- The functionality of the dots (for example in ADSR node input sockets) has likely been misunderstood or not yet implemented. First of all, it is supposed to be a dot, moreso than a capsule shape. But more importantly - a dot in this way signalizes that you CAN make a connection there, but there is none yet and instead the node is using the "hard-set" value on the right (via the slider). When you connect - the dot changes to the appropriate symbol (arrow/?/!) and the slider disappears as it is no longer being used. The value is remembered tho, as if disconnected, the value falls back to the slider one.
- Selected state (box select) is only applied on box select release. It should already be being applied on selecting. 
- This is not so relevant, as the nodes are still only mock, but a Frame is titled "Bass", Master Out has an output, Macro doesnt have one. But please dont waste time with fixing these, as this will come later.
- The node connections should coonnect perfectly with the arrows. For symbols such as ? or !, this obviously doesnt apply. But for arrows - there should be no space between the connetion cable and the arrow. This only applies for cables leading in an INPUT arrow. In the section where a cable is leading from an arrow, a gap should be there.
- Get rid of the grid dots in the background.
- The UI is very pixelated on zoom. Is that on purpose? 