// M20 (C5): the node editor's real inline preview mount point — replaces
// NodeCard.tsx's old PlaceholderPreview (100% static fake bars). Reads a
// descriptor's own previews[] entries and, for a live canvas instance
// (instanceId set), subscribes the engine-side tap and draws whatever the
// shared render loop (previewRenderLoop.ts, C3) hands it each frame,
// reusing M5's own draw functions (telemetryDraw.ts) verbatim.
import { useEffect, useId, useRef } from 'react'
import type { PreviewDescriptor } from '../graph/descriptorTypes'
import { subscribeNodePreview, unsubscribeNodePreview, tapNameForPreview, frameTypeForPreviewKind } from '../graph/previewSubscriptions'
import { getInterpolatedTap } from '../telemetry/telemetryClient'
import { registerPreviewRenderer, unregisterPreviewRenderer } from '../analysis/previewRenderLoop'
import { drawScope, drawSpectrum, drawMeter } from '../analysis/telemetryDraw'
import { tokens } from '../theme/tokens'

interface NodePreviewProps {
  nodeId: string
  preview: PreviewDescriptor
}

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
  const frameType = frameTypeForPreviewKind(preview.kind)

  useEffect(() => {
    if (!frameType) return
    void subscribeNodePreview(nodeId, preview.portId, preview.kind)
    return () => unsubscribeNodePreview(nodeId, preview.portId, preview.kind)
  }, [nodeId, preview.portId, preview.kind, frameType])

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || !frameType) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const tap = tapNameForPreview(nodeId, preview.portId)

    const render = () => {
      const dpr = window.devicePixelRatio || 1
      const width = canvas.clientWidth
      const height = canvas.clientHeight
      if (width === 0 || height === 0) return
      if (canvas.width !== width * dpr || canvas.height !== height * dpr) {
        canvas.width = width * dpr
        canvas.height = height * dpr
      }

      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.fillStyle = tokens.color.panel
      ctx.fillRect(0, 0, width, height)

      const tapData = getInterpolatedTap(tap, frameType)
      if (tapData) {
        if (preview.kind === 'waveform') drawScope(ctx, width, height, tapData.payload)
        else if (preview.kind === 'spectrum') drawSpectrum(ctx, width, height, tapData.payload)
        else if (preview.kind === 'meter') drawMeter(ctx, width, height, tapData.payload)
      }
    }

    registerPreviewRenderer(id, render)
    return () => unregisterPreviewRenderer(id)
  }, [nodeId, preview.portId, preview.kind, frameType, id])

  if (!frameType) return null

  return <canvas className="node-preview-canvas" ref={canvasRef} />
}
