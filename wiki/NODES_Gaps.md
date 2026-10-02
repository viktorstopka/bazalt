# Node & architecture gaps — identified, most now fixed

Milestone 0.1's deliverable — **updated after Milestone 0.5**, which fixed the four
confirmed, mechanical findings below (marked **FIXED** inline) at the user's
go-ahead, before the user's own review pass happened. The review is still worth
doing — it's what confirms these were the right fixes, not a reason they were
blocked on it. The lower-confidence items (SVF naming, the `math.*`/`adapt.*`
no-fallback pattern, `instance.allocate.voice`'s randoms) are untouched, exactly as
originally scoped, pending that review.

Process: every specific mistake you named gets
generalized into a named category, then checked against **every one of the 55 real,
registered node headers** in `engine/include/bazalt/engine/nodes/` (the full list is
at the bottom, so this scan is checkable, not just asserted) — flagged only where I'm
confident it actually applies. Several things you flagged read as intentional,
documented design once checked against the code and `wiki/NODES.System.md`'s
architecture — those are called out separately, for your confirmation, not silently
agreed with.

**Most of this is now fixed** (marked **FIXED** inline, each citing the milestone that
fixed it) — this line used to say "nothing here is fixed yet," written when this file
was identification-only per the original instruction, and never updated once `0.3`
through `0.7` actually landed. The lower-confidence items this file itself flags as
needing your judgment first (SVF naming, the `math.*`/`adapt.*` no-fallback pattern,
`instance.allocate.voice`'s randoms) are genuinely still open, pending that review.

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

### `single-type-preview-coverage` — the requested minimal node is FIXED (Milestone 0.6)
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

**Fix applied:** your own proposed direction — a minimal, title-less, single-in/
single-out preview node — is now real: `view.glance` (`ViewGlanceNode.h`,
`NodeLayoutVariant::Glance`). Splices into any Audio/Control/Boolean/Event cable like
`util.reroute` does (same polymorphic mechanism) and shows a live trace of whatever
passes through, with no title bar and no parameter list — just an input glyph, a
compact preview, an output glyph.

**Left open, deliberately** — a separate, smaller question this milestone didn't
try to answer: *which* nodes should get an **automatic**, built-in preview
(`getPreviews()` declared on the node itself, no placing/wiring needed) beyond the
three that already do (`osc.analog`, `osc.sine`, `mix.gain`). `view.glance` closes
the "there's no minimal way to look at a Control/Boolean/Event signal at all" gap;
it doesn't by itself decide that e.g. `env.adsr` or `random.stepped` should show
themselves without the user placing a `view.glance` next to them — a real, separate,
smaller follow-up if wanted, not assumed here.

---

## Part 2 — Flagged by you, reads as intentional design (needs your confirmation, not treated as a mistake)

### `instance.allocate.voice`'s `random1`/`random2`
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

**A real, separate bug found alongside this (FIXED, `09-28-InstanceAllocator.2`,
2026-10-01):** the "seeded from `(patch seed, spawn ordinal)`" part of the design
above was aspirational until now — `InstanceVoiceNode::prepare()` actually called
`random.setSeedRandomly()` (wall-clock-seeded, once per plugin-process-lifetime),
directly contradicting `DOMAINS.md` §4's own stated reason these ports are
allocator-owned state ("the same patch, the same MIDI, the same seed produce
bit-identical output... required for the offline render CLI to be a useful
regression tool"). A new `instance.allocate.voice.seed` structural parameter
(matching `random.stepped.seed`'s own convention) now actually backs it: each
`noteOn()` constructs a fresh `juce::Random` from `(seed, instanceIndex)`, a pure
function with no dependency on wall-clock time or call history. See
`wiki/MILESTONES.md`'s own `09-28-InstanceAllocator.2` entry for the fix.

---

## Part 3 — Architecture / UX mistakes (from your live testing)

### Master Out doesn't decide the output — real, confirmed, root-caused — FIXED (Milestone 0.3)
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

**Fixed (Milestone 0.3):** added the missing `graphSetOutput` wrapper
(`ui/src/graph/graphCommands.ts`), wired into every gesture that can land a
connection on a node's input (`addWire`/`commitWireDrag`/`spliceInsert` in
`graphStore.ts`) — wiring into `io.output` auto-designates it as the graph's real
output, no separate step needed — plus a general "Set as Output" right-click action
for designating any node's output explicitly.

### Occupied-port rejection instead of replace — FIXED (Milestone 0.4)
Confirmed real and intentional today — the compiler enforces one source per input
(referenced directly in `RerouteNode.h`'s own comment: "the compiler's one-source-
per-input check"). Dropping a second cable onto an already-wired input is rejected
rather than replacing the old one.

**Fixed (Milestone 0.4):** `GraphEditController::connect()`/`connectWithAutoAdapt()`
now call a new `replaceExistingInputConnection()` helper that removes any existing
connection already targeting that `(toNodeId, toPortId)` pair before adding the new
one — covers direct connects, the adapter-chain path, and the polymorphic-endpoint
(Reroute) path. The compiler's own "Input port already connected" check still exists
as a safety net, it just never fires on an ordinary user gesture any more.

### Stereo: `left`/`right` port pairs instead of one real cable — FIXED
A stereo signal is one real Audio cable now, catalog-wide. `space.pan`/`space.width`/
`io.output`/`mix.downmix`/`stereo.split`/`stereo.combine` each declare a real
`Channels::Stereo` port instead of separate `left`/`right` ports — `GraphCompiler.cpp`
backs `canConnect`'s already-existing mono/stereo rules with real per-channel buffers
(a stereo port occupies two flat buffer slots, resolved by the compiler; node
`processSample`/`processBlock` signatures didn't change). The Init Patch wires
`space.pan`'s `out` straight into `io.output`'s `in` as one cable — a fresh plugin
instance opens playing real, audibly panned stereo. A mono source into a stereo
destination still duplicates/broadcasts exactly as before (canConnect's free
mono→stereo rule, now real) — regression-tested, including a mutation-testing pass
confirming the broadcast logic is load-bearing. `mix.downmix` becoming a genuine
1-in-1-out node also closed a second, related gap: it's now auto-insertable by
`connectWithAutoAdapt`, which used to refuse it outright. Full detail, including what's
deliberately still out of scope (channel-0-only tap/preview lookups, no visually
distinct stereo cable in the UI, no patch migration — CLAUDE.md rule 3 is suspended
for now, see its own note), is in `wiki/NODES.System.md` §9.

### Reroute "not connectable"
`util.reroute` (`RerouteNode.h`) reads correctly in isolation — real polymorphic
port support for Audio/Control/Boolean/Event/Spectral/Note, adopting both signal type
and quantity from whatever feeds it. Its `NodeLayoutVariant::Decoration` layout is the
one thing visually different from an ordinary node card, which is a plausible place
for a port-anchor/hit-testing UI bug to hide (the WebGL cable layer finds port screen
positions via `data-node-id`/`data-port-id` attributes on the DOM — worth checking
whether the Decoration layout variant emits those correctly). **Needs live repro**,
not confirmed as an engine bug.

### Note-port "same color won't connect" — ROOT-CAUSED AND FIXED (Milestone 0.7)
My earlier working theory (a Note-buffer fan-out limitation) was **wrong** — checked
and ruled out by reading `GraphCompiler.cpp`'s actual Note-connection logic directly:
`noteInputsUsed` guards one thing only, the same "one source per input" rule every
ordinary connection already has, keyed by the *destination*. A Note **output** fanning
out to several inputs (`io.noteIn.notes` → both `instance.allocate.voice.spawn` and a
`util.reroute`, say) is never rejected — there's no fan-out limitation at all.

**The real cause, confirmed by reading `ui/src/graph/portUiKind.ts` directly:**
`classifyPortUiKind()` had no case for `SignalType::Note` at all — it fell through
the generic `type !== 'control'` branch straight into `'value'`, **the exact same
white a real-quantity Control port (Pitch, Frequency, Time, ...) renders as.** Wiring
`io.noteIn`'s white "notes" output into some other white-looking-but-actually-
Control-typed input was always a genuine, correctly-rejected type mismatch —
`canConnect` was right every time — it just *looked* like "same color won't connect"
because Note had no color of its own to tell it apart. Confirmed live: screenshotted
`instance.allocate.voice`'s own `spawn` input in the running Standalone app and it now
renders in the new distinct teal (`tokens.color.portNote`, `#3ecfc0`), visibly
different from the white ports around it.

**Fix applied:** `portUiKind.ts` gained a real `'note'` kind (a distinct teal, and a
"♪" glyph instead of the shared arrow), so a Note-typed port now looks like what it
is — genuinely different from a same-looking Control port — everywhere: node cards,
the component gallery legend, and the WebGL cable layer (all three already read this
one shared classifier, so no separate fix was needed for any of them). `Data`'s color
stays open (still no real node produces one to observe against), noted as such rather
than guessed at.

### Dropdowns not opening — ROOT-CAUSED AND FIXED (Milestone 0.7)
My earlier working theory (a pointer-capture/propagation conflict with the canvas's
node-drag gesture) was **wrong** — checked and ruled out by reading
`InfiniteCanvas.tsx`'s actual mousedown/click handling directly: `isOwnGestureTarget()`
already excludes `.trigger-select` by name (a real, deliberate, already-documented fix
for exactly this class of problem, originally built for `.value-slider`), and the
canvas's raw `click` listener no-ops whenever no node-placement ghost is active — the
ordinary "open a dropdown" case. Neither one touches this at all.

**The real cause, confirmed by reading `TriggerSelect.css` directly:** `.trigger-select`
copied its shell wholesale from `.value-slider` (same pill visual language, per
`TriggerSelect.tsx`'s own header comment) — including `.value-slider`'s `overflow:
hidden`, which is genuinely load-bearing *there* (it clips `.value-slider-fill`'s
absolutely-positioned value bar to the pill's rounded corners). `TriggerSelect` has no
fill bar; it has a dropdown menu instead — a DOM *child* of that same clipped 20px-tall
box, positioned below it (`top: calc(100% + 2px)`). The click handler and the `open`
state toggle were **never broken** — the menu opened correctly every time, it was just
clipped to zero visible height by its own parent. Confirmed live: temporarily forced
`open` to `true` (no synthetic clicks involved — this environment's WebView2 content
doesn't reliably accept synthetic mouse input or expose a UI Automation tree, per prior
session notes, so a code-level force-open + screenshot was the only reliable way to see
it), screenshotted the Standalone app, saw both `instance.allocator`'s "Configuration"
and `osc.analog`'s "Shape" dropdowns render their full option lists correctly, reverted
the hack, rebuilt clean.

**Fix applied:** removed `overflow: hidden` from `.trigger-select` — safe, since the
label/value text already truncates itself independently via its own `overflow: hidden;
text-overflow: ellipsis` on the child spans.

### Scope/Glance preview quality, node cards growing without bound, whole-editor sluggishness — ROOT-CAUSED AND FIXED (2026-10-01)
Four complaints that read as unrelated ("visuals getting infinitely bigger — now
only the width," "glitching for half a second around every second," "scope/glance
looks terrible, pixelated," "the program is extremely slow from the beginning")
turned out to share ONE root cause. The entry below is what this file used to say
about the pixelation half alone — kept for the record, then superseded by the real
finding underneath it, the same "don't retroactively rewrite a wrong theory, mark it
superseded" convention this file already uses elsewhere (see `instance.allocator`
color's own entry two sections up).

~~Two different things share this complaint, worth telling apart:~~
~~- `ui/src/analysis/TelemetryScope.tsx` (the separate M5 analysis panel, not a
  node) — already a known, documented gap (Canvas2D, not WebGL, unchanged since M5).~~
~~- `ui/src/nodes/NodePreview.tsx` (the newer, M20 inline per-node preview —
  `osc.analog`'s/`mix.gain`'s own built-in scope/meter) — also Canvas2D, but its
  resize logic **is** correctly devicePixelRatio-aware (`canvas.width = width * dpr`,
  `ctx.setTransform(dpr, 0, 0, dpr, 0, 0)`) — the standard fix for exactly this kind
  of blurriness is already present. If this one still looks pixelated, the more
  likely cause is the draw routine itself (`telemetryDraw.ts`'s line width/
  anti-aliasing) or the genuinely small on-screen size of a node-card preview, not a
  missing-DPR bug. Needs a live look to tell which node's preview you mean before
  diagnosing further.~~ **This last paragraph's conclusion was wrong** — the DPR
logic being present didn't mean the *size it was scaling* was ever actually stable.

**The real root cause:** `NodePreview.tsx`'s render loop read `canvas.clientWidth`/
`clientHeight` off the `<canvas>` element itself, then wrote a bigger `canvas.width`/
`height` ATTRIBUTE back in (the DPR/zoom backing-store scaling the paragraph above
correctly describes). That's fine as long as the canvas's own CSS size can never be
influenced by its own attribute values — but `.node-card` is `inline-flex` with only
a `min-width` floor, never a `width`, so its own width is shrink-to-fit: computed by
asking every descendant "how wide do you intrinsically want to be." A `<canvas>`'s
answer to THAT specific query ignores its `width: 100%` (a percentage has no base to
resolve against when the thing being asked IS what determines that base) and falls
back to its INTRINSIC size — its `width`/`height` ATTRIBUTES, the exact values the
render loop was enlarging every frame. That intrinsic size fed `.node-card`'s own
shrink-to-fit width, `width: 100%` then resolved against THAT on the next layout
pass (one frame bigger), the render loop read it back via `clientWidth` and enlarged
the attribute again — an unbounded feedback loop, once per reflow, explaining every
one of the four symptoms at once: width grows without bound (height didn't, because
it was already pinned to a fixed `48px`/`28px`/`96px`, never a percentage, so it was
never exposed to this); the periodic "glitch" is the visible jump each time the
browser's shrink-to-fit recomputation catches up; "pixelated" is the buffer
overshooting the browser's real canvas-size ceiling and the DPR math going wrong once
it does; "slow from the beginning" is the WHOLE editor's dirty-flag render loop never
going idle — `InfiniteCanvas.tsx`'s own `nodeResizeObserver` watches each node card's
element for size changes and calls `requestFrame()` on every single one, so a
continuously-growing preview inside a continuously-growing card keeps the main WebGL
cable/grid layer re-rendering forever, not just the one small preview canvas.
`InfiniteCanvas.css`'s own `.infinite-canvas > canvas` fix (its own comment has the
history) solved the exact same class of bug for the OTHER canvas's HEIGHT, via an
accidentally-matched CSS rule — this was the same root mechanism finally catching up
with the preview canvas's WIDTH, through its own legitimate rule this time, not an
accident.

**Fix applied:** `.node-preview-canvas` can no longer be the element asked "how wide
do you want to be." `NodeCard.css` moved every bit of its sizing (`width`/`height`/
`min-width`) onto a new, plain, non-replaced wrapper div (`.node-preview-canvas-wrap`,
`position: relative; overflow: hidden`); the canvas itself is now
`position: absolute; inset: 0` inside it — an absolutely-positioned element
contributes nothing to its container's intrinsic-size computation, full stop,
regardless of its own attribute values. `NodePreview.tsx` measures
`canvas.parentElement` (this wrapper) instead of the canvas's own `clientWidth`/
`clientHeight`, for the same reason — structurally non-circular, not just a
different way of reading the same number. `npm run build`/`npm run lint` clean; no
engine/C++ change at all, so no rebuild needed — the already-running dev server
(Vite HMR) picked the fix up live in the already-open Standalone instance. A human
hands-on look at the actual running app (does a node preview's width genuinely stay
put now, does the editor stay smooth) is still the user's own next confirmation step
— same computer-use caveat every entry in this file already carries.

### Zoom cropping/offset, borders disappearing, everything vanishing above a zoom threshold — ROOT-CAUSED AND FIXED (ADR-0032, 2026-10-01)
Direct feedback, raised immediately after the preview-canvas-growth fix above landed: "it now
zooms weird as hell... zooms independently from the frame... too big for the frame so is
cropped... on zoom, sometimes some borders disappear, and after zooming above a threshold
everything just disappears." Not caused by that fix (different files entirely — `InfiniteCanvas.tsx`'s
own camera/zoom code was never touched by it) and not a new regression either: `InfiniteCanvas.css`'s
own header comment had already named the real suspect as a known, accepted limitation *before*
this was ever reported as a live bug — "WebView2's own native pinch-zoom/edge-swipe handling isn't
reachable from CSS/JS at all; that would need a JUCE-side `WebBrowserComponent` option."

**Confirmed, not assumed:** read JUCE's own vendored source (`build/_deps/juce-src`) directly.
`WebBrowserComponent::Options::WinWebView2` exposes zero zoom-related options (only DLL location/
user data folder/status bar/error page/background colour) — contrast the Linux WebKit backend's
own `LinuxWkWebView::withNativeZoomGesture`, **disabled by default** there specifically so native
pinch/Ctrl+wheel zoom translates into ordinary JS wheel events instead. Nothing equivalent exists
for Windows, and `juce_WebBrowserComponent_windows.cpp`'s own `setWebViewPreferences()` never
calls WebView2's own `put_IsZoomControlEnabled` — its default is enabled, and per Microsoft's own
documentation it governs Ctrl+MouseWheel/Ctrl+Plus/Ctrl+Minus/pinch zoom at the WebView2 HOST
level, independent of the hosted page's own wheel-event handling (`InfiniteCanvas.tsx`'s own
`onWheel` already calls `preventDefault()` unconditionally — this was never a missing-call bug on
the app side; native zoom simply isn't reachable that way at all).

That's the real mechanism behind every symptom: **two independent zoom transforms were
compounding** — this app's own `camera.zoom` (JS/CSS, clamped `[0.1, 8]`, the only one
`InfiniteCanvas.tsx`'s code has ever known about) and WebView2's own native host-level zoom
(uncontrolled, fired by the same Ctrl+wheel gesture), neither aware of the other. "Too big for
the frame, cropped" is native zoom scaling the whole rendered page past the WebView2 control's
own viewport, clipped at a different boundary than the app's own `.infinite-canvas { overflow:
hidden }` was ever designed to account for. "Borders disappearing"/"everything disappearing above
a threshold" is the COMBINED effective scale (camera.zoom × WebView2's own factor) pushing past
what either transform was ever tested against alone. This almost certainly also explains some of
the "still glitching periodically" report that persisted after the preview-canvas-growth fix —
Windows' own smooth-scroll/trackpad input occasionally synthesizes wheel deltas that read as a
zoom gesture to WebView2 even without a deliberate Ctrl press, which would read as exactly this
kind of unpredictable periodic hiccup.

**Fix applied (ADR-0032 has the full reasoning):** `cmake/ApplyJuceWebView2ZoomPatch.cmake`, wired
into the top-level `CMakeLists.txt`'s `FetchContent_Declare(JUCE ...)` as its `PATCH_COMMAND`,
patches `juce_WebBrowserComponent_windows.cpp` to call `settings->put_IsZoomControlEnabled
(false)` — one line, same pattern already used for `put_IsStatusBarEnabled`/
`put_IsBuiltInErrorPageEnabled` right above it. Runs once, automatically, right after JUCE is
first fetched (never re-run against an already-populated `build/_deps/juce-src`, and idempotent
even if it somehow were) — a durable fix that survives a clean rebuild, not a hand-edit that
silently vanishes the next time `build/_deps` gets wiped. `cmake/patches/
0001-webview2-disable-native-zoom.patch` is the human-readable record of exactly what changes, in
standard diff form.

**Verified:** `ctest --test-dir build -C Debug` 515/515 green (no engine/plugin-logic change at
all — this only touches WebView2 host configuration). Confirmed the patched line actually compiles
against this project's real, linked WebView2 SDK (not just assumed from Microsoft's docs) by
building `BazaltPlugin_Standalone` clean. A human hands-on look at the actual running app — does
Ctrl+wheel/pinch now leave zoom entirely to the app's own camera, do borders stay solid across the
zoom range, does the periodic glitch actually stop — is still the user's own next confirmation
step, same computer-use caveat every entry in this file already carries; this environment can
confirm the fix compiles and is wired correctly, not what it looks like on screen.

### "Input port already connected" — see Occupied-port rejection above (same item).

---

## Part 4 — found live during `wiki/plans/DomainRedesign.md`'s implementation

Not from your review pass — these two were real defects this redesign's own batches
ran into and fixed along the way, tracked here per this doc's stated job (found-defect
tracking for existing behavior), not folded into Parts 1-3 above since they postdate
that original 55-node scan.

### `maxInstances-never-enforced` — FIXED (Batch 4)
*A node's own Structural parameter is declared and editable in the UI but never
actually read anywhere in the audio path.*

**Confirmed:** `instance.allocate.voice`'s `maxInstances` (`InstanceAllocatorNode.h`)
has been settable since M17, but the voice pool it should cap (`VoiceManager`) always
hardcoded its ceiling to the full physical `numVoices` regardless of what this
parameter said — dialing it down to, say, 2 did nothing; every voice lane above
whatever number you typed kept allocating exactly as before.

**Fix applied:** `VoiceManager::setMaxActiveVoices()`/`getMaxActiveVoices()` now
actually enforce the ceiling — `findIdleVoice()`/`stealVoice()` both respect it,
clamped to `[1, numVoices]` so it can never ask for more lanes than physically exist
or drop to zero. `getActiveVoiceCount()` (a relaxed atomic, recomputed on every stage
change) backs the instance-count badge's live numerator. `tests/VoiceManagerTests.cpp`
covers enforcement end to end.

### MIDI-independence dispatch — FIXED (Batch 2)
*Two coexisting note sources feeding one origin — a real `io.noteIn` wiring, and an
internal `clock`→`seq`→`note.assemble` chain triggering the same allocator's note-in
mechanism — must never be conflated into a single trigger detector, or an ordinary
incoming MIDI note phantom-spawns a duplicate, phase-offset voice at the same pitch.*

**Confirmed, found live:** `spawnEventsThisBlock` increments on every noteOn/noteOff
regardless of source, by design (one counter for both directions) — including an
ordinary note arriving through real `io.noteIn` wiring one render call after
`triggerVoiceNote()`'s own internal poke. The first cut of the internal-trigger
detection misread this as an internal trigger and spawned a phantom extra voice at
the same pitch, caught by `InitPatchTests.cpp`'s polyphony RMS test showing the
resulting chord QUIETER than expected (destructive phase interference from the
duplicate, phase-offset oscillator — not louder, the naive expectation).

**Fix applied:** the internal-trigger detection block is now gated on
`voicePlans[0] != nullptr && voicePlans[0]->noteInNodeId.isEmpty()` — only an origin
with NO real `io.noteIn` wired runs the internal-trigger mechanism at all, so a real
MIDI-driven origin and an internally-triggered one never cross wires even when both
exist in the same graph simultaneously (up to `MultiplicityResolver::maxOrigins`, 4).

---

### "The program is still extremely slow from the beginning" — a real, significant cause found and fixed (2026-10-01)
Direct feedback, raised repeatedly before a root cause was actually found: constant background
sluggishness, present from the moment the app launches, independent of graph size or any user
interaction. Found by reading `ui/src/telemetry/telemetryClient.ts`'s own `startTelemetryPolling()`
directly: it unconditionally seeded the M4 baseline (`main` + 4 aux buses, all 3 frame types = 15
taps) at app boot, then polled every one of them via a real `fetch()` call against the native
resource-provider bridge on **every single animation frame** (~900 fetch calls/second) — for the
entire app lifetime, starting the instant `main.tsx` runs. The only consumer of those specific tap
names is `AnalysisPanel.tsx` (the M5 panel) — confirmed by grepping `ui/src/` for every reference
to the baseline tap names, nothing else touches them — and `App.tsx`'s own `analysisOpen` is a
hardcoded `false`: that panel has never been mounted in the current build. All 900 fetch/sec were
pure waste, for data nobody was ever reading, running since before the user even places a single
node.

**Fix applied:** `startTelemetryPolling()` no longer seeds anything — it only starts the shared
polling-loop infrastructure itself (the same `activePolls` Map/rAF loop `subscribeNodePreview`'s
own `pollTap` calls use for real, dynamic node previews, which this leaves untouched). The baseline
seed moved into two new exported functions, `seedBaselineTaps()`/`unseedBaselineTaps()`, called from
`AnalysisPanel.tsx`'s own mount/unmount effect — the exact same subscribe-on-mount/unsubscribe-on-
unmount discipline `NodePreview.tsx` already uses for its own taps. The panel still works identically
whenever it IS opened; the cost now only exists while it's actually on screen.

**Also investigated and ruled out, empirically, not by inspection alone:** a separate report of audio
timing feeling unstable while holding a note with a periodic modulation source ("it would get faster
and faster... LFOs for instance... automating the frequency to make an arp") — new
`tests/ClockSeqNodesTests.cpp` case renders `ClockPulseNode` for a full 10 simulated minutes
(26.46M samples) at a musically plausible 5Hz and checks the sample-exact interval between ticks near
the start of the render against ticks near the end: **identical, exactly 8820 samples, every time** —
`basePhase` is a `double` accumulator with no block-size reference anywhere in its formula (CLAUDE.md
rule 6), and this proves it genuinely doesn't drift over a long hold. The perceived speed instability
is NOT an engine/DSP math bug; it's much more likely a symptom of the SAME real-time performance
pressure this entry's own fix addresses (a CPU-starved real-time audio thread under-running/glitching
reads as "speeding up and slowing down," a completely different failure mode from the engine's own
tick timing being wrong) — consistent with the two reports surfacing together.

**Verified:** `ctest --test-dir build -C Debug` 516/516 green (515 + 1 new drift test — zero
regressions, confirms the clock math itself was never the problem). `npm run build`/`npm run lint`
(`ui/`): clean. A human hands-on look at whether the app actually *feels* faster from launch, and
whether the audio-timing complaint improves, is still the user's own next confirmation step — this
environment can prove the clock math is airtight and that 900 wasted fetch/sec is now zero; it can't
feel CPU load the way a person running the actual app can.

### Zoom cropping/offset, borders disappearing — STILL OPEN, narrowed but not yet fixed (2026-10-01)
ADR-0032's WebView2-native-zoom fix (entry above) turned out NOT to be the cause: direct feedback
confirmed the symptom reproduces on a **plain scroll wheel, no Ctrl held** — the app's own zoom
gesture, never WebView2's native one, which only engages on Ctrl+wheel/pinch. That fix is still
correct and worth keeping (it closes a real, separate gap), it just wasn't THIS bug.

**Narrowed, with a real clue:** direct feedback that cables stay correctly rendered throughout,
while node cards (and other DOM content) crop/disappear. Cable positions and the DOM world layer's
CSS transform are both driven from the exact same `camera.zoom`/`panX`/`panY` numbers, read once per
frame (`InfiniteCanvas.tsx`'s `frame()`) — cables via plain JS arithmetic
(`portAnchors.ts::offsetToScreenAnchor`), node cards via a literal
`transform: translate(...) scale(...)` CSS string on `.infinite-canvas-world`. Worked through the
formula by hand: both paths compute the identical `screen = pan + local * zoom`, so the camera
numbers themselves aren't corrupted (confirmed, not assumed) — if they were, cables would be wrong
too. The bug is specifically in how the BROWSER renders a `transform: scale()`'d DOM subtree, not in
this app's own math.

**Leading theory, not yet confirmed:** Chromium/Blink has known practical limits on how large a
`transform`'d element's effective paint bounds can get before content gets culled — this app's
`.infinite-canvas-world` is deliberately NOT GPU-layer-promoted (`InfiniteCanvas.css`'s own comment:
removed `will-change: transform` on purpose, to stay sharp rather than cache a blurry rasterized
bitmap), meaning Blink re-paints the scaled DOM content fresh every frame and has to compute a real
paint rect for it each time — a wide graph zoomed in far enough could plausibly exceed whatever
internal bound triggers culling, which WebGL (a fixed-size canvas viewport, immune to this specific
class of limit) would never hit. Not yet proven — needs either a repro with a known graph
size/zoom level, or a deliberate stress test building a wide graph and checking node visibility
programmatically at a range of zoom values. Pending that.

### `resonator.plate.quality` crash — FIXED (2026-10-03)
*A structural parameter's own `prepare()`-time buffer allocation read the
node's just-constructed DEFAULT value instead of the parameter's declared
CEILING — safe only if the two happen to be equal.*

**Confirmed, found live, direct feedback**: "setting plate quality to high
crashed it." Root cause, confirmed by reading `GraphCompiler.cpp` directly:
a freshly-constructed node gets `prepare()` called BEFORE its stored
parameters are applied via `setParameter()` (the compiler's own real,
established order — prepare a fresh node first, then loop `setParameter()`
over everything the graph has saved for it). `ResonatorPlateNode::prepare()`
sized its `state1`/`state2` mode-state vectors from the LIVE `quality`
member — still at its just-constructed default, `Medium` (16 modes), since
`setParameter()` hadn't run yet — rather than the parameter's own declared
maximum, `High` (32 modes). The very next `setParameter
("resonator.plate.quality", 2.0)` call raised `quality` to `High` without
ever resizing the vectors; `processSample()`'s own per-mode loop then
indexed `state1[16..31]`/`state2[16..31]`, a real out-of-bounds vector
access — reproduced directly as a hang (not a clean crash) when the exact
real construction order was mutation-tested.

`resonator.modal`'s own structurally-identical `maxModes` parameter has the
SAME pattern (`prepare()` reads the live `maxModesParam` member) but was
never actually broken by it — only because its default (64) already equals
its own declared ceiling, so `prepare()` always allocates the full ceiling
by coincidence. Hardened anyway, to the same explicit "always allocate to
the ceiling, not the live value" pattern, so this exact bug class can't
reappear here later if that coincidence ever stops holding.

**Fix applied:** both nodes' `prepare()` now size their state vectors to
their own parameter's declared ceiling explicitly (`ResonatorPlateNode`:
`maxModesFor (Quality::High)`; `ResonatorModalNode`: `defaultMaxModes`) —
the same "fixed-maximum allocation, a structural parameter only ever
controls how much of it is USED" pattern `filter.ladder`'s own fixed-size
internal state already follows for its own structural `poles` parameter.

**Tests:** `tests/PMCoreNodesTests.cpp` — a direct node-level test calling
`prepare()` then `setParameter()` in the EXACT real order (the opposite of
every other test in that file, which calls `setParameter()` first — a real
gap in this batch's own original test coverage, not a second copy of an
already-covered case), and a real compiled-graph test placing
`resonator.plate` with `quality` already saved as `High` from the start
(the literal reported/saved-patch shape). Mutation-tested: reverting the
fix reproduced the hang under the new regression test, confirming it
actually catches this bug; restored, clean and fast.

**Verified:** full rebuild clean, `ctest --test-dir build -C Debug` 577/577
green (575 + 2 new). Standalone app relaunched, stays up.

### Release build never actually served `ui/dist` — FIXED (2026-10-03)
*A code comment claimed real behavior that no code anywhere actually
implemented — a stale "this is already done" claim, the opposite failure
mode from the usual "this note never got updated after the fix landed."*

**Confirmed, found while answering a direct, practical question**: "can I
get a VST3 export to test on a different computer?" Investigating that
surfaced a genuine contradiction: `CLAUDE.md`'s own "Known interim
simplifications" section said a Release build always serves a hardcoded
placeholder page, "still true as of M6" — but `plugin/CMakeLists.txt` had
its own, DIFFERENT comment claiming "release builds serve ui/dist from disk
via a resource provider." Reading `PluginEditor.cpp`'s actual
`serveResource()` directly settled it: the CMakeLists.txt comment was
wrong, describing an intent that was never implemented — every Release
request for `/`/`/index.html` always returned the same hardcoded
`placeholderHtml` string, completely independent of whether a real
`ui/dist` existed on disk anywhere.

**Why this mattered for the actual question asked:** the Debug build (the
only one with a real editor) loads its UI from `http://localhost:5173` — a
dev server running on the SAME machine. Copying just the compiled `.vst3`
to a different computer, as asked, would never get a working editor there:
Debug needs a dev server that doesn't exist on the target machine, and
Release never read `ui/dist` at all regardless of whether it was present.

**Fix applied:** a real mechanism, not a workaround. `plugin/CMakeLists.txt`
gained a new `cmake/CopyUiDist.cmake` post-build step for both the VST3 and
Standalone targets — copies a built `ui/dist` (`cd ui && npm run build`)
into the VST3 bundle's own `Contents/Resources/ui/` and a flat sibling
`ui/` next to the Standalone executable, skipping (with a status message,
never a build failure) when `ui/dist` hasn't been built. `PluginEditor.cpp`
gained `findUiDistRoot()` (tries both packaging shapes, returns an invalid
`File` if neither exists) and `serveUiDistFile()` (reads a real file by
extension-derived MIME type) — wired into `serveResource()` ahead of the
old placeholder, which still exists and still serves as a real, honest
fallback when no `ui/dist` was shipped at all, rather than the only path.

**Verified:** `cd ui && npm run build` (fresh, current `ui/dist`). Both
Debug and Release rebuilds clean; the post-build step's own status message
confirms the copy actually ran for every target
(`BazaltPlugin_VST3`/`BazaltPlugin_Standalone`, both configs), and the
files were confirmed present on disk at the exact paths `findUiDistRoot()`
expects. Release Standalone launched and stayed up (no crash) — this
environment has no screenshot/visual-inspection capability to confirm the
real UI renders pixel-for-pixel rather than the placeholder (the same
long-standing limitation every UI milestone in this project's history has
noted), but the code path is provably exercised: Release never touches
`JUCE_DEBUG`'s dev-server branch at all, so this is the exact mechanism a
different computer would also use, not a Debug-only proxy for it.
Debug rebuilt and relaunched afterward too, confirming its own dev-server
path is genuinely untouched — the new copy step runs for Debug as well
(harmless; Debug's `JUCE_DEBUG` branch never calls `serveResource()` for
`/`, only for `/tap/...`). `ctest --test-dir build -C Debug` 577/577 green
throughout, unaffected (no engine-level change at all).

**Deliberately not built**: ARCHITECTURE.md §7's original full
binary-embedded end state (compiling `ui/dist` into the plugin binary
itself as `BinaryData`, so a single `.vst3` file needs no sibling `ui/`
folder at all) — this fix ships `ui/dist` ALONGSIDE the binary, which still
means two things to copy to another machine (`Bazalt.vst3` and its own
`Contents/Resources/ui/`, or `Bazalt.exe` and its own sibling `ui/`) rather
than one self-contained file. Real, separate, larger future work if a
single-file release is ever actually needed — nothing today requires it.

### Every plugin instance shared one fixed WebView2 profile folder — FIXED (2026-10-03)
*A real, DAW-relevant gap the Standalone app could never have surfaced
(it's always exactly one instance) — found by deliberately auditing for
real-host crash risks before testing on another machine, not from a live
reproduced crash.*

**The gap:** `PluginEditor.cpp`'s `makeWebViewOptions()` pointed every
editor's WebView2 at the exact same fixed path,
`%TEMP%/Bazalt/WebView2/`, with no per-instance distinction at all.
Microsoft's own WebView2 guidance is one user-data folder per running
environment; a shared, fixed path is a real correctness gap regardless of
the exact failure mode in any one host version — a real host routinely
runs several instances of the same plugin at once (multiple tracks in one
project), and separately, the Standalone app and a DAW's own VST3 instance
could easily be running concurrently as two unrelated OS processes pointing
at the same folder either way. Not reproduced as an actual crash here (no
second machine or multi-instance Ableton session available in this
environment to force it) — flagged and fixed as a real, plausible,
best-practice violation found by code audit, which is exactly what was
asked for ("research what could cause a crash... fix these things, if
any").

**Fix applied:** `BazaltAudioProcessor` gained a `const juce::Uuid
instanceId`, generated once per instance at construction
(`getInstanceId()`). `PluginEditor.cpp`'s WebView2 options now build the
user-data folder as `%TEMP%/Bazalt/WebView2/<instance-uuid>/` instead of
the one shared path — every instance, in every process, genuinely isolated.
A new instance (including the SAME plugin reloaded in a new session) gets a
fresh UUID and therefore a fresh folder; old instances' folders are left in
`%TEMP%` rather than deleted on unload (deleting a WebView2 profile
immediately at destruction risks racing WebView2's own async, not-
necessarily-synchronous teardown of resources tied to that folder — a real,
considered tradeoff, not an oversight: cheap, OS-cleaned temp-folder growth
over many sessions versus a genuine risk of deleting files a still-
shutting-down WebView2 instance has open).

**Also investigated, during the same audit, and ruled out — not bugs:**
- **State restored before the first `prepareToPlay`** (a real VST3 host
  doesn't guarantee call order): already correctly handled —
  `GraphEditController::recompileAndPublish()` checks its own `isPrepared`
  flag first and defers the real compile until `prepare()` eventually runs,
  rather than compiling against a not-yet-valid sample rate/block size.
- **Denormal numbers from the new long-decaying resonator feedback loops**
  (comb/modal/string/plate): `processBlock()` already wraps its entire body
  in `juce::ScopedNoDenormals`, present since well before this session —
  covers every new PM Core node's own feedback loop for free.
- **`MacroParameters`'s own triple-buffer mapping handoff**: theoretically
  requires 3 separate `setMappings()` calls to land within the same few-
  microsecond window as one `applyToPlans()` call to actually race — not
  achievable by anything this codebase's own call pattern produces
  (`setMappings()` only ever fires once per discrete, UI/API-paced graph
  edit, never in a tight loop). The repeated "Parameter thread safety"
  pluginval timeout this project's own history notes several times (always
  confirmed "environmental" via `git stash` A/B comparison) most likely
  reflects pluginval's own known stress-test flakiness on a loaded dev
  machine, not a bug in this code — inspected directly here rather than
  re-asserting the prior dismissal at face value, since `pluginval` itself
  isn't installed in this environment to re-run and settle it conclusively.

**Verified:** full rebuild clean (Debug and Release, both VST3 and
Standalone). New regression test
(`tests-plugin/BusLayoutTests.cpp`, "Two processor instances get distinct
identities") confirms two separately-constructed processors never collide.
`ctest --test-dir build -C Debug` 578/578 green. Release Standalone
launched, confirmed a genuinely new, uniquely-named subfolder appears under
`%TEMP%/Bazalt/WebView2/` for that run.

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
