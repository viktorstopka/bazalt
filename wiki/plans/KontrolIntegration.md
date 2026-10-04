# Kontrol keyboard integration — phase 1 (macros)

**Status:** Proposed, 2026-10-06. Written before any implementation code, per direct instruction —
stop here for review before Batch 1 starts. Protocol knowledge this plan depends on lives in
`wiki/notes/KontrolProtocol.md` (read that first); the "app-level service, not a graph node"
decision has its own formal record at `archive_docs/decisions/0033-kontrol-integration-is-an-
app-level-service.md`.

## 0. Origin

Direct instruction: make an NI Kontrol S-series MK3 keyboard's 8 knobs + display work against
Bazalt's existing macro pool the way Ableton's own NI integration does, for the standalone app.
Driven by the same 32-slot `MacroParameters` pool host automation already uses
(`wiki/plans/UtilMacro.md`) — no new parallel parameter system. Protocol is unofficial; learned by
reading the GPL-licensed reaKontrol REAPER extension (clean-room: `KontrolProtocol.md` is the only
thing the implementation is written from, not reaKontrol's source a second time).

## 1. What already exists that this plugs into

- `MacroParameters` (`plugin/source/MacroParameters.h/.cpp`): the 32 `juce::AudioParameterFloat`s,
  each independently smoothed (~20ms), applied to every active plan every block. A macro's **raw
  stored value is always 0..1** (`MacroNode.h`'s own reasoning) — real-unit min/max/quantity only
  exist on the `util.macro` node's own structural parameters in the live graph, never in
  `MacroParameters` itself.
- `MacroMapping` (`PatchDocument.h`): slot index + target node/param + a hardcoded 0..1 range —
  deliberately thin, carries nothing about a slot's title or real units. `GraphEditController::
  deriveMacroMappings()` rebuilds the list of these from the graph's own `util.macro` nodes on
  every successful compile, and `recompileAndPublish()` calls `processor.setMacroMappings(...)`
  from its two exit points. This is the existing "a macro was added/removed/re-slotted" signal —
  reused as-is, not duplicated.
- A macro's **title** (`properties["title"]`) and **real range/quantity/isInteger**
  (`util.macro.min`/`max`/`quantity`/`isInteger` structural parameters) currently have exactly one
  reader: the UI, via `graphStore.ts` walking the live graph mirror (`App.tsx`'s macro panel,
  `MacroKnob.tsx`). Nothing on the C++ side derives this richer "what should the display say for
  slot N" information today — **this is new**, not a reuse of an existing mechanism.
- `OutputLimiter`: the precedent for "a plain `BazaltAudioProcessor` member, unconditional, not a
  node" (ADR-0033 generalises this).
- `StandaloneApp.cpp`/`juce::StandalonePluginHolder` own the *generic* MIDI note-input device list
  (the audio/MIDI settings dialog) via `juce::AudioDeviceManager`. The Kontrol DAW port is a
  **separate, dedicated** MIDI I/O pair this feature opens and owns itself — never routed through
  `AudioDeviceManager`, never shown in that generic settings dialog, and never feeding `io.noteIn`.

## 2. Design

### 2.1 Ownership and lifecycle

New class `KontrolSurface` (`plugin/source/KontrolSurface.h/.cpp`), owned as a plain member of
`BazaltAudioProcessor` (declared next to `macroParameters`/`outputLimiter`, per ADR-0033).
Constructed once at processor construction; `prepare()`/`shutdown()`-shaped like the other
app-level members, called from the processor's own `prepareToPlay`/destructor.

A `juce::Timer` (message thread, ~2s interval) owns the whole device lifecycle:
- If not connected: enumerate `juce::MidiInput::getAvailableDevices()`/
  `juce::MidiOutput::getAvailableDevices()`, match by the suffix rules in
  `KontrolProtocol.md` §1, attempt to open both. `juce::MidiInput::openDevice`/
  `MidiOutput::openDevice` return `nullptr` on failure (no exception) — a null result just means
  "still not available," logged once (not every 2s — track a `wasConnected` bool so the log/status
  only reports the EDGE, not a connection-attempt spam) and left for the next timer tick. This is
  also what covers "only one app can own the DAW port" (Ableton running first): the open call
  itself fails exactly the same way a missing device does, no special-case needed.
- If connected: do nothing (no polling needed while live — see §2.5 for how disconnect is
  detected).
- On a successful open: send the handshake (`KontrolProtocol.md` §3), await the version reply
  (handled in the MIDI input callback, §2.3), and only flip to "connected" state once that reply
  arrives — a port that opens but never replies (unplugged mid-handshake, a non-Kontrol device
  that happens to match a port name) times out back to "not found" after a few seconds rather than
  reporting falsely connected.

### 2.2 Threading — the real-time-safety boundary (constraint 2)

```
MIDI thread (JUCE's own callback thread for this device)
    handleIncomingMidiMessage()
        — parse only: is this our handshake reply, a knob delta, or a page button?
        — push a small fixed-size struct (KnobEvent{slotOnPage, delta} or PageEvent{direction})
          into a lock-free SPSC FIFO (juce::AbstractFifo over a preallocated ring buffer,
          same kind of mechanism Tap.h already uses for the audio→analysis handoff, just
          message-thread-facing instead of audio-thread-facing)
        — no allocation, no logging, no locking here either, even though this thread isn't the
          hard audio-thread deadline — no reason to risk it

Message thread (juce::Timer or AsyncUpdater, ~20-30Hz)
    drains the FIFO
        — for a KnobEvent: resolve slotOnPage + currentPage → absolute macro slot (0-31);
          if that slot is claimed, read the macro's current 0..1 value
          (MacroParameters::getParameter(slot).get()), add the scaled delta, clamp to [0,1],
          call setValueNotifyingHost() — the ordinary, safe, message-thread way to change a
          JUCE parameter's value; this IS "the same smoothed path host automation already
          uses": the audio thread's existing advanceSmoothers()/applyToPlans() picks up the
          new get() value on its next block and smooths toward it exactly like a host
          automation move would, with no new audio-thread code at all
        — for a PageEvent: currentPage = clamp(currentPage + direction, 0, 3); refresh all 8
          display slots for the new page (§2.4)

Audio thread
    — touches NONE of this. No MIDI I/O, no FIFO access, no new code here at all. The only
      "new" audio-thread-visible effect is that a macro's AudioParameterFloat::get() can now
      also change because of a knob turn, which is indistinguishable from any other host
      automation move as far as advanceSmoothers()/applyToPlans() are concerned.
```

### 2.3 Incoming message handling

`KontrolSurface` implements `juce::MidiInputCallback`. `handleIncomingMidiMessage` branches on the
message shape (`KontrolProtocol.md` §2):
- Before handshake confirmed: only the hello-reply CC is meaningful (anything else is ignored —
  no device sends knob events before replying to hello).
- After handshake: dispatch 7-bit CC knob messages (protocol < 4, not expected on MK3 but handled
  for completeness/future A-/M-series support) or 14-bit SysEx knob messages (protocol >= 4, the
  MK3 path) into `KnobEvent`s; the page-navigation CC into `PageEvent`s.

### 2.4 Outgoing display updates (constraint 2's "rate-limited")

A `dirtySlots` bitset (8 bits, this page only) marks which of the 8 currently-visible knob
positions need a fresh name+value SysEx pair sent. Marked dirty by:
- A page change (all 8 at once — see §2.2).
- `recompileAndPublish()`'s own existing hook (§2.6) — a macro was added/removed/renamed/
  re-slotted/had its range edited; only the affected slot(s), not all 8.
- A macro's live value changing enough to matter: NOT recomputed every block. The same ~20-30Hz
  message-thread timer that drains the knob FIFO also compares each of the 8 visible slots'
  current value against what was last SENT to the display, and marks it dirty only past a
  readable-text-changed threshold (e.g. the formatted display STRING actually differs — comparing
  formatted strings, not raw floats, is what naturally rate-limits this to "only when a human
  would see a different number," with no separate magic epsilon to tune).

Each dirty slot sends its two SysEx messages (name, value-text — `KontrolProtocol.md` §5) once per
timer tick, never per-audio-block, satisfying "don't flood the device."

**Value text formatting (resolving the phase-1 spec's own "if the protocol allows"):** confirmed
in `KontrolProtocol.md` §5 that value-text is host-formatted freeform ASCII — the protocol places
no constraint on this at all. Format in the macro's own real quantity/unit (reusing
`ui/src/format/valueFormat.ts`'s own `formatTrimmed`-equivalent rounding rule, ported to C++ — a
small, self-contained formatting helper, not a dependency on the UI code) when the macro declares
a real `quantity`; plain 0-100% for an unquantified/dimensionless macro. An unclaimed slot sends an
empty name string (device blanks the display — `KontrolProtocol.md` §5's own interpretation note).

### 2.5 Disconnect detection

No protocol-level "are you still there" exists (`KontrolProtocol.md` §3). Detected instead by a
**MIDI device-list change** — `juce::MidiInput`/`MidiOutput` report callback failure or the
device simply stops appearing in `getAvailableDevices()`; the same ~2s timer (§2.1), once
connected, also confirms the open device is still present in that list, and if not, tears down
(closes both ports, resets handshake/page state) and falls back to "searching" — which naturally
becomes a reconnect once the device reappears, no special-cased "reconnect" code path distinct
from "initial connect."

### 2.6 Bidirectional refresh hook

One new call, `processor.notifyKontrolMacrosChanged(graph)`, added at the exact same two call
sites in `GraphEditController::recompileAndPublish()` that already call
`processor.setMacroMappings(deriveMacroMappings(graph))` — reads every `util.macro` node's own
slot/title/min/max/quantity/isInteger directly from the live `NodeGraph` (the same data
`MacroKnob.tsx` reads client-side, just read here instead), diffs against what `KontrolSurface`
last knew per slot, and marks the changed slots dirty (§2.4). This is what makes renaming,
re-slotting, deleting, and adding a macro all "just work" through the one hook — no separate
notification path for each.

A **mouse-dragged** macro value (not a recompile — `MacroKnob.tsx`'s own live
`setNormalisedValue()` calls during a drag, never going through the command bridge at all) is
picked up the same way knob-turn-driven changes are: the §2.4 timer polling
`getParameter(slot).get()` against last-sent text doesn't care WHY the value changed, only THAT it
did — one mechanism covers both directions without the audio/message-thread boundary needing to
know which side originated a change.

### 2.7 UI status surface (constraint 4's "non-blocking status in the UI")

No existing "app-level status" channel exists to extend (checked — the UI's only status concepts
today are per-command `lastError` and per-node `error`, both graph-editing concerns). New, minimal:
one native function `getKontrolStatus()` (shape: `{ connected: bool, deviceName: string | null,
lastError: string | null }`), polled every few seconds by a small new `KontrolStatus.tsx` — a
single small icon/label in the top bar (near `PatchMenu.tsx`, same row), never a blocking dialog.
Exact visual treatment (icon vs. text, tooltip contents) left to the implementation step, not
decided here.

### 2.8 Page/slot mapping

32 macro slots = 4 pages of 8, `currentPage` is `KontrolSurface`'s own int state (not persisted —
always starts at page 0 on connect). Knob `index` (0-7, from the protocol) + `currentPage * 8` =
absolute macro slot. Page CC direction (`KontrolProtocol.md` §6) increments/decrements, clamped
`[0, 3]` — the device has no concept of "page 5 doesn't exist," so clamping here is required, not
optional.

### 2.9 Knob resolution (constraint "no jumps")

Resolved by the protocol's own shape, not extra logic: both knob encodings
(`KontrolProtocol.md` §4) are **relative deltas**, never an absolute position the hardware thinks
is current. There is no "hardware value disagrees with software value" state to reconcile at
all — a delta is added to whatever the current value already is, full stop. No soft-takeover/
pickup logic is needed for phase 1; noted here so it isn't accidentally built as unneeded
complexity, and flagged as something to re-examine ONLY if a future phase adds an absolute-
position control (none currently planned).

## 3. File list (implementation step — not built yet)

- `plugin/source/KontrolSurface.h/.cpp` — the class itself (§2.1-2.6).
- `plugin/source/KontrolDisplayFormat.h/.cpp` — small, pure value→display-string helper (§2.4),
  engine-independent, easily unit-tested alone.
- `plugin/source/PluginProcessor.h/.cpp` — new `KontrolSurface kontrolSurface;` member,
  `notifyKontrolMacrosChanged()` passthrough, `getKontrolStatus()` native-function plumbing
  (mirrors how `getNodeDescriptors` is wired — `PluginEditor.cpp`'s `withGraphCommands`-style
  native function registration, read-only, not a graph command).
- `plugin/source/GraphEditController.cpp` — the two new call sites (§2.6).
- `ui/src/KontrolStatus.tsx/.css` — the status indicator (§2.7).
- `ui/src/graph/graphCommands.ts` — `getKontrolStatus()` binding.
- `wiki/notes/KontrolProtocol.md` — already written (this plan depends on it).
- `archive_docs/decisions/0033-...md` — already written.

## 4. Test plan

- **Unit (encode/decode), no real MIDI device:** byte sequences straight from
  `KontrolProtocol.md` — handshake hello/reply round-trip, 7-bit CC delta decode (`KontrolProtocol.
  md` §4's two's-complement table, both boundary values 63/64), 14-bit SysEx delta decode (0,
  8192, 8193, 16383 boundary values), SysEx frame assembly (header bytes, command/value/index
  placement, terminator) for an outgoing name/value message, page CC clamp at both ends (0 and 3).
- **Mock-port test:** a fake `juce::MidiInput`/`MidiOutput` pair (or a thin seam `KontrolSurface`
  reads/writes through, swappable in tests) driving: full handshake → connected; a sequence of
  knob deltas → correct macro slot's value changes; a page-button message → the other 8 slots'
  worth of display messages get sent; a simulated disconnect (device vanishes from
  `getAvailableDevices()`) → reconnect once it reappears; two ports with matching names but a
  failed `openDevice()` → status reports "not found/busy," nothing else breaks.
- **Bidirectional:** wire a `util.macro` node, claim a slot, rename it / change its min/max via
  the ordinary command bridge, confirm `notifyKontrolMacrosChanged` produces the expected dirty-
  slot diff without a real device attached (assert on what WOULD be sent, via the same mock-port
  seam).
- **Real-time-safety:** a test exercising `KontrolSurface`'s knob-FIFO push/drain under
  `bazalt::engine::ScopedAudioThreadAllocationTrap` (CLAUDE.md's own enforcement mechanism) proving
  nothing on that path allocates — note this specifically guards the FIFO push/drain, not a literal
  audio-thread call (the audio thread never touches `KontrolSurface` at all, per §2.2); the trap is
  used here as a convenient allocation-detector, not because this code runs on the audio thread.

## 5. Manual hardware checklist (for the user, once implemented)

1. Enumerate MIDI ports with the S-MK3 connected and NI's background service running — record the
   exact names seen (`KontrolProtocol.md` §1's own open question).
2. Launch Bazalt standalone with the keyboard already connected → status shows connected, device
   name shown.
3. Place a few `util.macro` nodes, claim slots 0, 1, 5, 9 (spanning two pages) → their titles
   appear on the matching knob positions (slot 9 only visible after paging).
4. Turn a knob → the matching macro's value moves in the UI, smoothly (not stepped).
5. Move a macro's knob in the UI with the mouse → the hardware display's value text follows.
6. Rename a macro → display updates. Delete a macro → its slot blanks. Re-slot a macro to a
   different number → old position blanks, new position shows it.
7. Press the page buttons → slots 9-16 (etc.) become visible/controllable; paging past page 4 (or
   before page 1) does nothing rather than erroring.
8. Unplug the keyboard, confirm status goes to "not found," replug → reconnects and re-syncs all
   visible slots without restarting Bazalt.
9. Launch Ableton (or any other app with its own NI integration) first, holding the DAW port, THEN
   launch Bazalt → status reports the port busy, Bazalt otherwise works completely normally; quit
   Ableton → Bazalt connects without being restarted.
10. Leave it connected and idle for several minutes → confirm no heartbeat is actually needed
    (`KontrolProtocol.md` §3's own open question).

## 6. Explicitly out of scope for phase 1 (follow-ups)

- Transport buttons → `io.transport`'s own `HostInputs` boundary (ADR-0028) — natural next
  integration point, not built here.
- 4-D encoder patch browsing, LEDs, light guide, mixer/track concepts, NKS — all have no Bazalt
  concept to attach to yet (`KontrolProtocol.md` §7); not partially stubbed, not speculatively
  designed here.
- Soft-takeover/pickup for an absolute-position control — no such control exists yet (§2.9); build
  it only if/when one does.
- macOS/Linux — CLAUDE.md rule 7, same as everywhere else in this project right now.

## 7. Docs to update once this lands

- `wiki/NODES.System.md` / status docs: only if phase 1 changes anything they currently describe —
  it doesn't (ADR-0033's whole point is that this never touches the node system). No edit expected
  here; re-check this line once implementation is actually done in case that assumption turns out
  wrong.
- This file's own `Status:` line, and ADR-0033's, flip to done/accepted once Batch work lands,
  mirroring `wiki/plans/UtilMacro.md`'s own "write the record at completion" convention.
