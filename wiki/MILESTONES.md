# Wiki milestones (0.x) — node & architecture gap-fixing arc

Not a continuation of `archive_docs/MILESTONES.md`'s M-numbering. That record stays
accurate for M0–M22 (shipped, real, unchanged) — `git log` remains the source of
truth for what's actually committed. This is a **separate, parallel plan**, numbered
`0.x`, for the specific arc kicked off by hands-on testing of the running app
surfacing real node-design and UI/architecture gaps M0–M22 didn't catch. See
`wiki/NODES_Gaps.md` for what was found and `wiki/NODES.System.md` §4/§6/§7 for the
architecture questions this arc reopens (the connection matrix's real-vs-aspirational
gap, the visual-language color table, the stereo-cable question).

Each milestone builds, passes its tests, and is committed before the next one starts
— same discipline `archive_docs/MILESTONES.md`'s M-arc used.

## 0.0 — Wiki scaffold — done

`docs/` → `archive_docs/` (git-mv, history preserved). New `wiki/NODES.md` (replaces
`NODE_CATALOG.md`, folds in both correction docs critically — not verbatim),
`wiki/NODES.System.md` (architecture rules + the full connection/adapter matrix, real
vs. aspirational columns), `wiki/NODES_Gaps.md` (0.1's output), `wiki/MILESTONES.md`
(this file). `CLAUDE.md`'s doc pointers repointed at `wiki/`.

## 0.1 — `wiki/NODES_Gaps.md`: generalize + scan every node — done

Every mistake you named, generalized into a named category, checked against all 55
real node headers. Confirmed: `redundant-composable-param` (`mix.sum`),
`jargon-naming` (`mix.gain`'s "VCA" title — confirmed; `filter.svf`'s "SVF Filter" —
flagged, lower confidence), `modulation-only-port` (`mix.gain.gain`, confirmed
high-severity; several `math.*`/`adapt.*` primary inputs, confirmed lower-severity),
`hardcoded-trigger` (`excite.burst`), `single-type-preview-coverage` (nuanced —
`view.scope`/`meter` already take Control/Boolean/Event; built-in automatic previews
are the actual Audio-biased gap). Root-caused the Master Out bug precisely
(`graphSetOutput` has zero UI call sites — confirmed by grep, not guessed). Flagged
`instance.allocator.random1`/`random2` as likely-intentional, for your confirmation
rather than treated as broken. Three items still need live repro before a fix is
designed: dropdown clicks, Note-port connectivity, Reroute connectivity.

## 0.2 — Stereo, decided for real

Write the full trade-off (current `left`/`right` mono-pair convention vs. a true
single-cable stereo `Audio` port) into `wiki/NODES.System.md`, scope the actual
engine-level change (`ExecutionPlan` buffer layout, `PortDescriptor::channels`, every
mono node's implicit broadcast behavior, `mix.downmix`'s role), land on the
stereo-by-default single-cable design per your stated preference.

## 0.3 — Master Out / output designation, fixed

Wire `graphSetOutput` into the UI: auto-call it when a cable is dropped onto
`io.output`'s input, plus a general "Set as Output" context-menu action on any node's
output. No new engine mechanism — the bridge command already exists and is tested.

## 0.4 — Connection-replace UX

Wiring into an already-occupied input auto-disconnects the old cable first, instead
of rejecting the new one. Locate the exact UI drop-handler call site.

## 0.5 — Node-level fixes from the confirmed `NODES_Gaps.md` entries

Executed only after your review of 0.1's findings corrects/confirms them. Expected
shape: `mix.gain` title fix, `mix.gain.gain` gets a real unconnected default,
`mix.sum`'s `level.N` removed in favor of auto-inserted `mix.gain`, `excite.burst`
gets a real `trigger : Event` port. Exact scope finalized after your review.

## 0.6 — Minimal preview nodes for non-Audio types

The type-less, single-in/single-out minimal preview node you described, scoped once
0.1's nuance (view.scope/meter already poly-typed; the real gap is automatic
per-node previews) is factored into the design.

## 0.7 — Live UI bug fixes

Dropdown-click and Note-port-connectivity root causes, once reproduced live.

## Verification, every wave from 0.2 on

Build + `ctest` green, `pluginval --strictness-level 10`, `cd ui && npm run build &&
npm run lint` clean, build+launch the Standalone app to confirm by ear/eye (never
browser testing). Commit each milestone once green, without asking.
