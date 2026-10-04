// wiki/plans/UtilMacro.md: replaces MacroSlider.tsx (M5, dead code since
// the M10 rewrite — never imported anywhere; see that file's own removal
// in this same change) now that a real, wireable util.macro node exists.
// Still the exact same underlying binding MacroSlider.tsx already proved
// out: JUCE's first-party WebSliderRelay/getSliderState mechanism
// (PluginEditor.cpp constructs one relay+attachment per
// MacroParameters::numMacros slot), not the M7+ command bridge — a plain
// host-automatable AudioProcessorParameter already has a first-party JUCE
// mechanism for exactly this, and it's what makes "host automation moves
// the knob" and "dragging the knob is real host automation" both true for
// free.
//
// Two real differences from MacroSlider.tsx:
//  - Knob-styled (circular dial, rotating indicator) rather than a plain
//    <input type="range">, decided over a true angle-based rotary drag —
//    direct instruction's own choice when asked.
//  - The relay's own normalised 0..1 value is raw storage ONLY (the
//    underlying juce::AudioParameterFloat's range is always a plain
//    0..1 identity — MacroParameters::addParametersTo), never the macro's
//    real semantic range. This component is handed that real range (min/
//    max/isInteger/quantity) from the GRAPH MIRROR (the util.macro node's own
//    structural parameters, read reactively via graphStore.ts/
//    useGraphSnapshot — see App.tsx's MacroPanel), and does the
//    raw<->display conversion itself, same "min + raw * (max - min)"
//    MacroNode.h's own processSample() uses engine-side.
//
// Drag interaction: vertical-only (direct instruction's decided choice,
// matching a knob's own real-world convention — up increases, down
// decreases, no horizontal contribution, unlike ValueSlider.tsx's combined
// horizontal+vertical for its rectangular fill-bar shape), delta-based
// rather than absolute-position (same reasoning ValueSlider.tsx's own
// header comment gives: you can keep dragging past the widget's own
// on-screen height for continued fine control). Shift drops to
// PRECISION_FACTOR speed, same convention as every other draggable value
// in this app. Deliberately no wheel support or click-to-type-edit yet —
// MacroSlider.tsx never had either; a real gap to close later; kept out
// now to match this component's own minimum test shape
// (CLAUDE.md-style "known interim simplification", not an oversight).
//
// Live, not commit-once-per-gesture: unlike ValueSlider.tsx's onCommit
// model (graphStore.ts owns the value, the slider reports once at gesture
// end), this talks DIRECTLY to the relay on every drag step —
// `sliderDragStarted()`/`sliderDragEnded()` bracket the gesture (the
// correct JUCE convention for host automation/undo grouping around a
// continuous parameter move), with `setNormalisedValue()` called live in
// between, exactly matching MacroSlider.tsx's own existing bidirectional
// contract.
import { useEffect, useReducer, useRef, type MouseEvent as ReactMouseEvent } from 'react'
import { getSliderState } from '@juce-framework/webview'
import type { Quantity } from '../graph/descriptorTypes'
import { quantityUnit } from '../format/valueFormat'
import './MacroKnob.css'

export interface MacroKnobProps {
  /** 0-based — the relay is named `macro${slot + 1}` (PluginEditor.cpp's
      own naming, "macro1".."macro32"), matching MacroMapping::macroIndex's
      own 0-based convention everywhere else in this codebase.
  */
  slot: number
  label: string
  min: number
  max: number
  isInteger: boolean
  /** Replaces a separately-seeded `unit` prop (direct instruction,
      2026-10-03: "Unit should not be a field") — the displayed unit is
      derived from this via valueFormat.ts's own `quantityUnit`, mirroring
      TypedValueNodeBase.h's `unitForQuantity` engine-side. */
  quantity: Quantity
  color: string
}

const DRAG_THRESHOLD_PX = 3
const PRECISION_FACTOR = 0.15 // holding Shift — same constant/convention as ValueSlider.tsx
// How many px of vertical drag sweep the full 0..1 raw range, at the
// knob's own default on-screen size — a plain constant rather than
// measuring the element (ValueSlider.tsx's own per-pixel `sensitivity()`
// makes sense for a bar whose fill width IS its own drag distance; a
// knob's drag distance is deliberately independent of its visual size).
const FULL_SWEEP_PX = 160

function clamp01(value: number): number {
  return Math.min(1, Math.max(0, value))
}

export function MacroKnob({ slot, label, min, max, isInteger, quantity, color }: MacroKnobProps) {
  const relayName = `macro${slot + 1}`
  // getSliderState() is idempotent (cached by name on the JS side, same as
  // MacroSlider.tsx already relied on) — safe to call every render.
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

  const decimals = isInteger ? 0 : 2
  const raw = clamp01(state.getNormalisedValue())
  const displayValue = min + raw * (max - min)
  // -135deg..+135deg, a common knob sweep (270deg of travel, pointing
  // straight UP at the centre/default position, raw=0.5 - MacroKnob.css's
  // own comment has the exact reasoning) — purely cosmetic, the indicator
  // line's own rotation, not a hit-test target (dragging is
  // delta-based from anywhere on the knob, not angle-from-centre).
  const rotationDeg = -135 + raw * 270

  const draggingRef = useRef<{ lastY: number; startClientX: number; startClientY: number; moved: boolean } | null>(null)

  const onMouseDown = (e: ReactMouseEvent<HTMLDivElement>): void => {
    if (e.button !== 0) return
    e.preventDefault()
    draggingRef.current = { lastY: e.clientY, startClientX: e.clientX, startClientY: e.clientY, moved: false }

    const onMove = (ev: globalThis.MouseEvent): void => {
      const drag = draggingRef.current
      if (!drag) return
      if (!drag.moved) {
        const totalDx = ev.clientX - drag.startClientX
        const totalDy = ev.clientY - drag.startClientY
        if (Math.hypot(totalDx, totalDy) <= DRAG_THRESHOLD_PX) return
        drag.moved = true
        state.sliderDragStarted()
      }
      // Incremental since the last move event (not since drag start) — see
      // ValueSlider.tsx's own header comment on why: it's what lets
      // toggling Shift mid-drag change speed smoothly instead of jumping.
      const stepDy = ev.clientY - drag.lastY
      drag.lastY = ev.clientY
      const factor = ev.shiftKey ? PRECISION_FACTOR : 1
      // Up decreases clientY (screen space grows downward) and should
      // INCREASE the value — hence the negation, same convention
      // ValueSlider.tsx's own vertical term uses.
      const next = clamp01(state.getNormalisedValue() - (stepDy / FULL_SWEEP_PX) * factor)
      state.setNormalisedValue(next)
    }
    const onUp = (): void => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
      if (draggingRef.current?.moved) state.sliderDragEnded()
      draggingRef.current = null
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }

  return (
    <div className="macro-knob" onMouseDown={onMouseDown}>
      <div className="macro-knob-dial" style={{ borderColor: color }}>
        <div className="macro-knob-indicator" style={{ transform: `rotate(${rotationDeg}deg)`, background: color }} />
      </div>
      <span className="macro-knob-label">{label}</span>
      <span className="macro-knob-value">
        {displayValue.toFixed(decimals)}
        {quantityUnit(quantity)}
      </span>
    </div>
  )
}
