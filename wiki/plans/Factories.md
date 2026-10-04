# Factories — rich editors for curves, wavetables, EQs and more

**Status:** Proposed, 2026-10-04. Not scheduled; no code exists. This plan turns
`wiki/NODES.System.md` §3 (Content) and §8 (Factories) — the agreed *shape* of a
factory — into a buildable sequence, and answers the five questions §8 lists as
"must be decided before building". The catalog entries (`factory.curve`, `.wave`,
`.eq`, `.material`, `.notes`, `.sample`) stay in `wiki/NODES.md`; this file is the
how and the order.

Companion plans that build on this one: `BakedPhysics.md` (a physics scene is a
factory) and `SpatialScene.md` (a 3D scene is a factory).

---

## 0. Origin

Direct instruction, 2026-10-04: "a set of editors that allow for a nice editing of
wavetables, eqs, lfo curves". The same conversation set the larger direction —
physically and spatially oriented sound, 3D editing, baked simulations — and the
factory is the one piece of infrastructure all of it shares: **a node that owns an
editable document and opens a dedicated editor for it.** Built once, it carries
curves today and scenes later.

## 1. What a factory is (recap of §8, unchanged)

A native node with three things an ordinary node lacks:

- **Content** — an editable document (`NodeContent`: `schemaId`, `schemaVersion`,
  `payload`, `externalized`), saved with the patch, never modulatable or host-
  automatable. Content is what's *edited*; ordinary ports are what's *played*.
- **A custom editor**, declared by the node (`ui.kind = "custom"`, `editorId`), not
  hard-coded by type id.
- **Unwrap** — a declared expansion into ordinary nodes, one undoable command,
  sound-preserving, one-way. A factory may never be more capable than the
  primitives it unwraps into.

Two shapes: **producers** publish a `Data` buffer (Curve, Wave, Material, and later
Scene/Physics); **processors** run their own DSP configured by the content (EQ,
Notes, Sample).

## 2. What already exists to build on

| Need | Already there |
|---|---|
| Publishing an immutable buffer to the audio thread without locks or allocation | The `Data` pipeline: `DataBuffer`/`DataPublisher`, `Node::getDataPublisher()`/`setDataInput()`, GraphCompiler's Data wiring (Data Foundations batch). Tags include `Curve`, `Wavetable`, `ModalSet`. |
| A producer the curve factory unwraps into | `data.table` (a fixed 32-point parameter bank today — §3's own note calls this the interim shape `NodeContent` replaces). |
| Drawing a live diagram inside a node | `NodeCard.tsx`'s `INLINE_DIAGRAMS` slot (Map's diagram), SVG + non-scaling strokes (zoom-proof — `PhaseLockedPreview.tsx`'s header). |
| A modal editor portalled out of the zoomed canvas | `MacroEditTypeModal` via `state.overlayTarget`. |
| Showing what a shape does as it plays | `PreviewKind::PhaseLocked` — a phase source evaluated off the audio thread, with a playhead. A curve consumer can be a phase source. |
| Patch format migrations | `PatchSerializer`'s versioned chain (v1 → v10). |
| Undo | Whole-graph snapshots (ADR-0025) — content rides along for free once it's in the patch. |

## 3. The five open decisions — recommended answers

1. **Asset store** (needed before any factory owns audio). *Recommendation:* defer
   until `factory.sample`/`factory.wave`-from-file. Curve, EQ, Material and Notes
   content is small JSON and lives inline. When assets arrive: content-addressed
   blobs (hash → bytes) in a patch-level `assets` table, embedded below a size
   budget (~1 MB), referenced (path + hash) above it, with a "collect assets"
   command for sharing.
2. **`NodeContent` as a real category.** *Recommendation:* yes, now, as
   `NodeInstance.content` (optional; absent for every ordinary node). Engine side, a
   node reads it via one new virtual, `setContent (const NodeContent&)` — called on
   the message thread at compile time, where the node builds its `DataBuffer` and
   publishes it (the same path `setParameter`-driven rebuilds already use).
   `data.table` and `seq.steps` migrate to content at the same time, retiring their
   32-slot parameter banks.
3. **`ui.custom` descriptor field.** *Recommendation:* add it with the first factory
   (`NodeDescriptor.editor { kind, editorId }`); the UI keeps an editor registry
   keyed by `editorId`, exactly like the `INLINE_DIAGRAMS` table but for full
   editors.
4. **`stock.*` rename.** *Recommendation:* do it whenever the first shipped group
   lands; free until then.
5. **Unwrap as a declared expansion.** *Recommendation:* yes — each factory returns
   its expansion as data (`NodeGraph` fragment + rewiring map), applied through
   `GraphEditController::applyBatch` as one command. Tested by rendering before and
   after and comparing (bit-exact for linear factories).

## 4. Architecture

### Engine
- `NodeContent` struct + `NodeInstance.content`, serialized by `PatchSerializer`
  (top-level patch version bumps once; each schema then versions itself).
- `Node::setContent()` and `Node::getContentSchema()`; GraphCompiler passes content
  like parameters, and a content change gets a fresh node (structural by
  definition — no live-apply path).
- **Live editing:** while an editor drags a point, the editor streams content to a
  `LiveContentEdits` channel (the content-sized sibling of `LiveParameterEdits.h`):
  the node rebuilds its buffer on a worker thread and publishes it, and the consumer
  crossfades — the "never clicks" rule from §3. Commit on release, exactly as
  sliders do today.

### UI
- **Where an editor opens.** Inline is too small for real editing; a modal hides the
  patch. *Recommendation:* a **docked editor panel** (bottom or right, resizable)
  showing the selected factory's editor, with the node itself showing a compact live
  preview (the curve, the EQ response) and an "open" affordance. One panel, one
  editor at a time, pinned or follow-selection.
- **Shared editor kit** (the only thing editors share, per §8): a pan/zoom canvas
  with snapping and selection, a point/handle model with undo, SVG/WebGL drawing
  helpers on the existing tokens, and a telemetry hook so an editor can show the
  playhead of whatever is reading its data.
- Every editor computes **nothing audible** (CLAUDE.md rule 1): it edits a document;
  the engine turns the document into `Data`.

## 5. Order

1. **Infrastructure + `factory.curve`** — the smallest real factory and the most
   reused: one curve feeds envelopes, LFO shapes, waveshaper transfer functions and
   Map's response curve.
   - Content: points (`x`, `y`, per-segment tension/shape), loop range, polarity.
   - Editor: add/move/delete points, segment-tension drag, grid snapping, presets
     (sine, ramp, steps, ADSR-like), playhead from the consumer.
   - Consumers that make it audible: a **curve-shaped oscillator** (a phase source
     reading `Data(curve)` — one node that is both a custom LFO and a custom audio
     waveform, sharing today's phase-locked preview), `env.curve`, and a `curve`
     input on `adapt.map` and `shape.waveshaper`.
   - Unwrap → `data.table` (now content-backed) + the consumer.
2. **`factory.wave`** — multi-frame single-cycle editor (draw, harmonic sliders,
   import a single cycle), feeding `osc.wavetable` (built alongside — it's also a
   phase source, so its preview is the frame being scanned). Band-limited mipmaps
   are built engine-side when the content is published.
3. **`factory.eq`** — the first *processor* factory: band list editor over a live
   spectrum (the existing Spectrum telemetry), unwrapping into `filter.peak`/
   `shelf`/`svf`. Bit-exact unwrap test.
4. **`factory.material`** — a mode-set editor on top of `data.material` (per-mode
   overrides, audition by striking), the bridge into `BakedPhysics.md`.
5. **`factory.notes`** — after the multi-note `Note` engine work (`note.chord`/
   `hold`/`select` are blocked on it today).
6. **`factory.sample`** — after the asset store (decision 1) and `sampler.player`.

## 6. Risks

- **Editors are expensive UI.** Each is a small application. Mitigation: the shared
  kit first, one factory at a time, and the docked panel so editors don't each
  invent their own window management.
- **Content-change crossfades** must be designed per `Data` tag (a curve swap, a
  wavetable swap and a mode-set swap fade differently). Settle the curve case first.
- **Unwrap fidelity** — "a factory mode that can't be expressed by primitives must
  not exist" constrains features; keep that rule rather than letting factories grow
  private DSP.

## 7. Done means

`factory.curve` placed, edited in the docked editor while audio plays (no clicks),
saved and reloaded with the patch, driving a curve-shaped oscillator/LFO, and
unwrapped into `data.table` + consumer with identical output — with tests for the
content round trip, the live-edit path, and the unwrap render comparison.
