# Native Instruments Kontrol DAW-integration protocol — notes

**Source:** [reaKontrol](https://github.com/jcsteh/reaKontrol) (James Teh, GPL v2), specifically
`src/niMidi.cpp`/`src/reaKontrol.h`/`src/main.cpp` as of its `master` branch, 2026-10. NI has never
published this protocol officially; everything below was learned by reading reaKontrol's own
implementation. This document is my own description of the **wire protocol and behaviour** — port
names, message byte layouts, numeric constants, call sequencing — written from scratch, in my own
words and with Bazalt's own naming, after reading that source. **No reaKontrol source code,
identifier names, or function structure is copied, translated, or paraphrased line-by-line below** —
constraint 1 of `wiki/plans/KontrolIntegration.md`. Anything in this document that reads as a
near-verbatim quote is a short run of raw protocol bytes (which aren't copyrightable expression —
they're the hardware's own wire format), never reaKontrol's source text itself. The actual Bazalt
implementation is written from this document alone, not from the source a second time.

Confidence note: every byte value and function-shape fact below was pulled from reaKontrol's source
directly (several separate, targeted reads of the real file content, cross-checked against each
other — not one secondhand summary taken on faith). Still, **nothing here has been confirmed
against a real keyboard yet** — reaKontrol targets REAPER's control-surface model, which has
concepts (tracks, FX chains, mixer banks) Bazalt doesn't share, so a few interpretive calls were
made translating "what REAPER did with this byte" into "what Bazalt should do with it." Those are
flagged inline as **(interpretation)**. The manual hardware checklist at the end of the integration
plan is what actually confirms or corrects them.

## 1. Hardware and ports

An NI Kontrol keyboard (S-series Mk1/Mk2/Mk3, A-series, M-series) exposes **two separate MIDI
ports** once NI's own background service (Native Access / the NIHIA host integration agent) is
installed and running: an ordinary MIDI port (keyboard notes, standard CC) and a second **DAW
port**, used exclusively for this custom host-integration protocol. The DAW port does nothing
useful without that background service — the keyboard firmware itself doesn't speak this protocol
directly over USB; NI's service is the thing that creates the virtual DAW MIDI port and relays to
the hardware. **Open question for the manual checklist:** confirm the NI background service
actually needs to be running for the DAW port to even appear in Windows' MIDI device list on this
machine — reaKontrol's own docs don't say explicitly, and I found no code that detects or warns
about its absence, only "the port isn't there."

reaKontrol finds the DAW port by **exact suffix match** against the enumerated MIDI port name (not
a substring search, not a prefix). The suffixes it recognises:
- `"Komplete Kontrol DAW - 1"`
- `"Komplete Kontrol A DAW"`
- `"Komplete Kontrol M DAW"`
- `"2 (KONTROL S49 MK3)"`
- `"2 (KONTROL S61 MK3)"`
- `"2 (KONTROL S88 MK3)"`
- `"MK3 - DAW"`

The S-MK3 models are matched two different ways in that list (the explicit `"2 (KONTROL S49/61/88
MK3)"` strings, and the generic `"MK3 - DAW"` suffix) — **(interpretation)** this looks like it's
covering two different naming schemes Windows has shown for the same port across driver/OS
versions, not two different ports. Bazalt's own detection should check both the regular and DAW
port lists and pick by suffix the same way, but match liberally (e.g. "ends with `MK3 - DAW`" OR
"contains `MK3`" AND "contains `DAW`") rather than hardcoding reaKontrol's exact string list, since
we can't be sure it's exhaustive for every MK3 keyboard size/locale — **first real action item for
the manual checklist: enumerate the actual port names this machine reports with the S-MK3
connected, and record them.**

An NI Kontrol **Mk1** keyboard is detected separately (reaKontrol checks a USB identifier, not a
port-name suffix) and falls back to a completely different protocol (Mackie Control Universal) —
not this one. Out of scope here entirely: the user's hardware is S-MK3, and Mk1 support is noted
in reaKontrol's own docs as "incomplete and nonfunctional" even there.

## 2. Transport framing

Two message shapes, both sent/received on the DAW port only:

- **Control Change (3 bytes):** status byte `0xBF` (Control Change, MIDI channel 16 —
  `0xB0 | 0x0F`), then a one-byte **command** number, then a one-byte **value**. This is used for
  the handshake and for low-resolution (7-bit) relative knob/button messages on Mk2/A/M-series.
- **SysEx (variable length):** `F0 00 21 09 00 00 44 43 01 00` (10 bytes — NI's manufacturer ID
  `00 21 09`, then 7 more bytes identifying this as the Komplete Kontrol host-integration stream),
  then one **command** byte, one **value** byte, one **index** byte, then zero or more **payload**
  bytes (ASCII text, or 2 packed bytes for an MK3 high-resolution knob delta — see §4), then `F7`.
  No length byte and no checksum — the payload length is just however many bytes sit between the
  fixed 13-byte prefix and the `F7` terminator.

These two message kinds share one numbering space of "command" bytes in appearance only — a CC
command byte and a SysEx command byte are read from different message types, so the same number
(e.g. `0x70`) means something completely different depending on whether it arrived as a 3-byte CC
or inside a SysEx frame (reaKontrol's own code reuses `0x70`-`0x77` for two unrelated purposes this
way — low-res param-knob CCs on old protocol versions, and unrelated SysEx display commands on
every version. A clean-room implementation should keep these in two separate enums from the start
to avoid that trap, not mirror it.)

## 3. Handshake

1. On opening the DAW port, the host immediately sends CC `command=0x01` ("hello"), `value=0x04`
   — the `0x04` is a fixed payload the host always sends, **(interpretation)** most plausibly "the
   highest protocol version this host implementation understands," though reaKontrol never branches
   on what it sent, only on what it receives back.
2. The device replies with its own CC `command=0x01` ("hello"), whose `value` byte is the
   **protocol version** the connected keyboard actually speaks. reaKontrol branches its whole
   feature set on this single byte: `< 4` means Mk2/A/M-series behaviour (7-bit CC knobs, plain-CC
   param knobs); `>= 4` means Mk3 behaviour (14-bit SysEx knob deltas). This is the real model
   detection — not the port name, which is only used to find the right MIDI ports in the first
   place.
3. The host then sends a short fixed sequence of CC/SysEx setup messages (enable quantize display,
   enable tempo display, declare clip-launch button availability, declare the surface's own
   "track-oriented" layout) that are **entirely about REAPER's track/clip/transport concepts** and
   have no Bazalt equivalent — not reused here at all (see §7).
4. If the reported version is `>= 4`, the host sends one more CC telling the device to switch its
   parameter knobs into "send high-resolution SysEx instead of plain CC" mode (command `0x06`,
   value `1`).
5. No further handshake confirmation is expected; the device is now live.

No periodic keep-alive/heartbeat exists in reaKontrol at all — the connection is assumed to stay
open as long as the MIDI port itself stays open. (This is worth treating with a little suspicion
for a from-scratch implementation — **open question for the manual checklist:** leave the
connection idle for a few minutes and confirm it really doesn't need anything to stay alive.)

## 4. Knob input (8 encoders)

Both encodings are **relative** (a delta per physical detent/tick), never absolute position — the
keyboard has no idea what value the host is currently showing, so there's no "jump to this value"
message at all; this is also exactly why there's no "value doesn't match, snap" problem to solve
on connect (§7 of the integration plan can rely on this instead of implementing acceleration/
pickup logic).

**Protocol version < 4 (Mk2 / A-series / M-series):** one CC command per knob, numbered
sequentially (8 consecutive command bytes, one per knob index 0–7). The `value` byte is a signed
7-bit delta packed into an unsigned byte the ordinary two's-complement way: byte values `0–63` mean
`+0…+63`, byte values `64–127` mean `-64…-1` (i.e. interpret the byte as `int8_t` directly — a byte
`>= 64` is `byte - 128`). A full physical turn is usually several of these messages in quick
succession, not one big jump.

**Protocol version >= 4 (Mk3 — the user's hardware):** one SysEx message per knob tick, all 8
knobs sharing **one** SysEx command byte rather than 8 separate ones. The SysEx `value` byte
carries a **group** (REAPER-specific: volume/pan/plugin-param — **(interpretation)** Bazalt has no
such grouping and should always use whichever group value means "plugin/generic parameter," the
one reaKontrol itself uses for arbitrary host-automatable parameters), the `index` byte is the
knob number 0–7, and the two payload bytes are a 14-bit relative delta split into two 7-bit MIDI
bytes (`lsb` then `msb`, matching ordinary MIDI 14-bit CC/pitch-bend byte ordering). Reconstructed
as `raw = lsb + (msb << 7)` (range `0…16383`), then re-centred the same two's-complement way as
the 7-bit case but at the 14-bit midpoint: `raw > 8192` means `raw - 16384` (negative), otherwise
`raw` as-is (positive or zero) — giving a signed range of roughly `-8192…+8191`. Normalising that
to a usable per-tick step means dividing by the magnitude of one extreme (8191 or 8192) to land
near `±1.0` per "full-speed" tick; the exact divisor only affects how fast turning the knob feels,
not correctness, so Bazalt's own value is free to tune this rather than copy reaKontrol's exact
constant.

Both encodings are turn-rate-sensitive (faster turning naturally produces either more messages per
second or occasionally a larger single delta, depending on the device's own internal acceleration
curve) — not something the host needs to compute itself.

## 5. Knob/parameter display (name + value text)

Per claimed knob slot (0–7), **two separate SysEx messages**, both addressed by the same `index`
byte (the slot number):
- A **name** message: SysEx command for "parameter name," `value` byte carrying a **display-hint**
  enum (0 = plain/unipolar, 1 = bipolar, 2 = on/off switch, 3 = stepped/discrete — purely a
  rendering hint for the device's own screen, e.g. how it draws the value arc), `index` = slot
  number, payload = the name as ASCII text, as many bytes as the string is long (reaKontrol does
  not truncate before sending — if the real display has a character limit, the device itself
  appears to clip it, not the host).
- A **value-text** message: a different SysEx command for "parameter value text," `value` byte
  unused (0), `index` = slot number, payload = the value already formatted as display text (e.g.
  `"3.4 dB"`, `"72%"`) — the HOST decides the string, not the device; the device just shows
  whatever bytes it's given.

These are sent once per slot whenever that slot's assignment or displayed value changes — not
continuously, and not batched into one message for all 8 slots; each slot gets its own pair of
messages. This maps directly onto Bazalt's own "bidirectional, updates immediately on rename/
recompile/mouse-drag" requirement: whenever a claimed macro's title or current value changes
(including a different macro claiming the slot), re-send that slot's name+value pair; nothing else
needs to change.

A completely empty slot (no macro claims it) has no reaKontrol equivalent at all — REAPER always
has *a* parameter in each bank position, even if silent. **(interpretation, per the integration
plan's own spec)**: send an empty name string for an unclaimed Bazalt slot, which should blank the
display the same way the device blanks an empty string in REAPER's own usage.

## 6. Paging / bank select

Not a dedicated "page" message — REAPER's own "bank of 8" concept is signalled with the same
general-purpose CC used for bank-style navigation elsewhere in the protocol (one CC command,
signed 7-bit value meaning "step one bank forward" or "one bank back," arriving from the device
when its own page buttons are pressed). There is no "jump directly to page N" message — only
relative step forward/back, so a host that wants absolute paging (Bazalt's "32 slots = 4 fixed
pages") has to track the current page index itself and clamp it at the ends (0–3), not rely on the
device to report which page it's on.

## 7. What reaKontrol does that Bazalt has no equivalent for (deliberately not reused)

All of the following are real reaKontrol features with no Bazalt analogue and are **not** part of
phase 1 (or any phase) of this integration: track selection/arm/mute/solo CCs, transport-adjacent
clip launch grid, the "track orientation" surface-config declaration, MCU-protocol fallback for
Mk1, tempo push (`CMD_SET_TEMPO`), FX plugin browsing/selection SysEx, preset name/navigation, and
the JSON-serialised plugin-tree message used for the 4-D encoder browser. Several of these
(transport, 4-D browsing) are explicitly deferred to a later phase by
`wiki/plans/KontrolIntegration.md`; the rest (track/mixer/clip concepts) simply don't map onto
anything Bazalt has and are not planned at all.

## 8. Disconnect

A single CC "goodbye" command (value `0`) sent once, right before closing the port. No response is
expected or waited for.
