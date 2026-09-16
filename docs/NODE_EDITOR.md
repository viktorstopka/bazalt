# Bazalt — Node Editor Design (M7+)

Status: **planning document, awaiting approval**. No node-editor code has been written against
this yet. Produced per `docs/bazalt_node_editor_prompt_v3.md` ("the blueprint" below), grounded in
the actual M0–M4 codebase rather than the architecture proposal alone. Design reference:
`docs/Frame 1 Bazalt.png` (the blueprint calls it `docs/design/Frame_1.png` — same image, different
path; referenced here by its real location).

This document assumes M5 (canvas & analysis panel) and M6 (testing/tooling hardening) are done, per
the blueprint's own stated sequencing ("this phase starts after M6"). Read together with
`docs/ARCHITECTURE.md` and `CLAUDE.md`; it doesn't repeat rules from those, only what's new or what
changes.

---

## 1. Scope

Unchanged from the blueprint §1 — repeating only the load-bearing line: **the UI never hardcodes a
node type**, **the engine owns the graph**, the UI edits it only through commands. Out of scope
(specific instrument/effect DSP, node groups, LOD rendering, colour-blind accessibility, real
Assist-menu recipe content) stays out of scope; extension points are called out below where they
apply.

---

## 2. What exists today vs. what the node editor needs

This is the grounding pass. Everything in this section is read directly from the M0–M4 code, not
inferred from the architecture doc.

| Concern | Exists today | Node editor needs |
|---|---|---|
| Editable graph | `NodeGraph` (`engine/include/bazalt/engine/graph/NodeGraph.h`): `NodeInstance{id, type, parameters: map<string,float>}`, `Connection{fromNodeId, fromPortIndex, toNodeId, toPortIndex}` — **ports addressed by index, not ID** (the header itself flags this as a known future change) | Port-ID-addressed connections; a generic (non-float) property bag per node for text/enum/blob values; per-node position |
| Node metadata | `Node::getInputPorts/getOutputPorts/getParameters()` return `PortDescriptor{id, type}` / `ParameterDescriptor{id, min, max, default, skew, unit, displayName}` — parameters are richly described, **ports are not** (no unit, no min/max, no primary-output flag, no integer/scaling) | Blueprint §2's full port metadata; node-level metadata (title, category, layout variant, icon) that `Node` doesn't expose at all today |
| Node type registry | `NodeFactory`: `registerType`/`create`/`isRegistered` — construct-only, no "list all registered types and their descriptors" query | Descriptor enumeration for the Add menu, without instantiating live DSP state on the audio thread |
| Signal types | `SignalType{Audio, Control, Event, Note, Spectral}` (`SignalType.h`) — an engine-level buffer-shape/timing tag | Blueprint's 6-colour UI taxonomy (Audio, Modulation, Value, Integer, Trigger, Boolean) — not a 1:1 match, see §5 |
| Graph→plan pipeline | `GraphCompiler::compile(NodeGraph, NodeFactory, NodePrepareInfo, generation) → CompileResult`; `PlanSwapper` (lock-free, tested in M2) — **built but not wired into `PluginProcessor`**; today's graph compiles exactly once, in `prepareToPlay`, never again | A live recompile-and-swap path triggered by UI commands |
| Voice/global domains | Only a **per-voice** graph exists in code (`buildVoiceProofGraph()`, compiled 8 times, once per voice, via `VoiceManager`). Sidechain mixing and macro application are hand-written C++ in `PluginProcessor.cpp`, not graph nodes. There is no "global" `NodeGraph` and no boundary node between the two domains | A single editable graph spanning both domains (voice nodes, a boundary, global nodes) matching `Frame 1 Bazalt.png`'s "Sum Voices → Master Out" |
| Patch format | `PatchDocument` (schema v1): graph + macro mappings/values + meta. **No `ui` section** — no positions, frames, headers, images, pan/zoom (this is a documented, deliberate gap: `PatchDocument.h`'s own comment says "add in the same commit as the editor") | Everything the blueprint's serialisation requirement (§2) lists |
| UI↔engine bridge | `PluginEditor` already calls `.withNativeIntegrationEnabled()` (JUCE's `window.__JUCE__.backend`), but **registers no `withNativeFunction`/`withEventListener`** — the only existing transport is the M4 telemetry `fetch()` resource-provider, which is pull-only and one-directional (UI→engine has no channel at all yet) | A command channel, UI→engine, plus an engine→UI notification channel (reconciliation, errors) |
| Telemetry | `TelemetryHub` creates a **fixed** small tap set (`main`, `aux1`–`aux4`) once in `prepareToPlay`; `AnalysisThread` drains all of them unconditionally every ~10ms | Many small, dynamic, on-demand taps (per visible node/connection/parameter), subscribed/unsubscribed as the viewport changes |
| UI stack | React 19 + Vite 8 + TS 6, no state library, no Canvas/WebGL yet, `ui/src/App.tsx` is still the M0 placeholder plus the M4 benchmark spike (to be deleted) | Command-driven state store, canvas/WebGL rendering surface, design tokens |
| 8 existing node types | `osc.basic`, `filter.svf`, `env.adsr`, `amp.vca`, `noise.burst`, `mix.add2`, `delay.basic`, `filter.onepole` — all DSP, none are the blueprint's utility nodes | `Constant`, `Map`, `Mix` (n-ary, or reuse `mix.add2`), `Add`, `Multiply`, `Reroute`, `Listen`, `Output` |

None of this is a criticism of M0–M4 — `ARCHITECTURE.md` explicitly deferred all of it ("MVP does
not ship a node-graph editor," §14) and even named several of these as exactly the next step
(`PatchDocument.h`'s `ui`-section comment, `NodeGraph`'s port-index comment). This table is that
deferred work, now itemized.

---

## 3. Node descriptor schema

Extends, rather than replaces, the existing `PortDescriptor`/`ParameterDescriptor`/`Node` metadata
so the 8 existing nodes keep compiling with only additive changes.

```cpp
// PortDescriptor.h — additive fields, all defaulted
struct PortDescriptor
{
    juce::String id;              // unchanged, stable, hand-assigned
    SignalType type;               // unchanged (engine-level tag, see §5)
    juce::String label;            // NEW — display name, defaults to id if empty
    bool isPrimaryOutput = false;  // NEW — drives Alt-drag Mix/Add/Multiply (§7) and
                                    //   the "primary output value indicator" on horizontal nodes
    // Numeric-value metadata (meaningful when type == Control with a "Value"/"Integer"
    // UI classification, see §5) — all optional/defaulted, absent for Audio/Trigger/Boolean ports:
    juce::String unit;
    std::optional<float> minValue;
    std::optional<float> maxValue;
    float defaultValue = 0.0f;
    bool isInteger = false;
    bool isLogScale = false;
};
```

```cpp
// Node.h — new virtuals, all with defaults so the 8 M1/M2 nodes need no changes to keep
// building; filling in real values for them is cheap and happens in the same milestone
// that adds these (M9, see §11), since a usable Add-menu needs real titles/categories.
class Node
{
    // ...existing interface unchanged...
    virtual juce::String getTitle() const { return {}; }        // falls back to type ID in the UI
    virtual juce::String getCategory() const { return "Uncategorized"; }
    virtual NodeLayoutVariant getLayoutVariant() const { return NodeLayoutVariant::Standard; }
    virtual juce::String getIcon() const { return {}; }
};

enum class NodeLayoutVariant { Standard, Horizontal, Singleton, Decoration };
```

`NodeFactory` gets a **non-instantiating-on-the-audio-thread** descriptor query: construct a
throwaway `Node` (message thread only, same cost class as compiling a plan — never called from
`process()`), read its metadata methods, discard it. This needs no new mechanism, just a method that
does what `GraphCompiler` already does per-node, once, for every registered type:

```cpp
struct NodeDescriptor
{
    juce::String typeId;
    juce::String title, category, icon;
    NodeLayoutVariant layoutVariant;
    std::vector<PortDescriptor> inputs, outputs;
    std::vector<ParameterDescriptor> parameters;
};
std::vector<NodeDescriptor> NodeFactory::describeAll() const; // NEW
```

Mock (UI-only) descriptors use the identical `NodeDescriptor` shape, sourced from a static JSON/TS
table instead of `NodeFactory` — one schema, two producers, exactly as the blueprint requires.
The JS side receives this as JSON once at editor load (over the command bridge, §6), not per-frame.

---

## 4. Generic node properties (non-float configuration)

`NodeInstance::parameters` is `unordered_map<juce::String, float>` — right for DSP-smoothed values,
wrong for a Frame's label text, a Macro's Enum value list, an Image's embedded asset reference, or a
placeholder dropdown's selected option string. Recommend a second, parallel bag using `juce::var`
(already used by `PatchSerializer`'s JSON round-trip, so no new dependency):

```cpp
struct NodeInstance
{
    juce::String id, type;
    juce::Point<float> position;                      // NEW — see §8
    std::unordered_map<juce::String, float> parameters;        // unchanged: DSP-facing, smoothed
    std::unordered_map<juce::String, juce::var> properties;    // NEW: text, enum selection,
                                                                 //   image data, Macro constraints
};
```

Decorations (Frame, Header, Image, and the Knob/Reroute utility) are `NodeInstance`s with
`layoutVariant == Decoration` and type IDs like `deco.frame`/`deco.header`/`deco.image`/
`util.reroute` — **not** a separate list or a special case in the graph model, matching
`Frame 1 Bazalt.png`'s own annotation ("A simple Frame. Can be added same way as nodes"). Frame/
Header/Image don't participate in `GraphCompiler` scheduling at all (no ports that carry signal;
`util.reroute` does have one in/many-out and *does* get scheduled, trivially). `GraphCompiler` skips
any node whose `layoutVariant == Decoration` when building the schedule.

---

## 5. Port type taxonomy: engine `SignalType` vs. UI palette

The blueprint's 6-colour table (Audio/Modulation/Value/Integer/Trigger/Boolean) does not map 1:1
onto the engine's 5-value `SignalType` (Audio/Control/Event/Note/Spectral). Recommendation:

- **Keep `SignalType` as the engine-level tag** (buffer shape / timing semantics) — this is a
  compiler-facing concept (does this port need a `blockBuffers` slot or a `regionScalars` slot,
  does it participate in per-sample regions) and shouldn't be redefined around UI colour needs.
- **Add one real value: `SignalType::Boolean`.** A boolean gate/flag is genuinely different at the
  buffer level from a continuous `Control` value (no smoothing, no skew), so this isn't a UI-only
  distinction — it earns a real engine tag. Small, additive, no migration needed (nothing currently
  emits it).
- **`Modulation`, `Value`, `Integer` are UI-level classifications derived from `SignalType::Control` +
  `PortDescriptor`'s new numeric metadata (§3), not new `SignalType` values:**
  - `Control` port with no `unit` and (`minValue`,`maxValue`) = (0,1) → renders as **Modulation** (orange).
  - `Control` port with a `unit` or a range outside 0–1 → renders as **Value** (white).
  - Either of the above with `isInteger == true` → renders as **Integer** (yellow) instead.
  - `SignalType::Event` → **Trigger** (violet).
  - `SignalType::Boolean` (new) → **Boolean** (blue).
  - `SignalType::Audio` → **Audio** (pink/magenta), unchanged.
  - `SignalType::Note`/`Spectral` have no UI port rendering yet (unused by any port today, per
    `CLAUDE.md`) — out of scope until something emits them.

This keeps the engine's semantics honest (a port that's actually boolean says so at the type level;
a port that's actually a real-unit value says so via its own metadata) while giving the UI exactly
the 6-way palette without inventing parallel type systems that can drift apart.

**Connecting Modulation → Value auto-inserts a Map node** (blueprint §4): mechanically, this means
`connect` command validation checks the UI classification (not raw `SignalType`) of both ends; if
source is Modulation-classified and target is Value-classified, the command that reaches the engine
is a composite (`addNode(map) + connect(source, map.in) + connect(map.out, target)`, seeded with the
target's `minValue`/`maxValue`) — one undo step, per §6.

---

## 6. Command API and bridge

**Transport**: JUCE's existing `WebBrowserComponent::Options::withNativeFunction` /
`withEventListener` / `emitEvent` mechanism (already available — `withNativeIntegrationEnabled()` is
already on in `PluginEditor`, just unused). This is a **separate transport from telemetry on
purpose**: telemetry is high-rate, pull-based, best-effort-fresh (§6.3 of `ARCHITECTURE.md`,
ADR-0005); commands are low-rate, push/RPC-shaped, and need reliable delivery + a return value
(success/failure/rejection reason). Using the resource-provider `fetch()` path for commands, or the
native-function path for telemetry, would be forcing one mechanism to do two jobs it isn't shaped
for — record this pairing as its own ADR (§11).

**Command list** (message thread only; every command is a `NodeGraph` mutation followed by a
recompile+swap, or a pure view-state mutation that never touches the graph):

```
addNode(typeId, position) -> nodeId
deleteNodes(nodeIds[])
connect(fromNodeId, fromPortId, toNodeId, toPortId) -> connectionId   // may expand to a
                                                                        //   composite w/ auto-Map, §5
disconnect(connectionId)
spliceInsert(connectionId, newNodeTypeId, position) -> nodeId          // one undo step
moveNodes(nodeId -> position, ...)                                    // coalesced while dragging
setParameterValue(nodeId, parameterId, value)                         // coalesced while dragging
setProperty(nodeId, propertyKey, value)                                // rename, Macro constraints, ...
setBypassed(nodeId, bool)
setListening(nodeId, bool)                                             // temporary Listen node, §7
unwrap(nodeId, portId) -> nodeId[]                                     // placeholder/Constant -> real nodes
addFrame/addHeader/addImage(...), resizeFrame(...), etc.               // decoration commands
setViewState(pan, zoom)                                                // no recompile, no undo step
```

**Engine-side handling**: a `GraphEditController` (new, `plugin/source/`, message-thread-only —
this is exactly where `tests-plugin/`'s existing rule-4 exception already lives, so it belongs next
to `PluginProcessor`, not in `engine/`) owns the live `NodeGraph`, applies commands to it, and on any
graph-shaped command: recompiles via `GraphCompiler::compile` for the voice-domain subgraph (×8, one
per voice — cheap, and each voice's `PlanSwapper::publish` is independently lock-free per ADR-0003;
publishing 8 times for one edit is not "atomic across voices" but is **individually glitch-free per
voice**, and since every voice shares the same topology there's no audible divergence — worth
stating explicitly rather than leaving as an implicit assumption) and the global-domain subgraph
(×1). A rejected compile (per ADR-0003/`CLAUDE.md` rule 5) leaves the prior plan live and the
command's promise/completion resolves with a rejection reason — the UI rolls back optimistically
and shows the error banner (§10).

**Undo/redo**: a linear command log on the UI side (per blueprint §2) is enough — every command
above is already expressed as a discrete, serializable op, so undo replays the inverse (most have an
obvious inverse; `addNode`'s inverse is `deleteNodes`, etc.) rather than needing engine-side
undo-awareness. Continuous gestures (`moveNodes` while dragging, `setParameterValue` while dragging
a slider) coalesce client-side into one log entry on gesture end, matching Blender/Figma-style
undo grouping — the engine only ever sees the final committed value plus (optionally, for live
preview during the drag) uncommitted intermediate `setParameterValue` calls that don't themselves
each get a log entry.

**Optimistic UI**: the JS-side graph store applies a command's effect immediately (so dragging feels
instant) and reconciles against the engine's confirmed state, which arrives either as the
`withNativeFunction` call's own completion callback (success/failure + canonical new state for that
op) or as an out-of-band `emitEvent` (e.g. a macro automated from the host moves a mapped node
parameter — the UI needs to hear about that even though it didn't originate the change).

---

## 7. Domain model: one editable graph, voice and global

The single highest-leverage engine change this phase needs, and the thing every other milestone
below assumes exists. Today there is no single graph spanning both domains — replace the fully
hardcoded voice/global split with:

- One `NodeGraph` the user actually edits.
- A new builtin node type, `util.voiceSum` (the "Sum Voices" node in `Frame 1 Bazalt.png`), which is
  the explicit, visible boundary: everything upstream of it (reachable backwards from its input)
  compiles into the **per-voice** domain (×8 `ExecutionPlan`s, current behaviour); everything
  downstream (reachable forwards from its output, including `util.voiceSum` itself) compiles into
  the **one global** `ExecutionPlan`. `GraphCompiler` needs a domain-partitioning pass before its
  existing per-domain compile (reachability from `util.voiceSum`, not a new concept — reuses the
  same graph-traversal machinery the cycle detector already has).
- A `Note`/MIDI source is implicitly available inside the voice domain (today's
  `PluginProcessor::handleMidiEvent` pokes voice nodes directly via `ExecutionPlan::getNodeById()` —
  `CLAUDE.md`'s documented interim simplification). This phase does **not** need to resolve full
  Note-typed port routing (still explicitly out of scope, unchanged) — `util.voiceSum` only needs to
  know which nodes are upstream of it, not how they receive MIDI.
- Sidechain aux inputs and macro application, currently hand-written in `PluginProcessor.cpp`,
  become ordinary global-domain nodes (`Audio In` reading a given aux bus, per `Frame 1 Bazalt.png`)
  once this exists — not required for M7 itself, but this is the change that makes it possible
  later, and is why the domain boundary is the thing to get right first.

---

## 8. Patch format v2

`PatchDocument.schemaVersion` bumps to 2, migrated via the existing (currently-empty) dispatcher in
`PatchSerializer`. New/changed fields:

```cpp
struct PatchDocument
{
    // ...existing fields...
    // Connection gains fromPortId/toPortId (string) alongside the existing index fields during
    // migration — v1→v2 resolves indices to IDs using each node's *current* registered-type port
    // order at migration time (the only source of truth available for old data); flag any patch
    // saved before a node's ports were reordered as a real risk worth a one-line warning in the
    // migration, not solved by the migration itself.
    struct ViewState { float panX = 0, panY = 0, zoom = 1.0f; };
    ViewState view;
    // Frames/headers/images are just NodeInstances (layoutVariant == Decoration, §4) and already
    // round-trip through `nodes` — no separate list needed.
};
```

`NodeInstance.position` (§4) is per-node and travels with the node itself, matching
`ARCHITECTURE.md` §3.1's own description of `NodeGraph` ("nodes... UI position") — the code has
simply never implemented that part of the description until now.

---

## 9. Telemetry: many small, dynamic taps

Extend `TelemetryHub` (currently a fixed 5-tap set created once in `prepareToPlay`) with
message-thread-owned dynamic registration:

```cpp
Tap* TelemetryHub::subscribeTap (const juce::String& tapId, size_t capacity);  // idempotent
void TelemetryHub::unsubscribeTap (const juce::String& tapId);
```

- Tap IDs are synthesized per what's being watched: `node:<nodeId>:preview`,
  `connection:<connectionId>:activity`, `param:<nodeId>:<paramId>:value` — same binary
  `TelemetryFrame` format from M4, no new wire format.
- The UI subscribes/unsubscribes as commands (§6), driven by viewport visibility (subscribe on
  entering the visible region + a small margin, unsubscribe some time after leaving it — exact
  debounce is a UI-side tuning knob, not an engine concern).
- `TelemetryHub` needs a hard cap on concurrent taps (preallocated pool, no runtime allocation on
  the audio thread — same RT-safety rule as everything else) and a documented eviction policy for
  when the cap is hit (LRU by last-subscribed time is the simplest correct default; revisit only if
  it causes visibly wrong behaviour). `AnalysisThread`'s drain loop needs a **per-frame telemetry
  budget** (blueprint §3) — degrade via lower per-tap drain rate or coarser oscilloscope decimation
  before ever dropping a whole tap's frames.
- This is exactly the piece the blueprint asks to prove **early** (§9 planning rules) — see M8.

---

## 10. Rendering architecture (proposal, to be benchmarked)

Proposed split, matching `ARCHITECTURE.md` §7's existing "React for chrome, Canvas/WebGL for
anything at audio/interaction rate" principle, made concrete for the node editor:

- **WebGL** (not Canvas2D — `ARCHITECTURE.md` §7 leaves this open, but the 120fps/500-node/
  1000-cable target with per-cable telemetry-driven activity strongly favours WebGL's batched
  draw-call model over Canvas2D's per-shape immediate-mode cost): the dot/line grid (already M5),
  cables (bezier, colour + activity-driven glow/opacity), inline previews (waveform/stepped/envelope/
  LFO-phase — all just small texture or line-strip draws), node-body backgrounds/borders for the
  common case.
- **DOM** (React, but reading from the same non-React telemetry/graph store `ARCHITECTURE.md` §7
  already specifies): node titles, port labels, parameter number/text inputs, dropdowns, the
  Constraints/Assist popovers, context menus — anything needing real text layout, IME, accessibility
  tree, or complex hit-testing that's cheap in DOM and expensive to reimplement in WebGL.
- Node bodies are therefore **hybrid**: a WebGL-drawn background/border positioned under an
  absolutely-positioned DOM overlay for the interactive/text parts, kept in sync via the same
  transform (pan/zoom) applied to both layers once per frame — not re-derived per node.

This is a hypothesis, not a decision — §11's ADR is where it gets confirmed or revised, against the
stress-test patch generator, before any real node-editor visuals are built on top of it (blueprint
§9: prove performance architecture early).

**Wire-feedback colours** (blueprint §6.3 explicitly asks for a proposal since green is taken by the
poly-audio placeholder): reuse the *source port's own type colour* for a valid in-progress wire and
for a valid splice-target highlight (so a drag from an Audio output previews pink, from Modulation
previews orange, etc. — no new hue needed, and it previews the colour the finished cable will
actually have); "not yet over a port" is the same colour at reduced opacity; "rejected" is a fixed
colour reused from the existing error-circle red plus a broken/dashed line style and a blocked
cursor, so rejection is never colour-only. **Listen node cables**: drop the fixed green from the
prototype (green stays reserved for the poly-audio placeholder) and use the source port's type
colour (almost always Audio pink) with the dotted line style as the sole "this is a Listen tap"
signal — orthogonal to the activity animation (which becomes a brightness pulse along the same
dotted line), so the two never collide.

---

## 11. Deliverables → milestones, ADRs

New ADRs to seed during M7–M9 (mirroring `docs/decisions/000N-*.md` style):
- `0006-command-bridge-transport.md` — the native-function/event-listener choice, why it's separate
  from the telemetry transport (§6).
- `0007-node-descriptor-schema.md` — §3/§4's schema, once implemented.
- `0008-graph-rendering-split.md` — §10's WebGL/DOM split, with the actual benchmark numbers.
- `0009-dynamic-telemetry-subscription.md` — §9, once implemented.
- `0010-wire-feedback-colours.md` — §10's colour proposal, once confirmed against the real palette.

See `docs/MILESTONES.md` M7–M13 for the concrete, ordered build-out (appended in this same change).

---

## 12. Conflicts and open questions

Numbered for reference; each has a recommended resolution above (cross-referenced) — none of these
block starting M7, but all of them are places this document overrides or extends something the
existing code/docs currently say, and are worth an explicit yes before implementation starts.

1. **Port-index vs. port-ID connections.** `NodeGraph::Connection` is index-addressed today; the
   node editor needs ID-addressed connections (splice/reconnect reason about ports by identity, and
   index-based serialization breaks if a node's port order ever changes). *Recommendation*: §8's
   patch v2 migration, §2's table. Low risk — the current code already flags this as expected.
2. **`SignalType` vs. the UI's 6-colour palette.** Not a 1:1 mapping. *Recommendation*: §5 — add one
   real engine value (`Boolean`); derive Modulation/Value/Integer from `Control` + port metadata,
   not new engine types.
3. **No node position/view-state storage exists anywhere.** *Recommendation*: §4/§8 — position lives
   on `NodeInstance` (matches `ARCHITECTURE.md`'s existing description of `NodeGraph`, just not yet
   implemented); frames/headers/images/pan/zoom live in patch v2.
4. **`Node` has no title/category/layout-variant/icon metadata**, so a real Add-menu can't be built
   from it as-is. *Recommendation*: §3 — additive virtuals with safe defaults, retrofit the 8
   existing nodes (cheap, same milestone).
5. **No editable graph spans both the voice and global domains** — today's "graph" is entirely
   hardcoded and voice-only; sidechain mixing/macros are hand-written C++, not nodes. This is the
   single biggest gap and everything else is comparatively mechanical once it's resolved.
   *Recommendation*: §7 — one graph, an explicit `util.voiceSum` boundary node, a compiler
   domain-partitioning pass. Flagging prominently: this is the item most worth discussing before
   M7 starts, since it reshapes `PluginProcessor`'s current voice-rendering path.
6. **`PlanSwapper` is built/tested (M2) but wired nowhere** — no live recompile has ever happened
   outside `prepareToPlay`. *Recommendation*: §6/§7 — wire one `PlanSwapper` per voice + one for the
   global plan; publishing isn't atomic across voices but is individually glitch-free and
   topologically identical across voices, so this is safe, not a race — worth stating in the ADR
   explicitly rather than leaving implicit.
7. **No UI→engine channel exists at all** (telemetry is pull-only, one-directional). JUCE's
   `withNativeFunction`/`withEventListener`/`emitEvent` are available and partially already enabled
   (`withNativeIntegrationEnabled()`) but unused. *Recommendation*: §6 — use them directly, keep this
   transport separate from telemetry's `fetch()` transport (different traffic shapes, different
   jobs).
8. **Telemetry's tap set is fixed and small**, sized for M4's proof, not for many dynamic per-node/
   per-connection/per-parameter taps. *Recommendation*: §9 — dynamic subscribe/unsubscribe with a
   capped pool and an LRU eviction policy; needs its own perf validation early (M8), per the
   blueprint's own planning rule.
9. **`NodeInstance::parameters` is float-only**, but decorations/Macros/placeholders need text/enum/
   blob-typed configuration (Frame label, Macro Enum value list, embedded Image data). *Recommendation*:
   §4 — a parallel `properties: map<string, juce::var>` bag, reusing the `juce::var` machinery
   `PatchSerializer` already has.
10. **Macro Constraints (Type/Shape/Enum) vs. ADR-0004's "always a plain host-facing float."**
    Changing a macro's declared Type mustn't reopen ADR-0004's "identical in every host" guarantee.
    *Recommendation*: keep the host-facing parameter a plain 0–1 float unconditionally; Type/Shape/
    Enum are engine/UI-side *interpretations* of that same float (bucketed for Enum, thresholded for
    Boolean, rounded for Integer) — the popover changes interpretation, never the underlying
    `AudioParameterFloat`. Surface the blueprint's requested "this affects host automation" warning
    when Enum bucket count or Type changes on a macro that already has host automation recorded (best
    effort — VST3 has no clean way to know that from the plugin side beyond "a value arrived
    recently").
