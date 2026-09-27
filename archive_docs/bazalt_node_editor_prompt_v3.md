# Bazalt — Node Editor Phase: Planning Prompt

The MVP milestones M0–M6 in `MILESTONES.md` build the engine, graph runtime, I/O, patch format, telemetry pipeline, the empty canvas, and the analysis panel. **This phase starts after M6.** It covers the node editor: the visual language, nodes, cables, controls, interactions, and real-time visualization inside the graph.

Your job in this step is **planning, not implementation.** Read this document, then study the existing codebase and docs, and produce:

1. `docs/NODE_EDITOR.md`: the node editor design grounded in what actually exists in the code (graph runtime, patch format, telemetry, UI stack, tokens).
2. New milestones **M7 onwards** appended to `MILESTONES.md`, in the same style (scope, then concrete exit criteria).
3. A list of **conflicts and open questions**: places where this document contradicts the codebase, the architecture docs, or itself, with a recommended resolution for each.

Stop after that and wait for my approval.

## Inputs to read

- `CLAUDE.md`, `docs/ARCHITECTURE.md`, `docs/decisions/`, `MILESTONES.md`, and the codebase itself. **The code is the source of truth** where it differs from the docs.
- `docs/design/Frame_1.png`: the visual design reference. Blue labelled boxes are designer annotations, not UI. Thin lines from a node to a side panel ("Audio In → Help", "Macro 1 → Constraints") mean "appears on click", they are not cables.
- This document. Section 6 (interactions) comes from a previous prototype of this project that was built as a web app on Elementary Audio. Its behaviours are wanted, but anything specific to the web build (e.g. the "Elementary Audio" label) must be **adapted to the native JUCE architecture**, not copied.

---

## 1. Scope

**In scope:**
- The complete node editor UI and interaction model.
- Integration with the existing C++ graph runtime through a command API.
- **Generic** graph-editing support in the engine where the UI requires it: bypass, temporary Listen taps, cable splicing, auto-inserted conversion nodes, and a small set of **utility nodes** (Constant, Map, Mix, Add, Multiply, Reroute, Listen, Output).
- Real-time visualization inside the graph (see §3), using the M4 telemetry pipeline.

**Out of scope:**
- DSP and architecture of specific instrument/effect nodes (oscillators, random, filters beyond what M1–M2 already provide…). Where the UI needs such nodes to demonstrate or test something, use **UI-only mock descriptors** clearly marked as mocks.
- Final visual encoding of polyphonic vs. monophonic signals (keep the placeholder, see §4).
- Node groups (encapsulated subgraphs), level-of-detail rendering when zoomed far out, colour-blind accessibility.
- Real recipe content for the Assist menu (mechanism only).

Out of scope does not mean ignore: the plan must leave clean extension points for all of these.

## 2. Architecture requirements (think ahead)

- **Data-driven nodes.** The UI never hardcodes a node type. Every node renders from a **node descriptor**: stable type ID, title, category, layout variant (standard, horizontal, singleton, decoration), ports, parameters, controls, preview kinds. Engine nodes provide descriptors from the C++ node registry; mock nodes provide UI-only descriptors in the same schema.
- **Port metadata:** type, direction, label, **primary output** flag, and for numeric values **unit, optional min/max, default, integer flag, and scaling (linear/log)**. This metadata drives slider behaviour, automatic Map node seeding, and value formatting.
- **The engine owns the graph.** The UI edits it only through **commands** sent over the bridge (add node, connect, splice, move, set value, bypass, unwrap…). Positions, frames, headers, images, and view state live in the patch too. Check how M2/M3 already model this and extend it rather than creating a parallel model.
- **Undo/redo** built on commands. Composite operations (splice insert, Unwrap, recipe insertion, auto-inserted Map, Alt-drag Mix/Add/Multiply) are one undo step. Continuous gestures (dragging a slider, moving a node) coalesce into one step.
- **Optimistic UI:** interactions feel instant; the UI reconciles with the engine's confirmed graph state and handles rejected commands gracefully (error banner, rollback).
- **Serialisation** through the existing patch format with its schema versioning and migrations: stable string IDs, node positions, frames, headers, embedded images (with position, size, rotation), view state.
- **Theming:** every colour, font, stroke, radius, and spacing comes from design tokens, including everything drawn in Canvas/WebGL.

## 3. Real-time visualization (core product value)

Visualization quality is a top priority. Everything in the graph that represents sound or modulation must be **perfectly reactive, smooth, and truthful** to what the engine is actually doing, at the level of Kilohearts Phase Plant or better.

Visual elements driven by live engine telemetry:
- **Cable signal flow:** cables show animated activity driven by real per-connection signal telemetry (level/activity). Idle cables dim.
- **Inline node previews:** e.g. waveform, stepped values, envelope shape with a playhead, LFO phase.
- **Output value indicators** on horizontal nodes, level indicators where useful.
- **Live values on controls:** a parameter driven by a cable shows its actual current value from the engine.

Requirements for the plan:
- Extend the M4 telemetry pipeline for **many small, dynamic taps**: per-node previews, per-connection activity, per-parameter current values. Taps are **subscribed on demand**, driven by what is visible in the viewport, and unsubscribed when off-screen, so cost scales with what the user sees, not with patch size.
- Define a per-frame telemetry budget and degrade gracefully (lower preview rate, coarser decimation) rather than dropping frames.
- The telemetry-to-render path must bypass the UI framework's render cycle entirely. Interpolate between telemetry frames for smooth motion at the display refresh rate.
- **Performance budget:** a 500-node / 1,000-cable patch pans and zooms at the display refresh rate (target 120 fps) with live telemetry on all visible elements; text stays crisp at every zoom level. Include a stress-test patch generator and an FPS/telemetry overlay.
- Propose the rendering split (e.g. WebGL/Canvas for grid, cables, and previews; DOM for node bodies and text inputs) with a benchmark, and record it as an ADR. Build on the M5 canvas rather than replacing it unless the benchmark justifies it.

## 4. Visual language (from the design reference)

Treat these as starting values; expose everything as tokens and match `Frame_1.png` closely.

**Canvas:** near-black warm background, monospace typography (font as a token), thin 1 px strokes.

**Port types, colours, and glyphs:**

| Type | Colour | Glyph | Meaning |
|---|---|---|---|
| Audio | pink/magenta | → | audio signal |
| Modulation | orange | → | normalised value 0–1 |
| Value | white | → | numeric value in real units (Hz, st, ms…), not normalised |
| Integer | yellow | → | whole number |
| Trigger | violet | ! | event, "do this" |
| Boolean | blue | ? | true/false |

- **Placeholder:** green marks polyphonic audio flowing into Sum Voices, as in the reference. Keep it isolated behind a token; poly/mono encoding will be redesigned later.
- Cables are coloured by source signal type and drawn as smooth bezier curves.
- Connecting Modulation → Value auto-inserts a **Map node** (min/max, seeded from the target's unit metadata). Value → Modulation inserts a normalising node.
- Errors: filled red circle with "!" beside the node title, visually distinct from the bare violet trigger glyph.

**Standard node:** rectangular outline, border **open where ports exit** (inputs left, outputs right); title top-left; circled **+** top-right opens the Assist menu (§5); title bar also holds the **Bypass** button (§6.4). Port rows show label + glyph in the type colour.

**Parameters are inputs:** a parameter row shows an inline control (§6.5) and also accepts a cable. When a cable is connected, the control is replaced by the connection and a live value readout.

**Merged pass-through port:** when input and output share type and meaning (e.g. Predelay's Audio), draw one row with the cable passing through.

**Singleton nodes:** a node that is one operation on one signal shows only its title, with the typed cable entering left and exiting right. Adjacent singletons **auto-merge into a chain** (frames touch, arrows join), as in the reference. Define the rules for snapping into a chain, pulling out, inserting between two, and what happens to connections.

**Placeholders and Unwrap:** an input can be set via dropdown to an implicit source (e.g. Random's Trigger = "On Note Legato"). The **◆ Unwrap** button converts it into real nodes with real cables, as one undo step. Unwrap is also how users inspect what a placeholder does. Implicit note sources work in both poly (per-voice trigger) and mono (once per instrument) parts of the graph. Unwrapping a plain slider value creates a **Constant node** (§6.5), so Unwrap behaves the same way everywhere.

**Horizontal node:** wide variant with inputs left, a large live preview centre, output value indicator next to the output.

**Listen node:** minimal ear icon; connecting an output to it auditions that point. In the reference its cable is dotted green; confirm whether that styling collides with animated cable flow and propose a resolution.

**Macro node:** dotted outline, title, info button, value slider. Clicking opens a **Constraints popover** anchored to the node (no cable): Type (Float, Integer, Boolean, Enum, Trigger…), range/shape, and for Enum an editable list of named values ("Squeaky", "Growly"). Changing a macro's type or enum count affects host automation; surface a warning.

## 5. Assist menu (+ button)

A contextual menu on each node with:
- **Recipes:** pick a goal and Bazalt inserts and wires a small group of nodes that achieves it. One undo step; inserted nodes briefly highlight.
- **Port options:** expose or add extra ports where supported.

Recipes are declarative data (nodes and connections relative to the source node). Implement the mechanism with 1–2 example recipes. Consider a clearer name than "Help" (e.g. "Assist").

## 6. Interactions (from the previous prototype, adapted)

### 6.1 Canvas navigation
- **Right-drag** always pans. **Space + left-drag** pans (temporary pan mode, Photoshop-style).
- **Left-drag** on empty canvas draws a **box selection**, partial mode (touching any part of a node selects it).
- **Scroll wheel** zooms, centred on the cursor. Propose sensible zoom limits.
- **Fit view:** the canvas fits all nodes when a patch loads.
- Right-click opens the Add menu while right-drag pans: resolve with a drag-distance threshold.

### 6.2 Adding nodes
- **Shift+A** opens the Add menu at the last mouse position; **right-click** on empty canvas opens it too.
- Right-clicking while a node is attached to the cursor **cancels placement** instead of opening another menu.
- Menu: auto-focused search input and category groups (derive categories from descriptors). Keyboard navigation through results; **Enter adds the selected item**. The menu auto-flips to stay on-screen.
- Choosing a node attaches a **ghost** label to the cursor with the hint "click to place · Esc to cancel".
- Hovering the ghost over a wire it can be **spliced into** highlights that wire and changes the hint to "click to insert here"; clicking removes the old wire and wires the node in on both sides. Clicking elsewhere drops it unconnected. **Escape** cancels.

### 6.3 Wiring
- Drag from port to port to connect.
- The in-progress wire is feedback-coded live: **valid**, **rejected**, and **not over a port yet** (the prototype used green, red, and gray). Green collides with the placeholder poly-audio colour: propose feedback that stays unambiguous with the type palette (colour, line style, glow, cursor icon), all tokenised. The same applies to the splice-target highlight.
- A connection that is valid but needs a conversion node (e.g. auto Map) counts as valid; decide whether it needs its own subtle indication.
- Dragging an existing wire's end off its port and releasing on empty space **deletes** the wire; releasing on a new valid port **reconnects** it.
- The **hit area for grabbing a wire is wider** than the drawn line. Define hover, selected, and dragging styles.
- Wire activity animation is telemetry-driven (§3).

### 6.4 Node interactions
- **Click** selects; **box-select** for multiple.
- **Drag the body** to move.
- **Delete/Backspace** on the selection, or right-click → Delete.
- **Double-click the title** renames inline (Enter commits, Escape cancels).
- **Right-click context menu:** Rename, Toggle Bypass, Delete.
- **Bypass** button (crossed circle) in the title bar toggles the node in and out of the signal path; the bypassed state must be clearly visible.
- **Ctrl/Cmd+click** a node with an audio output: **solo-listen** to that point by inserting a temporary **Listen node**. Clicking again removes it. Ctrl/Cmd+click on the Output node clears every active Listen and restores normal playback. Temporary and manually placed Listen nodes use the same engine mechanism; make the active listening state obvious.
- **Alt+drag** from one node to another: live line to the cursor with valid-target feedback. On release:
  - both primary outputs are **audio** → spawn a **Mix node** combining both signals;
  - both primary outputs are **numbers** → show a small dialog to choose **Add** or **Multiply**, then spawn that node.
  - Define the behaviour for mixed or incompatible types.

### 6.5 Parameter controls
Sliders follow **Blender material node** behaviour:
- A parameter that is **not connected** shows an inline slider with an editable value. It can be **Unwrapped** into a **Constant node** wired to the input.
- A **background fill shows the ratio within bounds**, only when the parameter defines both min and max. Unbounded parameters show no fill.
- **Integer parameters** only allow integer values while dragging and typing.
- Dragging changes the value both **horizontally and vertically** (vertical mouse movement also drives the horizontal slider). **Holding Shift slows it down** for fine adjustment. Respect linear/log scaling from metadata.
- **Single-click the number** to type an exact value (Enter or blur commits, Escape cancels). **Double-click resets** to default.
- **Growable inputs** (e.g. Math, Mix in add mode): a fresh empty input port appears once the last one is wired, capped at a maximum, never fewer than 2.

### 6.6 Decorations
- **Knob (reroute):** a single dot, one input, many outputs, for fanning a signal to multiple destinations or detangling wires. Gray until connected, then takes the source signal's colour.
- **Frame:** resizable, draggable backdrop for visually grouping nodes; sits behind everything; resize handles appear when selected; double-click the label to rename. Nodes inside move with it. Tint from theme tokens, as in the design reference.
- **Header:** floating text label for annotating the graph; double-click to edit.
- **Image:** floating image, **resizable and rotatable** when selected. Added via drag-and-drop or paste. **Embedded in the patch** (compressed, e.g. WebP, with a size limit) so patches can be visual canvases worth sharing.

### 6.7 Keyboard
| Key | Action |
|---|---|
| A W S E D F T G Y H U J K | Computer-keyboard piano, one chromatic octave C4–C5 (ignored while typing in a text field) |
| Ctrl/Cmd+Z | Undo |
| Ctrl/Cmd+Shift+Z, Ctrl+Y | Redo |
| Shift+A | Open Add menu at cursor |
| Delete / Backspace | Delete selected nodes and wires |
| Escape | Cancel node placement, or close open menus |
| Space (held) | Temporary pan mode |

Plugin WebViews inside DAWs often don't receive keyboard focus reliably, and hosts capture many keys. Investigate JUCE/WebView focus handling, decide where the computer-keyboard piano is available (likely Standalone by default, optional in the plugin), and document the result.

## 7. Shell elements

- **Status bar:** active voice count and the persistent hint "Shift+A / Right-click to add". Replace the prototype's "Elementary Audio" label with something useful for the native build (e.g. engine CPU load).
- **Error banner:** compile, runtime, and rejected-command errors surface as a dismissible banner at the top.

## 8. Deliverables to plan for

1. The node editor integrated into the existing Bazalt UI, working in the VST3 and the Standalone.
2. A dev-only **component gallery** with every node variant, port type, control, and state (default, hover, selected, connected, bypassed, listening, error).
3. A **demo patch** recreating `Frame_1.png`.
4. A **stress-test patch generator** with FPS and telemetry overlay.
5. Tests: unit tests for commands, undo coalescing, serialisation round-trip (including images), chaining rules, Unwrap (placeholder and Constant), splice, auto Map insertion, Alt-drag Mix/Add/Multiply, growable ports, slider behaviour (bounds, integers, Shift fine mode, reset); visual regression screenshots of the gallery; engine tests for bypass, Listen taps, and plan swaps triggered by editing operations under load (no clicks).
6. `docs/NODE_EDITOR.md`, ADRs for rendering split, descriptor schema, telemetry subscription model, and wire-feedback colours; updated `CLAUDE.md` rules for UI work.

## 9. Planning rules

- Ground every milestone in the existing code; name the files and modules it touches or adds.
- Order milestones so each one delivers something visible and testable, and so the performance and telemetry architecture is proven **early**, before many features are built on top of it.
- Give every milestone concrete exit criteria in the style of `MILESTONES.md`, including performance numbers where relevant.
- Keep the core rules: no audio processing in JavaScript, nothing that blocks or allocates on the audio thread, every graph edit reaches the engine as a glitch-free plan swap.
- Where this document conflicts with the code or seems wrong, say so and recommend a fix rather than silently choosing.
