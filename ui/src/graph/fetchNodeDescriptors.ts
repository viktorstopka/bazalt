// Fetches the real engine's node descriptors once, at editor load
// (NODE_EDITOR.md §3: "The JS side receives this as JSON once at editor
// load... not per-frame") over the M7 command bridge's native-function
// transport (ADR-0006) rather than the telemetry fetch() transport — this
// is exactly the RPC shape that transport was chosen for.
import { getNativeFunction } from '@juce-framework/webview'
import type { NodeDescriptor } from './descriptorTypes'

// @juce-framework/webview's own types declare window.__JUCE__ as always
// present, but at runtime it's only injected inside the real WebView
// (withNativeIntegrationEnabled()) — undefined when this page is opened in
// a plain browser tab during UI-only iteration, which is a harmless
// "no real descriptors yet" case here, not an error (the same guard every
// other native-function wrapper in this codebase uses, e.g.
// graphCommands.ts's callCommand).
export async function fetchNodeDescriptors(): Promise<NodeDescriptor[]> {
  if (typeof window.__JUCE__ === 'undefined') return []

  const result = await getNativeFunction('getNodeDescriptors')()
  return Array.isArray(result) ? (result as NodeDescriptor[]) : []
}
