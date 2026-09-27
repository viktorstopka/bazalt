# 0012 — Design tokens: one TS object, mirrored onto CSS at runtime

## Status
Accepted (built in M5). Resolves the open question `ARCHITECTURE.md` §7 left as "TBD in M5":
"the canvas code reads computed-style values or a parallel JS token object generated from the same
source file."

## Context
`CLAUDE.md`'s naming rules and `ARCHITECTURE.md` §7 both require exactly one place a theme is
defined, read identically by DOM/React components and by Canvas/WebGL rendering code that has no
DOM element of its own to read computed styles from. Two real options existed: (a) define tokens as
CSS custom properties and have canvas code call `getComputedStyle()` and parse the resulting
strings, or (b) define tokens as a plain JS/TS object and generate the CSS custom properties from
it at runtime.

## Decision
`ui/src/theme/tokens.ts` exports a single nested `const tokens` object (colours, fonts, spacing,
stroke widths, radii) — the one source of truth. `applyTokensToCss()` walks that object once at
startup (`main.tsx`) and calls `documentElement.style.setProperty()` for every leaf value, so DOM/
CSS consumers use ordinary `var(--color-background)`-style custom properties. Canvas/WebGL code
(`InfiniteCanvas.tsx`, `TelemetryScope.tsx`) imports `tokens` directly and reads plain JS values
(numbers, colour strings) — no `getComputedStyle()` call, no string parsing, no per-frame DOM
read-back.

## Consequences
- Adding a token is one edit in one file; it's live in both DOM and canvas rendering without
  touching either consumer.
- Canvas code never round-trips through the DOM to get a colour it already has in memory as a plain
  JS value — one fewer per-frame cost, relevant given `ARCHITECTURE.md` §3's 120 fps target for the
  eventual node editor (NODE_EDITOR.md §3).
- `applyTokensToCss()` runs once, synchronously, before the first paint (`main.tsx`, before
  `createRoot(...).render(...)`) — there's no flash-of-unstyled-content risk to manage, and no
  reactivity concerns since tokens don't change at runtime (there is exactly one default theme per
  `MILESTONES.md` M5's scope; a future runtime theme switch would need `applyTokensToCss()` callable
  again with a different token set, which this design already supports without restructuring).
- The full NODE_EDITOR.md port-type colour palette (§5/§10) extends this same `tokens.ts` object
  additively when M9 needs it — this ADR's mechanism doesn't change, only the object's contents
  grow.
