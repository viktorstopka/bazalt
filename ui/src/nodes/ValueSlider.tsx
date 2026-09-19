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
//  - Click without moving: enters type-to-edit mode.
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
import { useEffect, useRef, useState, type MouseEvent as ReactMouseEvent, type WheelEvent as ReactWheelEvent } from 'react'
import './ValueSlider.css'

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
      unbounded: no real limit exists (e.g. a Remap node's own inMin/
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
  /** Omitted (the M9 gallery's static call sites): the slider still drags/
      types locally so it's visually demonstrable, it just never persists
      anywhere — see the internal `uncontrolledValue` fallback below.
  */
  onCommit?: (value: number) => void
}

const DRAG_THRESHOLD_PX = 3
const DOUBLE_CLICK_MS = 300
const WHEEL_COMMIT_DEBOUNCE_MS = 400
const WHEEL_STEP_FRACTION = 0.02 // one wheel "tick" ~= 2% of the full range
const PRECISION_FACTOR = 0.15 // holding Shift: drag/scroll move the value at ~15% of normal speed

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value))
}
function roundTo(value: number, decimals: number): number {
  const factor = 10 ** decimals
  return Math.round(value * factor) / factor
}
function formatValue(value: number, decimals: number): string {
  return value.toFixed(decimals)
}

export function ValueSlider({ label, value, min, max, hardMin, hardMax, defaultValue, isInteger, unit, color, onCommit }: ValueSliderProps) {
  const decimals = isInteger ? 0 : 2
  const clampMin = hardMin ?? -Infinity
  const clampMax = hardMax ?? Infinity

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
  const clickTimeoutRef = useRef<number | null>(null)
  const editResolvedRef = useRef(false)

  const updateLiveValue = (next: number | null): void => {
    liveValueRef.current = next
    setLiveValue(next)
  }

  useEffect(
    () => () => {
      if (wheelTimeoutRef.current !== null) window.clearTimeout(wheelTimeoutRef.current)
      if (clickTimeoutRef.current !== null) window.clearTimeout(clickTimeoutRef.current)
    },
    [],
  )

  const displayValue = liveValue ?? committedValue
  const fraction = clamp((displayValue - min) / (max - min || 1), 0, 1)

  const commit = (next: number): void => {
    commitOut(roundTo(clamp(next, clampMin, clampMax), decimals))
  }

  // Distance (px) the drag needs to cover, on either axis, to sweep the
  // full min-max range — the slider's own current width, so a wider/taller
  // instance naturally gets proportionally coarser-per-pixel movement
  // instead of a fixed magic-number sensitivity.
  const sensitivity = (): number => {
    const width = rootRef.current?.getBoundingClientRect().width || 150
    return (max - min) / width
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
      const next = current + (stepDx - stepDy) * sensitivity() * factor
      updateLiveValue(roundTo(clamp(next, clampMin, clampMax), decimals))
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
        // A clean click, no drag. Disambiguated from a double-click (reset
        // to default) by waiting one short window before committing to
        // edit mode — direct feedback asked whether double-click-to-reset
        // exists; it didn't. A plain single click still enters edit mode,
        // just DOUBLE_CLICK_MS later than before, the standard cost of
        // telling one click from the first half of two.
        if (clickTimeoutRef.current !== null) {
          window.clearTimeout(clickTimeoutRef.current)
          clickTimeoutRef.current = null
          if (defaultValue !== undefined) commit(defaultValue)
        } else {
          clickTimeoutRef.current = window.setTimeout(() => {
            clickTimeoutRef.current = null
            editResolvedRef.current = false
            setEditing(true)
          }, DOUBLE_CLICK_MS)
        }
      }
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }

  const onWheel = (e: ReactWheelEvent<HTMLDivElement>): void => {
    e.preventDefault()
    e.stopPropagation()
    const factor = e.shiftKey ? PRECISION_FACTOR : 1
    const step = (max - min) * WHEEL_STEP_FRACTION * factor
    const direction = e.deltaY < 0 ? 1 : -1 // scrolling "up"/away increases, matching most DAW conventions
    const next = roundTo(clamp((liveValueRef.current ?? committedValue) + direction * step, clampMin, clampMax), decimals)
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

  return (
    <div
      ref={rootRef}
      className="value-slider"
      style={{ borderColor: color, color }}
      onMouseDown={onMouseDown}
      onWheel={onWheel}
      onClick={(e) => e.stopPropagation()}
      onContextMenu={(e) => e.stopPropagation()}
    >
      <div className="value-slider-fill" style={{ width: `${fraction * 100}%`, background: color }} />
      {editing ? (
        <input
          className="value-slider-input"
          autoFocus
          defaultValue={formatValue(committedValue, decimals)}
          onFocus={(e) => e.currentTarget.select()}
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => e.stopPropagation()}
          onBlur={(e) => commitEdit(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') commitEdit(e.currentTarget.value)
            else if (e.key === 'Escape') cancelEdit()
          }}
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
