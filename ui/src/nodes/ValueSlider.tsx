// A draggable, typeable numeric value control — used wherever a node shows
// a "hard-set" value that can also be dragged/typed directly (the
// unconnected-input-port fallback in NodeCard.tsx's PortRow, and every
// ParameterRow). Local, uncontrolled UI state only: whoever renders this
// owns the actual value (graphStore.ts, per node instance) and passes it
// back in via `value`; this component just reports commits via `onCommit`.
//
// Three ways to adjust it, all relative/delta-based rather than snapping to
// an absolute cursor position — direct feedback: "apart from changing it by
// scrolling... also be able to change it by dragging vertically, just like
// in Blender":
//  - Click-drag: horizontal AND vertical mouse movement both contribute
//    (right/up increase, left/down decrease), combined into one delta. Not
//    clamped to the slider's own on-screen width the way an absolute
//    cursor-position mapping would be — you can keep dragging past the
//    widget's edge for continued fine control, which is also why this
//    needed to become delta-based rather than position-based in the first
//    place.
//  - Scroll wheel: each tick nudges the value by a fixed step of the range;
//    a burst of ticks previews live and commits once after a short pause,
//    the same "one undo step per gesture" idea commitNodeMoves already
//    uses for dragging, applied to a gesture that has no natural mouseup.
//  - Click without moving: enters type-to-edit mode IMMEDIATELY, no
//    artificial delay. Direct feedback: "there is maybe a conflict of the
//    double click to reset and click to edit — the edit has priority."
//    There used to be one: single-click waited DOUBLE_CLICK_MS to see
//    whether a second click would arrive before committing to edit mode,
//    a hand-timed guess that could lose the double-click-to-reset race
//    against a real double-click paced any slower than that guess (a
//    trackpad, a deliberate slower double-click, or simply a value picked
//    too aggressively) — reset would silently never fire, edit mode would
//    "win" every time. Fixed by not guessing at all: a native `dblclick`
//    (fires after the browser's own, OS-timing-aware double-click
//    detection — never a hardcoded delay) always overrides whatever the
//    preceding click(s) did and commits the reset instead, handled below
//    via onDoubleClick. The edit `<input>` can flash on screen for a
//    single click's worth of a genuine double-click before the reset
//    fires; a real, deliberate trade-off for a click-to-edit that's
//    instant the rest of the time, not a bug.
//
// Tab/Shift+Tab while editing (direct feedback: "I would like the
// functionality of Tab and Shift Tab to work when editing a prop... it
// focuses on the next prop and saves the value") commit the current value
// (same as Enter) and move edit focus to the next/previous ValueSlider
// within the same node card (`.node-card`), wrapping around at either end —
// see the input's onKeyDown and the `bazalt-enter-edit` custom-event
// listener below. Deliberately scoped to ValueSlider rows only (not
// TriggerSelect's dropdowns, which have no typed value to Tab out of).
//
// Holding Shift during a drag or a wheel tick drops the effective speed to
// PRECISION_FACTOR (direct feedback: "holding shift should slow the
// progress/make it more precise") — checked live on every move/wheel event
// rather than once at gesture start, and applied only to each *incremental*
// step (not the whole gesture retroactively), so toggling Shift mid-drag
// changes speed smoothly instead of causing the value to jump.
//
// Range is the caller's choice — for now (direct instruction) most call
// sites pass a plain 0-10-ish fallback range rather than deriving real
// per-node-type bounds from the descriptor for ports that never got real
// ones — that's deferred to when the real node architecture is designed,
// not something to guess at here. `isInteger`, though, is real descriptor
// metadata both PortDescriptor and ParameterDescriptor actually carry (the
// gap wasn't "no data," it was this component never reading it) — direct
// feedback caught an integer-only value accepting fractional input, which
// is what `isInteger` below fixes: 0 decimals and whole-number rounding on
// every path (drag, wheel, typed edit) instead of a hardcoded 2 decimals
// for everything regardless of what the descriptor actually says.
import { useEffect, useRef, useState, type KeyboardEvent as ReactKeyboardEvent, type MouseEvent as ReactMouseEvent, type WheelEvent as ReactWheelEvent } from 'react'
import './ValueSlider.css'
import { fromNormalizedPosition, toNormalizedPosition } from './sliderCurve'
import { tokens, withAlpha } from '../theme/tokens'

export interface ValueSliderProps {
  label: string
  value: number
  /** Visual range only — the fill bar's proportion and how much value one
      pixel of drag covers (`sensitivity` below). Not a clamp: dragging or
      scrolling past either edge keeps moving the value at the same rate,
      it just reads off-scale visually (the fill simply caps at 0%/100%).
      Direct feedback, citing Blender: "the slider has max at 10 if that is
      the max, visually (as in the background)... but you can, even via
      dragging, move it higher." Callers pass a port/parameter's
      softMin/softMax here when declared (a comfortable default range for
      a value with no real hard limit), falling back to minValue/maxValue
      when they aren't.
  */
  min: number
  max: number
  /** The actual clamp — a value can never be typed, dragged, or scrolled
      past these, because doing so would be physically/semantically
      meaningless for this specific quantity (a filter cutoff can't go
      negative, a pitch can't exceed 127). Omitted entirely means
      unbounded: no real limit exists (e.g. a Map node's own inMin/
      inMax/outMin/outMax), so nothing here should invent one — this is
      NOT the same as `min`/`max` above being absent, which still fall
      back to a generic 0-10 visual range at the call site.
  */
  hardMin?: number
  hardMax?: number
  /** Double-click resets to this value, when given (a descriptor's own
      defaultValue) — direct feedback asked whether this exists; it didn't.
      Omitted where no default makes sense.
  */
  defaultValue?: number
  isInteger: boolean
  unit: string
  color: string
  /** Pre-resolved drag/wheel/fill curve exponent (sliderCurve.ts's
      resolveSkew — ValueSlider itself stays agnostic of Curve/Quantity
      vocabulary, matching its existing "range is the caller's choice"
      philosophy). 1 (the default) is plain linear, unchanged from before
      this existed. Direct feedback: attack/decay/release etc. already
      declare Curve::Logarithmic engine-side (ValueTypes::timeSecondsPort)
      and had since M14 — this was a wiring gap, not a missing feature.
  */
  skew?: number
  /** Display-only decimal count override (wiki/plans/PropsAndMacroRedesign.md
      Batch A2/A3) — defaults to the existing `isInteger ? 0 : 2` when
      omitted, so every call site that doesn't pass one keeps today's
      behaviour exactly. Never affects what's actually stored/committed,
      only `formatValue`'s non-editing display text — see `commit()` below
      for why storage itself is no longer rounded to this count at all.
  */
  decimals?: number
  /** Omitted (the M9 gallery's static call sites): the slider still drags/
      types locally so it's visually demonstrable, it just never persists
      anywhere — see the internal `uncontrolledValue` fallback below.
  */
  onCommit?: (value: number) => void
  /** Called with the in-progress value while a drag/wheel gesture is live,
      and with null when it ends — for a readout that must follow the drag
      before anything is committed (design/Map.png's diagram). */
  onLiveChange?: (value: number | null) => void
}

const DRAG_THRESHOLD_PX = 3
const WHEEL_COMMIT_DEBOUNCE_MS = 400
const WHEEL_STEP_FRACTION = 0.02 // one wheel "tick" ~= 2% of the full range
const PRECISION_FACTOR = 0.15 // holding Shift: drag/scroll move the value at ~15% of normal speed

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value))
}
// Display-only rounding (Batch A2) — never applied to a committed/stored
// value, only to the non-editing <span>'s text. See ValueSlider's own
// `quantize()` for what actually gates the stored value (clamp + integer
// rounding only, no decimal truncation for a float).
function formatValue(value: number, decimals: number): string {
  return value.toFixed(decimals)
}
// Full-precision text for the edit <input>'s initial value — opening the
// field to nudge a value must never silently snap it to the 2-decimal
// display string first (an integer is already exact, so it still goes
// through formatValue for a clean "5" rather than "5.000000000001").
function formatForEditing(value: number, isInteger: boolean, decimals: number): string {
  return isInteger ? formatValue(value, decimals) : String(value)
}

export function ValueSlider({ label, value, min, max, hardMin, hardMax, defaultValue, isInteger, unit, color, skew, decimals: decimalsProp, onCommit, onLiveChange }: ValueSliderProps) {
  const decimals = decimalsProp ?? (isInteger ? 0 : 2)
  const curveSkew = skew ?? 1
  const clampMin = hardMin ?? -Infinity
  const clampMax = hardMax ?? Infinity
  // Direct feedback: "the prop is saved in much more [precision than the
  // 2-decimal display]" — a real hard bound (hardMin/hardMax both declared)
  // still clamps the stored value fully, this only gates whether the fill
  // bar renders at all (Batch A4 — a genuinely unbounded port, e.g.
  // adapt.map's own in/out, shouldn't imply a range that doesn't exist).
  const hasBounds = hardMin !== undefined && hardMax !== undefined

  // No onCommit (gallery demo context): the slider becomes its own
  // uncontrolled source of truth instead of silently doing nothing on
  // drag/type — still a real, demonstrable control, just not wired to
  // anything outside itself.
  const [uncontrolledValue, setUncontrolledValue] = useState(value)
  const committedValue = onCommit ? value : uncontrolledValue
  const commitOut = onCommit ?? setUncontrolledValue

  // Non-null while actively dragging/scrolling: a local preview so the
  // fill/text track the gesture every frame without waiting on a round
  // trip through graphStore's own (undo-tracked, commit-once-per-gesture)
  // update. Mirrored into a ref (liveValueRef) because the drag's onMove/
  // onUp handlers are `window`-level listeners attached once per
  // mousedown, outside React's normal re-render cycle — reading the
  // `liveValue` state binding from inside them would close over whatever
  // it was *when the drag started*, not its latest value, since that
  // closure is never recreated as state updates during the drag.
  const [liveValue, setLiveValue] = useState<number | null>(null)
  const liveValueRef = useRef<number | null>(null)
  const [editing, setEditing] = useState(false)
  const rootRef = useRef<HTMLDivElement | null>(null)
  const dragRef = useRef<{ startX: number; startY: number; lastX: number; lastY: number; moved: boolean } | null>(null)
  const wheelTimeoutRef = useRef<number | null>(null)
  const editResolvedRef = useRef(false)

  // Read through a ref: the drag listeners capture updateLiveValue once at
  // mousedown, but must always report to the current callback.
  const onLiveChangeRef = useRef(onLiveChange)
  useEffect(() => {
    onLiveChangeRef.current = onLiveChange
  })
  const updateLiveValue = (next: number | null): void => {
    liveValueRef.current = next
    setLiveValue(next)
    onLiveChangeRef.current?.(next)
  }

  useEffect(
    () => () => {
      if (wheelTimeoutRef.current !== null) window.clearTimeout(wheelTimeoutRef.current)
    },
    [],
  )

  // Tab/Shift+Tab lands here from a SIBLING ValueSlider's own onKeyDown
  // (below) — there's no shared parent state tracking "which field is
  // active" across the node's whole prop list, so a custom DOM event is the
  // simplest way for one instance to tell a specific sibling "you're next"
  // without a context/refactor spanning every row type in NodeCard.tsx.
  useEffect(() => {
    const el = rootRef.current
    if (!el) return
    const onEnterEdit = (): void => {
      editResolvedRef.current = false
      setEditing(true)
    }
    el.addEventListener('bazalt-enter-edit', onEnterEdit)
    return () => el.removeEventListener('bazalt-enter-edit', onEnterEdit)
  }, [])

  const displayValue = liveValue ?? committedValue
  // Curve-aware (sliderCurve.ts) — skew===1 (everything that doesn't
  // declare a real curve) reduces to the old plain-linear fraction exactly.
  // toNormalizedPosition itself stays unclamped past [0,1] for a linear
  // slider (so drag/wheel can keep moving an unbounded value past its
  // visual range — see that function's own comment); the fill width is the
  // one place that must still cap at 0%/100%, same as before this change.
  const fraction = clamp(toNormalizedPosition(displayValue, min, max, curveSkew), 0, 1)

  // Direct feedback: "the prop is saved in much more [precision]... the
  // display when not editing is rounded to 2 decimal places" — commit/live-
  // preview only clamp (and, for an integer value, round to a whole
  // number); a float is never rounded to `decimals` on the way to storage,
  // only `formatValue` below rounds for the non-editing display text.
  const quantize = (next: number): number => {
    const clamped = clamp(next, clampMin, clampMax)
    return isInteger ? Math.round(clamped) : clamped
  }

  const commit = (next: number): void => {
    commitOut(quantize(next))
  }

  // Distance (px) the drag needs to cover, on either axis, to sweep the
  // full 0..1 drag/wheel POSITION (not the raw value range — see
  // sliderCurve.ts: a curved slider's drag feel stays constant-per-pixel in
  // position-space, only the position->value warp is curved) — the
  // slider's own current width, so a wider/taller instance naturally gets
  // proportionally coarser-per-pixel movement instead of a fixed
  // magic-number sensitivity.
  const sensitivity = (): number => {
    const width = rootRef.current?.getBoundingClientRect().width || 150
    return 1 / width
  }

  const onMouseDown = (e: ReactMouseEvent<HTMLDivElement>): void => {
    if (editing || e.button !== 0) return
    e.preventDefault()
    // Read the ref, not the `committedValue` prop: if a wheel gesture's
    // debounce commit hasn't fired yet, this resumes from its live value
    // instead of snapping back to the last actually-committed one — and
    // cancel that pending commit, since the drag's own mouseup now owns
    // committing whatever value it ends on.
    if (wheelTimeoutRef.current !== null) {
      window.clearTimeout(wheelTimeoutRef.current)
      wheelTimeoutRef.current = null
    }
    const startValue = liveValueRef.current ?? committedValue
    dragRef.current = { startX: e.clientX, startY: e.clientY, lastX: e.clientX, lastY: e.clientY, moved: false }
    updateLiveValue(startValue)

    const onMove = (ev: globalThis.MouseEvent): void => {
      const drag = dragRef.current
      if (!drag) return
      // Incremental (since the *last* move event), not "since drag start"
      // — this is what lets holding/releasing Shift change sensitivity
      // mid-drag smoothly instead of jumping: only the *next* increment
      // gets scaled differently, nothing already applied gets rescaled.
      const stepDx = ev.clientX - drag.lastX
      const stepDy = ev.clientY - drag.lastY
      drag.lastX = ev.clientX
      drag.lastY = ev.clientY

      if (!drag.moved) {
        const totalDx = ev.clientX - drag.startX
        const totalDy = ev.clientY - drag.startY
        if (Math.hypot(totalDx, totalDy) > DRAG_THRESHOLD_PX) drag.moved = true
        else return
      }

      // Right or up both increase; left or down both decrease (screen Y
      // grows downward, hence the negation). Shift: precise mode, direct
      // feedback ("holding shift should slow the progress/make it more
      // precise").
      const factor = ev.shiftKey ? PRECISION_FACTOR : 1
      const current = liveValueRef.current ?? startValue
      const currentPosition = toNormalizedPosition(current, min, max, curveSkew)
      const nextPosition = currentPosition + (stepDx - stepDy) * sensitivity() * factor
      const next = fromNormalizedPosition(nextPosition, min, max, curveSkew)
      updateLiveValue(quantize(next))
    }
    const onUp = (): void => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
      const drag = dragRef.current
      const wasDrag = drag?.moved ?? false
      const finalLive = liveValueRef.current
      dragRef.current = null
      updateLiveValue(null)
      if (wasDrag && finalLive !== null) {
        commit(finalLive)
      } else if (!wasDrag) {
        // A clean click, no drag: enter edit mode immediately (see the
        // header comment on why this used to wait, and why waiting was the
        // actual bug). A following native dblclick, if one arrives,
        // overrides this via onDoubleClick below.
        editResolvedRef.current = false
        setEditing(true)
      }
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }

  const onWheel = (e: ReactWheelEvent<HTMLDivElement>): void => {
    e.preventDefault()
    e.stopPropagation()
    const factor = e.shiftKey ? PRECISION_FACTOR : 1
    const step = WHEEL_STEP_FRACTION * factor // a position-space fraction now, not a value-space one — see sliderCurve.ts
    const direction = e.deltaY < 0 ? 1 : -1 // scrolling "up"/away increases, matching most DAW conventions
    const currentPosition = toNormalizedPosition(liveValueRef.current ?? committedValue, min, max, curveSkew)
    const next = quantize(fromNormalizedPosition(currentPosition + direction * step, min, max, curveSkew))
    updateLiveValue(next)
    if (wheelTimeoutRef.current !== null) window.clearTimeout(wheelTimeoutRef.current)
    wheelTimeoutRef.current = window.setTimeout(() => {
      commit(next)
      updateLiveValue(null)
      wheelTimeoutRef.current = null
    }, WHEEL_COMMIT_DEBOUNCE_MS)
  }

  const commitEdit = (raw: string): void => {
    if (editResolvedRef.current) return
    editResolvedRef.current = true
    const parsed = parseFloat(raw)
    if (!Number.isNaN(parsed)) commit(parsed)
    setEditing(false)
  }
  const cancelEdit = (): void => {
    editResolvedRef.current = true
    setEditing(false)
  }

  // Native dblclick — fires after the browser's own OS-timing-aware
  // double-click detection, so this always wins over whatever the
  // preceding click(s) already did (including having entered edit mode;
  // see the header comment). No defaultValue declared for this port/
  // parameter: nothing to reset to, so a double-click is just two edit-mode
  // entries, same as today.
  const onDoubleClick = (): void => {
    if (defaultValue === undefined) return
    editResolvedRef.current = true
    setEditing(false)
    commit(defaultValue)
  }

  // Tab: commit like Enter, then hand off edit focus to the next/previous
  // ValueSlider in this node card (wrapping at either end) via a custom
  // event — see the `bazalt-enter-edit` listener above for the receiving
  // side. preventDefault so the browser's own tab-to-next-focusable never
  // also fires and fights this.
  const onEditKeyDown = (e: ReactKeyboardEvent<HTMLInputElement>): void => {
    if (e.key === 'Enter') {
      commitEdit(e.currentTarget.value)
    } else if (e.key === 'Escape') {
      cancelEdit()
    } else if (e.key === 'Tab') {
      e.preventDefault()
      commitEdit(e.currentTarget.value)
      const root = rootRef.current
      const scope = root?.closest('.node-card')
      if (!root || !scope) return
      const fields = Array.from(scope.querySelectorAll<HTMLElement>('.value-slider'))
      const index = fields.indexOf(root)
      if (index === -1) return
      const nextIndex = (index + (e.shiftKey ? -1 : 1) + fields.length) % fields.length
      fields[nextIndex]?.dispatchEvent(new CustomEvent('bazalt-enter-edit'))
    }
  }

  return (
    <div
      ref={rootRef}
      className="value-slider"
      style={{ borderColor: withAlpha(color, tokens.opacity.border), color }}
      onMouseDown={onMouseDown}
      onWheel={onWheel}
      onClick={(e) => e.stopPropagation()}
      onDoubleClick={(e) => {
        e.stopPropagation()
        onDoubleClick()
      }}
      onContextMenu={(e) => e.stopPropagation()}
    >
      {hasBounds && <div className="value-slider-fill" style={{ width: `${fraction * 100}%`, background: color }} />}
      {editing ? (
        <input
          className="value-slider-input"
          autoFocus
          defaultValue={formatForEditing(committedValue, isInteger, decimals)}
          onFocus={(e) => e.currentTarget.select()}
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => e.stopPropagation()}
          onBlur={(e) => commitEdit(e.currentTarget.value)}
          onKeyDown={onEditKeyDown}
        />
      ) : (
        <>
          <span className="value-slider-label">{label}</span>
          <span className="value-slider-value">
            {formatValue(displayValue, decimals)}
            {unit}
          </span>
        </>
      )}
    </div>
  )
}
