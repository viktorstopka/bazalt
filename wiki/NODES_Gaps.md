# Node & architecture gaps — identified, most now fixed

Milestone 0.1's deliverable — **updated after Milestone 0.5**, which fixed the four
confirmed, mechanical findings below (marked **FIXED** inline) at the user's
go-ahead, before the user's own review pass happened. The review is still worth
doing — it's what confirms these were the right fixes, not a reason they were
blocked on it. The lower-confidence items (SVF naming, the `math.*`/`adapt.*`
no-fallback pattern, `instance.allocator`'s randoms) are untouched, exactly as
originally scoped, pending that review.

Process: every specific mistake you named gets
generalized into a named category, then checked against **every one of the 55 real,
registered node headers** in `engine/include/bazalt/engine/nodes/` (the full list is
at the bottom, so this scan is checkable, not just asserted) — flagged only where I'm
confident it actually applies. Several things you flagged read as intentional,
documented design once checked against the code and `wiki/NODES.System.md`'s
architecture — those are called out separately, for your confirmation, not silently
agreed with.

**Nothing here is fixed yet.** This is identification only, per your instruction —
review it, correct anything I got wrong, and fixes become their own later milestones.

---

## Part 1 — Node-level mistakes (generalized categories, checked against all 55 nodes)

### `redundant-composable-param` — FIXED (Milestone 0.5)
*A node bakes in a control that duplicates what composing a separate node already
does, instead of relying on the graph itself.*

**Confirmed:** `mix.sum` (`MixNode.h`) — its `level.N` companion on every input
duplicated `mix.gain`. **Checked against all 55 and found nowhere else** —
`mix.crossfade`'s `position` is the crossfade's own defining parameter (not a
duplicate of anything), `mix.downmix`'s `mode` is a structural algorithm choice, and
no other growable-group or multi-input node bakes in a per-input gain stage.

**Fix applied:** `level.N` removed from `MixNode.h` — `mix.sum` is now a plain
`out = sum(in.i)`. Went further than "auto-insert on connect" (not built — the
Map/Normalise auto-insertion precedent triggers on a type/quantity *mismatch*, not on
every ordinary connection, so it wasn't actually the right mechanism here): instead, a
real patch-schema migration (v3→v4, `PatchDocument.h`/`PatchSerializer.cpp`) preserves
an old patch's non-default or connected `level.N` by splicing in a real, visible
`mix.gain` node at load time — a constant `level.N` becomes that node's own `gain`
parameter; a `level.N` that was itself wired to a modulator gets that same modulator
rewired into the new node's `gain` *input*. A `level.N` left at its default (1.0,
unconnected) needs nothing — dropped with no trace, since a plain `in.N` connection
already behaves identically. New regression test (`tests/GrowablePortsTests.cpp`,
"Schema v3 -> v4 preserves mix.sum's level.N...") covers all three cases end to end,
including a real recompile proving the migrated patch sounds the same.

### `jargon-naming` — FIXED (Milestone 0.5)
*A node's display name uses inaccessible technical jargon where a plain name is
equally accurate.*

**Confirmed and fixed:** `mix.gain`'s running `getTitle()` returned **"VCA"** — a
**code bug, not a catalog problem** (`wiki/NODES.md`'s own entry already called it
"Gain"). `GainNode.h` now returns "Gain", matching the catalog.

**Possible, lower confidence — your call:** `filter.svf`'s title is "SVF Filter."
Unlike VCA, there's no simpler everyday word being avoided here — "SVF" names a
specific filter *topology* (state-variable), the same way "Ladder Filter" names
Moog's actual ladder circuit; "State Variable Filter" (the literal expansion) is
still fairly technical. Flagging this for your judgment rather than confirming it,
since it doesn't clearly meet your own test ("where it is possible to just call it
normally, we do that" — I'm not sure a plainer name exists here that loses nothing).

**Checked against all 55 titles, nothing else qualifies**: Constant, Delay, Listen,
Normalise, Downmix, Voice Mix, ADSR, Round, Clamp, Remap, Map, Threshold, Subtract,
Abs, Min/Max, Power, Modulo, Crossfade, Not, Toggle, Slew, Divide, Add, Multiply,
Boolean, Select, Compare, Sample & Hold, MIDI Control, Transport, Audio In, Scope,
Spectrum, Meter, Oscillator, DC Blocker, Envelope Follower, Sine, One-Pole Filter,
Peak Filter, Shelf Filter, Allpass Filter, Ladder Filter, Drift, Random, Instance
Allocator, Reroute, Master Out, Note In, Noise Burst, Mix, Pan, Width — all plain or
already-established terms (ADSR/LFO-class), none read as unnecessary jargon.

### `modulation-only-port` — no safe default when left unpatched — the confirmed instance is FIXED (Milestone 0.5)
*A Control-typed input has neither `hasFallbackWhenUnconnected`+`defaultValue` on its
`PortDescriptor` nor NaN-safe handling in `processSample()`, so leaving it unpatched
produces NaN/undefined output instead of a sane default — directly contradicts
`VALUE_MODEL.md`'s own rule (`wiki/NODES.System.md` §3): "node DSP code should see
exactly one shape: a connected input."*

**Confirmed, high-confidence, fixed:** `mix.gain`'s `gain` input (`GainNode.h`) — had
no fallback declared, `outputs[0] = inputs[0] * inputs[1]` directly, so an unpatched
Gain node multiplied by NaN. Now declares `hasFallbackWhenUnconnected` +
`defaultValue = 1.0` (unity) and falls back to a stored value in `processSample()`,
matching the pattern every other knob-style port in this codebase already follows.
The lower-confidence secondary instances below (`math.subtract`/`abs`/`minmax`/etc.)
were **left untouched** — deliberately, pending your review, since they're a
genuinely different, lower-severity/debatable case (see below).

**Confirmed, lower confidence/severity — a real inconsistency, but debatable
whether it matters:** `math.subtract` (`a`,`b`), `math.abs` (`in`), `math.minmax`
(`a`,`b`), `math.clamp`'s own `in`, `math.round`'s own `in`, `adapt.normalise`'s
`in`, `adapt.map`'s `in`, `adapt.remap`'s `in`, `adapt.threshold`'s `by` — none
declare a fallback on their primary signal input, and several (`subtract`, `abs`,
`minmax`) don't even NaN-guard in `processSample()`. **The concrete evidence this is
a real inconsistency, not just how these nodes must work:** `math.add` and
`math.multiply` — the closest siblings to `math.subtract`/`math.abs`/`math.minmax` —
DO give every one of their (growable) inputs a stored identity default (0 for Add, 1
for Multiply) specifically so an unwired input behaves sanely rather than poisoning
the sum/product. `math.subtract` has an equally obvious identity (0) and doesn't get
the same treatment. Whether this is worth fixing depends on whether you consider
"leave one side of a Subtract unwired" a real use case — flagging it as the same
underlying gap, explicitly lower priority than `mix.gain`.

**Checked against all 55 nodes' Control inputs** — every "knob-style" tunable port
elsewhere (cutoff, resonance, drive, attack/decay/sustain/release, rate, amount,
smooth, bias, spread, threshold, glide, rise/fall, exponent, divisor, step, low/high
in Clamp, delay time, DC-block cutoff, crossfade position, downmix inputs) correctly
declares `hasFallbackWhenUnconnected` + a sensible `defaultValue`. This is the norm
the codebase follows almost everywhere — `mix.gain` and the math/adapter groups above
are the exceptions, not the pattern.

### `hardcoded-trigger` — FIXED (Milestone 0.5)
*A source node has no Event-typed port to trigger it from the graph at all — only a
direct C++ poke from whoever compiled it in.*

**Confirmed, one instance:** `excite.burst` (`NoiseBurstNode.h`) — had **zero** input
ports (`getInputPorts()` returned `{}`); only startable via `trigger(int
durationSamples)`, a method nothing in the graph could call. Whoever wired it into a
proof graph poked it directly, the same pre-Note-era pattern everything else (MIDI,
the allocator) has since moved off of.

**Checked against all 55 and found nowhere else**: every other zero-input node is
either a genuine constant/held value (`util.constant` — meant to hold a static
number, not fire) or a host-driven I/O boundary node that's deliberately not
graph-triggerable (`io.audioIn`, `io.transport`, `io.control` all read from
`HostInputs`, by design). `excite.burst` was the only "fires an event" node with
nothing to wire an event into.

**Fix applied:** a real `trigger : Event` input port, as the catalog specifies, plus a
real `duration` port (catalog: 0.1–2000ms) so a graph-driven trigger produces a
sensible, configurable burst rather than a hardcoded length. Your ask ("this trigger
should then allow for other ways of triggering — a clock based triggering for
instance") now genuinely works: wire any Event source into it (a `clock.pulse` once
that's built, an `adapt.threshold`, etc.). The old `trigger(int)` C++ poke still
exists too — tests/tools that want exact sample-accurate control (render-cli's
Karplus-Strong path) still use it, and it now shares one implementation with the real
Event path rather than being a second one. `tone`/`shape` (the catalog's remaining two
ports) are still not built — this node stays 🚧 partial catalog compliance, not full,
in `wiki/NODES.md`.

### `single-type-preview-coverage`
*Visual feedback is Audio-shaped by default; other signal types have no automatic
preview.*

**More nuanced than "doesn't exist at all" once checked:** the manually-placed
`view.scope`/`view.meter` nodes (`ViewNodes.h`) **already accept Control, Boolean and
Event inputs today** — they use the same polymorphic-port mechanism `util.reroute`
does (`InheritingPortsNode`), not an Audio-only type. You genuinely can wire an LFO,
an envelope, or a gate into a `view.scope` right now and see it. `view.spectrum`
really is Audio-only (a spectrum of a Control signal isn't a meaningful thing).

**What's actually missing:** the *automatic, built-in* inline preview a node shows on
its own card (`getPreviews()`) is curated and Audio/Gain-biased — only `osc.analog`,
`osc.sine`, and `mix.gain` declare one. A Control-only source like `env.adsr`,
`random.stepped`, or `random.drift` shows **nothing** on its own card; you have to
know to place a separate `view.scope`/`view.meter` and wire it by hand. That's a real
gap (an envelope or a random generator is exactly the kind of thing you want to *see*
working without extra wiring), but it's a "which nodes get a free built-in preview"
question, not a "Control signals can't be visualized" one.

Your own proposed direction — a minimal, title-less, single-in/single-out preview
node — is recorded as the target shape for closing this, separate from whichever
nodes should get one automatically.

---

## Part 2 — Flagged by you, reads as intentional design (needs your confirmation, not treated as a mistake)

### `instance.allocator`'s `random1`/`random2`
Checked against `wiki/NODES.System.md` §5 (`DOMAINS.md`'s own design): these are
documented as **"a stable random value for the instance's lifetime... several
independent ones, addressable by index,"** seeded from `(patch seed, spawn ordinal)`.
This is specifically *not* reproducible by a plain `random.*` node, which runs once
per block for the whole graph, not once per spawned instance — there's no way to get
"this voice's own random pan position, stable for as long as this voice lives" out of
an ordinary node today. That's the actual reason these live directly on the
allocator rather than being achieved by wiring a separate node.

This may still be worth revisiting for *readability* (e.g., better port labels than
`random1`/`random2`, or documenting the pattern more visibly in the node's own UI) —
but the underlying mechanism looks like real, necessary design, not an oversight. I'm
not confirming this as a mistake; flagging it back to you as the instruction said to
expect.

---

## Part 3 — Architecture / UX mistakes (from your live testing)

### Master Out doesn't decide the output — real, confirmed, root-caused
`io.output` (`OutputNode.h`) is a plain 1-in/1-out passthrough. The real mechanism
that designates what the compiled graph actually outputs is
`GraphEditController::setOutput()` (`graphSetOutput` over the M7 bridge) — confirmed
present, wired, and tested (`GraphEditControllerTests.cpp`). **`ui/src/graph/
graphCommands.ts` exports exactly 9 of the 10 real bridge commands
(`graphAddNode`, `graphDeleteNode`, `graphDisconnect`, `graphConnectWithAutoAdapt`,
`graphSetParameterValue`, `graphMoveNode`, `graphSetProperty`, `graphGetSnapshot`,
`graphRestoreSnapshot`) — `graphSetOutput` is the one missing entirely,** confirmed
by grepping every call site in `ui/src`. There is no context menu action, no
automatic call on wiring into Master Out, nothing — the live output designation is
permanently stuck wherever the last C++-side default graph construction left it,
no matter what you rewire in the UI afterward. This is a real, high-value,
precisely-located bug, not an architecture philosophy problem. (`plain graphConnect`
is also absent from the export list, but that's superseded by
`graphConnectWithAutoAdapt` for all real UI use — not a gap.)

### Occupied-port rejection instead of replace
Confirmed real and intentional today — the compiler enforces one source per input
(referenced directly in `RerouteNode.h`'s own comment: "the compiler's one-source-
per-input check"). Dropping a second cable onto an already-wired input is rejected
rather than replacing the old one. Not yet located to the exact UI drop-handler call
site that would need to change to auto-disconnect first.

### Stereo: `left`/`right` port pairs, not one stereo cable
Confirmed deliberate (`ADR-0023` Amendment, M22) — `space.pan`'s outputs and
`space.width`'s `in.left`/`in.right` inputs are genuinely two separate mono Audio
ports today, not a true stereo cable. `PortDescriptor::channels = Channels::Stereo`
exists in the type system but no real node uses it. Your ask (stereo-by-default, one
cable) is a real architecture reopening — full writeup in `wiki/NODES.System.md` §4,
scoped as Milestone 0.2.

### Reroute "not connectable"
`util.reroute` (`RerouteNode.h`) reads correctly in isolation — real polymorphic
port support for Audio/Control/Boolean/Event/Spectral/Note, adopting both signal type
and quantity from whatever feeds it. Its `NodeLayoutVariant::Decoration` layout is the
one thing visually different from an ordinary node card, which is a plausible place
for a port-anchor/hit-testing UI bug to hide (the WebGL cable layer finds port screen
positions via `data-node-id`/`data-port-id` attributes on the DOM — worth checking
whether the Decoration layout variant emits those correctly). **Needs live repro**,
not confirmed as an engine bug.

### Note-port "same color won't connect"
`canConnect`'s real rule allows `Note → Note` unconditionally. Today there are only
two real Note ports in the whole engine (`io.noteIn.notes` output,
`instance.allocator.spawn` input) plus polymorphic `util.reroute`. Working theory:
what looks like a color-matching failure is actually the **Note-buffer fan-out
limitation** (a connected Note port uses a dedicated single-producer buffer, not the
ordinary block-buffer mechanism that supports fan-out to several destinations) —
surfacing as a misleading symptom, not a real type/color bug. Also worth noting per
`wiki/NODES.System.md` §6: `Note` has **no color assigned at all** yet in the visual
language table, so "same color won't connect" may be describing two default/fallback
colors that happen to look similar, not a genuine same-color case. **Needs live
repro** against the exact graph you tried, before deciding which of these it is.

### Dropdowns not opening
`TriggerSelect.tsx` (the enum-picker control `ParameterRow` renders for every
structural enum parameter) reads correctly in isolation: click toggles an `open`
state, an outside-click/Escape handler closes it, `stopPropagation()` is called on
its own click handler. **Likely cause, not confirmed:** a pointer-capture or
propagation conflict with the node-drag gesture on the canvas — a `pointerdown`
handler higher up the tree (node drag start) swallowing the event before
`TriggerSelect`'s `onClick` fires. **Needs live repro/debugging**, not a docs fix.

### Scope preview quality
Two different things share this complaint, worth telling apart:
- **`ui/src/analysis/TelemetryScope.tsx`** (the separate M5 analysis panel, not a
  node) — already a known, documented gap (Canvas2D, not WebGL, unchanged since M5).
- **`ui/src/nodes/NodePreview.tsx`** (the newer, M20 inline per-node preview —
  `osc.analog`'s/`mix.gain`'s own built-in scope/meter) — also Canvas2D, but its
  resize logic **is** correctly devicePixelRatio-aware (`canvas.width = width * dpr`,
  `ctx.setTransform(dpr, 0, 0, dpr, 0, 0)`) — the standard fix for exactly this kind
  of blurriness is already present. If this one still looks pixelated, the more
  likely cause is the draw routine itself (`telemetryDraw.ts`'s line width/
  anti-aliasing) or the genuinely small on-screen size of a node-card preview, not a
  missing-DPR bug. **Needs a live look to tell which node's preview you mean before
  diagnosing further.**

### "Input port already connected" — see Occupied-port rejection above (same item).

---

## Full node list checked (55 files, `engine/include/bazalt/engine/nodes/`)

ConstantNode, OutputNode, DelayNode, ListenNode, NoiseBurstNode, NormaliseNode,
DownmixNode, InstanceMixNode, InstanceAllocatorNode, IoNoteInNode, AdsrNode,
SvfFilterNode, RoundNode, ClampNode, RemapNode, GainNode, MapNode, ThresholdNode,
SubtractNode, AbsNode, MinMaxNode, PowerNode, ModuloNode, CrossfadeNode,
LogicNotNode, LogicToggleNode, SlewNode, DivideNode, AddNode, MultiplyNode, MixNode,
LogicBooleanNode, RerouteNode, LogicSelectNode, LogicCompareNode, SampleHoldNode,
IoControlNode, IoTransportNode, IoAudioInNode, ViewNodes (Scope/Spectrum/Meter),
OscillatorNode, DcBlockNode, EnvelopeFollowerNode, SineOscillatorNode,
OnePoleFilterNode, PeakFilterNode, ShelfFilterNode, AllpassFilterNode,
LadderFilterNode, RandomDriftNode, RandomSteppedNode, WidthNode, PanNode
(`GrowableGroupNode`/`InheritingPortsNode` are shared base classes, not node types,
and are excluded from the count).
