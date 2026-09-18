// Finds whatever element on a rendered NodeCard actually carries
// `.node-title` — every layout variant has exactly one now (NodeCard.tsx's
// Standard/Horizontal/Singleton/Decoration bodies all render it) — and
// returns its box relative to a positioned ancestor. Used by
// GraphSurface.tsx to position the rename `<input>` against the *real*
// title geometry instead of a single hardcoded top-bar-shaped CSS rect that
// only matched the Standard layout (M10_REVIEW.md §12). offsetLeft/Top/
// Width/Height (not getBoundingClientRect()) so no camera/zoom conversion
// is needed — both elements already share the same unscaled world-space
// coordinate system.
export interface TitleGeometry {
  left: number
  top: number
  width: number
  height: number
}

export function getTitleGeometry(wrapperEl: HTMLElement): TitleGeometry | null {
  const titleEl = wrapperEl.querySelector<HTMLElement>('.node-title')
  if (!titleEl) return null
  return { left: titleEl.offsetLeft, top: titleEl.offsetTop, width: titleEl.offsetWidth, height: titleEl.offsetHeight }
}
