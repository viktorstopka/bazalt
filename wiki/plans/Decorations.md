# Decorations — a canvas, not only an engineering space

**Status:** Proposed, 2026-10-04. Planned only, no code. The next batch after the
Tune viewer.

---

## 0. Origin

Direct instruction, 2026-10-04: a decorative node batch — Reroute ("built now, but
doesn't work", plus Ctrl-dragging a wire to spawn one for cable management),
Header (big text), Comment (paragraphs), Box (purely layout — none of a Group's
functionality), and dropping an image onto the canvas: "Decals are a nice twist ...
cute images as part of the patch making the node canvas more of a canvas, than an
engineering space." Boxes and images resize. None of them look like ordinary nodes
(no ports rows, no parameters). A new category: **Decorations**.

## 1. Why Reroute "doesn't work" today

`util.reroute` exists in the engine (a polymorphic pass-through, `RerouteNode.h`)
and compiles fine, but its card (`NodeCard.tsx`'s `DecorationBody`) renders a bare
dot with **no port anchors** (`data-port-anchor`), so the canvas has nothing to drop
a cable on or drag one from. The engine half is done; the UI half was never built.

## 2. Model: decorations are ordinary graph nodes with no signal

**Recommendation:** keep every decoration a `NodeInstance` in the graph, not a
separate canvas layer. Selection, move, delete, copy/paste, undo/redo (whole-graph
snapshots, ADR-0025) and patch save/load then work for them with no new code
paths. What makes them different:

- A `Decoration` layout variant (exists) plus a descriptor flag `isDecoration`; they
  have no ports (Reroute excepted), no parameters, no previews.
- **The compiler skips them** (except Reroute, which carries signal): no node
  instance, no buffers, no step — they cost nothing at runtime.
- Their content lives in `NodeInstance.properties` (already persisted, already
  edited via `graphSetProperty`): `text`, `width`/`height`, `colour`, `asset`
  (images, §4).
- Rendered **below** ordinary nodes (boxes and images are backgrounds), selected
  and moved like nodes; resize handles on Box, Comment and Image.

Type ids (the never-rename rule is suspended, but these are fresh anyway):
`deco.reroute` (renamed from `util.reroute`, migration for old patches),
`deco.header`, `deco.comment`, `deco.box`, `deco.image` — replacing the three
`deco.*` mocks in `mockDescriptors.ts` (Frame/Header/Image), which this batch makes
real. Add-menu category: **Decorations**.

## 3. The five

### Reroute (`deco.reroute`)
- A small dot with real input and output anchors — the whole dot is both: drop a
  cable on it, drag a new cable out of it; fan-out (several cables out) works as
  for any output.
- **Ctrl/Cmd-drag on an existing wire** spawns a reroute at the cursor, splitting
  the wire in two (disconnect + add + 2× connect through `applyBatch` — one undo
  step, one recompile), and keeps dragging the new dot. Double-click a wire does the
  same without the drag.
- Takes the colour/type of whatever feeds it (it already resolves polymorphically).
- Deleting a reroute reconnects its source to its destinations (one undo step), so
  cleaning up cable routing never breaks the patch.

### Header (`deco.header`)
- Large text (the primary face, two or three sizes), no frame. Double-click to edit
  inline; Enter commits, Shift+Enter new line. `properties.text`, `.size`.

### Comment (`deco.comment`)
- Paragraph text that wraps to a resizable width, muted colour, subtle background so
  it reads as a note. Plain text with line breaks for now; light Markdown
  (bold/italic/lists) is a later option. `properties.text`, `.width`.

### Box (`deco.box`)
- A resizable rectangle with an optional label in its corner and a colour from a
  small muted palette (theme tokens, so it works in any theme). Drawn behind nodes.
- **Purely visual**: it does not own, move or collapse what sits inside it — that
  is what a future Group/Frame is for. (Open question §6: an optional Alt-drag that
  moves whatever is inside.)

### Image / Decal (`deco.image`)
- Drag and drop an image file (PNG, JPEG, WebP, GIF, SVG) onto the canvas: a decal
  appears where it was dropped, at its natural size capped to a sensible maximum.
  Resize from the corners (aspect locked; Shift frees it), optional opacity.
- Paste from the clipboard does the same.

## 4. Storing images — compression: yes, on import

Patches are saved as JSON, travel inside DAW sessions (plugin state) and get shared,
so an untouched 8 MB phone photo per decal would be a real cost. **Recommendation:**
compress once, at import, in the UI (images are chrome, not audio — CLAUDE.md
rule 1 is about sound):

- Decode, then downscale so the longest side is at most **2048 px** (about twice the
  largest sensible on-screen size, so it stays sharp when zoomed in), re-encode as
  **WebP at quality ~0.85** — typically 50–300 KB. Where the WebView can't encode
  WebP, fall back to JPEG for opaque images and PNG for ones with transparency.
- Keep as-is: **SVG** (vector, already small, rendered through `<img>`, which runs
  no scripts) and **animated GIF/WebP** under a size cap (re-encoding would lose the
  animation); above the cap, keep the first frame and say so.
- Images live in the patch as **content-addressed assets** (`assets: { hash → bytes,
  type }`), referenced by `properties.asset` — the asset store `Factories.md`
  decision 1 already calls for, with decals as its first, simplest user. The same
  image used twice is stored once.
- A soft budget: warn when a patch's assets pass ~5 MB.

## 5. Order

1. **Reroute** — anchors on the dot, Ctrl/Cmd-drag and double-click on a wire,
   delete-reconnects, the `util.reroute` → `deco.reroute` migration.
2. **Header and Comment** — the text decorations and inline editing.
3. **Box** — resize handles (shared with Comment and Image), palette, z-order.
4. **Asset store + Image** — the patch `assets` section (schema bump), drop/paste,
   compression, resize.

Tests: compile ignores decorations (no step, no buffer); reroute is transparent for
every signal type; split/delete-reconnect are single undo steps; patch round trip
with text and assets; asset dedup and size cap. The visual parts are checked by
hand in the running editor.

## 6. Open questions

- Box: should Alt-drag move everything inside it? (Leaning: yes, as an explicit
  modifier — the default stays purely visual, as asked.)
- Comment: plain text only, or light Markdown from the start? (Leaning: plain first.)
- Image size limit per patch: warn only, or refuse past a hard cap? (Leaning: warn.)
