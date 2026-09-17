// Design tokens (ARCHITECTURE.md §7): the single source of truth for every
// colour, font, stroke, and spacing value used anywhere in the UI, DOM or
// canvas. `applyTokensToCss()` mirrors this object onto `:root` as CSS
// custom properties (`var(--bg)`, etc.) for DOM/CSS consumers; canvas/WebGL
// code imports and reads `tokens` directly — one definition, two consumers,
// resolving ARCHITECTURE.md §7's "TBD in M5" note on how that's done.
//
// Values match `docs/Frame 1 Bazalt.png` where M5 already touches them
// (background, monospace type, thin strokes). `port`/`node` below are the
// M9 addition: NODE_EDITOR.md §5's 6-colour port-type palette (glyphs live
// in ui/src/graph/portUiKind.ts, not here — a glyph is a character/shape
// choice, not a design-token value) plus the node-body surface tokens the
// component gallery (M9) and the WebGL renderer (M10+) both read from this
// one object, resolving ARCHITECTURE.md §7's "one place a theme is
// defined" for the node editor specifically.
export const tokens = {
  color: {
    background: '#171414',
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

    // Port-type palette (NODE_EDITOR.md §5, blueprint §4's table). `poly`
    // is the explicit placeholder the blueprint calls out ("green marks
    // polyphonic audio flowing into Sum Voices... poly/mono encoding will
    // be redesigned later") — kept isolated behind its own token so nothing
    // else ever reaches for "green" directly.
    portAudio: '#e0339e',
    portModulation: '#e0924b',
    portValue: '#e8e8ea',
    portInteger: '#e6c85b',
    portTrigger: '#8c7fff',
    portBoolean: '#5b8fff',
    portPoly: '#4ade80',

    // Node-body surface (M9 component gallery; NODE_EDITOR.md §10 — DOM
    // node bodies until the hybrid WebGL-background sync is proven in M10,
    // see ADR-0008's amended consequences). Border is solid opaque white,
    // sharp corners (no radius) — corrected from the initial M9 build
    // after direct feedback against `docs/Slice 1 (1).png`, a closer crop
    // of the design reference than `Frame 1 Bazalt.png` alone made clear.
    // No glow/box-shadow token on purpose (second round of feedback: reads
    // as an AI-generated-UI cliché) — state changes are flat border-colour
    // swaps (accent for selected, nodeListening for listening) or, for
    // hover, a header-only background tint using `accent`/white directly
    // rather than a dedicated token.
    nodeFill: '#08090a',
    nodeBorder: '#ffffff',
    nodeDivider: 'rgba(255, 255, 255, 0.4)',
    nodeListening: '#7fd9e0',
    frameFill: 'rgba(120, 45, 45, 0.22)',
  },
  font: {
    // Design reference's specified face (loaded via Google Fonts in
    // index.html); falls back to the M5 choice if the web font hasn't
    // loaded yet or is unavailable.
    mono: "'Iosevka Charon', 'JetBrains Mono', 'Cascadia Mono', Consolas, monospace",
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
