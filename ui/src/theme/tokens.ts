// Design tokens (ARCHITECTURE.md §7): the single source of truth for every
// colour, font, stroke, and spacing value used anywhere in the UI, DOM or
// canvas. `applyTokensToCss()` mirrors this object onto `:root` as CSS
// custom properties (`var(--bg)`, etc.) for DOM/CSS consumers; canvas/WebGL
// code imports and reads `tokens` directly — one definition, two consumers,
// resolving ARCHITECTURE.md §7's "TBD in M5" note on how that's done.
//
// Values match `docs/Frame 1 Bazalt.png` where M5 already touches them
// (background, monospace type, thin strokes). The full port-type palette
// (NODE_EDITOR.md §5/§10) is deliberately not defined yet — out of scope
// until M9 — extend this object additively when it lands, don't restructure
// it.
export const tokens = {
  color: {
    background: '#14161a',
    panel: '#1b1e24',
    panelBorder: '#2c2f37',
    textPrimary: '#e8e8ea',
    textSecondary: '#9a9ca3',
    gridLineMinor: '#22252c',
    gridLineMajor: '#2c2f37',
    accent: '#5b8def',
    scopeTrace: '#e8798a',
    spectrumTrace: '#e8798a',
    meterPeak: '#e8798a',
    meterRms: '#5b8def',
    sliderFill: '#5b8def',
    error: '#e0454f',
  },
  font: {
    mono: "'JetBrains Mono', 'Cascadia Mono', Consolas, monospace",
  },
  space: {
    xs: '4px',
    sm: '8px',
    md: '16px',
    lg: '24px',
  },
  stroke: {
    thin: '1px',
  },
  radius: {
    sm: '4px',
    md: '8px',
  },
} as const

/** Injects every leaf value in `tokens` onto document.documentElement as a
    `--color-background`-style CSS custom property (path segments joined by
    '-', camelCase preserved within a segment). Call once at startup.
*/
export function applyTokensToCss(): void {
  const root = document.documentElement.style

  const walk = (node: object, path: string[]) => {
    for (const [key, value] of Object.entries(node)) {
      const nextPath = [...path, key]
      if (typeof value === 'object' && value !== null) {
        walk(value, nextPath)
      } else {
        root.setProperty(`--${nextPath.join('-')}`, String(value))
      }
    }
  }

  walk(tokens, [])
}
