// design/Macro.png / wiki/plans/PropsAndMacroRedesign.md Batch E: the
// Macro node's own custom body — replaces the barebones render (a plain
// StandardBody listing "Type"/"Min"/"Max"/"Quantity"/"Slot" as ordinary
// ParameterRows) with four fixed rows: the live value, a read-only type
// chip + "Edit T" button, a (placeholder, see below) "Display On Top?"
// toggle, and the output port. Dispatched from NodeCard.tsx by typeId, not
// a NodeLayoutVariant — see that file's own comment on why this is a
// client-side-only visual swap with no engine change.
//
// Rewritten per direct correction, 2026-10-03: the real type system is
// three top-level kinds — Control (with Value/Modulation/Int subtypes
// falling out of `quantity`/`isInteger`, Int itself toggled to Enum via
// `isEnum`), Trigger, Bool — not five parallel types, and "Unit" is never
// a separately-set field, only ever derived from `quantity`. See
// graphStore.ts's `MacroValueType`/`classifyMacroControlSubtype` doc
// comments for the full reasoning.
//
// This file reads/writes exactly what TypedValueNodeBase.h/MacroNode.h
// expose: the ordinary structural parameters (type/isInteger/isEnum/min/
// max/quantity/slot, via the same state.onParameterCommit every other
// node's rows already use) for everything EXCEPT the macro's own live
// value, which deliberately isn't a listed ParameterDescriptor at all
// (MacroNode.h's own comment: listing it would invite this row fighting
// host automation every block) — that one goes straight through the same
// JUCE WebSliderRelay mechanism MacroKnob.tsx already uses for the top-bar
// panel, by this node's own claimed `util.macro.slot`.
import { useEffect, useReducer, useRef, useState, type ReactNode } from 'react'
import { createPortal } from 'react-dom'
import { getSliderState } from '@juce-framework/webview'
import type { NodeDescriptor } from '../graph/descriptorTypes'
import { type NodeCardState, PortGlyph } from './NodeCard'
import { portUiStyle } from '../graph/portUiKind'
import { ValueSlider } from './ValueSlider'
import { TriggerSelect } from './TriggerSelect'
import { ToggleSwitch } from '../controls/ToggleSwitch'
import { Button } from '../controls/Button'
import { quantityFromOrdinal, macroTypeFromOrdinal, getEndpoint, type MacroValueType } from '../graph/graphStore'
import { macroTypeChipText } from '../format/valueFormat'
import { MacroEditTypeModal } from './MacroEditTypeModal'
import { tokens, withAlpha } from '../theme/tokens'
import './MacroBody.css'

const TRIGGER_PULSE_MS = 120

function clamp01(value: number): number {
  return Math.min(1, Math.max(0, value))
}

function paramVal(descriptor: NodeDescriptor, state: NodeCardState, id: string): number {
  const fallback = descriptor.parameters.find((p) => p.id === id)?.defaultValue ?? 0
  return state.parameterValues?.[id] ?? fallback
}

/** `stored` (GraphNode.macroEnumOptionLabels, user-edited via the "Edit T"
    modal) when its length matches the real option count; otherwise
    numbered placeholders — see GraphNode's own doc comment on why there's
    no real per-instance label storage engine-side yet. */
function enumOptionLabelsFor(count: number, stored: readonly string[] | undefined): string[] {
  const n = Math.max(1, count)
  if (stored && stored.length === n) return [...stored]
  return Array.from({ length: n }, (_, i) => stored?.[i] ?? `Option ${i + 1}`)
}

type RelayState = ReturnType<typeof getSliderState>

/** Live 0..1 relay subscription, same mechanism as MacroKnob.tsx's own
    (JUCE's WebSliderRelay/getSliderState) — see this file's own header
    comment for why the Value row can't go through the ordinary
    parameterValues/onParameterCommit path every other row here uses.
    Called unconditionally (Rules of Hooks) even with no real claimed slot
    or no live instanceId at all (the M9 gallery) — callers gate actual
    DISPLAY/interaction on `hasValidSlot`/`instanceId`, not this hook. */
function useMacroRelay(slot: number): RelayState {
  const relayName = `macro${slot + 1}`
  const state = getSliderState(relayName)
  const [, forceUpdate] = useReducer((n: number) => n + 1, 0)
  useEffect(() => {
    const valueListenerId = state.valueChangedEvent.addListener(forceUpdate)
    const propsListenerId = state.propertiesChangedEvent.addListener(forceUpdate)
    return () => {
      state.valueChangedEvent.removeListener(valueListenerId)
      state.propertiesChangedEvent.removeListener(propsListenerId)
    }
  }, [state])
  return state
}

/** Sets the relay's raw 0..1 fraction from a REAL value already in
    `min..max` — the inverse of MacroNode::processSample's own
    `min + raw*(max-min)` remap (MacroNode.h). Bracketed in a drag
    gesture exactly like MacroKnob.tsx's own drag, even for an
    instantaneous commit — JUCE's relay expects a start/end pair around
    any change for correct host-automation/undo grouping, not only a
    continuous drag. */
function commitRelayRealValue(relay: RelayState, realValue: number, min: number, max: number): void {
  const raw = max > min ? clamp01((realValue - min) / (max - min)) : 0
  relay.sliderDragStarted()
  relay.setNormalisedValue(raw)
  relay.sliderDragEnded()
}

/** MacroNode.h's own documented Trigger contract: "set the relay to 1,
    then back to 0 shortly after" reads as one brief ordinary parameter
    move, which `processSample`'s rising-edge detector turns into a single
    one-block pulse. One drag-gesture bracket spans both edges. */
function fireRelayTrigger(relay: RelayState): void {
  relay.sliderDragStarted()
  relay.setNormalisedValue(1)
  window.setTimeout(() => {
    relay.setNormalisedValue(0)
    relay.sliderDragEnded()
  }, TRIGGER_PULSE_MS)
}

function MacroTitleBar({ descriptor, state }: { descriptor: NodeDescriptor; state: NodeCardState }) {
  // No bypass/assist icon row at all (design/Macro.png shows none) — bypass
  // state still reads via the outer card's own `.node-card-bypassed`
  // opacity (applied below, same as every other variant), just without the
  // (currently non-interactive-by-click-elsewhere-anyway) icon glyph.
  return (
    <div className="node-title-bar">
      <span className="node-title macro-node-title">{descriptor.title || descriptor.typeId}</span>
      {state.error && (
        <span className="node-error-badge" title={state.error}>
          !
        </span>
      )}
    </div>
  )
}

/** Where a typed-value card's Value row reads and writes its value — the
    only thing that differs between a Macro (its host-automation relay, a
    0..1 slot shared with the DAW) and a Constant (its own ordinary
    parameter, util.constant.value). Real (not normalised) values both ways. */
interface ValueBinding {
  get: () => number
  commit: (value: number) => void
  /** A slider mid-drag (value) or released (null). */
  live?: (value: number | null) => void
  fire?: () => void
}

/** Row 1 (design/Macro.png table) — branches on `type`/`isInteger`/
    `isEnum`. Value and Modulation (both `!isInteger`) share the exact same
    ValueSlider widget, matching design/Macro.png's own "Breath Frequency"
    (Value) and "Children Ratio" (Modulation) rows, which only differ in
    colour/unit, never shape. `min`/`max`/`unit` come from the node's own
    OUTPUT PORT (`buildTypedOutputPort`, engine-computed), not raw stored
    parameters — that's what makes Modulation's implicit 0..1/-1..1 range
    and the quantity-derived unit correct here for free. */
function TypedValueRow({
  type,
  isInteger,
  isEnum,
  binding,
  min,
  max,
  unit,
  color,
  enumLabels,
}: {
  type: MacroValueType
  isInteger: boolean
  isEnum: boolean
  binding: ValueBinding
  min: number
  max: number
  unit: string
  color: string
  enumLabels: readonly string[] | undefined
}) {
  if (type === 'trigger') {
    return <Button label="Trigger" color={color} fullWidth onClick={() => binding.fire?.()} />
  }

  const displayValue = binding.get()

  if (type === 'bool') {
    const boolValue = displayValue >= 0.5 ? 1 : 0
    return <ToggleSwitch label="Value" value={boolValue} color={color} onCommit={(v) => binding.commit(v)} />
  }

  if (isInteger && isEnum) {
    const count = Math.max(1, Math.round(max - min) + 1)
    const labels = enumOptionLabelsFor(count, enumLabels)
    const selectedIndex = Math.min(count - 1, Math.max(0, Math.round(displayValue - min)))
    return (
      <TriggerSelect label="Value" options={labels} selectedIndex={selectedIndex} color={color} onCommit={(index) => binding.commit(min + index)} />
    )
  }

  // Value, Modulation, or plain Int — one widget, min/max/unit already
  // carry whatever distinguishes them.
  return (
    <ValueSlider
      label="Value"
      value={displayValue}
      min={min}
      max={max}
      hardMin={min}
      hardMax={max}
      isInteger={isInteger}
      unit={unit}
      color={color}
      onCommit={(v) => binding.commit(v)}
      onLiveChange={binding.live}
    />
  )
}

/** The design/Macro.png card, shared by every TypedValueNodeBase node
    (util.macro, util.constant): title, Value row, type chip + Edit Type,
    output. `binding` is null when there is nothing to bind to yet (the
    gallery, or a macro without a claimed slot). `extraRows` is the Macro's
    own "Display On Top?" — nothing else differs. */
function TypedValueCard({
  descriptor,
  state,
  instanceId,
  binding,
  extraRows,
  className,
}: {
  descriptor: NodeDescriptor
  state: NodeCardState
  instanceId?: string
  binding: ValueBinding | null
  extraRows?: ReactNode
  className: string
}) {
  const [editTypeOpen, setEditTypeOpen] = useState(false)
  const prefix = descriptor.typeId

  const macroType = macroTypeFromOrdinal(paramVal(descriptor, state, `${prefix}.type`))
  const isInteger = paramVal(descriptor, state, `${prefix}.isInteger`) >= 0.5
  const isEnum = paramVal(descriptor, state, `${prefix}.isEnum`) >= 0.5
  const rawMin = paramVal(descriptor, state, `${prefix}.min`)
  const rawMax = paramVal(descriptor, state, `${prefix}.max`)
  const quantity = quantityFromOrdinal(paramVal(descriptor, state, `${prefix}.quantity`))

  // Live-resolved (graphStore.ts's endpointFor/getEndpoint), NOT
  // `descriptor.outputs[0]` directly — that static descriptor is a
  // one-shot default-constructed-instance snapshot (fetchNodeDescriptors.ts
  // fetches it once, at editor load) and would otherwise show every node
  // as Control/Dimensionless/white forever, regardless of its real
  // configured type (direct feedback, 2026-10-03: "why is the macro value
  // output not adapting to the color" — a real, now-fixed bug). Falls back
  // to the static port with no live instance to resolve against (the M9
  // gallery), same as every other port lookup in this codebase.
  const outputPort = (instanceId && getEndpoint(instanceId, 'out', 'output')?.port) || descriptor.outputs[0]
  const color = portUiStyle(outputPort).color
  const outputLabel = macroType === 'trigger' ? 'Trigger' : 'Value'
  const outputConnected = state.connectedPortIds?.has(outputPort.id) ?? false
  // The LIVE-resolved effective min/max/unit (same resolution as
  // `outputPort` above) — not the raw stored parameters above — so
  // Modulation's implicit 0..1/-1..1 range and the quantity-derived unit
  // are correct here for free, matching exactly what processSample()
  // actually remaps through.
  const effectiveMin = outputPort.minValue ?? rawMin
  const effectiveMax = outputPort.maxValue ?? rawMax
  const unit = outputPort.unit

  const classNames = ['node-card', className, state.selected && 'node-card-selected', state.bypassed && 'node-card-bypassed', state.listening && 'node-card-listening', state.error && 'node-card-error']
    .filter(Boolean)
    .join(' ')

  const chipText = macroTypeChipText(macroType, isInteger, isEnum, quantity, rawMin, rawMax)

  return (
    <div className={classNames}>
      <MacroTitleBar descriptor={descriptor} state={state} />

      <div className="node-row">
        {binding ? (
          <TypedValueRow
            type={macroType}
            isInteger={isInteger}
            isEnum={isEnum}
            binding={binding}
            min={effectiveMin}
            max={effectiveMax}
            unit={unit}
            color={color}
            enumLabels={state.macroEnumOptionLabels}
          />
        ) : (
          // Nothing to bind to yet — no interactive control, just a quiet
          // static placeholder.
          <div className="macro-field-static" style={{ borderColor: withAlpha(color, tokens.opacity.border), color }}>
            <span>Value</span>
            <span>—</span>
          </div>
        )}
      </div>

      <div className="node-row">
        <div className="macro-type-chip" style={{ borderColor: withAlpha(color, tokens.opacity.border), color }} title={chipText}>
          {chipText}
        </div>
        <Button label="Edit T" onClick={() => setEditTypeOpen(true)} title="Configure this node's type" />
      </div>

      {extraRows}

      <div className="node-row node-row-port node-row-output">
        <span style={{ color }}>{outputLabel}</span>
        <PortGlyph port={outputPort} side="right" instanceId={instanceId} connected={outputConnected} />
      </div>

      {editTypeOpen &&
        // Portalled (not rendered inline) — this node lives inside
        // InfiniteCanvas's pan/zoom-transformed `.infinite-canvas-world`
        // layer, and a `position: fixed` descendant of a transformed
        // ancestor positions relative to THAT ancestor, not the viewport
        // (the exact reason NodeContextMenu already portals through this
        // same `overlayTarget`, GraphSurface.tsx's screen-space overlay
        // div). Falls back to `document.body` when there's no live canvas
        // at all (the M9 gallery) rather than not rendering.
        createPortal(
          <MacroEditTypeModal
            descriptor={descriptor}
            state={state}
            macroType={macroType}
            isInteger={isInteger}
            isEnum={isEnum}
            min={rawMin}
            max={rawMax}
            quantity={quantity}
            enumLabels={state.macroEnumOptionLabels}
            onClose={() => setEditTypeOpen(false)}
          />,
          state.overlayTarget ?? document.body,
        )}
    </div>
  )
}

export function MacroBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const [displayOnTop, setDisplayOnTop] = useState(false)
  const slot = Math.round(paramVal(descriptor, state, 'util.macro.slot'))
  const hasValidSlot = slot >= 0 && slot < 32

  // Always called (Rules of Hooks) — see useMacroRelay's own comment on why
  // an invalid/absent slot still gets a (unused) subscription rather than a
  // conditional hook call.
  const relay = useMacroRelay(hasValidSlot ? slot : 0)
  // Open while the Value slider is being dragged: the relay (the host slot)
  // follows the drag, so the macro's output — and everything it feeds —
  // moves in real time, not only on release. One host gesture per drag.
  const relayDragOpen = useRef(false)

  // A macro's value lives in its host-automation slot, normalised 0..1
  // against the macro's own effective range.
  const outputPort = (instanceId && getEndpoint(instanceId, 'out', 'output')?.port) || descriptor.outputs[0]
  const min = outputPort.minValue ?? paramVal(descriptor, state, 'util.macro.min')
  const max = outputPort.maxValue ?? paramVal(descriptor, state, 'util.macro.max')
  const binding: ValueBinding | null =
    instanceId && hasValidSlot
      ? {
          get: () => min + clamp01(relay.getNormalisedValue()) * (max - min),
          commit: (value) => commitRelayRealValue(relay, value, min, max),
          live: (value) => {
            if (value === null) {
              if (relayDragOpen.current) relay.sliderDragEnded()
              relayDragOpen.current = false
              return
            }
            if (!relayDragOpen.current) {
              relay.sliderDragStarted()
              relayDragOpen.current = true
            }
            relay.setNormalisedValue(max > min ? clamp01((value - min) / (max - min)) : 0)
          },
          fire: () => fireRelayTrigger(relay),
        }
      : null

  return (
    <TypedValueCard
      descriptor={descriptor}
      state={state}
      instanceId={instanceId}
      binding={binding}
      className="node-card-macro"
      extraRows={
        <div className="node-row">
          {/* Placeholder (direct instruction, 2026-10-03): flips local state
              only, not persisted, drives nothing — "no macro is displayed on
              top" for now; see App.tsx's own comment on the disabled panel
              this will eventually control. Always `portBoolean` (blue), NOT
              the macro's own configured type colour — direct feedback,
              2026-10-03: "Display on top is bool. Why is it not blue?" */}
          <ToggleSwitch label="Display On Top?" value={displayOnTop ? 1 : 0} color={tokens.color.portBoolean} onCommit={(v) => setDisplayOnTop(v >= 0.5)} />
        </div>
      }
    />
  )
}

/** util.constant — the same card as a Macro (direct instruction, 2026-10-04:
    "Modify the constant node, so it matches more a macro node, just without
    the macro functionalities"): no host-automation slot, no "Display On
    Top?". Its value is its own ordinary parameter, so dragging it plays live
    like every other slider (graphStore.ts's setParameterLive), and it keeps
    the ordinary solid node border — the dashed border is what marks a
    Macro, the one node the host can reach. */
export function ConstantBody({ descriptor, state, instanceId }: { descriptor: NodeDescriptor; state: NodeCardState; instanceId?: string }) {
  const id = 'util.constant.value'
  const binding: ValueBinding = {
    get: () => paramVal(descriptor, state, id),
    commit: (value) => state.onParameterCommit?.(id, value),
    live: (value) => state.onParameterLive?.(id, value),
  }
  return <TypedValueCard descriptor={descriptor} state={state} instanceId={instanceId} binding={binding} className="node-card-constant" />
}
