// M20 (C5): the node editor's real inline preview mount point — replaces
// NodeCard.tsx's old PlaceholderPreview (100% static fake bars). Reads a
// descriptor's own previews[] entries and, for a live canvas instance
// (instanceId set), subscribes the engine-side tap and draws whatever the
// shared render loop (previewRenderLoop.ts, C3) hands it each frame,
// reusing M5's own draw functions (telemetryDraw.ts) verbatim.
//
// M20 (C2): only actually subscribed/registered while the canvas element
// is near the viewport (IntersectionObserver, root: null — the browser's
// own viewport, not InfiniteCanvas's own clipping bounds; a node scrolled
// under a side panel while technically still inside the browser viewport
// stays subscribed in that edge case, an accepted simplification rather
// than threading InfiniteCanvas's scroll container down through NodeCard/
// GraphSurface just for this). This is what keeps the 64-tap cap from
// being a real ceiling once a graph has hundreds of nodes — most are
// off-screen at once (NODE_EDITOR.md §9).
import { useEffect, useId, useRef, useState } from 'react'
import type { PreviewDescriptor } from '../graph/descriptorTypes'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview, frameTypeForPreviewKind } from '../graph/previewSubscriptions'
import { getInterpolatedTap } from '../telemetry/telemetryClient'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { drawScopeFilled, drawSpectrum, drawMeter } from '../analysis/telemetryDraw'
import { tokens } from '../theme/tokens'
import { getCamera } from '../canvas/interactionStore'
import type { NodeCardState } from './NodeCard'
import { PhaseLockedPreview } from './PhaseLockedPreview'
import { nextPlayheadMode, playheadModeFromProperty } from './playheadMode'

interface NodePreviewProps {
  nodeId: string
  preview: PreviewDescriptor
  /** Carries the per-node phase-locked playhead mode and its setter. */
  state?: NodeCardState
}

// A generous margin so a preview subscribes just before it scrolls into
// view and unsubscribes only once well clear of it, rather than thrashing
// subscribe/unsubscribe right at the viewport's exact edge.
const VISIBILITY_ROOT_MARGIN = '200px'

/** One node's one declared preview — a node with more than one previews[]
    entry mounts one of these per entry (NodeCard.tsx decides where). Renders
    nothing for a kind with no real producer yet (frameTypeForPreviewKind
    returns undefined for ShapeWithPlayhead/RollingHistory/Spectrogram/
    Goniometer — Part A's "documented for later" list) rather than drawing
    a misleading blank canvas. EventImpulse DOES have a real producer now
    (AnalysisThread::publishEventImpulse, design/Visualization/Ripple.png)
    but still isn't drawn through this generic per-kind dispatch below —
    view.ripple is a bespoke node body (RippleBody.tsx, NodeCard.tsx's own
    typeId dispatch) with its own persistent ring-list animation state,
    which this component's own "redraw whatever the latest payload says,
    every frame, no memory between frames" model doesn't fit.
*/
/** Which preview a node draws is its own declaration (PreviewDescriptor.kind,
    from the node's C++ getPreviews()) — a phase source declares
    'phaseLocked', anything without a phase keeps a time-based kind — so this
    dispatch never needs to know which node it is drawing. */
export function NodePreview({ nodeId, preview, state }: NodePreviewProps) {
  if (preview.kind === 'phaseLocked') {
    const mode = playheadModeFromProperty(state?.previewPlayheadMode)
    const setMode = state?.onSetPreviewPlayheadMode
    return (
      <PhaseLockedPreview
        nodeId={nodeId}
        portId={preview.portId}
        playheadMode={mode}
        onCyclePlayheadMode={setMode ? () => setMode(nextPlayheadMode(mode)) : undefined}
      />
    )
  }
  return <TimeDomainPreview nodeId={nodeId} preview={preview} />
}

function TimeDomainPreview({ nodeId, preview }: { nodeId: string; preview: PreviewDescriptor }) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const id = useId()
  // Compared against undefined, NEVER tested for truthiness: TelemetryFrameType.Oscilloscope
  // is 0, so `!frameType` silently disabled every Waveform preview (osc.analog's, and
  // view.scope's) since M20 - they rendered nothing at all.
  const frameType = frameTypeForPreviewKind(preview.kind)
  const [visible, setVisible] = useState(false)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || frameType === undefined) return
    const observer = new IntersectionObserver(([entry]) => setVisible(entry.isIntersecting), {
      rootMargin: VISIBILITY_ROOT_MARGIN,
    })
    observer.observe(canvas)
    return () => observer.disconnect()
  }, [frameType])

  useEffect(() => {
    if (!visible || frameType === undefined) return
    void subscribeNodePreview(nodeId, preview.portId, preview.kind)
    return () => unsubscribeNodePreview(nodeId, preview.portId, preview.kind)
  }, [visible, nodeId, preview.portId, preview.kind, frameType])

  useEffect(() => {
    const canvas = canvasRef.current
    if (!visible || !canvas || frameType === undefined) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return
    // The size source of truth is this canvas's own wrapper div
    // (.node-preview-canvas-wrap, NodeCard.css), never the canvas element
    // itself. A <canvas>'s width/height ATTRIBUTES (the backing-store size
    // this effect enlarges below) count as its intrinsic size for any
    // shrink-to-fit ancestor's own "how wide do you want to be" layout
    // query — reading clientWidth/clientHeight off the canvas and writing
    // a bigger attribute back in response to what THAT query produced is
    // exactly the unbounded feedback loop NodeCard.css's own comment on
    // `.node-preview-canvas-wrap` documents in full ("visuals getting
    // infinitely bigger... now it is only the width" — a real, recurring
    // bug). The wrapper is a plain, non-replaced <div> with explicit CSS
    // sizing of its own (width/height/min-width) and no path back to this
    // canvas's attributes at all, so measuring IT instead is what actually
    // breaks the cycle, not just a different way of reading the same
    // (circular) number.
    const sizeSource = canvas.parentElement
    if (!sizeSource) return

    const tap = tapNameForPreview(nodeId, preview.portId)

    const render = () => {
      // This canvas is an ordinary child of InfiniteCanvas.tsx's "world" DOM
      // layer, which gets `scale(camera.zoom)` applied to it as a plain CSS
      // transform (InfiniteCanvas.tsx's own frame()) - a transform runs AFTER
      // layout, so clientWidth/clientHeight (and the backing-store size we'd
      // size from them) never reflect it. Sized from devicePixelRatio alone,
      // the canvas keeps rasterizing at its zoom=1 resolution forever, and
      // zooming in just has the browser stretch that fixed bitmap - the exact
      // "still pixelated after zooming" InfiniteCanvas.tsx's own `will-change`
      // comment already names for the DOM layer generally, just not fixable
      // there since vector DOM content (borders/text) doesn't have this
      // problem in the first place. A `<canvas>` genuinely is a fixed-
      // resolution bitmap, so it needs the zoom folded into its own backing-
      // store size instead. `Math.max(zoom, 1)` never asks for LESS than the
      // zoom=1 baseline (a preview drawn zoomed out is already smaller
      // on-screen than its natural size - nothing to compensate for there),
      // and keeps this correct even where `getCamera()`'s singleton zoom
      // isn't meaningful (the M9 Component Gallery, which doesn't live
      // inside the zoomable "world" layer at all).
      const dpr = (window.devicePixelRatio || 1) * Math.max(getCamera().zoom, 1)
      const width = sizeSource.clientWidth
      const height = sizeSource.clientHeight
      if (width === 0 || height === 0) return
      const targetWidth = Math.round(width * dpr)
      const targetHeight = Math.round(height * dpr)
      if (canvas.width !== targetWidth || canvas.height !== targetHeight) {
        canvas.width = targetWidth
        canvas.height = targetHeight
      }

      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      // The node's own fill, so a preview (a meter above all) reads as part
      // of the card rather than a lighter box set into it.
      ctx.fillStyle = tokens.color.nodeFill
      ctx.fillRect(0, 0, width, height)

      const tapData = getInterpolatedTap(tap, frameType)
      if (tapData) {
        if (preview.kind === 'waveform') drawScopeFilled(ctx, width, height, tapData.payload)
        else if (preview.kind === 'spectrum') drawSpectrum(ctx, width, height, tapData.payload)
        else if (preview.kind === 'meter') drawMeter(ctx, width, height, tapData.payload)
      }
    }

    registerPreviewRenderer(id, render)
    return () => unregisterPreviewRenderer(id)
  }, [visible, nodeId, preview.portId, preview.kind, frameType, id])

  if (frameType === undefined) return null

  // The wrapper (.node-preview-canvas-wrap, NodeCard.css) owns every bit of
  // CSS sizing; the canvas itself is `position: absolute; inset: 0` inside
  // it — see this file's own render()/sizeSource comment above for why
  // that split is load-bearing, not just a markup nicety.
  return (
    <div className="node-preview-canvas-wrap">
      <canvas className="node-preview-canvas" ref={canvasRef} />
    </div>
  )
}
