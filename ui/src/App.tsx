import { useRef } from 'react'
import { InfiniteCanvas, type InfiniteCanvasHandle, type SnapSettings } from './canvas/InfiniteCanvas'
import { AnalysisPanel } from './analysis/AnalysisPanel'
import { redo, undo } from './graph/graphStore'
import { useGraphSnapshot } from './graph/useGraphSnapshot'
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
  const { canUndo, canRedo, lastError } = useGraphSnapshot()

  return (
    <div id="app-root">
      <InfiniteCanvas ref={canvasHandleRef} snapSettings={snapSettings}>
        <div className="top-bar">
          <span className="top-bar-title">Bazalt</span>
          {/* M19 (NODE_EDITOR.md §6): "a rejected command surfaces as an
              error banner" — the real engine's own rejection reason for the
              most recent command, cleared at the start of the next gesture. */}
          {lastError && <span className="top-bar-error">{lastError}</span>}
          <div className="top-bar-spacer" />
          <button className="top-bar-icon-button" onClick={() => undo()} disabled={!canUndo} title="Undo (Ctrl+Z)" aria-label="Undo">
            <UndoIcon />
          </button>
          <button className="top-bar-icon-button" onClick={() => redo()} disabled={!canRedo} title="Redo (Ctrl+Shift+Z)" aria-label="Redo">
            <RedoIcon />
          </button>
        </div>
      </InfiniteCanvas>
      {analysisOpen && <AnalysisPanel />}
    </div>
  )
}

export default App
