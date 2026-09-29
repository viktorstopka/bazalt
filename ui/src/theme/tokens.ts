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
    portBoolean: '#7cc6f7',
    portPoly: '#4ade80',
    // wiki/NODES_Gaps.md's Note-port-connectivity finding: SignalType::Note
    // had no colour of its own (portUiKind.ts's classifier fell through to
    // portValue, the exact colour a real-quantity Control port uses) — a
    // genuine same-colour-but-incompatible collision, not a connection bug.
    // A distinct teal/cyan, unused anywhere else in this palette.
    portNote: '#3ecfc0',

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
    nodeFill: '#171414',
    nodeBorder: '#ffffff',
    nodeListening: '#7fd9e0',
    frameFill: 'rgba(120, 45, 45, 0.22)',

    // Per-node domain indicator (09-28-InstanceAllocator.1's own debugging
    // arc: DomainSplitter's voice/global split has repeatedly been the
    // source of confusing, hard-to-guess-at-from-the-canvas behaviour — a
    // simple always-visible marker beats needing to reason about
    // reachability by eye). A small title-bar dot, not a border/glow (those
    // already carry meaning — accent for selected, nodeListening for
    // listening, error for error — and DOMAINS.md §11 already leans toward
    // NOT colour for the cable-level version of this same question, to
    // avoid competing with the 6-colour port-type palette; a small dot in
    // the title bar sits away from both). `domainVoice` reuses `portPoly`
    // exactly — this codebase's own established "green marks polyphonic
    // content" convention already means the same thing. `domainGlobal`
    // matches `textSecondary`'s value on purpose (the mundane, "just runs
    // once" side, no separate token needed for the colour itself, but kept
    // named for what it means here rather than reading as a random reuse).
    // Mono-only graphs (no allocator, no instance.mix) used to show no dot
    // at all here — direct feedback reversed that: showing nothing for the
    // first, most common case read as the indicator not being live yet
    // ("only appearing once you add a Voice node... they should be there
    // from the start"), not as "nothing to report." `domainMono` is its own
    // distinct, deliberately quiet tone — neither `domainGlobal`'s grey nor
    // `domainVoice`'s green, so all three domains stay visually distinct
    // rather than mono silently reusing one of the other two's meaning.
    domainVoice: '#4ade80',
    domainGlobal: '#9a9ca3',
    domainMono: '#5f6672',
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
