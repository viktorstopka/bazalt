// M20 (C1/C2): the one place a node preview's engine-side tap subscription
// and its client-side polling get started/stopped together — a caller
// (InfiniteCanvas.tsx's viewport-visibility check, C2; NodeCard.tsx's own
// mount/unmount, C5) never touches graphCommands.ts's CommandResult shape
// or telemetryClient.ts's polling Map directly, just these two functions.
//
// Not part of graphCommands.ts on purpose: subscribeNodePreviewTap/
// unsubscribeNodePreviewTap are NOT graph-editing commands (no recompile,
// no undo step, no CommandResult — PluginEditor.cpp's own comment on why
// these don't go through GraphEditController) and return a plain boolean,
// not { success, errorMessage }.
import { getNativeFunction } from '@juce-framework/webview'
import { TelemetryFrameType } from '../telemetry/parseTelemetryFrame'
import { pollTap, stopPollingTap } from '../telemetry/telemetryClient'
import type { PreviewKind } from './descriptorTypes'

function tapNameFor(nodeId: string, portId: string): string {
  return `node:${nodeId}:${portId}` // NODE_EDITOR.md §9's own naming scheme
}

/** Mirrors PluginProcessor.cpp's frameTypesNeededFor() exactly, including
    its `default: return {}` branch — a kind with no real producer yet
    (PreviewDescriptor.h's "documented for later" list) needs no frame type
    at all, engine-side, so this returns undefined for those rather than
    guessing one: polling a frame type the engine will never publish for
    that tap would be pure waste, exactly what B4's per-tap frame-type
    selection exists to avoid.
*/
export function frameTypeForPreviewKind(kind: PreviewKind): TelemetryFrameType | undefined {
  switch (kind) {
    case 'waveform':
      return TelemetryFrameType.Oscilloscope
    case 'spectrum':
      return TelemetryFrameType.Spectrum
    case 'meter':
      return TelemetryFrameType.Meter
    case 'eventImpulse':
      return TelemetryFrameType.EventImpulse
    case 'rollingHistory':
      return TelemetryFrameType.RollingHistory
    default:
      return undefined
  }
}

/** Subscribes a live preview for one node's own port — establishes the
    engine-side tap (resolving which domain the node lives in, pointing it
    at the right plan; see PluginProcessor::subscribeVisualizationTap) and,
    only once that succeeds, starts client-side polling for it. Returns
    false without calling the engine at all for a kind with no real
    producer yet (frameTypeForPreviewKind returns undefined), and false if
    the engine couldn't resolve (nodeId, portId) to a real output buffer
    (outside the real WebView, or the node/port doesn't exist).
*/
export async function subscribeNodePreview(nodeId: string, portId: string, kind: PreviewKind): Promise<boolean> {
  const frameType = frameTypeForPreviewKind(kind)
  if (frameType === undefined) return false
  if (typeof window.__JUCE__ === 'undefined') return false

  const ok = (await getNativeFunction('subscribeNodePreviewTap')(nodeId, portId, kind)) as boolean
  if (ok) pollTap(tapNameFor(nodeId, portId), frameType)
  return ok
}

/** Reverses subscribeNodePreview() — stops local polling first (so a
    late-arriving frame between the two calls never gets displayed after
    the caller has already decided to unsubscribe), then releases the
    engine-side tap. Safe to call even if never successfully subscribed.
*/
export function unsubscribeNodePreview(nodeId: string, portId: string, kind: PreviewKind): void {
  const frameType = frameTypeForPreviewKind(kind)
  if (frameType !== undefined) stopPollingTap(tapNameFor(nodeId, portId), frameType)
  if (typeof window.__JUCE__ === 'undefined') return
  void getNativeFunction('unsubscribeNodePreviewTap')(nodeId, portId)
}

/** The tap name a subscribed preview's data lives under in telemetryClient's
    store — exported so a preview-rendering component can call
    getInterpolatedTap(tapNameForPreview(...), frameType) without
    reimplementing NODE_EDITOR.md §9's naming scheme itself.
*/
export function tapNameForPreview(nodeId: string, portId: string): string {
  return tapNameFor(nodeId, portId)
}
