# Props edit: value contract, Macro/Constant redesign, port coloring, polarity cleanup

**Status:** In progress, started 2026-10-03. Five sequenced batches (A-E);
this header gets updated to "Built" once Batch E lands, same convention
`wiki/plans/UtilMacro.md` already established for this file family.

---

## 0. Origin

Direct instruction: a 9-item "big props edit" covering select-list spacing,
a real Boolean in-node control, a full `util.macro`/`util.constant` redesign
(multi-type, adaptive UI, shared components), un-truncated value precision,
curve-aware sliders, bounds-aware fill visibility, live-resolved port
coloring for polymorphic ports, and removing the Unipolar/Bipolar mode
selector from three nodes in favor of the already-working generic adapter
plus two new explicit converter nodes.

## 1. The common thread

Three parallel research passes (UI controls, polarity/adapter system,
Macro/Constant architecture) plus direct reads of every file in scope found
that most of this isn't new design — it's **finishing wiring that M14's
Value Contract (`wiki/NODES.System.md` §2) already put in place**.
`ValueKind::Bool` already exists on `PortDescriptor`/`ParameterDescriptor`.
`Curve`/`Quantity`/`Polarity` are already set correctly on real nodes —
`env.adsr`'s attack (`ValueTypes::timeSecondsPort`) has declared
`Curve::Logarithmic` since M14. The UI layer (`ValueSlider.tsx`,
`portUiKind.ts`, `NodeCard.tsx`) predates most of that and never went back
to read it. The other real piece of new design is `util.macro`/
`util.constant`, which predate `Enum`/`Bool` value kinds entirely.

## 2. Batches

### Batch A — finish wiring the value contract into existing controls
- A1: `TriggerSelect.css`'s dropdown row padding/line-height (cramped text).
- A2: `ValueSlider.tsx`'s `commit()` was rounding every committed float to
  the same 2 decimals used for *display* — i.e. lossily truncating the real,
  stored/engine value, not just how it's shown. Split storage (full
  precision) from display (`formatValue`, unchanged 2dp default).
- A3: Wire the already-declared `curve`/`isLogScale` field into
  `ValueSlider`'s drag/wheel/fill math (normalized-space curve warp) — this
  is the actual fix for "attack has to go to 10s and 0.05s is unreachable,"
  since the engine already marked that port `Curve::Logarithmic`.
- A4: `.value-slider-fill` rendered unconditionally; make it conditional on
  `hardMin !== undefined && hardMax !== undefined` (ports can genuinely be
  unbounded via `std::optional<float>`; parameters never are, by struct
  default) so a truly-unbounded port like `adapt.remap`'s shows no fill.
- A5: `classifyPortUiKind`'s Modulation (orange) classification was a
  numeric coincidence (`unit==='' && min===0 && max===1`), never reading
  `port.quantity` at all — so a real Bipolar -1..1 port rendered white, not
  orange. Switch to checking `quantity === 'unipolar' | 'bipolar'` directly.

### Batch B — real Boolean in-node control
New `ToggleSwitch` component; wired into `PortRow`'s `'boolean'` kind and
`ParameterRow`'s `kind === 'bool'`. Concrete beneficiaries found by reading
the code: `env.adsr`'s `gate` port already declares
`hasFallbackWhenUnconnected = true` and gets a toggle for free; added a new
`logic.toggle.initialState` Bool structural parameter to `LogicToggleNode.h`
(previously hardcoded `on = false`, no way to configure it).

### Batch C — polymorphic port coloring
`view.glance` and friends (`PortPolymorphism::SignalAndQuantity`) default to
Audio-colored even though they accept Audio/Control/Boolean/Event — make an
unresolved polymorphic port render white (`'value'`) by default. Also closed
a real asymmetry: `InfiniteCanvas.tsx`'s cables already recolor live from
`graphStore.ts`'s `endpointFor()`, but `NodeCard.tsx`'s own port dot/label
read a static per-typeId descriptor and never updated — so a wired Glance's
cable showed the live type while its own node-body dot stayed frozen at the
stale default. Both now resolve through the same live endpoint.

### Batch D — remove the Unipolar/Bipolar selector; add explicit converters
`random.stepped.polarity`, `seq.steps.range`, `data.lookup.polarity` removed
(three independent hand-rolled 2-line remaps). Confirmed before removing:
the generic `adapt.remap` fallback in `CanConnect.cpp`/`canConnect.ts`
already auto-resolves any Unipolar<->Bipolar mismatch today, so this is a
pure simplification, zero connectivity regression. Added `util.unipolarToBipolar`/
`util.bipolarToUnipolar` as small, explicit, manually-placed (never
auto-inserted) nodes for readability, matching the existing
`adapt.normalise`/`adapt.map`/`adapt.pitchToFrequency` convention of thin
dedicated wrappers over a more general mechanism.

### Batch E — Macro & Constant redesign
New shared `TypedValueNodeBase` (`MacroValueType`: Float/Int/Bool/Enum/
Trigger) that `MacroNode`/`ConstantNode` both inherit from — closing the
confirmed gap that these two shared zero code despite being "a Constant
that also binds" by `MacroNode.h`'s own description. A Bool-typed macro's
output genuinely declares `SignalType::Boolean` (not Control), so it
connects straight into a real Boolean port with no adapter — closing the
exact gap `graphStore.ts`'s `isMacroablePort()` comment documents today
(no Control->Boolean adapter exists). Trigger-typed macros stay fully
decoupled from `MacroParameters`' existing smoothed-level mechanism: the
node itself edge-detects a rising level and emits one pulse, so nothing
about the 32-slot pool/smoothing/mapping code needs to change. Constant
gets the same `type`/`min`/`max`/`quantity` contract (no `slot`, no
`Trigger` — a manual button there would cost a full recompile per press
with no relay to cheat through, a deliberate, justified scope cut) and
drops its old hardcoded ±100000 range for a genuinely unbounded default
with a sane `softMin`/`softMax` of ±10. New shared `ui/src/format/
valueFormat.ts` (decimals-by-quantity-or-step, MIDI note-name + cents
formatting for Pitch/Frequency macros in the top panel) and a shared
`TypedValueFields`/`MacroControl` component set used by both the top-bar
panel and each node's own in-node rows, and by both Macro and Constant.

## 3. Decisions made along the way (not re-litigated per-batch)

- Constant does not get a Trigger type (E4 above) — asymmetric cost vs.
  Macro's free relay-backed one.
- The two new polarity-converter nodes are deliberately NOT auto-inserted
  adapters — manual-only, per direct instruction.
- Agreed explicitly with removing the Unipolar/Bipolar selector when asked
  for an opinion: it was pure duplication the generic adapter already
  covered; built the two explicit nodes anyway for discoverability, in
  the same spirit as the project's other thin dedicated adapter nodes.

## 4. Build record

See `wiki/MILESTONES.md`'s dated entry for this arc for the actual
build/test/commit log per batch.
