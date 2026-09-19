// M20 (C3): one shared requestAnimationFrame loop for every telemetry
// preview canvas — replaces the pattern TelemetryScope.tsx used through M5
// (one independent rAF loop per mounted instance; fine at M5's fixed count
// of 15 canvases, not at the node editor's dynamic node count).
// NODE_EDITOR.md §9 calls for exactly one shared loop; this is it. A
// registered callback owns its own canvas ref/context/tap lookup entirely
// (this module never touches a canvas itself) — it's called once per frame
// with no arguments, same as each TelemetryScope instance's own render()
// used to be called by its own rAF.
const renderers = new Map<string, () => void>()

let rafHandle: number | null = null

function tick() {
  for (const render of renderers.values()) render()
  rafHandle = requestAnimationFrame(tick)
}

/** Registers a per-frame render callback under `id`, starting the shared
    loop if this is the first registration. Call once per mounted preview
    (TelemetryScope, and later NodeCard's inline previews via C5) and call
    unregisterPreviewRenderer(id) on unmount. Re-registering the same id
    replaces its callback — a component re-rendering with new props (a
    fresh closure over new props) just calls this again.
*/
export function registerPreviewRenderer(id: string, render: () => void): void {
  renderers.set(id, render)
  if (rafHandle === null) rafHandle = requestAnimationFrame(tick)
}

/** Unregisters a render callback, stopping the shared loop once nothing is
    left registered. Safe to call for an id that was never registered.
*/
export function unregisterPreviewRenderer(id: string): void {
  renderers.delete(id)
  if (renderers.size === 0 && rafHandle !== null) {
    cancelAnimationFrame(rafHandle)
    rafHandle = null
  }
}
