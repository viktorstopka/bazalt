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

interface NodePreviewProps {
  nodeId: string
  preview: PreviewDescriptor
}

// A generous margin so a preview subscribes just before it scrolls into
// view and unsubscribes only once well clear of it, rather than thrashing
// subscribe/unsubscribe right at the viewport's exact edge.
const VISIBILITY_ROOT_MARGIN = '200px'

/** One node's one declared preview — a node with more than one previews[]
    entry mounts one of these per entry (NodeCard.tsx decides where). Renders
    nothing for a kind with no real producer yet (frameTypeForPreviewKind
    returns undefined for ShapeWithPlayhead/RollingHistory/EventImpulse/
    Spectrogram/Goniometer — Part A's "documented for later" list) rather
    than drawing a misleading blank canvas.
*/
export function NodePreview({ nodeId, preview }: NodePreviewProps) {
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
      const width = canvas.clientWidth
      const height = canvas.clientHeight
      if (width === 0 || height === 0) return
      const targetWidth = Math.round(width * dpr)
      const targetHeight = Math.round(height * dpr)
      if (canvas.width !== targetWidth || canvas.height !== targetHeight) {
        canvas.width = targetWidth
        canvas.height = targetHeight
      }

      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.fillStyle = tokens.color.panel
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

  return <canvas className="node-preview-canvas" ref={canvasRef} />
}
