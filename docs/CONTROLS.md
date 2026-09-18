# Basalt — Node Editor Controls

Reference for the node editor's interaction surface — controls, keymaps, and
usage. Scoped to the UI/UX layer only (not the node/patch architecture
underneath), for use as a checklist during the revamp.

## Canvas navigation
- **Pan**: right-drag always pans. Left-drag pans instead of box-selecting
  only while **Space** is held (Photoshop-style temporary pan).
- **Box-select**: left-drag (without Space) draws a selection box; selection
  mode is "partial" — touching any part of a node selects it.
- **Zoom**: scroll wheel, clamped 0.05x-2x.
- **Fit view**: canvas fits all nodes on load.

## Adding nodes
- Opened via **Shift+A** (appears at last mouse position) or **right-click**
  on empty canvas.
- Right-clicking while a node is already "attached" to the cursor cancels
  placement instead of opening a second menu.
- Menu has a search input (auto-focused) and category groups (Voice, MIDI,
  Modulators, Audio FX, Utility, Output, Decorations), collapsed to
  one-open-at-a-time when idle, flattened to flat matches while searching.
  Enter adds the node if search narrows to exactly one match. Menu
  auto-flips position to stay on-screen.
- Picking a type doesn't drop it immediately — it attaches a ghost label to
  the cursor ("click to place - Esc to cancel"). Moving over a wire that it
  could be spliced into highlights that wire green and changes the hint to
  "click to insert here"; clicking there removes the old wire and inserts
  the node inline (auto-wired both sides). Clicking elsewhere just drops it
  unconnected at the cursor. **Escape** cancels placement.

## Wiring / connections
- Drag from a port to another port to connect. The in-progress line is
  color-coded live: **green** = valid direct wire, **amber** = valid but
  will auto-insert a bridging conversion node, **red** = rejected, **gray**
  = not over a port yet.
- Dragging an existing wire's endpoint off its port and releasing on empty
  space deletes that wire; releasing on a new valid port reconnects it.
- Edges are colored by their source signal type, and animate (moving
  dashes) while a note is actively sounding; they dim to partial opacity
  when idle.
- Hitbox for grabbing a wire is generous (wider than the drawn line) to
  make reconnecting easier.

## Node interactions
- **Select**: click. **Multi-select**: box-select.
- **Move**: drag by the body.
- **Delete**: select + **Delete/Backspace**, or right-click -> Delete.
- **Rename**: double-click the title text (inline input, Enter commits,
  Escape cancels).
- **Right-click context menu**: Rename, Toggle Bypass, Delete.
- **Bypass**: (crossed-circle) button in the title bar, toggles the node
  in/out of the signal path.
- **Fold**: triangle button in the title bar — collapses the node down to
  only its currently-connected ports (hides everything unwired);
  connections stay visible/anchored.
- **Ctrl/Cmd+click** a node with an audio output: solo-listens to that
  point in the chain (inserts a temporary Preview tap). Clicking it again
  removes the preview. Ctrl/Cmd+clicking the Output node clears every
  active preview and restores normal playback.
- **Alt+drag** from one audio-output node to another: draws a live line to
  the cursor (green when hovering a valid second audio node), and on
  release spawns a Mix node combining both signals.

## Parameter controls
- **Inline sliders** (for params that live on a port row) and **static
  sliders** (bottom section, non-connectable params): drag to change;
  linear or log-scaled depending on the param.
- **Numeric value**: single-click the number to type an exact value (Enter
  commits, Escape cancels, blur commits); **double-click resets to
  default**.
- Some params render as a plain number input instead of a slider; enum
  params render as a dropdown.
- Growable inputs (e.g. Math/Mix add-mode) automatically reveal a fresh
  empty input port once the last one gets wired, capped at a max and never
  fewer than 2.

## Modulation range control
When a param has an incoming modulation connection, its slider track shows
a colored band for the actual sub-range the signal can reach, with a live
animated marker sweeping through it in real time (traced back through at
most one Map hop to an LFO or Envelope source). Dragging either edge of the
band reshapes the range — if the connection was a direct wire, dragging
auto-promotes it to a bridging Map node first (seeded so nothing changes
until you actually move the handle).

## Decoration nodes
- **Knob**: a single dot, one input/many outputs — a routing waypoint for
  fanning a signal to multiple destinations or detangling wires. Gray
  until connected, then takes on the color of its source signal.
- **Box**: a resizable, draggable backdrop for visually grouping nodes;
  sits behind everything else. Resize handles appear when selected.
  Double-click the label to rename.
- **Header**: a floating text label for annotating sections of the graph.
  Double-click to edit.

## Step sequencer grid (Sequence / Steps nodes)
- Click-and-drag across the bars paints per-step values (bar height =
  value); drag continues across steps.
- A row of small buttons below toggles each step's gate on/off
  (mute/unmute) independently of its value.
- Sequence has a Pitch/Velocity tab switch (two lanes); Steps has a single
  lane with a configurable output range.
- Keyboard, when the grid has focus: **Left/Right** move focus between
  steps, **Up/Down** nudge the focused step's value, **Space** toggles
  that step's gate.
- A live playhead highlights the currently-sounding step during playback.

## Global keyboard shortcuts
| Key | Action |
|---|---|
| A W S E D F T G Y H U J K | Computer-keyboard piano, one chromatic octave C4-C5 (ignored while typing in a text field) |
| Ctrl/Cmd+Z | Undo |
| Ctrl/Cmd+Shift+Z or Ctrl+Y | Redo |
| Shift+A | Open Add menu at cursor |
| Delete / Backspace | Delete selected nodes/edges |
| Escape | Cancel node placement in progress, or close open menus |
| Space (held) | Temporary pan mode — left-drag pans instead of box-selecting |

## MIDI hardware input
Any connected MIDI device works alongside the computer keyboard: note
on/off (with velocity), mod wheel (CC1), 14-bit pitch bend, and
poly/channel aftertouch (unified into one aftertouch value). Fails
silently if MIDI access is unavailable/denied — the on-screen keyboard
keeps working regardless.

## Toolbar
- **Brand** label, **Editor/Play mode** toggle.
- **Preset bar**: shows current preset name + author, click opens a
  dropdown to switch between factory presets.
- **Import**: file picker loads a preset from a `.json` file.
- **Save**: opens a dialog (name, author, tag chips) and downloads the
  exported patch as `.json`.
- **Record**: toggles capturing the real-time audio output; stopping
  downloads it (webm/ogg/m4a). Hidden entirely if unsupported (native
  build, or no MediaRecorder).
- **Latency**: cycles Low/Balanced/High (web build only — reopens the
  audio device on change).
- **Main knob**: master gain. Click-drag vertically (150px = full 0-1
  sweep).

## Play mode
Minimal by design — just the active preset name, large and centered. (No
onscreen keyboard or macro UI yet, though the layout leaves room for it.)

## Status bar
Shows active voice count, an "Elementary Audio" label, and a persistent
hint: "Shift+A / Right-click to add".

## Other UX behaviors worth preserving
- **Error banner**: compile/runtime errors surface as a dismissible banner
  at the top.
- **Autosave**: web build debounce-saves the patch to localStorage so a
  refresh doesn't lose work (native build skips this — the DAW host owns
  session persistence).
- **Undo/redo**: scoped to audio-relevant patch changes, not raw per-pixel
  drag noise.
