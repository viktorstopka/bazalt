// design/Macro.png's "Edit T" dialog — configures a macro's type/quantity/
// range/enum-options. Direct instruction: "Bare minimum is fine — it needs
// to work, not to be design-polished", so this is plain HTML form controls
// (not ValueSlider/TriggerSelect) in a simple fixed-centered panel, not a
// themed, animated dialog.
//
// Rewritten per direct correction, 2026-10-03: three top-level Types
// (Control/Bool/Trigger), not five — when Type=Control, a second "Subtype"
// control (Value/Modulation/Int — graphStore.ts's own
// `classifyMacroControlSubtype`, a pure UI grouping over `isInteger`/
// `quantity`, not a separately stored field) picks which of Quantity
// (Value), Uni/Bi (Modulation), or Enum-toggle+range (Int) applies. "Unit"
// is no longer a field at all — it's derived from Quantity engine-side
// (TypedValueNodeBase.h's unitForQuantity). Type/isInteger/isEnum/min/max/
// quantity are real structural ParameterDescriptors (MacroNode.h) and
// commit through the same state.onParameterCommit every other row already
// uses; enum option labels are the one cosmetic property this still
// writes via graphStore.ts's setMacroEnumOptionLabels (see GraphNode's own
// doc comment for why).
import { useState } from 'react'
import type { NodeDescriptor, Quantity } from '../graph/descriptorTypes'
import type { NodeCardState } from './NodeCard'
import {
  MACRO_TYPE_ORDER,
  QUANTITY_ORDER,
  macroTypeOrdinal,
  quantityOrdinal,
  classifyMacroControlSubtype,
  type MacroValueType,
  type MacroControlSubtype,
} from '../graph/graphStore'
import { Button } from '../controls/Button'
import './MacroBody.css'

const TYPE_LABELS: Record<MacroValueType, string> = {
  control: 'Control',
  bool: 'Bool',
  trigger: 'Trigger',
}

const SUBTYPE_LABELS: Record<MacroControlSubtype, string> = {
  value: 'Value',
  modulation: 'Modulation',
  int: 'Int',
}
const SUBTYPE_ORDER: readonly MacroControlSubtype[] = ['value', 'modulation', 'int']

const QUANTITY_LABELS: Record<Quantity, string> = {
  dimensionless: 'Dimensionless',
  frequency: 'Frequency',
  pitch: 'Pitch',
  time: 'Time',
  gain: 'Gain',
  ratio: 'Ratio',
  unipolar: 'Unipolar',
  bipolar: 'Bipolar',
  count: 'Count',
  phase: 'Phase',
  audio: 'Audio',
  boolean: 'Boolean',
}
// Value's own quantity choices exclude Unipolar/Bipolar (that pair IS
// Modulation, picked via its own Uni/Bi control below, not this list).
const VALUE_QUANTITIES: readonly Quantity[] = QUANTITY_ORDER.filter((q) => q !== 'unipolar' && q !== 'bipolar')
const MODULATION_QUANTITIES: readonly Quantity[] = ['unipolar', 'bipolar']

function enumLabelsToText(labels: readonly string[] | undefined): string {
  return (labels ?? []).join('\n')
}
function enumTextToLabels(text: string): string[] {
  return text
    .split('\n')
    .map((l) => l.trim())
    .filter((l) => l.length > 0)
}

export function MacroEditTypeModal({
  descriptor,
  state,
  macroType,
  isInteger,
  isEnum,
  min,
  max,
  quantity,
  enumLabels,
  onClose,
}: {
  descriptor: NodeDescriptor
  state: NodeCardState
  macroType: MacroValueType
  isInteger: boolean
  isEnum: boolean
  min: number
  max: number
  quantity: Quantity
  enumLabels: readonly string[] | undefined
  onClose: () => void
}) {
  const [draftType, setDraftType] = useState(macroType)
  const [draftSubtype, setDraftSubtype] = useState<MacroControlSubtype>(classifyMacroControlSubtype(isInteger, quantity))
  const [draftQuantity, setDraftQuantity] = useState(quantity)
  const [draftIsEnum, setDraftIsEnum] = useState(isEnum)
  const [draftMin, setDraftMin] = useState(String(min))
  const [draftMax, setDraftMax] = useState(String(max))
  const [draftEnumText, setDraftEnumText] = useState(enumLabelsToText(enumLabels))

  const isControl = draftType === 'control'
  const showRange = isControl && draftSubtype !== 'modulation' // Modulation's range is implicit (0..1/-1..1) — TypedValueNodeBase.h's effectiveMinMax
  const showEnumToggle = isControl && draftSubtype === 'int'
  const showEnumOptions = showEnumToggle && draftIsEnum

  const hasParamId = (id: string): boolean => descriptor.parameters.some((p) => p.id === id)
  // Shared by every TypedValueNodeBase node (util.macro, util.constant): their
  // parameters are "<typeId>.type", "<typeId>.min", ... (TypedValueNodeBase.h's
  // commonParameters). A node whose `type` enum stops before Trigger
  // (util.constant) doesn't offer it.
  const idOf = (suffix: string): string => `${descriptor.typeId}.${suffix}`
  const typeParameter = descriptor.parameters.find((p) => p.id === idOf('type'))
  const allowTrigger = (typeParameter?.maxValue ?? 2) >= 2

  const changeSubtype = (next: MacroControlSubtype) => {
    setDraftSubtype(next)
    // Land on a sensible quantity for the new subtype rather than leaving
    // e.g. Unipolar selected after switching away from Modulation — a
    // stale cross-subtype quantity would silently misclassify this same
    // macro's own chip/colour the moment it's reopened.
    if (next === 'modulation' && draftQuantity !== 'unipolar' && draftQuantity !== 'bipolar') setDraftQuantity('unipolar')
    else if (next !== 'modulation' && (draftQuantity === 'unipolar' || draftQuantity === 'bipolar')) setDraftQuantity('dimensionless')
  }

  const apply = () => {
    const commit = state.onParameterCommit
    if (commit) {
      if (hasParamId(idOf('type'))) commit(idOf('type'), macroTypeOrdinal(draftType))
      if (isControl) {
        if (hasParamId(idOf('isInteger'))) commit(idOf('isInteger'), draftSubtype === 'int' ? 1 : 0)
        if (hasParamId(idOf('isEnum'))) commit(idOf('isEnum'), showEnumToggle && draftIsEnum ? 1 : 0)
        if (hasParamId(idOf('quantity'))) commit(idOf('quantity'), quantityOrdinal(draftQuantity))
        if (showRange) {
          const parsedMin = Number.parseFloat(draftMin)
          const parsedMax = Number.parseFloat(draftMax)
          if (hasParamId(idOf('min')) && Number.isFinite(parsedMin)) commit(idOf('min'), parsedMin)
          if (hasParamId(idOf('max')) && Number.isFinite(parsedMax)) commit(idOf('max'), parsedMax)
        }
      }
    }
    if (showEnumOptions) state.onSetMacroEnumOptionLabels?.(enumTextToLabels(draftEnumText))
    onClose()
  }

  return (
    <div
      className="macro-edit-modal-backdrop"
      onMouseDown={(e) => {
        if (e.target === e.currentTarget) onClose()
      }}
    >
      <div className="macro-edit-modal" onMouseDown={(e) => e.stopPropagation()} onContextMenu={(e) => e.preventDefault()}>
        <div className="macro-edit-modal-title">Edit Type</div>

        <label className="macro-edit-field">
          <span>Type</span>
          <select value={draftType} onChange={(e) => setDraftType(e.target.value as MacroValueType)}>
            {MACRO_TYPE_ORDER.filter((t) => t !== 'trigger' || allowTrigger).map((t) => (
              <option key={t} value={t}>
                {TYPE_LABELS[t]}
              </option>
            ))}
          </select>
        </label>

        {isControl && (
          <label className="macro-edit-field">
            <span>Subtype</span>
            <select value={draftSubtype} onChange={(e) => changeSubtype(e.target.value as MacroControlSubtype)}>
              {SUBTYPE_ORDER.map((s) => (
                <option key={s} value={s}>
                  {SUBTYPE_LABELS[s]}
                </option>
              ))}
            </select>
          </label>
        )}

        {isControl && draftSubtype === 'value' && (
          <label className="macro-edit-field">
            <span>Quantity</span>
            <select value={draftQuantity} onChange={(e) => setDraftQuantity(e.target.value as Quantity)}>
              {VALUE_QUANTITIES.map((q) => (
                <option key={q} value={q}>
                  {QUANTITY_LABELS[q]}
                </option>
              ))}
            </select>
          </label>
        )}

        {isControl && draftSubtype === 'modulation' && (
          <label className="macro-edit-field">
            <span>Uni/Bi</span>
            <select value={draftQuantity} onChange={(e) => setDraftQuantity(e.target.value as Quantity)}>
              {MODULATION_QUANTITIES.map((q) => (
                <option key={q} value={q}>
                  {QUANTITY_LABELS[q]}
                </option>
              ))}
            </select>
          </label>
        )}

        {showEnumToggle && (
          <label className="macro-edit-field">
            <span>Enum</span>
            <input type="checkbox" checked={draftIsEnum} onChange={(e) => setDraftIsEnum(e.target.checked)} />
          </label>
        )}

        {showRange && (
          <>
            <label className="macro-edit-field">
              <span>Min</span>
              <input type="number" value={draftMin} onChange={(e) => setDraftMin(e.target.value)} />
            </label>
            <label className="macro-edit-field">
              <span>Max</span>
              <input type="number" value={draftMax} onChange={(e) => setDraftMax(e.target.value)} />
            </label>
          </>
        )}

        {showEnumOptions && (
          <label className="macro-edit-field macro-edit-field-column">
            <span>Enum options (one per line)</span>
            <textarea value={draftEnumText} onChange={(e) => setDraftEnumText(e.target.value)} rows={4} />
          </label>
        )}

        <div className="macro-edit-modal-actions">
          <Button label="Cancel" onClick={onClose} />
          <Button label="Apply" onClick={apply} />
        </div>
      </div>
    </div>
  )
}
