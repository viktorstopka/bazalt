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

    // Port-type palette (NODE_EDITOR.md §5, blueprint §4's table).
    // `portAudio` is Audio's colour for a Scalar-resolved port — unchanged
    // value, but its MEANING changed with wiki/plans/DomainRedesign.md
    // Batch 4: it used to be Audio's only colour, full stop; now it's
    // specifically the Scalar half of a real Multiplicity distinction (see
    // `portAudioPoly` below).
    portAudio: '#e0339e',
    portModulation: '#e0924b',
    portValue: '#e8e8ea',
    portInteger: '#e6c85b',
    portTrigger: '#8c7fff',
    portBoolean: '#7cc6f7',
    // wiki/plans/DomainRedesign.md §5.6/Batch 4: a direct, explicit user
    // decision, reversing this file's own earlier "don't give Multiplicity
    // its own hue" recommendation — Poly and Scalar Audio are "very tricky
    // in plugging each other," and that debugging value outweighs the
    // colour-budget cost, for now (explicitly reversible: "we can revert
    // back later"). Retires `portPoly` (`#4ade80` — a DIFFERENT green from
    // this one) outright: that was the blueprint's own mock-only "poly/mono
    // encoding will be redesigned later" placeholder, and this is that
    // redesign, landed for real, Audio-only (§8's still-open question on
    // Control/other types is not resolved here).
    portAudioPoly: '#40FF69',
    // wiki/NODES_Gaps.md's Note-port-connectivity finding: SignalType::Note
    // had no colour of its own (portUiKind.ts's classifier fell through to
    // portValue, the exact colour a real-quantity Control port uses) — a
    // genuine same-colour-but-incompatible collision, not a connection bug.
    // A distinct teal/cyan, unused anywhere else in this palette.
    portNote: '#3ecfc0',
    // Direct feedback, the same real collision Note's own comment above
    // already names and fixes: portUiKind.ts's classifyPortUiKind() fell
    // Data through to 'value' (`type !== 'control' -> 'value'`) - the exact
    // white a real-quantity Control port renders as, so a Data(scale)/
    // Data(curve) port looked like an ordinary numeric control, with no way
    // to tell it needed a Data-tagged source, not just any cable. A muted
    // rust/terracotta, distinct from every hue above (pink, orange, white,
    // yellow, purple, sky-blue, teal).
    portData: '#c1665a',

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

    // wiki/plans/DomainRedesign.md Batch 4: the per-node DomainDot (title-bar
    // dot encoding voice/global/mono) is REMOVED outright — MultiplicityResolver
    // replaced DomainSplitter's whole-graph voice/global split with a
    // per-PORT Scalar/Poly resolution, so "this node's domain" is no longer a
    // single fact a dot could show; `domainVoice`/`domainGlobal`/`domainMono`
    // are retired along with it (superseded by `portAudioPoly` above for the
    // per-port distinction, and by `textFaint` below for the instance-count
    // badge that replaces the dot's UI slot).
    //
    // Instance-count badge, direct feedback's own redesign of it: no pill
    // background/border at all anymore (this token used to be exactly
    // that, `structuralGrey`, `#5f6672` — a legible mid-grey, deliberately
    // retired, not just renamed, along with the pill it painted) — plain
    // text sitting fully outside the node, "very slightly lighter than the
    // bg" so it reads as a quiet readout rather than a UI chrome element.
    // `nodeFill`/`background` are both `#171414`; this is that value with a
    // small, deliberately subtle bump on every channel (+16), nowhere near
    // `textSecondary`'s actual "readable label" contrast.
    textFaint: '#272424',
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
