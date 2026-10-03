import { useRef, useState } from 'react'
import { InfiniteCanvas, type InfiniteCanvasHandle, type SnapSettings } from './canvas/InfiniteCanvas'
import { AnalysisPanel } from './analysis/AnalysisPanel'
import { redo, undo, quantityFromOrdinal, type GraphNode } from './graph/graphStore'
import { graphExportSnapshot } from './graph/graphCommands'
import { useGraphSnapshot } from './graph/useGraphSnapshot'
import { MacroKnob } from './controls/MacroKnob'
import { classifyPortUiKind, PORT_UI_STYLE } from './graph/portUiKind'
import type { Quantity } from './graph/descriptorTypes'
import './App.css'

/** A plain curved-arrow pair, not an icon font/library — small enough not
    to be worth a dependency for two glyphs.
*/
function UndoIcon() {
  return (
    <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M9 14 4 9l5-5" />
      <path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11" />
    </svg>
  )
}
function RedoIcon() {
  return (
    <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M15 14l5-5-5-5" />
      <path d="M20 9H9.5a5.5 5.5 0 0 0 0 11H13" />
    </svg>
  )
}
/** A plain arrow-into-a-tray, the standard export/download glyph — same
    "no icon library for two glyphs" reasoning as Undo/Redo above.
*/
function ExportIcon() {
  return (
    <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M12 3v12" />
      <path d="M7 10l5 5 5-5" />
      <path d="M4 19h16" />
    </svg>
  )
}

// wiki/plans/UtilMacro.md: the structural parameter ids a real util.macro
// node stores (MacroNode.h/getParameters()) — read straight off the graph
// mirror's parameterValues, never off the relay (see MacroKnob.tsx's own
// header comment on why the relay's own 0..1 is raw storage only).
const MACRO_SLOT_PARAM = 'util.macro.slot'
const MACRO_MIN_PARAM = 'util.macro.min'
const MACRO_MAX_PARAM = 'util.macro.max'
const MACRO_IS_INTEGER_PARAM = 'util.macro.isInteger'
const MACRO_QUANTITY_PARAM = 'util.macro.quantity'

interface MacroEntry {
  slot: number
  label: string
  min: number
  max: number
  isInteger: boolean
  unit: string
  quantity: Quantity
  color: string
}

/** Every CLAIMED util.macro node currently in the live graph, sorted by
    slot — a macro left at its descriptor default (-1, unclaimed; see
    MacroNode.h/graphStore.ts's own Finding B comment) has no real relay to
    bind a knob to, so it's filtered out rather than rendered broken.
    `color` reuses the exact same Modulation/Value/Integer classification
    every ordinary Control port/parameter already gets (portUiKind.ts) —
    one palette, not a second macro-only one.
*/
function macroEntriesFrom(nodes: readonly GraphNode[]): MacroEntry[] {
  const entries: MacroEntry[] = []
  for (const node of nodes) {
    if (node.typeId !== 'util.macro') continue
    const slotValue = node.parameterValues?.[MACRO_SLOT_PARAM]
    if (slotValue === undefined) continue
    const slot = Math.round(slotValue)
    if (slot < 0 || slot >= 32) continue

    const min = node.parameterValues?.[MACRO_MIN_PARAM] ?? 0
    const max = node.parameterValues?.[MACRO_MAX_PARAM] ?? 1
    const isInteger = (node.parameterValues?.[MACRO_IS_INTEGER_PARAM] ?? 0) >= 0.5
    const unit = node.macroUnit ?? ''
    const quantity = quantityFromOrdinal(node.parameterValues?.[MACRO_QUANTITY_PARAM] ?? 0)
    const kind = classifyPortUiKind({ type: 'control', isInteger, quantity })

    entries.push({
      slot,
      label: node.titleOverride ?? `Macro ${slot + 1}`,
      min,
      max,
      isInteger,
      unit,
      quantity,
      color: PORT_UI_STYLE[kind].color,
    })
  }
  entries.sort((a, b) => a.slot - b.slot)
  return entries
}

/** The top-bar knob panel (direct instruction: "Macros will also
    automatically appear with knobs on the top of the window") — renders
    nothing at all with zero claimed macros, matching this project's
    established "cheap to leave dormant" default (e.g. snap-to-grid's own
    App.tsx header comment below).
*/
function MacroPanel({ nodes }: { nodes: readonly GraphNode[] }) {
  const entries = macroEntriesFrom(nodes)
  if (entries.length === 0) return null
  return (
    <div className="macro-panel">
      {entries.map((entry) => (
        <MacroKnob
          key={entry.slot}
          slot={entry.slot}
          label={entry.label}
          min={entry.min}
          max={entry.max}
          isInteger={entry.isInteger}
          unit={entry.unit}
          color={entry.color}
        />
      ))}
    </div>
  )
}

/** M5 built the empty canvas + analysis panel. M10 (NODE_EDITOR.md) fills
    the canvas in with the real node editor interface — see
    InfiniteCanvas.tsx/GraphSurface.tsx/graphStore.ts — against local,
    UI-only graph state rather than the M7 command bridge (CLAUDE.md's
    interim-simplifications note has the full framing for why). The
    telemetry client and design tokens underneath are unchanged from M5.

    Top bar is deliberately just the title + Undo/Redo now (direct
    feedback). Snap-to-grid's checkbox, Fit View's button, and the analysis
    panel's toggle are gone from here — but NOT deleted underneath:
      - Snap-to-grid (snapSettings, still threaded into InfiniteCanvas) is a
        real, standard node-editor feature, not scaffolding — defaulted off
        for now, ready for its control to come back.
      - Fit View (InfiniteCanvasHandle.fitView, still wired via
        canvasHandleRef) is likewise a standard navigation feature, not
        scaffolding.
      - The analysis panel (AnalysisPanel/TelemetryScope/telemetryClient) is
        explicitly NOT disposable — CLAUDE.md's own M5 note says NODE_EDITOR
        §11's in-graph live previews are *built on* this telemetry
        plumbing, not a replacement that throws it away. `analysisOpen`
        just has no toggle to flip it right now, so it stays permanently
        false.
    All three are cheap to leave dormant and a real rebuild to delete and
    later re-invent, so nothing past their UI trigger was removed.
*/
// Plain constants, not useState — there's no toggle to ever call a setter
// right now (see the header comment above), and a hook with a dead setter
// is exactly the kind of bloat not to leave lying around. Restoring either
// toggle later is a one-line change back to useState.
const snapSettings: SnapSettings = { enabled: false, sizeWorldUnits: 24 }
const analysisOpen = false

function App() {
  const canvasHandleRef = useRef<InfiniteCanvasHandle | null>(null)
  // Undo/Redo is the one keyboard-only action (Ctrl+Z/Shift+Z/Y) that never
  // got a visible UI fallback (M10_REVIEW.md §16/§23's retrospective) — the
  // most likely shortcut to be intercepted by a host DAW's own accelerators.
  const { canUndo, canRedo, lastError, nodes } = useGraphSnapshot()

  // Dev-convenience export (direct instruction — see graphCommands.ts's
  // graphExportSnapshot doc comment for the full scope). Purely local,
  // ephemeral UI feedback for one button's own last click — not part of
  // graphStore.ts's mirrored state, since nothing about the export itself
  // is part of the live graph.
  const [exportStatus, setExportStatus] = useState<string | null>(null)
  const handleExport = () => {
    void graphExportSnapshot().then((result) => {
      setExportStatus(result.success ? `Exported to ${result.path}` : `Export failed: ${result.errorMessage}`)
      window.setTimeout(() => setExportStatus(null), 4000)
    })
  }

  return (
    <div id="app-root">
      <InfiniteCanvas ref={canvasHandleRef} snapSettings={snapSettings}>
        <div className="top-bar">
          <span className="top-bar-title">Bazalt</span>
          {/* M19 (NODE_EDITOR.md §6): "a rejected command surfaces as an
              error banner" — the real engine's own rejection reason for the
              most recent command, cleared at the start of the next gesture. */}
          {lastError && <span className="top-bar-error">{lastError}</span>}
          {exportStatus && <span className="top-bar-status">{exportStatus}</span>}
          <div className="top-bar-spacer" />
          <button className="top-bar-icon-button" onClick={handleExport} title="Export patch to exported-patch.json" aria-label="Export patch">
            <ExportIcon />
          </button>
          <button className="top-bar-icon-button" onClick={() => undo()} disabled={!canUndo} title="Undo (Ctrl+Z)" aria-label="Undo">
            <UndoIcon />
          </button>
          <button className="top-bar-icon-button" onClick={() => redo()} disabled={!canRedo} title="Redo (Ctrl+Shift+Z)" aria-label="Redo">
            <RedoIcon />
          </button>
        </div>
        <MacroPanel nodes={nodes} />
      </InfiniteCanvas>
      {analysisOpen && <AnalysisPanel />}
    </div>
  )
}

export default App
