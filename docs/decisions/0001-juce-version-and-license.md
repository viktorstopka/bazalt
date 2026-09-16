# 0001 — JUCE version and license

## Status
Accepted (2026-09-16).

## Context
The brief specifies "latest stable major version" of JUCE. As of this writing (2026-09-16) that is
JUCE 9.0.2 (9.0.0 released 2026-07-21). JUCE ships under a dual license: GPLv3 (free, but requires
Bazalt itself to be GPL-compatible/open-source) or a commercial license (paid, allows closed-source
distribution).

## Decision
Pin JUCE to the exact tag `9.0.2` via CMake `FetchContent` (not a moving branch), so upgrades are a
deliberate, tested step rather than something CI silently picks up.

**License: commercial JUCE license** (user decision, 2026-09-16). Bazalt is closed-source
commercial software; no GPL obligations apply. JUCE's free-tier splash-screen/attribution
requirements do not apply. This also means no license-compatibility constraint on third-party
libraries added later beyond their own terms.

## Consequences
- Bumping JUCE later means editing one tag string and re-running the full test suite, not tracking
  a branch that can change under us.
- Commercial license needs to be active/current before any build is distributed outside the
  development team; not a concern for local development but worth tracking as a real business
  cost/renewal item alongside the codebase.
